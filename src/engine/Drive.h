// Drive.h - what airImager knows about a drive, and the rules for which
// drives may be written.
//
// The rules are kept apart from the Haiku code that finds the drives
// (DriveRoster) so they are tested on any machine. The drive air/OS booted
// from is never writable, whatever else is true of it.
#ifndef AIRIMAGER_DRIVE_H
#define AIRIMAGER_DRIVE_H

#include <cstdint>
#include <string>
#include <vector>

namespace imager {

enum class DriveBus {
	kUsb,			// disk/usb: sticks, card readers, external drives
	kSdCard,		// disk/mmc: SD and MMC cards
	kFile,			// disk/virtual/files: a disk image file (diskimage)
	kNvme,
	kSata,			// disk/scsi (AHCI) and disk/ata
	kVirtual,		// virtio_block and other virtual disks
	kOther
};

struct DriveInfo {
	int32_t			id = -1;			// partition_id of the disk device
	std::string		path;				// /dev/disk/.../raw
	std::string		model;				// vendor and product, if known
	uint64_t		size = 0;
	uint32_t		blockSize = 512;
	DriveBus		bus = DriveBus::kOther;
	bool			removable = false;
	bool			readOnly = false;
	bool			hasMedia = true;
	bool			optical = false;
	bool			isBoot = false;		// holds the volume air/OS runs from
	bool			holdsImage = false;	// holds the selected image file
	std::vector<std::string> volumes;	// names of its mounted volumes
};

enum class DriveState {
	kAvailable,
	kBootDrive,
	kHoldsImage,
	kNoMedia,
	kReadOnly,
	kTooSmall
};

DriveBus BusForPath(const std::string& path);
// Internal drives are listed only on request and need a second confirmation.
bool IsInternal(const DriveInfo& drive);
// Whether the image (imageSize bytes; 0 when none is chosen yet) may be
// written to the drive, and if not, why.
DriveState Assess(const DriveInfo& drive, uint64_t imageSize);
// A few words for the list: "air/OS boot drive", "Too small", ...
std::string StateLabel(DriveState state);
// "SanDisk Ultra", or "USB drive" when the drive has no model name.
std::string DisplayName(const DriveInfo& drive);
// "USB", "SD card", "NVMe", ...
std::string BusLabel(DriveBus bus);

}  // namespace imager

#endif
