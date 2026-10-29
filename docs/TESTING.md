# Testing airImager

## Engine tests

`make check-host` (Linux) or `make check` (Haiku) builds `tests/EngineTests.cpp`
against the engine and runs it (about 340 checks). It needs the `xz` command
for the xz cases and uses `sha256sum` when present. Covered:

- XXH64 against the reference values; every tail length.
- Formatting of sizes, speeds and durations; the rate meter.
- Drive rules: buses from paths; the boot drive refused whatever else is
  true of it; too small, holds the image, read-only, optical, no media;
  internal and external.
- Raw images that are not a whole number of sectors, 512-byte and 4 KiB
  sectors, an exact number of chunks; progress phases in order.
- Verification catching one flipped bit (and in the last, partial chunk);
  write errors; cancelling while writing and while verifying.
- xz with one block, many blocks, blocks unaligned with the chunks, CRC32,
  SHA-256 and no check, the x86 BCJ filter, a 128 MiB dictionary,
  concatenated streams with stream padding, through both the streaming and the
  parallel decoder; a truncated file (refused when chosen); a damaged byte
  (accepted, then the write stops); image kinds (ISO, hybrid, GPT, MBR).
- `.sha256` files: match, mismatch, cancel; `*` marker, paths, upper case,
  CRLF, several lines, a renamed download.

## On air/OS

Tested on the X399 workstation (air/OS hrev60207+2112, x86_64) on 2026-10-30
with the published Pi 4 image `airos-rpi4.img.xz` (1.0 GB, 4.0 GB inside) and
its `.sha256`, writing to file-backed disks (`diskimage register`), never to a
real drive:

- The drive list showed the NVMe system drive as *air/OS boot drive* (greyed,
  only with *Show internal drives*), two real USB drives with their model
  names and icons, and the disk image file. `hey airImager set Target to
  /dev/disk/nvme/0/raw` was refused.
- The download check passed (green); Write waited for it.
- Write and verify finished with the completion alert. The first 4,030,726,144
  bytes of the target hashed to the manifest's `image_sha256`
  (`67a86894…80ea`), so the write is byte-exact.
- With a mounted BFS volume on the target: the confirmation named the volume,
  it was unmounted, and the write verified (hash checked again).
- Cancel mid-write: *Cancelled* with an explanation; the window is usable again.
- A `.sha256` that does not match: *Download damaged* alert, Write disabled.

A file-backed disk is cached by the file system holding its file, so its
read-back is fast. A USB stick or SD card is read back from the medium.

## Driving it

```sh
hey airImager set Image to /path/airos-rpi4.img.xz
hey airImager set Target to /dev/disk/virtual/files/N/raw
hey airImager do Write
hey airImager get Status
```

Window messages useful for screenshots: `hey airImager sdrv of Window 0` opens
the drive list, `writ of Window 0` presses Write (with its confirmation),
`ALTB of Window 1 with which=int32(1)` presses an alert's second button.
