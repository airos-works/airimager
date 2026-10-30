# airImager design

## Layout

| Part | Files | Notes |
| --- | --- | --- |
| Engine (portable) | `src/engine/` | No Haiku API. Built and tested on Linux too. |
| Image reading | `ImageSource.*` | Raw files, and xz through XZ Embedded. |
| Write and verify | `WriteJob.*` | Pipeline, flush, read-back. |
| Drive rules | `Drive.*` | Which drives may be written, and how they are labelled. |
| Download check | `Checksum.*` | `<image>.sha256` beside the image. |
| Hash, formatting | `Hash.*`, `Format.*` | XXH64; sizes, speeds, durations, the rate meter. |
| Drives on Haiku | `src/DriveRoster.*` | `BDiskDeviceRoster`: listing, the boot drive, unmounting, cache flush. |
| Threads | `src/Workers.*` | Write and checksum threads that report with messages. |
| Windows | `src/MainWindow.*`, `src/DriveWindow.*`, `src/Views.*` | |
| Application | `src/App.*` | Settings, Tracker's *Open with*, `hey` scripting. |

## Reading images

The format is told by the file's first bytes (`FD 37 7A 58 5A 00` is xz), not
by its name. Everything else is written as it is: ISO 9660 hybrids (the
x86_64 and ARM64 `.iso`), MBR and GPT disk images (the Pi and Cubie `.img`).
The image kind shown under the name comes from the first 64 KiB.

For xz, `ReadXzIndex()` walks the file from its end: stream padding, the
stream footer (its CRC32 and backward size), the index (its CRC32, records of
unpadded and uncompressed block sizes), back to the stream header, for every
stream of the file. That gives the uncompressed size before decompressing
anything, and a truncated download (no footer) is refused at once.

Two decoders:

- `XzReader`: XZ Embedded in multi-call mode with `xz_dec_catrun()`
  (concatenated streams, stream padding). Used for single-block files.
- `ParallelXzReader`: for files with several blocks (the airos.works images are
  made with `xz -T0 -6`: 24 MiB blocks). Up to four worker threads each take
  the next block, at most `threads + 2` blocks ahead of the writer, and decode
  it alone in single-call mode. XZ Embedded has no block-level API, so a block
  is wrapped in a stream of its own: the real stream header flags, the block's
  bytes, an index with its one record (unpadded and uncompressed size from the
  real index) and a footer. The decoder then checks the block's own check
  (CRC64 for xz's default) and the sizes, as in any stream. Blocks over 256
  MiB fall back to the streaming decoder (memory).

Measured on the build server (Threadripper) with the 4 GiB Pi 4 image (1.0 GB
xz): streaming 119 MiB/s, parallel 381 MiB/s; `xz -dc -T1` 277 MiB/s.

## Writing and verifying

`RunWriteJob()`:

1. A decoder thread fills 4 MiB page-aligned chunks (four in flight) and
   hashes each with XXH64.
2. The job's thread writes them in order with `pwrite()`. The last chunk is
   zero-padded to a whole sector (`B_GET_GEOMETRY`), past the image's end.
3. `fsync()`, then `B_FLUSH_DRIVE_CACHE`. Drivers without it are fine, and so
   are USB card readers and sticks that reject SYNCHRONIZE CACHE (usb_disk
   answers `B_UNSUPPORTED`): the read-back still checks every byte.
4. Every chunk is read back (sector-rounded) and hashed; the first chunk whose
   hash differs fails the job with its offset.

Raw devices on Haiku are not cached by the kernel (only file systems use the
block and file caches), so the read-back reads the medium. Cancelling is
checked between chunks.

## Drives

`DriveRoster::Scan()` lists every `BDiskDevice` with its raw device path, size,
block size, media flags, model (`B_GET_DEVICE_NAME`: "Verbatim STORE N GO",
"Samsung SSD 950 PRO") and mounted volumes. The bus comes from the path
(`/disk/usb/`, `/disk/mmc/`, `/disk/nvme/`, `/disk/scsi/`, `/disk/virtual/files/`,
...).

The **boot drive** is the device of the partition holding the boot volume
(`BVolumeRoster::GetBootVolume()` and `FindPartitionByVolume()`), or the one
mounted at `/boot`; a partition mounted at `/boot` found while listing marks
its drive too. `Assess()` puts *boot drive* ahead of every other state, so it
is refused even if it is also removable, big enough and so on. The check is
repeated when Write is pressed and again after the confirmation, just before
the volumes are unmounted, so a drive list that went stale cannot slip
through.

USB drives, SD cards and disk image files (`diskimage register`) are external;
NVMe, SATA and virtual disks are internal (unless the driver reports removable
media) and hidden by default.

Before writing, every mounted volume of the drive is unmounted
(`BPartition::Unmount()`); if one is busy the write does not start. While
writing, the window ignores disk device notifications (the unmounting sends
some).

## Download check

If `<image>.sha256` exists, its line for the image's name (or its only line,
for a renamed download) gives the expected SHA-256. A worker hashes the file
(XZ Embedded's SHA-256, about 190 MB/s) while the window stays usable; Write is
enabled once it passes. A mismatch blocks writing.
