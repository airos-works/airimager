// DriveRoster.h - the drives Haiku knows, through the disk device API.
//
// Every disk device is listed with what the rules in engine/Drive.h need:
// its bus, size, media state, model name, mounted volumes, whether air/OS
// booted from it and whether the chosen image lives on it.
#ifndef AIRIMAGER_DRIVE_ROSTER_H
#define AIRIMAGER_DRIVE_ROSTER_H

#include <Messenger.h>
#include <String.h>

#include <string>
#include <vector>

#include "engine/Drive.h"

class BBitmap;

namespace DriveRoster {

// All disk devices. imagePath (may be NULL) marks the drive holding it.
std::vector<imager::DriveInfo> Scan(const char* imagePath);

// The partition_id of the drive air/OS booted from, or -1 when /boot is not
// on a disk device.
int32 BootDriveID();

// Unmounts every mounted volume of the drive; on failure error names the
// volume that would not unmount.
status_t UnmountAll(int32 id, BString& error);

// Notifies target (B_DEVICE_UPDATE) when drives come and go, media change
// or volumes are mounted.
status_t StartWatching(BMessenger target);
void StopWatching(BMessenger target);

// The drive's own icon (USB stick, SD card, hard disk), at size pixels.
BBitmap* DriveIcon(const imager::DriveInfo& drive, float size);

// Flushes the drive's write cache after fsync() (an FdTarget flush hook).
bool FlushDriveCache(int fd, std::string& error);

// The sector size of an open raw device.
uint32 SectorSize(int fd, uint32 fallback);

}  // namespace DriveRoster

#endif
