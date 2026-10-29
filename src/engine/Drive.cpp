// Drive.cpp - see Drive.h.
#include "engine/Drive.h"

namespace imager {

namespace {

bool
Contains(const std::string& text, const char* part)
{
	return text.find(part) != std::string::npos;
}

}  // namespace


DriveBus
BusForPath(const std::string& path)
{
	if (Contains(path, "/disk/usb/"))
		return DriveBus::kUsb;
	if (Contains(path, "/disk/mmc/"))
		return DriveBus::kSdCard;
	if (Contains(path, "/disk/virtual/files/"))
		return DriveBus::kFile;
	if (Contains(path, "/disk/nvme/"))
		return DriveBus::kNvme;
	if (Contains(path, "/disk/scsi/") || Contains(path, "/disk/ata"))
		return DriveBus::kSata;
	if (Contains(path, "/disk/virtual/"))
		return DriveBus::kVirtual;
	return DriveBus::kOther;
}


bool
IsInternal(const DriveInfo& drive)
{
	switch (drive.bus) {
		case DriveBus::kUsb:
		case DriveBus::kSdCard:
		case DriveBus::kFile:
			return false;
		default:
			return !drive.removable;
	}
}


DriveState
Assess(const DriveInfo& drive, uint64_t imageSize)
{
	if (drive.isBoot)
		return DriveState::kBootDrive;
	if (drive.holdsImage)
		return DriveState::kHoldsImage;
	if (!drive.hasMedia || drive.size == 0)
		return DriveState::kNoMedia;
	if (drive.readOnly || drive.optical)
		return DriveState::kReadOnly;
	if (imageSize > drive.size)
		return DriveState::kTooSmall;
	return DriveState::kAvailable;
}


std::string
StateLabel(DriveState state)
{
	switch (state) {
		case DriveState::kAvailable:
			return "";
		case DriveState::kBootDrive:
			return "air/OS boot drive";
		case DriveState::kHoldsImage:
			return "Holds the image";
		case DriveState::kNoMedia:
			return "No media";
		case DriveState::kReadOnly:
			return "Read-only";
		case DriveState::kTooSmall:
			return "Too small";
	}
	return "";
}


std::string
BusLabel(DriveBus bus)
{
	switch (bus) {
		case DriveBus::kUsb:
			return "USB";
		case DriveBus::kSdCard:
			return "SD card";
		case DriveBus::kFile:
			return "Disk image file";
		case DriveBus::kNvme:
			return "NVMe";
		case DriveBus::kSata:
			return "SATA";
		case DriveBus::kVirtual:
			return "Virtual disk";
		case DriveBus::kOther:
			return "Disk";
	}
	return "Disk";
}


std::string
DisplayName(const DriveInfo& drive)
{
	if (!drive.model.empty())
		return drive.model;
	switch (drive.bus) {
		case DriveBus::kUsb:
			return "USB drive";
		case DriveBus::kSdCard:
			return "SD card";
		case DriveBus::kFile:
			return "Disk image file";
		case DriveBus::kNvme:
			return "NVMe drive";
		case DriveBus::kSata:
			return "SATA drive";
		default:
			return "Drive";
	}
}

}  // namespace imager
