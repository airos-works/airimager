// ImageSource.cpp - raw and xz image readers, and the xz index walk that
// gives an xz file's uncompressed size.
#include "engine/ImageSource.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "xz.h"

namespace imager {

namespace {

const uint8_t kXzMagic[6] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };
const size_t kXzHeaderSize = 12;
const size_t kXzFooterSize = 12;
// An index this large would describe millions of blocks: not an image.
const uint64_t kMaxXzIndexSize = 64 * 1024 * 1024;
const size_t kXzInputSize = 1024 * 1024;
// LZMA2 dictionaries up to this size are accepted (xz -9 uses 64 MiB).
const uint32_t kMaxXzDictionary = 1536u * 1024 * 1024;
// Multi-block files are decoded on up to this many threads, when no block
// is larger than kMaxParallelBlock.
const unsigned kMaxDecodeThreads = 4;
const uint64_t kMaxParallelBlock = 256 * 1024 * 1024;


void
InitCrcTables()
{
	static std::once_flag once;
	std::call_once(once, [] {
		xz_crc32_init();
		xz_crc64_init();
	});
}


std::string
ErrnoText(const char* what, int error)
{
	return std::string(what) + ": " + strerror(error);
}


// pread() until size bytes are read; false on an error or a short file.
bool
ReadAt(int fd, uint64_t offset, void* buffer, size_t size)
{
	uint8_t* p = static_cast<uint8_t*>(buffer);
	while (size > 0) {
		ssize_t bytes = pread(fd, p, size, offset);
		if (bytes < 0 && errno == EINTR)
			continue;
		if (bytes <= 0)
			return false;
		p += bytes;
		size -= bytes;
		offset += bytes;
	}
	return true;
}


uint32_t
Le32(const uint8_t* p)
{
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16
		| uint32_t(p[3]) << 24;
}


// A variable-length integer of the xz format (7 bits per byte, at most 9
// bytes); false when it runs past end or is not minimally encoded.
bool
ReadVli(const uint8_t*& p, const uint8_t* end, uint64_t& value)
{
	value = 0;
	for (int i = 0; i < 9; i++) {
		if (p >= end)
			return false;
		uint8_t byte = *p++;
		value |= uint64_t(byte & 0x7F) << (i * 7);
		if ((byte & 0x80) == 0)
			return i == 0 || byte != 0;
	}
	return false;
}


const char* kDamagedXz = "The xz file is incomplete or damaged. If it was"
	" downloaded, download it again.";


// The kind of image, from its first bytes (at least 36 KiB when there are).
std::string
DescribeImage(const uint8_t* data, size_t size)
{
	bool iso = size >= 0x8006 && memcmp(data + 0x8001, "CD001", 5) == 0;
	bool gpt = size >= 520 && memcmp(data + 512, "EFI PART", 8) == 0;
	bool mbr = size >= 512 && data[510] == 0x55 && data[511] == 0xAA;
	if (iso && (gpt || mbr))
		return "Bootable ISO image (hybrid)";
	if (iso)
		return "ISO 9660 image";
	if (gpt)
		return "Disk image (GPT)";
	if (mbr)
		return "Disk image (MBR)";
	return "Raw disk image";
}


class RawReader : public ImageReader {
public:
	RawReader(int fd, uint64_t size)
		:
		fFd(fd),
		fSize(size),
		fPosition(0)
	{
	}

	~RawReader() override
	{
		close(fFd);
	}

	int64_t Read(uint8_t* buffer, size_t size, std::string& error) override
	{
		if (fPosition + size > fSize)
			size = fSize - fPosition;
		size_t done = 0;
		while (done < size) {
			ssize_t bytes = read(fFd, buffer + done, size - done);
			if (bytes < 0 && errno == EINTR)
				continue;
			if (bytes < 0) {
				error = ErrnoText("Could not read the image", errno);
				return -1;
			}
			if (bytes == 0) {
				error = "The image file ended early: it changed while being"
					" written.";
				return -1;
			}
			done += bytes;
		}
		fPosition += done;
		return done;
	}

