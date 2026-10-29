// EngineTests.cpp - tests of the platform independent parts of airImager:
// the hash, the xz index walk and decoder, the write/verify job (with a file
// as the drive), the drive rules and the formatting. Runs anywhere with a
// C++17 compiler and the xz command (make check-host, or make check on Haiku).
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "engine/Checksum.h"
#include "engine/Drive.h"
#include "engine/Format.h"
#include "engine/Hash.h"
#include "engine/ImageSource.h"
#include "engine/WriteJob.h"

using namespace imager;

static int sFailures = 0;
static int sChecks = 0;

#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			sFailures++; \
			fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, \
				#condition); \
		} \
	} while (0)

#define CHECK_EQ(a, b) \
	do { \
		sChecks++; \
		auto _a = (a); \
		auto _b = (b); \
		if (!(_a == _b)) { \
			sFailures++; \
			fprintf(stderr, "%s:%d: %s == %s failed\n", __FILE__, __LINE__, \
				#a, #b); \
		} \
	} while (0)


static std::string sDir;


static std::string
TempPath(const std::string& name)
{
	return sDir + "/" + name;
}


static void
WriteFile(const std::string& path, const std::vector<uint8_t>& data)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(reinterpret_cast<const char*>(data.data()), data.size());
}


static std::vector<uint8_t>
ReadFile(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
		std::istreambuf_iterator<char>());
}


static bool
Run(const std::string& command)
{
	return system(command.c_str()) == 0;
}


// Something like a disk image: an MBR signature, a stretch of random
// (incompressible) data, long runs of zeros and some text.
static std::vector<uint8_t>
MakeImage(size_t size, unsigned seed)
{
	std::vector<uint8_t> data(size, 0);
	std::mt19937 random(seed);
	for (size_t i = 0; i < size; i++) {
		size_t region = (i / 65536) % 4;
		if (region == 0)
			data[i] = random() & 0xFF;
		else if (region == 2)
			data[i] = "air/OS image "[i % 13];
	}
	if (size >= 512) {
		data[510] = 0x55;
		data[511] = 0xAA;
	}
	return data;
}


// A Target on a file that can be told to corrupt reads or fail writes.
class TestTarget : public Target {
public:
	TestTarget(const std::string& path, uint32_t blockSize)
		:
		fFd(open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644)),
		fFile(fFd, blockSize),
		fFlipAt(UINT64_MAX),
		fFailWriteAt(UINT64_MAX),
		fUnaligned(0),
		fFlushes(0)
	{
	}

	~TestTarget() override { close(fFd); }

	bool Write(const uint8_t* data, size_t size, uint64_t offset,
		std::string& error) override
	{
		if (size % BlockSize() != 0 || offset % BlockSize() != 0)
			fUnaligned++;
		if (offset <= fFailWriteAt && fFailWriteAt < offset + size) {
			error = "write failed";
			return false;
		}
		return fFile.Write(data, size, offset, error);
	}

	bool Flush(std::string& error) override
	{
		fFlushes++;
		return fFile.Flush(error);
	}

	bool Read(uint8_t* data, size_t size, uint64_t offset,
		std::string& error) override
	{
		if (size % BlockSize() != 0 || offset % BlockSize() != 0)
			fUnaligned++;
		if (!fFile.Read(data, size, offset, error))
			return false;
		if (offset <= fFlipAt && fFlipAt < offset + size)
			data[fFlipAt - offset] ^= 0x10;
		return true;
	}

	uint32_t BlockSize() const override { return fFile.BlockSize(); }

	int			fFd;
	FdTarget	fFile;
	uint64_t	fFlipAt;
	uint64_t	fFailWriteAt;
	int			fUnaligned;
	int			fFlushes;
};


