// WriteJob.h - writing an image to a drive and verifying it.
//
// A decoder thread reads (and decompresses) the image into a few large
// chunks and hashes each; the job's own thread writes them to the drive in
// order, so decompression and the drive's writes overlap. Then the drive's
// cache is flushed and every chunk is read back and hashed again: a chunk
// whose hash differs is data the drive did not keep.
//
// Nothing here is Haiku-specific: the drive is a Target, which the tests
// back with a file.
#ifndef AIRIMAGER_WRITE_JOB_H
#define AIRIMAGER_WRITE_JOB_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "engine/ImageSource.h"

namespace imager {

// Where the image goes.
class Target {
public:
	virtual				~Target() {}

	virtual	bool		Write(const uint8_t* data, size_t size, uint64_t offset,
							std::string& error) = 0;
	// Makes everything written so far permanent (the drive's own cache too).
	virtual	bool		Flush(std::string& error) = 0;
	virtual	bool		Read(uint8_t* data, size_t size, uint64_t offset,
							std::string& error) = 0;
	// Writes and reads are whole multiples of this (the sector size).
	virtual	uint32_t	BlockSize() const = 0;
};

// A Target on an open file descriptor, a raw device or a file. flush, if
// set, runs after fsync() (on Haiku: the drive cache flush ioctl).
class FdTarget : public Target {
public:
						FdTarget(int fd, uint32_t blockSize,
							std::function<bool(int, std::string&)> flush
								= nullptr);

	bool				Write(const uint8_t* data, size_t size,
							uint64_t offset, std::string& error) override;
	bool				Flush(std::string& error) override;
	bool				Read(uint8_t* data, size_t size, uint64_t offset,
							std::string& error) override;
	uint32_t			BlockSize() const override { return fBlockSize; }

private:
	int					fFd;
	uint32_t			fBlockSize;
	std::function<bool(int, std::string&)> fFlush;
};

enum class Phase { kWriting, kFlushing, kVerifying };

struct Progress {
	Phase		phase;
	uint64_t	done;		// bytes of the image written or verified
	uint64_t	total;		// the image's size
};

struct JobOptions {
	bool		verify = true;
	size_t		chunkSize = 4 * 1024 * 1024;	// a multiple of 64 KiB
	int			chunks = 4;						// decoded chunks in flight
	int			decodeThreads = 0;				// see OpenImage()
};

struct JobResult {
	enum Status { kSucceeded, kCancelled, kFailed, kVerifyFailed };

	Status		status = kFailed;
	std::string	message;			// why, when it did not succeed
	uint64_t	written = 0;		// bytes of the image written
	uint64_t	mismatchOffset = 0;	// first chunk that read back differently
	double		writeSeconds = 0;	// writing and flushing
	double		verifySeconds = 0;
};

// Runs the whole job on the calling thread. progress is called from that
// thread after every chunk; cancel is checked between chunks.
JobResult RunWriteJob(const ImageInfo& image, Target& target,
	const JobOptions& options, const std::atomic<bool>& cancel,
	const std::function<void(const Progress&)>& progress);

}  // namespace imager

#endif