	uint64_t SourcePosition() const override
	{
		return fPosition;
	}

private:
	int			fFd;
	uint64_t	fSize;
	uint64_t	fPosition;
};


class XzReader : public ImageReader {
public:
	XzReader(int fd, uint64_t imageSize)
		:
		fFd(fd),
		fDecoder(xz_dec_init(XZ_DYNALLOC, kMaxXzDictionary)),
		fInput(kXzInputSize),
		fInputEnd(false),
		fFinished(false),
		fImageSize(imageSize),
		fProduced(0),
		fConsumed(0)
	{
		memset(&fBuffer, 0, sizeof(fBuffer));
		fBuffer.in = fInput.data();
	}

	~XzReader() override
	{
		if (fDecoder != NULL)
			xz_dec_end(fDecoder);
		close(fFd);
	}

	bool InitCheck() const
	{
		return fDecoder != NULL;
	}

	int64_t Read(uint8_t* buffer, size_t size, std::string& error) override
	{
		fBuffer.out = buffer;
		fBuffer.out_pos = 0;
		fBuffer.out_size = size;
		while (fBuffer.out_pos < size && !fFinished) {
			if (fBuffer.in_pos == fBuffer.in_size && !fInputEnd) {
				ssize_t bytes;
				do {
					bytes = read(fFd, fInput.data(), fInput.size());
				} while (bytes < 0 && errno == EINTR);
				if (bytes < 0) {
					error = ErrnoText("Could not read the image", errno);
					return -1;
				}
				fBuffer.in_pos = 0;
				fBuffer.in_size = bytes;
				fConsumed += bytes;
				fInputEnd = bytes == 0;
			}
			xz_ret result = xz_dec_catrun(fDecoder, &fBuffer, fInputEnd);
			switch (result) {
				case XZ_OK:
					break;
				case XZ_STREAM_END:
					fFinished = true;
					break;
				case XZ_UNSUPPORTED_CHECK:
					// Every check is built in; this cannot happen. Decoding
					// goes on without the check (XZ_DEC_ANY_CHECK).
					break;
				case XZ_MEM_ERROR:
					error = "Not enough memory to decompress the image.";
					return -1;
				case XZ_MEMLIMIT_ERROR:
					error = "The image was compressed with a dictionary"
						" larger than 1.5 GiB, which airImager does not"
						" decompress.";
					return -1;
				case XZ_OPTIONS_ERROR:
					error = "The xz file uses a compression option airImager"
						" does not support.";
					return -1;
				default:
					error = kDamagedXz;
					return -1;
			}
		}
		fProduced += fBuffer.out_pos;
		// The index gave the size; the data must agree with it.
		if (fProduced > fImageSize || (fFinished && fProduced != fImageSize)) {
			error = kDamagedXz;
			return -1;
		}
		return fBuffer.out_pos;
	}