static JobResult
WriteImage(const ImageInfo& info, TestTarget& target, JobOptions options = {},
	std::atomic<bool>* cancelFlag = NULL, int cancelAfter = -1,
	std::vector<Progress>* log = NULL)
{
	std::atomic<bool> localCancel(false);
	std::atomic<bool>& cancel = cancelFlag != NULL ? *cancelFlag : localCancel;
	int calls = 0;
	return RunWriteJob(info, target, options, cancel,
		[&](const Progress& progress) {
			if (log != NULL)
				log->push_back(progress);
			if (++calls == cancelAfter)
				cancel = true;
		});
}


static void
TestHash()
{
	CHECK_EQ(Xxh64("", 0), 0xEF46DB3751D8E999ULL);
	CHECK_EQ(Xxh64("a", 1), 0xD24EC4F1A98C6E5BULL);
	CHECK_EQ(Xxh64("abc", 3), 0x44BC2CF5AD770999ULL);
	// Every length up to 100 bytes takes every path of the tail code; the
	// hash must change when any byte does.
	std::vector<uint8_t> data = MakeImage(100, 1);
	for (size_t length = 1; length <= data.size(); length++) {
		uint64_t before = Xxh64(data.data(), length);
		data[length - 1] ^= 1;
		CHECK(Xxh64(data.data(), length) != before);
		data[length - 1] ^= 1;
	}
	CHECK(Xxh64("abc", 3, 1) != Xxh64("abc", 3, 0));
}


static void
TestFormat()
{
	CHECK_EQ(FormatBytes(0), std::string("0 B"));
	CHECK_EQ(FormatBytes(1023), std::string("1023 B"));
	CHECK_EQ(FormatBytes(1536), std::string("1.5 KiB"));
	CHECK_EQ(FormatBytes(32ULL * 1000 * 1000 * 1000), std::string("29.8 GiB"));
	CHECK_EQ(FormatBytes(1024ULL * 1024 * 1024 - 1), std::string("1.0 GiB"));
	CHECK_EQ(FormatBytes(500ULL * 1024 * 1024), std::string("500 MiB"));
	CHECK_EQ(FormatRate(0), std::string("0 B/s"));
	CHECK_EQ(FormatRate(38.2 * 1024 * 1024), std::string("38.2 MiB/s"));
	CHECK_EQ(FormatDuration(-3), std::string("0 s"));
	CHECK_EQ(FormatDuration(44.6), std::string("45 s"));
	CHECK_EQ(FormatDuration(130), std::string("2 min 10 s"));
	CHECK_EQ(FormatDuration(3900), std::string("1 h 5 min"));

	RateMeter meter(4.0);
	CHECK_EQ(meter.Rate(), 0.0);
	CHECK(meter.SecondsLeft(100) < 0);
	meter.Add(0, 0);
	meter.Add(0.5, 50);
	CHECK_EQ(meter.Rate(), 0.0);		// under a second of samples
	for (int i = 2; i <= 20; i++)
		meter.Add(i * 0.5, i * 50);		// 100 bytes per second
	CHECK(meter.Rate() > 99.9 && meter.Rate() < 100.1);
	CHECK(meter.SecondsLeft(2000) > 9.9 && meter.SecondsLeft(2000) < 10.1);
	// The speed drops to 10 bytes per second: after a full window only the
	// new speed counts.
	for (int i = 21; i <= 30; i++)
		meter.Add(i * 0.5, 1000 + (i - 20) * 5);
	CHECK(meter.Rate() > 9.9 && meter.Rate() < 10.1);
	meter.Reset();
	CHECK_EQ(meter.Rate(), 0.0);
}


