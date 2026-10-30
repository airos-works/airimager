// DriveRoster.cpp - see DriveRoster.h.
#include "DriveRoster.h"

#include <Bitmap.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <DiskDeviceVisitor.h>
#include <Drivers.h>
#include <IconUtils.h>
#include <Mime.h>
#include <Path.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

using imager::DriveInfo;

namespace {

const uint32 kWatchEvents = B_DEVICE_REQUEST_DEVICE_LIST
	| B_DEVICE_REQUEST_DEVICE | B_DEVICE_REQUEST_MOUNTING
	| B_DEVICE_REQUEST_PARTITION;


// The device (partition_id) holding the volume, or -1.
int32
DeviceOfVolume(const BVolume& volume)
{
	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* partition;
	if (volume.InitCheck() != B_OK
		|| roster.FindPartitionByVolume(volume, &device, &partition) != B_OK) {
		return -1;
	}
	return device.ID();
}


// Collects the names of a device's mounted volumes, and notices one mounted
// at /boot (a second way of finding the boot drive).
class VolumeVisitor : public BDiskDeviceVisitor {
public:
	VolumeVisitor(DriveInfo& drive)
		:
		fDrive(drive)
	{
	}

	bool Visit(BDiskDevice* device) override
	{
		return Visit(device, 0);
	}

	bool Visit(BPartition* partition, int32 level) override
	{
		if (!partition->IsMounted())
			return false;
		BPath mountPoint;
		if (partition->GetMountPoint(&mountPoint) == B_OK
			&& strcmp(mountPoint.Path(), "/boot") == 0) {
			fDrive.isBoot = true;
		}
		BString name = partition->ContentName();
		if (name.IsEmpty() && mountPoint.InitCheck() == B_OK)
			name = mountPoint.Leaf();
		fDrive.volumes.push_back(name.String());
		return false;
	}

private:
	DriveInfo&	fDrive;
};


// Unmounts each mounted partition; stops at the first that will not go.
class UnmountVisitor : public BDiskDeviceVisitor {
public:
	UnmountVisitor()
		:
		fStatus(B_OK)
	{
	}

	bool Visit(BDiskDevice* device) override
	{
		return Visit(device, 0);
	}

	bool Visit(BPartition* partition, int32 level) override
	{
		if (!partition->IsMounted())
			return false;
		BString name = partition->ContentName();
		status_t status = partition->Unmount();
		if (status != B_OK) {
			fStatus = status;
			fFailed = name.IsEmpty() ? BString("a volume") : name;
			return true;
		}
		return false;
	}

	status_t	fStatus;
	BString		fFailed;
};


std::string
DeviceModel(const char* path)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return "";
	char name[B_FILE_NAME_LENGTH] = {};
	status_t status = ioctl(fd, B_GET_DEVICE_NAME, name, sizeof(name) - 1);
	close(fd);
	if (status != B_OK)
		return "";
	BString model(name);
	model.Trim();
	// Collapse the runs of spaces in fixed-width SCSI vendor fields.
	while (model.FindFirst("  ") >= 0)
		model.ReplaceAll("  ", " ");
	return model.String();
}

}  // namespace