	uint64_t SourcePosition() const override
	{
		return fConsumed - (fBuffer.in_size - fBuffer.in_pos);
	}

private:
	int						fFd;
	xz_dec*					fDecoder;
	xz_buf					fBuffer;
	std::vector<uint8_t>	fInput;
	bool					fInputEnd;
	bool					fFinished;
	uint64_t				fImageSize;
	uint64_t				fProduced;
	uint64_t				fConsumed;
};


// Appends value as an xz variable-length integer.
void
AppendVli(std::vector<uint8_t>& data, uint64_t value)
{
	while (value >= 0x80) {
		data.push_back(uint8_t(value) | 0x80);
		value >>= 7;
	}
	data.push_back(uint8_t(value));
}


void
AppendLe32(std::vector<uint8_t>& data, uint32_t value)
{
	for (int i = 0; i < 4; i++)
		data.push_back(uint8_t(value >> (i * 8)));
}


// Decodes one block of a multi-block xz file on its own: the block is
// wrapped in a stream of its own (the stream header, the block, an index
// with its one record and a footer), which XZ Embedded decodes in
// single-call mode straight into out. The block's check and the index
// record (its sizes) are verified as in any stream.
bool
DecodeBlock(int fd, const XzBlock& block, xz_dec* decoder,
	std::vector<uint8_t>& input, std::vector<uint8_t>& out)
{
	uint64_t padded = (block.unpaddedSize + 3) & ~uint64_t(3);
	input.clear();
	input.insert(input.end(), kXzMagic, kXzMagic + sizeof(kXzMagic));
	input.insert(input.end(), block.streamFlags, block.streamFlags + 2);
	AppendLe32(input, xz_crc32(block.streamFlags, 2, 0));
	size_t blockStart = input.size();
	input.resize(blockStart + padded);
	if (!ReadAt(fd, block.offset, input.data() + blockStart, padded))
		return false;

	size_t indexStart = input.size();
	input.push_back(0);
	AppendVli(input, 1);
	AppendVli(input, block.unpaddedSize);
	AppendVli(input, block.uncompressedSize);
	while (input.size() % 4 != 0)
		input.push_back(0);
	AppendLe32(input, xz_crc32(input.data() + indexStart,
		input.size() - indexStart, 0));
	uint32_t backwardSize = (input.size() - indexStart) / 4 - 1;

	uint8_t footer[6];
	for (int i = 0; i < 4; i++)
		footer[i] = uint8_t(backwardSize >> (i * 8));
	memcpy(footer + 4, block.streamFlags, 2);
	AppendLe32(input, xz_crc32(footer, sizeof(footer), 0));
	input.insert(input.end(), footer, footer + sizeof(footer));
	input.push_back('Y');
	input.push_back('Z');

	out.resize(block.uncompressedSize);
	xz_buf buffer;
	buffer.in = input.data();
	buffer.in_pos = 0;
	buffer.in_size = input.size();
	buffer.out = out.data();
	buffer.out_pos = 0;
	buffer.out_size = out.size();
	return xz_dec_run(decoder, &buffer) == XZ_STREAM_END
		&& buffer.out_pos == out.size();
}


// An xz file with several blocks, decoded on several threads: each worker
// takes the next block not yet taken, at most a few blocks ahead of the
// reader, and Read() hands the blocks out in order.
class ParallelXzReader : public ImageReader {
public:
	ParallelXzReader(int fd, std::vector<XzBlock> blocks, int threads)
		:
		fFd(fd),
		fBlocks(std::move(blocks)),
		fWindow(threads + 2),
		fClaimed(0),
		fNext(0),
		fFailed(false),
		fStop(false),
		fCurrentPosition(0),
		fHaveCurrent(false)
	{
		for (int i = 0; i < threads; i++)
			fThreads.emplace_back(&ParallelXzReader::_Work, this);
	}

	~ParallelXzReader() override
	{
		{
			std::lock_guard<std::mutex> lock(fLock);
			fStop = true;
		}
		fCondition.notify_all();
		for (std::thread& thread : fThreads)
			thread.join();
		close(fFd);
	}

	int64_t Read(uint8_t* buffer, size_t size, std::string& error) override
	{
		size_t done = 0;
		while (done < size) {
			if (!fHaveCurrent) {
				std::unique_lock<std::mutex> lock(fLock);
				if (fNext == fBlocks.size())
					break;
				fCondition.wait(lock, [this] {
					return fFailed || fReady.count(fNext) != 0;
				});
				if (fFailed) {
					error = kDamagedXz;
					return -1;
				}
				fCurrent = std::move(fReady[fNext]);
				fReady.erase(fNext);
				fCurrentPosition = 0;
				fHaveCurrent = true;
			}
			size_t bytes = std::min(size - done,
				fCurrent.size() - fCurrentPosition);
			memcpy(buffer + done, fCurrent.data() + fCurrentPosition, bytes);
			done += bytes;
			fCurrentPosition += bytes;
			if (fCurrentPosition == fCurrent.size()) {
				fHaveCurrent = false;
				std::vector<uint8_t>().swap(fCurrent);
				{
					std::lock_guard<std::mutex> lock(fLock);
					fNext++;
				}
				fCondition.notify_all();
			}
		}
		return done;
	}

	uint64_t SourcePosition() const override
	{
		size_t next = fNext;
		return next < fBlocks.size() ? fBlocks[next].offset : 0;
	}

private:
	void _Work()
	{
		xz_dec* decoder = xz_dec_init(XZ_SINGLE, 0);
		std::vector<uint8_t> input;
		while (true) {
			size_t index;
			{
				std::unique_lock<std::mutex> lock(fLock);
				fCondition.wait(lock, [this] {
					return fStop || fFailed || fClaimed == fBlocks.size()
						|| fClaimed < fNext + fWindow;
				});
				if (fStop || fFailed || fClaimed == fBlocks.size())
					break;
				index = fClaimed++;
			}
			std::vector<uint8_t> out;
			bool ok = decoder != NULL
				&& DecodeBlock(fFd, fBlocks[index], decoder, input, out);
			{
				std::lock_guard<std::mutex> lock(fLock);
				if (ok)
					fReady[index] = std::move(out);
				else
					fFailed = true;
			}
			fCondition.notify_all();
		}
		if (decoder != NULL)
			xz_dec_end(decoder);
	}