static void
TestDrives()
{
	CHECK(BusForPath("/dev/disk/usb/0/0/raw") == DriveBus::kUsb);
	CHECK(BusForPath("/dev/disk/mmc/1/raw") == DriveBus::kSdCard);
	CHECK(BusForPath("/dev/disk/nvme/0/raw") == DriveBus::kNvme);
	CHECK(BusForPath("/dev/disk/scsi/0/0/0/raw") == DriveBus::kSata);
	CHECK(BusForPath("/dev/disk/ata/0/master/raw") == DriveBus::kSata);
	CHECK(BusForPath("/dev/disk/virtual/files/3/raw") == DriveBus::kFile);
	CHECK(BusForPath("/dev/disk/virtual/virtio_block/0/raw")
		== DriveBus::kVirtual);

	DriveInfo usb;
	usb.path = "/dev/disk/usb/0/0/raw";
	usb.bus = DriveBus::kUsb;
	usb.size = 16ULL << 30;
	CHECK(Assess(usb, 4ULL << 30) == DriveState::kAvailable);
	CHECK(Assess(usb, 0) == DriveState::kAvailable);
	CHECK(Assess(usb, 17ULL << 30) == DriveState::kTooSmall);
	CHECK(!IsInternal(usb));

	// The boot drive is never available, removable or not, big enough or
	// not; that outranks every other reason.
	DriveInfo boot = usb;
	boot.isBoot = true;
	CHECK(Assess(boot, 1) == DriveState::kBootDrive);
	boot.readOnly = true;
	boot.holdsImage = true;
	boot.hasMedia = false;
	CHECK(Assess(boot, 1ULL << 50) == DriveState::kBootDrive);
	CHECK_EQ(StateLabel(DriveState::kBootDrive),
		std::string("air/OS boot drive"));

	DriveInfo source = usb;
	source.holdsImage = true;
	CHECK(Assess(source, 1) == DriveState::kHoldsImage);

	DriveInfo cd = usb;
	cd.optical = true;
	CHECK(Assess(cd, 1) == DriveState::kReadOnly);

	DriveInfo empty = usb;
	empty.hasMedia = false;
	CHECK(Assess(empty, 1) == DriveState::kNoMedia);

	DriveInfo nvme;
	nvme.bus = DriveBus::kNvme;
	nvme.size = 1ULL << 40;
	CHECK(IsInternal(nvme));
	CHECK(Assess(nvme, 1) == DriveState::kAvailable);
	nvme.removable = true;
	CHECK(!IsInternal(nvme));

	CHECK_EQ(DisplayName(usb), std::string("USB drive"));
	usb.model = "SanDisk Ultra";
	CHECK_EQ(DisplayName(usb), std::string("SanDisk Ultra"));
}


static void
TestRawImage()
{
	// A size that is not a whole number of sectors: the last write is
	// padded, verification still covers exactly the image.
	std::vector<uint8_t> data = MakeImage(10 * 1024 * 1024 + 300, 2);
	std::string path = TempPath("raw.img");
	WriteFile(path, data);

	ImageInfo info;
	std::string error;
	CHECK(InspectImage(path, info, error));
	CHECK(info.compression == Compression::kNone);
	CHECK_EQ(info.imageSize, uint64_t(data.size()));
	CHECK_EQ(info.name, std::string("raw.img"));
	CHECK_EQ(info.kind, std::string("Disk image (MBR)"));

	std::string drive = TempPath("drive-raw");
	std::vector<Progress> log;
	JobResult result;
	{
		TestTarget target(drive, 512);
		result = WriteImage(info, target, JobOptions(), NULL, -1, &log);
		CHECK_EQ(target.fUnaligned, 0);
		CHECK_EQ(target.fFlushes, 1);
	}
	CHECK(result.status == JobResult::kSucceeded);
	CHECK_EQ(result.written, uint64_t(data.size()));
	std::vector<uint8_t> written = ReadFile(drive);
	CHECK_EQ(written.size(), (data.size() + 511) / 512 * 512);
	CHECK(std::equal(data.begin(), data.end(), written.begin()));
	CHECK(written.back() == 0);

	// Progress: writing up to the size, a flush, verifying up to the size.
	bool sawFlush = false;
	uint64_t lastWrite = 0, lastVerify = 0;
	for (const Progress& p : log) {
		CHECK_EQ(p.total, uint64_t(data.size()));
		if (p.phase == Phase::kWriting) {
			CHECK(!sawFlush && p.done >= lastWrite);
			lastWrite = p.done;
		} else if (p.phase == Phase::kFlushing)
			sawFlush = true;
		else {
			CHECK(sawFlush && p.done >= lastVerify);
			lastVerify = p.done;
		}
	}
	CHECK(sawFlush);
	CHECK_EQ(lastWrite, uint64_t(data.size()));
	CHECK_EQ(lastVerify, uint64_t(data.size()));

	// 4 KiB sectors.
	{
		TestTarget target(drive, 4096);
		result = WriteImage(info, target);
		CHECK(result.status == JobResult::kSucceeded);
		CHECK_EQ(target.fUnaligned, 0);
	}
	CHECK_EQ(ReadFile(drive).size(), (data.size() + 4095) / 4096 * 4096);

	// An image that is an exact number of chunks.
	std::vector<uint8_t> even = MakeImage(8 * 1024 * 1024, 3);
	WriteFile(path, even);
	CHECK(InspectImage(path, info, error));
	{
		TestTarget target(drive, 512);
		result = WriteImage(info, target);
	}
	CHECK(result.status == JobResult::kSucceeded);
	CHECK(ReadFile(drive) == even);
}


