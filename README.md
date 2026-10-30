# airImager

airImager writes air/OS images to USB sticks and SD cards, on air/OS. It is a
native Haiku application, found in the Deskbar menu under **Utilities ›
airImager**.

1. **Image**: choose an image from [airos.works/downloads](https://airos.works/downloads/):
   the compressed `.iso.xz` (PCs, UEFI ARM64 machines) or `.img.xz` (Raspberry
   Pi 4, Cubie A7S) as downloaded, or a plain `.iso` or `.img`. You can also drop
   the file on the window, or open it with airImager from Tracker.
2. **Drive**: choose the USB stick or SD card to write to.
3. **Write**: airImager erases the drive, writes the image, then reads the whole
   drive back to verify it. The bar shows the progress, the write speed and the
   time left. An alert (and a notification, if the window is in the background)
   reports the result.

## Safety

- **The drive air/OS is running from is never offered.** It is listed, greyed
  out, as *air/OS boot drive*, and every path to a write (the list, scripting,
  and a last check just before writing) refuses it.
- Internal drives (NVMe, SATA, virtual disks) are hidden unless **Show internal
  drives** is checked, and choosing one asks again.
- The drive holding the image, read-only drives, empty card readers and drives
  smaller than the image cannot be chosen; the list says why.
- Nothing is written before you confirm. The confirmation names the drive, its
  size and bus, and the volumes that will be unmounted.
- **Downloads are checked.** If the image's `.sha256` file from airos.works is
  in the same folder, airImager checks the image against it first and refuses a
  damaged or incomplete download. An `.xz` file whose end is missing (a download
  that stopped early) is refused as soon as it is chosen.
- **Every byte is verified.** Each 4 MiB chunk is hashed (XXH64) as it is
  written, and again as it is read back from the drive after its cache has been
  flushed. A difference, such as a failing or fake-capacity drive, is reported
  with where it happened. (The airos.works guide warns that another popular
  writer once dropped 2 MB of an image and still reported success.)

## Compressed images

airImager reads `.xz` files itself, with [XZ Embedded](https://github.com/tukaani-project/xz-embedded)
(0BSD, in `third_party/xz-embedded`), so nothing needs to be extracted first and
no extra package is needed. The image's size comes from the xz index, so the
size check and the time left are known from the start. The airos.works images
are compressed with `xz -T0`, in many independent blocks, and airImager
decompresses up to four blocks at once (about 380 MiB/s on a Ryzen
workstation), so decompression keeps up with fast USB 3 drives.

## Building

On Haiku or air/OS, with the development tools that come with it:

```sh
make             # build-haiku/airImager
make check       # engine tests (xz, hashing, write and verify, drive rules)
make package     # artifacts/airimager-<version>-<arch>.hpkg
```

From Linux with an air/OS cross SDK (airos-ci `sdk/build-sdk.sh`):

```sh
tools/build-cross.sh x86_64      # or arm64; build-<arch>/airImager
make check-host                  # the engine tests on Linux
```

The application icon and the three step illustrations use native HVIF vectors.
Their sources are `tools/make-icon.py` and the native USB drive artwork in
`tools/artwork/usb-drive.hvif`; run `make icon` to regenerate all three
assets (the Write step shares the application icon). For PNG previews, run
`python3 tools/make-icon.py --preview build-host/icon-preview` with Pillow
installed. The artwork follows [Haiku's icon guidelines](https://www.haiku-os.org/development/icon-guidelines/)
for perspective, dark silhouettes, restrained highlights and upper-left lighting.

The air/OS build server builds the x86_64 and arm64 packages on every push
to `main` (airos-ci `apps/build-app-packages.sh`, `build_airimager`), and every
air/OS image (x86_64, ARM64 UEFI, Raspberry Pi 4 and Cubie A7S) includes
airImager.

## Scripting

```sh
hey airImager set Image to /boot/home/Downloads/airos-rpi4.img.xz
hey airImager get Drives
hey airImager set Target to /dev/disk/usb/0/0/raw
hey airImager do Write          # no confirmation when scripted
hey airImager get Status        # idle, checking 40%, writing 12% 38 MiB/s, done: verified, ...
hey airImager do Cancel
```

`Verify` (bool) turns verification off and on. Scripting obeys the same drive
rules as the window.

See [docs/DESIGN.md](docs/DESIGN.md) for how it works and
[docs/TESTING.md](docs/TESTING.md) for how it was tested.

## Latest builds

<!-- airos-ci:latest-builds:start -->
## Latest builds

Built automatically by air/OS CI from commit `d31891a` on 2026-10-30 ([all files](https://github.com/airos-works/airimager/releases/tag/latest)).

| Architecture | Package |
|---|---|
| arm64 | [airimager-1.0.0-4-arm64.hpkg](https://github.com/airos-works/airimager/releases/download/latest/airimager-1.0.0-4-arm64.hpkg) |
| x86_64 | [airimager-1.0.0-4-x86_64.hpkg](https://github.com/airos-works/airimager/releases/download/latest/airimager-1.0.0-4-x86_64.hpkg) |

Install with `pkgman install <file>`, or copy the file into `/boot/system/packages`. Haiku SDK: x86_64 hrev60207-2114-g2646c1fb83, arm64 hrev60207-2115-g8e97a85464.
<!-- airos-ci:latest-builds:end -->

## Licence

MIT (see `LICENSE`). XZ Embedded is 0BSD (`third_party/xz-embedded/COPYING`).
The USB drive artwork is from Haiku's USB disk driver (`kDeviceIcon` in
`src/add-ons/kernel/drivers/disk/usb/usb_disk/icons.h`), used under the MIT
license. The Drive illustration retains it, and the application/Write icon
adds a green arrow.
