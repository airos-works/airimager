// Checksum.h - checking a download against the SHA-256 file published next
// to it.
//
// airos.works publishes airos-<target>.<iso|img>.xz.sha256 beside every
// image, in the format of sha256sum ("<64 hex digits>  <file name>"). When
// that file is in the same folder as the image, airImager checks the image
// against it before writing, so a damaged or incomplete download is caught
// before it is written to a drive.
#ifndef AIRIMAGER_CHECKSUM_H
#define AIRIMAGER_CHECKSUM_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace imager {

struct Sha256Digest {
	uint8_t		bytes[32];
};

// The checksum file for an image: "<image path>.sha256", if it exists.
bool FindChecksumFile(const std::string& imagePath, std::string& checksumPath);

// Reads the digest for imageName from a sha256sum-style file: the line that
// names imageName, or the file's only line when no line names it (the
// browser may have renamed the download, as "airos-rpi4(1).img.xz").
bool ReadChecksumFile(const std::string& checksumPath,
	const std::string& imageName, Sha256Digest& digest, std::string& error);

enum class ChecksumResult { kMatch, kMismatch, kFailed, kCancelled };

// Hashes the file and compares the result with expected. progress gets the
// bytes hashed so far and the file's size.
ChecksumResult CheckSha256(const std::string& path, const Sha256Digest& expected,
	const std::atomic<bool>& cancel,
	const std::function<void(uint64_t, uint64_t)>& progress,
	std::string& error);

}  // namespace imager

#endif
