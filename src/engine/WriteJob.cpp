// WriteJob.cpp - the decode/write pipeline, the flush and the read-back.
#include "engine/WriteJob.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <unistd.h>

#include "engine/Format.h"
#include "engine/Hash.h"

namespace imager {

namespace {

using Clock = std::chrono::steady_clock;


double
SecondsSince(Clock::time_point start)
{
	return std::chrono::duration<double>(Clock::now() - start).count();
}


uint64_t
RoundUp(uint64_t value, uint64_t multiple)
{
	return (value + multiple - 1) / multiple * multiple;
}


// A page-aligned buffer (raw device I/O goes straight to the driver).
class AlignedBuffer {
public:
	explicit AlignedBuffer(size_t size)
		:
		fData(NULL)
	{
		void* data;
		if (posix_memalign(&data, 4096, size) == 0)
			fData = static_cast<uint8_t*>(data);
	}

	~AlignedBuffer()
	{
		free(fData);
	}

	uint8_t* Data() const { return fData; }

private:
	uint8_t*	fData;
};


struct Chunk {
	explicit Chunk(size_t capacity) : buffer(capacity), size(0), hash(0) {}

	AlignedBuffer	buffer;
	size_t			size;
	uint64_t		hash;
};


// The decoder thread and the queues between it and the writer.
class Decoder {
public:
	Decoder(ImageReader& reader, const JobOptions& options,
			const std::atomic<bool>& cancel)
		:
		fReader(reader),
		fChunkSize(options.chunkSize),
		fCancel(cancel),
		fStop(false),
		fFinished(false)
	{
		for (int i = 0; i < options.chunks; i++) {
			fChunks.emplace_back(new Chunk(options.chunkSize));
			if (fChunks.back()->buffer.Data() == NULL) {
				fError = "Not enough memory for the write buffers.";
				fFinished = true;
				return;
			}
			fFree.push_back(fChunks.back().get());
		}
		fThread = std::thread(&Decoder::_Run, this);
	}

	~Decoder()
	{
		{
			std::lock_guard<std::mutex> lock(fLock);
			fStop = true;
		}
		fCondition.notify_all();
		if (fThread.joinable())
			fThread.join();
	}

	// The next full chunk in image order, or NULL at the end (error() says
	// whether that end is a failure).
	Chunk* Next()
	{
		std::unique_lock<std::mutex> lock(fLock);
		fCondition.wait(lock, [this] {
			return !fFull.empty() || fFinished || fCancel;
		});
		if (fFull.empty())
			return NULL;
		Chunk* chunk = fFull.front();
		fFull.pop_front();
		return chunk;
	}

	void Recycle(Chunk* chunk)
	{
		{
			std::lock_guard<std::mutex> lock(fLock);
			fFree.push_back(chunk);
		}
		fCondition.notify_all();
	}

	std::string Error()
	{
		std::lock_guard<std::mutex> lock(fLock);
		return fError;
	}

private:
	void _Run()
	{
		while (true) {
			Chunk* chunk;
			{
				std::unique_lock<std::mutex> lock(fLock);
				fCondition.wait(lock, [this] {
					return !fFree.empty() || fStop || fCancel;
				});
				if (fStop || fCancel)
					break;
				chunk = fFree.front();
				fFree.pop_front();
			}

			std::string error;
			int64_t bytes = fReader.Read(chunk->buffer.Data(), fChunkSize,
				error);
			if (bytes > 0) {
				chunk->size = bytes;
				chunk->hash = Xxh64(chunk->buffer.Data(), bytes);
			}

			std::lock_guard<std::mutex> lock(fLock);
			if (bytes < 0)
				fError = error;
			if (bytes > 0)
				fFull.push_back(chunk);
			else
				fFree.push_back(chunk);
			if (bytes < int64_t(fChunkSize))
				break;
			fCondition.notify_all();
		}
		{
			std::lock_guard<std::mutex> lock(fLock);
			fFinished = true;
		}
		fCondition.notify_all();
	}