static void
TestVerifyAndErrors()
{
	std::vector<uint8_t> data = MakeImage(9 * 1024 * 1024, 4);
	std::string path = TempPath("verify.img");
	WriteFile(path, data);
	ImageInfo info;
	std::string error;
	CHECK(InspectImage(path, info, error));
	std::string drive = TempPath("drive-verify");

	// One bit that does not read back is found, in its chunk.
	{
		TestTarget target(drive, 512);
		target.fFlipAt = 5 * 1024 * 1024 + 17;
		JobResult result = WriteImage(info, target);
		CHECK(result.status == JobResult::kVerifyFailed);
		CHECK_EQ(result.mismatchOffset, uint64_t(4 * 1024 * 1024));
		CHECK(result.message.find("differs") != std::string::npos);
	}
	// The last, partial chunk is checked too.
	{
		TestTarget target(drive, 512);
		target.fFlipAt = data.size() - 1;
		JobResult result = WriteImage(info, target);
		CHECK(result.status == JobResult::kVerifyFailed);
		CHECK_EQ(result.mismatchOffset, uint64_t(8 * 1024 * 1024));
	}
	// Without verification the flip goes unnoticed.
	{
		TestTarget target(drive, 512);
		target.fFlipAt = 100;
		JobOptions options;
		options.verify = false;
		JobResult result = WriteImage(info, target, options);
		CHECK(result.status == JobResult::kSucceeded);
		CHECK_EQ(result.verifySeconds, 0.0);
	}
	// A write error stops the job.
	{
		TestTarget target(drive, 512);
		target.fFailWriteAt = 6 * 1024 * 1024;
		JobResult result = WriteImage(info, target);
		CHECK(result.status == JobResult::kFailed);
		CHECK_EQ(result.message, std::string("write failed"));
		CHECK_EQ(result.written, uint64_t(4 * 1024 * 1024));
	}
	// Cancelling while writing, and while verifying.
	{
		TestTarget target(drive, 512);
		std::atomic<bool> cancel(false);
		JobResult result = WriteImage(info, target, JobOptions(), &cancel, 2);
		CHECK(result.status == JobResult::kCancelled);
		CHECK(result.written < data.size());
	}
	{
		TestTarget target(drive, 512);
		std::atomic<bool> cancel(false);
		// 1 + 3 writing, 1 flushing, then the first verify report
		JobResult result = WriteImage(info, target, JobOptions(), &cancel, 6);
		CHECK(result.status == JobResult::kCancelled);
		CHECK_EQ(result.written, uint64_t(data.size()));
		CHECK(result.message.find("not checked") != std::string::npos);
	}

	// Files that are not images.
	CHECK(!InspectImage(TempPath("missing.img"), info, error));
	WriteFile(TempPath("empty.img"), {});
	CHECK(!InspectImage(TempPath("empty.img"), info, error));
	CHECK(error.find("empty") != std::string::npos);
	CHECK(!InspectImage(sDir, info, error));
}


