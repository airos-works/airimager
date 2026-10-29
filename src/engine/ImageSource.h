// ImageSource.h - reading a disk image: raw (.img, .iso, anything) or
// xz-compressed (.img.xz, .iso.xz, as airos.works publishes them).
//
// The format is told by the file's first bytes, not by its name. For an xz
// file the size of the image inside comes from the xz index at the end of
// the file, so progress, time left and the drive size check work before a
// single byte is decompressed, and a download that stopped early is refused
// at once (its footer is missing).
#ifndef AIRIMAGER_IMAGE_SOURCE_H
#define AIRIMAGER_IMAGE_SOURCE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace imager {

enum class Compression { kNone, kXz };

// What an image file holds, from a quick look at it.
struct ImageInfo {
	std::string	path;
	std::string	name;				// file name without the directory
	Compression	compression = Compression::kNone;
	uint64_t	fileSize = 0;		// bytes on disk
	uint64_t	imageSize = 0;		// bytes written to the drive
	std::string	kind;				// "ISO 9660 image", "Disk image (GPT)", ...
	uint32_t	xzBlocks = 0;		// blocks in an xz file
};

// Inspects path. Returns false with a sentence in error when it is not an
// image airImager can write (unreadable, empty, a damaged or truncated xz
// file, an xz file whose index has no blocks).
bool InspectImage(const std::string& path, ImageInfo& info, std::string& error);

// Reads the image's bytes in order, decompressing if needed.
class ImageReader {
public:
	virtual				~ImageReader() {}

	// Fills buffer with up to size bytes; returns fewer only at the end of
	// the image, 0 at the end. Returns -1 with error set on failure.
	virtual	int64_t		Read(uint8_t* buffer, size_t size,
							std::string& error) = 0;

	// Bytes of the source file consumed so far (for an xz file, compressed
	// bytes).
	virtual	uint64_t	SourcePosition() const = 0;
};

// threads: how many threads decode a multi-block xz file (0: one per CPU,
// up to four; 1: the single-threaded streaming decoder).
std::unique_ptr<ImageReader> OpenImage(const ImageInfo& info,
	std::string& error, int threads = 0);

// A block of an xz file, from its index.
struct XzBlock {
	uint64_t	offset = 0;				// in the file
	uint64_t	unpaddedSize = 0;		// header, data and check
	uint64_t	uncompressedSize = 0;
	uint8_t		streamFlags[2] = {};	// of the stream holding it
};

// The blocks of every stream of an xz file (with stream padding), read from
// the indexes at the end of each stream, walking back from the file's end.
bool ReadXzIndex(int fd, uint64_t fileSize, std::vector<XzBlock>& blocks,
	std::string& error);
// Their total uncompressed size.
bool XzUncompressedSize(int fd, uint64_t fileSize, uint64_t& size,
	std::string& error);

}  // namespace imager

#endif
