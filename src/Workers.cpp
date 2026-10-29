// Workers.cpp - see Workers.h.
#include "Workers.h"

#include <Message.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "DriveRoster.h"
#include "engine/WriteJob.h"

using namespace imager;

namespace {

// Progress messages at most this often (microseconds); the window redraws
// for each.
const bigtime_t kReportInterval = 100000;

}  // namespace


Worker::Worker(BMessenger target, int32 job)
	:
	fTarget(target),
	fJob(job),
	fCancel(false),
	fThread(-1)
{
}


Worker::~Worker()
{
	Stop();
}


status_t
Worker::Start(const char* name)
{
	fThread = spawn_thread(&Worker::_Entry, name, B_NORMAL_PRIORITY, this);
	if (fThread < 0)
		return fThread;
	return resume_thread(fThread);
}


void
Worker::Stop()
{
	if (fThread < 0)
		return;
	fCancel = true;
	status_t result;
	wait_for_thread(fThread, &result);
	fThread = -1;
}


void
Worker::Send(BMessage& message)
{
	message.AddInt32("job", fJob);
	fTarget.SendMessage(&message);
}


status_t
Worker::_Entry(void* worker)
{
	static_cast<Worker*>(worker)->Run();
	return B_OK;
}


WriteWorker::WriteWorker(BMessenger target, int32 job, const ImageInfo& image,
		const DriveInfo& drive, bool verify)
	:
	Worker(target, job),
	fImage(image),
	fDrive(drive),
	fVerify(verify),
	fLastReport(0)
{
}


void
WriteWorker::Run()
{
	JobResult result;
	int fd = open(fDrive.path.c_str(), O_RDWR);
	if (fd < 0) {
		result.message = std::string("Could not open the drive: ")
			+ strerror(errno) + ".";
	} else {
		FdTarget target(fd, DriveRoster::SectorSize(fd, fDrive.blockSize),
			&DriveRoster::FlushDriveCache);
		JobOptions options;
		options.verify = fVerify;
		result = RunWriteJob(fImage, target, options, fCancel,
			[this](const Progress& progress) {
				bigtime_t now = system_time();
				bool last = progress.done == progress.total
					|| progress.phase == Phase::kFlushing || progress.done == 0;
				if (!last && now - fLastReport < kReportInterval)
					return;
				fLastReport = now;
				BMessage message(kMsgJobProgress);
				message.AddInt32("phase", int32(progress.phase));
				message.AddInt64("done", progress.done);
				message.AddInt64("total", progress.total);
				Send(message);
			});
		close(fd);
	}

	BMessage message(kMsgJobFinished);
	message.AddInt32("status", result.status);
	message.AddString("message", result.message.c_str());
	message.AddInt64("written", result.written);
	message.AddInt64("mismatch", result.mismatchOffset);
	message.AddDouble("write seconds", result.writeSeconds);
	message.AddDouble("verify seconds", result.verifySeconds);
	Send(message);
}


ChecksumWorker::ChecksumWorker(BMessenger target, int32 job,
		const std::string& path, const Sha256Digest& digest)
	:
	Worker(target, job),
	fPath(path),
	fDigest(digest)
{
}


void
ChecksumWorker::Run()
{
	bigtime_t lastReport = 0;
	std::string error;
	ChecksumResult result = CheckSha256(fPath, fDigest, fCancel,
		[&](uint64_t done, uint64_t total) {
			bigtime_t now = system_time();
			if (now - lastReport < kReportInterval && done != total)
				return;
			lastReport = now;
			BMessage message(kMsgCheckProgress);
			message.AddInt64("done", done);
			message.AddInt64("total", total);
			Send(message);
		}, error);

	BMessage message(kMsgCheckFinished);
	message.AddInt32("result", int32(result));
	message.AddString("message", error.c_str());
	Send(message);
}