static void
TestXzImage(const std::string& name, const std::vector<uint8_t>& data,
	const std::string& xzOptions)
{
	std::string raw = TempPath(name + ".img");
	std::string xz = raw + ".xz";
	WriteFile(raw, data);
	unlink(xz.c_str());
	CHECK(Run("xz -k -q " + xzOptions + " '" + raw + "'"));

	ImageInfo info;
	std::string error;
	bool ok = InspectImage(xz, info, error);
	CHECK(ok);
	if (!ok) {
		fprintf(stderr, "  %s: %s\n", name.c_str(), error.c_str());
		return;
	}
	CHECK(info.compression == Compression::kXz);
	CHECK_EQ(info.imageSize, uint64_t(data.size()));
	CHECK_EQ(info.fileSize, uint64_t(ReadFile(xz).size()));

	// The streaming decoder, and the parallel one (multi-block files).
	for (int threads : { 1, 3 }) {
		std::string drive = TempPath("drive-" + name);
		JobOptions options;
		options.decodeThreads = threads;
		JobResult result;
		{
			TestTarget target(drive, 512);
			result = WriteImage(info, target, options);
		}
		CHECK(result.status == JobResult::kSucceeded);
		if (result.status != JobResult::kSucceeded) {
			fprintf(stderr, "  %s, %d threads: %s\n", name.c_str(), threads,
				result.message.c_str());
		}
		std::vector<uint8_t> written = ReadFile(drive);
		CHECK(written.size() >= data.size());
		CHECK(std::equal(data.begin(), data.end(), written.begin()));
	}
}