	int						fFd;
	std::vector<XzBlock>	fBlocks;
	size_t					fWindow;
	std::vector<std::thread> fThreads;
	std::mutex				fLock;
	std::condition_variable	fCondition;
	size_t					fClaimed;
	std::atomic<size_t>		fNext;
	std::map<size_t, std::vector<uint8_t>> fReady;
	bool					fFailed;
	bool					fStop;
	// The block Read() is handing out; only the reader's thread uses these.
	std::vector<uint8_t>	fCurrent;
	size_t					fCurrentPosition;
	bool					fHaveCurrent;
};

}  // namespace


bool
ReadXzIndex(int fd, uint64_t fileSize, std::vector<XzBlock>& blocks,
	std::string& error)
{
	InitCrcTables();
	blocks.clear();
	uint64_t position = fileSize;
	int streams = 0;
	while (position > 0) {
		// Stream padding: groups of four null bytes after a stream.
		uint8_t word[4];
		while (position >= 4) {
			if (!ReadAt(fd, position - 4, word, 4)) {
				error = kDamagedXz;
				return false;
			}
			if (Le32(word) != 0)
				break;
			position -= 4;
		}
		if (position == 0 && streams > 0)
			break;
		if (position < kXzHeaderSize + kXzFooterSize || position % 4 != 0) {
			error = kDamagedXz;
			return false;
		}

		uint8_t footer[kXzFooterSize];
		if (!ReadAt(fd, position - kXzFooterSize, footer, sizeof(footer))
			|| footer[10] != 'Y' || footer[11] != 'Z'
			|| xz_crc32(footer + 4, 6, 0) != Le32(footer)) {
			error = kDamagedXz;
			return false;
		}
		uint64_t indexSize = (uint64_t(Le32(footer + 4)) + 1) * 4;
		if (indexSize > kMaxXzIndexSize
			|| indexSize + kXzHeaderSize + kXzFooterSize > position) {
			error = kDamagedXz;
			return false;
		}
		uint64_t indexStart = position - kXzFooterSize - indexSize;
		std::vector<uint8_t> index(indexSize);
		if (!ReadAt(fd, indexStart, index.data(), indexSize)
			|| index[0] != 0
			|| xz_crc32(index.data(), indexSize - 4, 0)
				!= Le32(index.data() + indexSize - 4)) {
			error = kDamagedXz;
			return false;
		}

		const uint8_t* p = index.data() + 1;
		const uint8_t* end = index.data() + indexSize - 4;
		uint64_t records;
		if (!ReadVli(p, end, records) || records > indexSize) {
			error = kDamagedXz;
			return false;
		}
		std::vector<XzBlock> streamBlocks;
		uint64_t blocksSize = 0;
		for (uint64_t i = 0; i < records; i++) {
			XzBlock block;
			if (!ReadVli(p, end, block.unpaddedSize)
				|| !ReadVli(p, end, block.uncompressedSize)
				|| block.unpaddedSize == 0) {
				error = kDamagedXz;
				return false;
			}
			block.offset = blocksSize;	// within the stream, for now
			memcpy(block.streamFlags, footer + 8, 2);
			blocksSize += (block.unpaddedSize + 3) & ~uint64_t(3);
			if (blocksSize > position) {
				error = kDamagedXz;
				return false;
			}
			streamBlocks.push_back(block);
		}
		// Index padding up to a multiple of four bytes, all zero.
		for (; p < end; p++) {
			if (*p != 0) {
				error = kDamagedXz;
				return false;
			}
		}
		if (blocksSize + kXzHeaderSize > indexStart) {
			error = kDamagedXz;
			return false;
		}
		uint64_t streamStart = indexStart - blocksSize - kXzHeaderSize;
		uint8_t header[kXzHeaderSize];
		if (!ReadAt(fd, streamStart, header, sizeof(header))
			|| memcmp(header, kXzMagic, sizeof(kXzMagic)) != 0
			|| memcmp(header + 6, footer + 8, 2) != 0) {
			error = kDamagedXz;
			return false;
		}
		for (XzBlock& block : streamBlocks)
			block.offset += streamStart + kXzHeaderSize;
		// The walk goes from the last stream to the first.
		blocks.insert(blocks.begin(), streamBlocks.begin(), streamBlocks.end());
		position = streamStart;
		streams++;
	}
	if (streams == 0) {
		error = kDamagedXz;
		return false;
	}
	return true;
}


