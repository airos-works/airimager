// Checksum.cpp - see Checksum.h. The SHA-256 code is XZ Embedded's (the
// decoder uses it for .xz files with SHA-256 checks), which compares a
// finished hash with an expected one.
#include "engine/Checksum.h"

#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "xz_private.h"
}

namespace imager {

namespace {

int
HexValue(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	c = tolower(c);
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}


// "<64 hex digits> <space or *><name>"; false when the line is not one.
bool
ParseLine(const std::string& line, Sha256Digest& digest, std::string& name)
{
	if (line.size() < 64)
		return false;
	for (int i = 0; i < 32; i++) {
		int high = HexValue(line[i * 2]);
		int low = HexValue(line[i * 2 + 1]);
		if (high < 0 || low < 0)
			return false;
		digest.bytes[i] = uint8_t(high << 4 | low);
	}
	if (line.size() > 64 && !isspace((unsigned char)line[64]))
		return false;
	size_t start = line.find_first_not_of(" \t", 64);
	if (start != std::string::npos && line[start] == '*')
		start++;		// binary mode marker
	name = start == std::string::npos ? "" : line.substr(start);
	while (!name.empty() && isspace((unsigned char)name.back()))
		name.pop_back();
	size_t slash = name.rfind('/');
	if (slash != std::string::npos)
		name = name.substr(slash + 1);
	return true;
}

}  // namespace


bool
FindChecksumFile(const std::string& imagePath, std::string& checksumPath)
{
	struct stat st;
	checksumPath = imagePath + ".sha256";
	return stat(checksumPath.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}


bool
ReadChecksumFile(const std::string& checksumPath, const std::string& imageName,
	Sha256Digest& digest, std::string& error)
{
	std::ifstream in(checksumPath);
	if (!in) {
		error = "Could not read the checksum file.";
		return false;
	}
	std::string line;
	int lines = 0;
	bool found = false;
	Sha256Digest only;
	while (std::getline(in, line) && lines < 1000) {
		Sha256Digest lineDigest;
		std::string name;
		if (!ParseLine(line, lineDigest, name))
			continue;
		lines++;
		only = lineDigest;
		if (name == imageName) {
			digest = lineDigest;
			found = true;
		}
	}
	if (found)
		return true;
	if (lines == 1) {
		digest = only;
		return true;
	}
	error = lines == 0 ? "The checksum file holds no SHA-256 checksum."
		: "The checksum file has no checksum for " + imageName + ".";
	return false;
}


ChecksumResult
CheckSha256(const std::string& path, const Sha256Digest& expected,
	const std::atomic<bool>& cancel,
	const std::function<void(uint64_t, uint64_t)>& progress,
	std::string& error)
{
	int fd = open(path.c_str(), O_RDONLY);
	struct stat st;
	if (fd < 0 || fstat(fd, &st) != 0) {
		error = std::string("Could not open the image: ") + strerror(errno);
		if (fd >= 0)
			close(fd);
		return ChecksumResult::kFailed;
	}

	xz_sha256 state;
	xz_sha256_reset(&state);
	std::vector<uint8_t> buffer(1024 * 1024);
	uint64_t done = 0;
	uint64_t total = st.st_size;
	progress(0, total);
	while (true) {
		if (cancel) {
			close(fd);
			return ChecksumResult::kCancelled;
		}
		ssize_t bytes = read(fd, buffer.data(), buffer.size());
		if (bytes < 0 && errno == EINTR)
			continue;
		if (bytes < 0) {
			error = std::string("Could not read the image: ")
				+ strerror(errno);
			close(fd);
			return ChecksumResult::kFailed;
		}
		if (bytes == 0)
			break;
		xz_sha256_update(buffer.data(), bytes, &state);
		done += bytes;
		progress(done, total);
	}
	close(fd);
	return xz_sha256_validate(expected.bytes, &state)
		? ChecksumResult::kMatch : ChecksumResult::kMismatch;
}

}  // namespace imager