static void
TestXz()
{
	if (!Run("xz --version > /dev/null 2>&1")) {
		fprintf(stderr, "note: no xz command, the xz tests are skipped\n");
		return;
	}
	std::vector<uint8_t> data = MakeImage(12 * 1024 * 1024 + 4321, 5);

	// One block (xz's default), several blocks (as `xz -T0` writes the
	// airos.works images), CRC32, SHA-256 and no check, the x86 BCJ filter,
	// a large dictionary.
	TestXzImage("single", data, "-6 -T1");
	TestXzImage("blocks", data, "-6 -T2 --block-size=1MiB");
	// Blocks that do not line up with the job's 4 MiB chunks, and one
	// block per stream of a file made of several streams (below).
	TestXzImage("oddblocks", data, "-1 -T2 --block-size=1234567");
	TestXzImage("crc32", data, "-1 --check=crc32");
	TestXzImage("sha256", data, "-1 --check=sha256");
	TestXzImage("nocheck", data, "-1 --check=none");
	TestXzImage("bcj", data, "--x86 --lzma2=preset=1");
	TestXzImage("dict", data, "--lzma2=preset=1,dict=128MiB");

	// Two streams one after the other, with stream padding between and
	// after them.
	std::string a = TempPath("cat-a.img"), b = TempPath("cat-b.img");
	std::vector<uint8_t> first = MakeImage(3 * 1024 * 1024 + 7, 6);
	std::vector<uint8_t> second = MakeImage(2 * 1024 * 1024 + 5, 7);
	WriteFile(a, first);
	WriteFile(b, second);
	unlink((a + ".xz").c_str());
	unlink((b + ".xz").c_str());
	CHECK(Run("xz -k -q -1 '" + a + "' '" + b + "'"));
	std::vector<uint8_t> joined = ReadFile(a + ".xz");
	joined.insert(joined.end(), 8, 0);
	std::vector<uint8_t> secondXz = ReadFile(b + ".xz");
	joined.insert(joined.end(), secondXz.begin(), secondXz.end());
	joined.insert(joined.end(), 4, 0);
	std::string cat = TempPath("cat.img.xz");
	WriteFile(cat, joined);
	ImageInfo info;
	std::string error;
	CHECK(InspectImage(cat, info, error));
	CHECK_EQ(info.imageSize, uint64_t(first.size() + second.size()));
	CHECK(info.xzBlocks >= 2);
	std::vector<uint8_t> both = first;
	both.insert(both.end(), second.begin(), second.end());
	for (int threads : { 1, 2 }) {
		JobOptions options;
		options.decodeThreads = threads;
		{
			TestTarget target(TempPath("drive-cat"), 512);
			CHECK(WriteImage(info, target, options).status
				== JobResult::kSucceeded);
		}
		std::vector<uint8_t> written = ReadFile(TempPath("drive-cat"));
		CHECK(std::equal(both.begin(), both.end(), written.begin()));
	}

	// A download that stopped early: refused when it is chosen.
	std::vector<uint8_t> whole = ReadFile(TempPath("blocks.img.xz"));
	std::vector<uint8_t> cut(whole.begin(), whole.begin() + whole.size() / 2);
	WriteFile(TempPath("cut.img.xz"), cut);
	CHECK(!InspectImage(TempPath("cut.img.xz"), info, error));
	CHECK(error.find("incomplete or damaged") != std::string::npos);
	// Cut at a multiple of four bytes, inside the index.
	cut.assign(whole.begin(), whole.end() - 16);
	WriteFile(TempPath("cut.img.xz"), cut);
	CHECK(!InspectImage(TempPath("cut.img.xz"), info, error));

	// A damaged byte in the compressed data: the index is fine, so the
	// image is accepted, but the write stops with a clear error.
	std::vector<uint8_t> damaged = whole;
	damaged[damaged.size() / 3] ^= 0x55;
	WriteFile(TempPath("damaged.img.xz"), damaged);
	CHECK(InspectImage(TempPath("damaged.img.xz"), info, error));
	CHECK_EQ(info.xzBlocks, uint32_t(13));
	for (int threads : { 1, 3 }) {
		TestTarget target(TempPath("drive-damaged"), 512);
		JobOptions options;
		options.decodeThreads = threads;
		JobResult result = WriteImage(info, target, options);
		CHECK(result.status == JobResult::kFailed);
		CHECK(result.message.find("incomplete or damaged")
			!= std::string::npos);
	}

	// The kind of image is read from the decompressed data: an ISO 9660
	// volume descriptor at 32 KiB, with and without an MBR.
	std::vector<uint8_t> iso(128 * 1024, 0);
	memcpy(&iso[0x8001], "CD001", 5);
	WriteFile(TempPath("iso.iso"), iso);
	unlink(TempPath("iso.iso.xz").c_str());
	CHECK(Run("xz -k -q '" + TempPath("iso.iso") + "'"));
	CHECK(InspectImage(TempPath("iso.iso.xz"), info, error));
	CHECK_EQ(info.kind, std::string("ISO 9660 image"));
	iso[510] = 0x55;
	iso[511] = 0xAA;
	WriteFile(TempPath("iso.iso"), iso);
	CHECK(InspectImage(TempPath("iso.iso"), info, error));
	CHECK_EQ(info.kind, std::string("Bootable ISO image (hybrid)"));
	std::vector<uint8_t> gpt(4096, 0);
	memcpy(&gpt[512], "EFI PART", 8);
	WriteFile(TempPath("gpt.img"), gpt);
	CHECK(InspectImage(TempPath("gpt.img"), info, error));
	CHECK_EQ(info.kind, std::string("Disk image (GPT)"));
}