bool
XzUncompressedSize(int fd, uint64_t fileSize, uint64_t& size,
	std::string& error)
{
	std::vector<XzBlock> blocks;
	if (!ReadXzIndex(fd, fileSize, blocks, error))
		return false;
	size = 0;
	for (const XzBlock& block : blocks) {
		size += block.uncompressedSize;
		if (size < block.uncompressedSize) {
			error = kDamagedXz;
			return false;
		}
	}
	return true;
}


bool
InspectImage(const std::string& path, ImageInfo& info, std::string& error)
{
	info = ImageInfo();
	info.path = path;
	size_t slash = path.rfind('/');
	info.name = slash == std::string::npos ? path : path.substr(slash + 1);

	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0) {
		error = ErrnoText("Could not open the image", errno);
		return false;
	}
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		error = "The image is not a regular file.";
		return false;
	}
	info.fileSize = st.st_size;
	if (info.fileSize == 0) {
		close(fd);
		error = "The image file is empty.";
		return false;
	}

	uint8_t magic[sizeof(kXzMagic)];
	bool xz = info.fileSize >= sizeof(magic)
		&& ReadAt(fd, 0, magic, sizeof(magic))
		&& memcmp(magic, kXzMagic, sizeof(magic)) == 0;
	if (xz) {
		info.compression = Compression::kXz;
		std::vector<XzBlock> blocks;
		bool ok = ReadXzIndex(fd, info.fileSize, blocks, error);
		close(fd);
		if (!ok)
			return false;
		for (const XzBlock& block : blocks)
			info.imageSize += block.uncompressedSize;
		info.xzBlocks = blocks.size();
		if (info.imageSize == 0) {
			error = "The xz file holds no data.";
			return false;
		}
	} else {
		info.imageSize = info.fileSize;
		close(fd);
	}

	// The first 64 KiB tell what kind of image it is.
	std::unique_ptr<ImageReader> reader = OpenImage(info, error, 1);
	if (!reader)
		return false;
	std::vector<uint8_t> head(64 * 1024);
	int64_t bytes = reader->Read(head.data(),
		std::min<uint64_t>(head.size(), info.imageSize), error);
	if (bytes < 0)
		return false;
	info.kind = DescribeImage(head.data(), bytes);
	return true;
}


std::unique_ptr<ImageReader>
OpenImage(const ImageInfo& info, std::string& error, int threads)
{
	InitCrcTables();
	int fd = open(info.path.c_str(), O_RDONLY);
	if (fd < 0) {
		error = ErrnoText("Could not open the image", errno);
		return nullptr;
	}
	if (info.compression == Compression::kNone)
		return std::unique_ptr<ImageReader>(new RawReader(fd, info.imageSize));

	if (threads <= 0) {
		threads = std::min<unsigned>(std::thread::hardware_concurrency(),
			kMaxDecodeThreads);
	}
	if (threads > 1) {
		// Blocks are decoded whole, in memory: only for files whose blocks
		// are of a sensible size (xz -T0 -6 makes 24 MiB blocks).
		std::vector<XzBlock> blocks;
		if (!ReadXzIndex(fd, info.fileSize, blocks, error)) {
			close(fd);
			return nullptr;
		}
		uint64_t largest = 0;
		for (const XzBlock& block : blocks)
			largest = std::max(largest, block.uncompressedSize);
		if (blocks.size() > 1 && largest <= kMaxParallelBlock) {
			threads = std::min<size_t>(threads, blocks.size());
			return std::unique_ptr<ImageReader>(
				new ParallelXzReader(fd, std::move(blocks), threads));
		}
	}

	XzReader* reader = new XzReader(fd, info.imageSize);
	if (!reader->InitCheck()) {
		delete reader;
		error = "Not enough memory to decompress the image.";
		return nullptr;
	}
	return std::unique_ptr<ImageReader>(reader);
}

}  // namespace imager
