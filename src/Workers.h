// Workers.h - the threads behind the window: writing (engine/WriteJob) and
// checking a download (engine/Checksum). Both report to a BMessenger and
// stop early when asked.
#ifndef AIRIMAGER_WORKERS_H
#define AIRIMAGER_WORKERS_H

#include <Messenger.h>
#include <OS.h>

#include <atomic>
#include <string>

#include "engine/Checksum.h"
#include "engine/Drive.h"
#include "engine/ImageSource.h"

// Messages from the workers; each carries "job" (int32), the number the
// worker was made with, so the window can drop reports of a job it already
// gave up on.
enum {
	kMsgJobProgress		= 'jprg',	// "phase" int32, "done" "total" int64
	kMsgJobFinished		= 'jfin',	// "status" int32, "message" string,
									// "written" int64, "mismatch" int64,
									// "write seconds" "verify seconds" double
	kMsgCheckProgress	= 'cprg',	// "done" "total" int64
	kMsgCheckFinished	= 'cfin'	// "result" int32, "message" string
};


class Worker {
public:
						Worker(BMessenger target, int32 job);
	virtual				~Worker();

			status_t	Start(const char* name);
			void		Cancel() { fCancel = true; }
			// Cancels and waits for the thread to end.
			void		Stop();
			bool		IsRunning() const { return fThread >= 0; }

protected:
	virtual	void		Run() = 0;
			void		Send(BMessage& message);

			BMessenger			fTarget;
			int32				fJob;
			std::atomic<bool>	fCancel;

private:
	static	status_t	_Entry(void* worker);

			thread_id	fThread;
};


// Writes image to the raw device of drive, then verifies it.
class WriteWorker : public Worker {
public:
						WriteWorker(BMessenger target, int32 job,
							const imager::ImageInfo& image,
							const imager::DriveInfo& drive, bool verify);

protected:
			void		Run() override;

private:
			imager::ImageInfo	fImage;
			imager::DriveInfo	fDrive;
			bool				fVerify;
			bigtime_t			fLastReport;
};


// Checks image against the digest from its .sha256 file.
class ChecksumWorker : public Worker {
public:
						ChecksumWorker(BMessenger target, int32 job,
							const std::string& path,
							const imager::Sha256Digest& digest);

protected:
			void		Run() override;

private:
			std::string				fPath;
			imager::Sha256Digest	fDigest;
};

#endif