static void
TestChecksum()
{
	const char* abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	std::string image = TempPath("abc.img.xz");
	WriteFile(image, { 'a', 'b', 'c' });
	std::string sidecar;
	CHECK(!FindChecksumFile(image, sidecar));
	std::ofstream(image + ".sha256") << abc << "  abc.img.xz\n";
	CHECK(FindChecksumFile(image, sidecar));
	CHECK_EQ(sidecar, image + ".sha256");

	Sha256Digest digest;
	std::string error;
	CHECK(ReadChecksumFile(sidecar, "abc.img.xz", digest, error));
	std::atomic<bool> cancel(false);
	uint64_t lastDone = 0, lastTotal = 0;
	auto progress = [&](uint64_t done, uint64_t total) {
		lastDone = done;
		lastTotal = total;
	};
	CHECK(CheckSha256(image, digest, cancel, progress, error)
		== ChecksumResult::kMatch);
	CHECK_EQ(lastDone, uint64_t(3));
	CHECK_EQ(lastTotal, uint64_t(3));
	WriteFile(image, { 'a', 'b', 'd' });
	CHECK(CheckSha256(image, digest, cancel, progress, error)
		== ChecksumResult::kMismatch);
	cancel = true;
	CHECK(CheckSha256(image, digest, cancel, progress, error)
		== ChecksumResult::kCancelled);
	cancel = false;
	CHECK(CheckSha256(TempPath("nothing"), digest, cancel, progress, error)
		== ChecksumResult::kFailed);

	// sha256sum's variants: binary marker, a path, upper case, CRLF; the
	// right line of several; a renamed download with a one-line file.
	std::string upper = abc;
	for (char& c : upper)
		c = toupper(c);
	std::ofstream(sidecar) << "# comment\n"
		<< "0000000000000000000000000000000000000000000000000000000000000000"
		<< "  other.img.xz\n" << upper << " *dir/abc.img.xz\r\n";
	Sha256Digest parsed;
	CHECK(ReadChecksumFile(sidecar, "abc.img.xz", parsed, error));
	CHECK(memcmp(parsed.bytes, digest.bytes, 32) == 0);
	CHECK(!ReadChecksumFile(sidecar, "abc(1).img.xz", parsed, error));
	CHECK(error.find("no checksum for") != std::string::npos);
	std::ofstream(sidecar) << abc << "  abc.img.xz\n";
	CHECK(ReadChecksumFile(sidecar, "abc(1).img.xz", parsed, error));
	CHECK(memcmp(parsed.bytes, digest.bytes, 32) == 0);
	std::ofstream(sidecar) << "not a checksum\n";
	CHECK(!ReadChecksumFile(sidecar, "abc.img.xz", parsed, error));

	// A large file, against sha256sum where there is one.
	std::string big = TempPath("big.img");
	WriteFile(big, MakeImage(5 * 1024 * 1024 + 77, 9));
	if (Run("sha256sum '" + big + "' > '" + big + ".sha256' 2>/dev/null")) {
		CHECK(FindChecksumFile(big, sidecar));
		CHECK(ReadChecksumFile(sidecar, "big.img", parsed, error));
		CHECK(CheckSha256(big, parsed, cancel, progress, error)
			== ChecksumResult::kMatch);
	}
}


int
main()
{
	char pattern[] = "/tmp/airimager-tests-XXXXXX";
	const char* tmp = getenv("TMPDIR");
	std::string base = std::string(tmp != NULL ? tmp : "/tmp")
		+ "/airimager-tests-XXXXXX";
	std::vector<char> name(base.begin(), base.end());
	name.push_back('\0');
	char* dir = mkdtemp(name.data());
	if (dir == NULL)
		dir = mkdtemp(pattern);
	if (dir == NULL) {
		perror("mkdtemp");
		return 1;
	}
	sDir = dir;

	TestHash();
	TestFormat();
	TestDrives();
	TestRawImage();
	TestVerifyAndErrors();
	TestXz();
	TestChecksum();

	Run("rm -rf '" + sDir + "'");
	printf("%d checks, %d failed\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