namespace DriveRoster {

int32
BootDriveID()
{
	BVolume bootVolume;
	BVolumeRoster().GetBootVolume(&bootVolume);
	int32 id = DeviceOfVolume(bootVolume);
	if (id >= 0)
		return id;
	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* partition;
	if (roster.FindPartitionByMountPoint("/boot", &device, &partition) == B_OK)
		return device.ID();
	return -1;
}


std::vector<DriveInfo>
Scan(const char* imagePath)
{
	std::vector<DriveInfo> drives;
	int32 bootID = BootDriveID();
	int32 imageID = -1;
	struct stat st;
	if (imagePath != NULL && stat(imagePath, &st) == 0)
		imageID = DeviceOfVolume(BVolume(st.st_dev));

	BDiskDeviceRoster roster;
	BDiskDevice device;
	while (roster.GetNextDevice(&device) == B_OK) {
		DriveInfo drive;
		BPath path;
		if (device.GetPath(&path) != B_OK)
			continue;
		drive.id = device.ID();
		drive.path = path.Path();
		drive.bus = imager::BusForPath(drive.path);
		drive.size = device.Size();
		drive.blockSize = device.BlockSize() > 0 ? device.BlockSize() : 512;
		drive.removable = device.IsRemovableMedia();
		drive.hasMedia = device.HasMedia();
		drive.readOnly = device.IsReadOnlyMedia() || device.IsReadOnly();
		drive.optical = device.IsWriteOnceMedia()
			|| drive.path.find("/atapi/") != std::string::npos;
		drive.isBoot = drive.id == bootID;
		drive.holdsImage = drive.id == imageID;
		if (drive.hasMedia)
			drive.model = DeviceModel(drive.path.c_str());
		VolumeVisitor visitor(drive);
		device.VisitEachDescendant(&visitor);
		drives.push_back(drive);
	}
	return drives;
}


status_t
UnmountAll(int32 id, BString& error)
{
	BDiskDeviceRoster roster;
	BDiskDevice device;
	status_t status = roster.GetDeviceWithID(id, &device);
	if (status != B_OK) {
		error = "The drive is gone. Was it removed?";
		return status;
	}
	UnmountVisitor visitor;
	device.VisitEachDescendant(&visitor);
	if (visitor.fStatus != B_OK) {
		error.SetToFormat("Could not unmount \"%s\" (%s). Close the windows"
			" and applications using it, then try again.",
			visitor.fFailed.String(), strerror(visitor.fStatus));
	}
	return visitor.fStatus;
}


status_t
StartWatching(BMessenger target)
{
	return BDiskDeviceRoster().StartWatching(target, kWatchEvents);
}


void
StopWatching(BMessenger target)
{
	BDiskDeviceRoster().StopWatching(target);
}


BBitmap*
DriveIcon(const DriveInfo& drive, float size)
{
	uint8* data = NULL;
	size_t dataSize = 0;
	type_code type = 0;
	if (get_device_icon(drive.path.c_str(), &data, &dataSize, &type) != B_OK)
		return NULL;
	BBitmap* icon = NULL;
	if (type == B_VECTOR_ICON_TYPE) {
		icon = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
		if (BIconUtils::GetVectorIcon(data, dataSize, icon) != B_OK) {
			delete icon;
			icon = NULL;
		}
	}
	delete[] data;
	return icon;
}


bool
FlushDriveCache(int fd, std::string& error)
{
	if (ioctl(fd, B_FLUSH_DRIVE_CACHE, NULL, 0) == 0)
		return true;
	// Drivers without a cache of their own (or without the ioctl) are done
	// once fsync() returned. usb_disk answers B_UNSUPPORTED (not
	// B_NOT_SUPPORTED, though both read "Operation not supported") for card
	// readers and sticks that reject SYNCHRONIZE CACHE; the read-back that
	// follows still checks every byte.
	if (errno == B_DEV_INVALID_IOCTL || errno == B_UNSUPPORTED
		|| errno == B_NOT_SUPPORTED || errno == ENOTSUP
		|| errno == B_BAD_VALUE || errno == EINVAL || errno == ENOSYS) {
		return true;
	}
	error = std::string("Could not flush the drive's cache: ")
		+ strerror(errno) + ".";
	return false;
}


uint32
SectorSize(int fd, uint32 fallback)
{
	device_geometry geometry;
	if (ioctl(fd, B_GET_GEOMETRY, &geometry, sizeof(geometry)) == 0
		&& geometry.bytes_per_sector >= 512
		&& geometry.bytes_per_sector <= 65536
		&& (geometry.bytes_per_sector & (geometry.bytes_per_sector - 1)) == 0) {
		return geometry.bytes_per_sector;
	}
	return fallback;
}

}  // namespace DriveRoster