	ImageReader&				fReader;
	size_t						fChunkSize;
	const std::atomic<bool>&	fCancel;
	std::vector<std::unique_ptr<Chunk>> fChunks;
	std::deque<Chunk*>			fFree;
	std::deque<Chunk*>			fFull;
	std::mutex					fLock;
	std::condition_variable		fCondition;
	bool						fStop;
	bool						fFinished;
	std::string					fError;
	std::thread					fThread;
};


JobResult
Failed(JobResult result, JobResult::Status status, const std::string& message)
{
	result.status = status;
	result.message = message;
	return result;
}

}  // namespace


FdTarget::FdTarget(int fd, uint32_t blockSize,
		std::function<bool(int, std::string&)> flush)
	:
	fFd(fd),
	fBlockSize(blockSize == 0 ? 512 : blockSize),
	fFlush(flush)
{
}


bool
FdTarget::Write(const uint8_t* data, size_t size, uint64_t offset,
	std::string& error)
{
	while (size > 0) {
		ssize_t bytes = pwrite(fFd, data, size, offset);
		if (bytes < 0 && errno == EINTR)
			continue;
		if (bytes <= 0) {
			error = "The drive refused the write at " + FormatBytes(offset)
				+ ": " + (bytes < 0 ? strerror(errno) : "no space left")
				+ ". It may have been removed, or it is write-protected.";
			return false;
		}
		data += bytes;
		size -= bytes;
		offset += bytes;
	}
	return true;
}


bool
FdTarget::Flush(std::string& error)
{
	if (fsync(fFd) != 0 && errno != EINVAL && errno != ENOTSUP) {
		error = std::string("Could not finish writing to the drive: ")
			+ strerror(errno) + ".";
		return false;
	}
	return !fFlush || fFlush(fFd, error);
}


bool
FdTarget::Read(uint8_t* data, size_t size, uint64_t offset, std::string& error)
{
	while (size > 0) {
		ssize_t bytes = pread(fFd, data, size, offset);
		if (bytes < 0 && errno == EINTR)
			continue;
		if (bytes <= 0) {
			error = "Could not read back the drive at " + FormatBytes(offset)
				+ ": " + (bytes < 0 ? strerror(errno) : "it ended early")
				+ ".";
			return false;
		}
		data += bytes;
		size -= bytes;
		offset += bytes;
	}
	return true;
}


JobResult
RunWriteJob(const ImageInfo& image, Target& target, const JobOptions& options,
	const std::atomic<bool>& cancel,
	const std::function<void(const Progress&)>& progress)
{
	JobResult result;
	std::string error;
	const uint64_t total = image.imageSize;
	const uint32_t blockSize = target.BlockSize();
	if (options.chunkSize % blockSize != 0)
		return Failed(result, JobResult::kFailed, "The drive's sector size is"
			" not supported.");

	std::unique_ptr<ImageReader> reader = OpenImage(image, error,
		options.decodeThreads);
	if (!reader)
		return Failed(result, JobResult::kFailed, error);

	// Writing.
	Clock::time_point start = Clock::now();
	std::vector<uint64_t> hashes;
	hashes.reserve(total / options.chunkSize + 1);
	{
		Decoder decoder(*reader, options, cancel);
		progress({ Phase::kWriting, 0, total });
		while (Chunk* chunk = decoder.Next()) {
			// The last chunk is padded with zeros to a whole sector; the
			// padding lands past the image's end, in space it does not use.
			size_t size = RoundUp(chunk->size, blockSize);
			memset(chunk->buffer.Data() + chunk->size, 0, size - chunk->size);
			if (!target.Write(chunk->buffer.Data(), size, result.written,
					error)) {
				return Failed(result, JobResult::kFailed, error);
			}
			hashes.push_back(chunk->hash);
			result.written += chunk->size;
			decoder.Recycle(chunk);
			progress({ Phase::kWriting, result.written, total });
			if (cancel)
				break;
		}
		if (cancel) {
			result.writeSeconds = SecondsSince(start);
			return Failed(result, JobResult::kCancelled, "Writing was"
				" cancelled. The drive holds part of the image and will not"
				" start; write it again.");
		}
		error = decoder.Error();
		if (!error.empty())
			return Failed(result, JobResult::kFailed, error);
	}
	if (result.written != total)
		return Failed(result, JobResult::kFailed, "The image ended before its"
			" recorded size: the file is damaged.");

	progress({ Phase::kFlushing, total, total });
	if (!target.Flush(error))
		return Failed(result, JobResult::kFailed, error);
	result.writeSeconds = SecondsSince(start);

	if (!options.verify) {
		result.status = JobResult::kSucceeded;
		return result;
	}

	// Verifying: read every chunk back and compare its hash.
	start = Clock::now();
	AlignedBuffer buffer(options.chunkSize);
	if (buffer.Data() == NULL)
		return Failed(result, JobResult::kFailed, "Not enough memory to"
			" verify the drive.");
	progress({ Phase::kVerifying, 0, total });
	uint64_t offset = 0;
	for (size_t i = 0; i < hashes.size(); i++) {
		if (cancel) {
			result.verifySeconds = SecondsSince(start);
			return Failed(result, JobResult::kCancelled, "Verification was"
				" cancelled. The image was written, but not checked.");
		}
		size_t size = std::min<uint64_t>(options.chunkSize, total - offset);
		if (!target.Read(buffer.Data(), RoundUp(size, blockSize), offset,
				error)) {
			return Failed(result, JobResult::kVerifyFailed, error);
		}
		if (Xxh64(buffer.Data(), size) != hashes[i]) {
			result.mismatchOffset = offset;
			result.verifySeconds = SecondsSince(start);
			return Failed(result, JobResult::kVerifyFailed, "The data read"
				" back from the drive differs from the image, near "
				+ FormatBytes(offset) + ". The drive may be failing, or"
				" smaller than it claims to be. Try another drive.");
		}
		offset += size;
		progress({ Phase::kVerifying, offset, total });
	}
	result.verifySeconds = SecondsSince(start);
	result.status = JobResult::kSucceeded;
	return result;
}

}  // namespace imager
