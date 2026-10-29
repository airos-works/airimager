// MainWindow.cpp - see MainWindow.h.
#include "MainWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Directory.h>
#include <Entry.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <NodeInfo.h>
#include <Notification.h>
#include <Path.h>
#include <Roster.h>
#include <StatusBar.h>
#include <StringView.h>

#include <DiskDeviceRoster.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "App.h"
#include "DriveRoster.h"
#include "DriveWindow.h"
#include "Views.h"
#include "Workers.h"

using namespace imager;

namespace {

enum {
	kMsgSelectImage		= 'simg',
	kMsgImageRefs		= 'iref',
	kMsgSelectDrive		= 'sdrv',
	kMsgWrite			= 'writ',
	kMsgWriteConfirmed	= 'wcnf',
	kMsgInternalConfirmed = 'icnf',
	kMsgCancel			= 'cncl',
	kMsgCancelConfirmed	= 'ccnf',
	kMsgQuitConfirmed	= 'qcnf',
	kMsgVerifyChanged	= 'vrfy',
	kMsgOpenDownloads	= 'dwnl',
	kMsgOpenGuide		= 'guid'
};

const char* kDownloadsURL = "https://airos.works/downloads/";
const char* kGuideURL = "https://airos.works/downloads/write/";

const rgb_color kGood = { 40, 150, 70, 255 };
const rgb_color kBad = { 200, 40, 40, 255 };
const rgb_color kWarn = { 200, 120, 0, 255 };
const rgb_color kVerifyBar = { 60, 170, 90, 255 };


// Image files: anything compressed with xz, ISO images, raw disk images.
class ImageFilter : public BRefFilter {
public:
	bool Filter(const entry_ref* ref, BNode* node, struct stat_beos* stat,
		const char* mimeType) override
	{
		BEntry entry(ref, true);
		if (entry.IsDirectory())
			return true;
		const char* dot = strrchr(ref->name, '.');
		if (dot == NULL)
			return false;
		static const char* const kExtensions[] = {
			".xz", ".iso", ".img", ".raw", ".bin", ".image"
		};
		for (const char* extension : kExtensions) {
			if (strcasecmp(dot, extension) == 0)
				return true;
		}
		return false;
	}
};


void
OpenURL(const char* url)
{
	const char* args[] = { url, NULL };
	be_roster->Launch("application/x-vnd.Be.URL.https", 1, args);
}


BString
DriveSummary(const DriveInfo& drive)
{
	BString text;
	text << FormatBytes(drive.size).c_str() << " · "
		<< BusLabel(drive.bus).c_str();
	return text;
}


BString
VolumeList(const DriveInfo& drive)
{
	BString text;
	for (size_t i = 0; i < drive.volumes.size(); i++) {
		if (i > 0)
			text << (i + 1 == drive.volumes.size() ? " and " : ", ");
		text << "\"" << drive.volumes[i].c_str() << "\"";
	}
	return text;
}

}  // namespace


MainWindow::MainWindow(BRect frame, bool verify)
	:
	BWindow(frame, "airImager", B_TITLED_WINDOW, B_NOT_ZOOMABLE
		| B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
		| B_ASYNCHRONOUS_CONTROLS),
	fImagePanel(NULL),
	fHaveImage(false),
	fTargetID(-1),
	fCheckState(kNoCheck),
	fCheckPercent(0),
	fChecker(NULL),
	fWriter(NULL),
	fJob(0),
	fPhase(-1),
	fPhaseStart(0),
	fQuitWhenStopped(false)
{
	float em = be_plain_font->Size();
	float stepWidth = floorf(em * 14);

	fImageButton = new BButton("image", "Select image…",
		new BMessage(kMsgSelectImage));
	fDriveButton = new BButton("drive", "Select drive…",
		new BMessage(kMsgSelectDrive));
	fWriteButton = new BButton("write", "Write", new BMessage(kMsgWrite));
	fWriteButton->MakeDefault(true);
	fImageStep = new StepView("Image", fImageButton, stepWidth);
	fDriveStep = new StepView("Drive", fDriveButton, stepWidth);
	fWriteStep = new StepView("Write", fWriteButton, stepWidth);
	fVerifyBox = new BCheckBox("verify", "Verify after writing",
		new BMessage(kMsgVerifyChanged));
	fVerifyBox->SetValue(verify ? B_CONTROL_ON : B_CONTROL_OFF);
	fVerifyBox->SetToolTip("Read the whole drive back and compare it with the"
		" image. Strongly recommended.");
	fWriteStep->AddExtra(fVerifyBox);
	fArrows[0] = new StepArrow();
	fArrows[1] = new StepArrow();

	fProgress = new BStatusBar("progress");
	fProgress->SetBarHeight(ceilf(em * 1.4f));
	fProgress->SetExplicitMinSize(BSize(stepWidth * 3, B_SIZE_UNSET));
	fProgressDetail = new BStringView("detail", "");
	fProgressDetail->SetTruncation(B_TRUNCATE_END);
	fProgressDetail->SetExplicitMinSize(BSize(stepWidth, B_SIZE_UNSET));
	fCancelButton = new BButton("cancel", "Cancel", new BMessage(kMsgCancel));
	fCancelButton->SetEnabled(false);

	BMenuBar* menuBar = new BMenuBar("menu");
	_BuildMenu(menuBar);

	LinkView* link = new LinkView("Get air/OS images at airos.works",
		kDownloadsURL);
	link->SetAlignment(B_ALIGN_RIGHT);
	link->SetExplicitMaxSize(link->PreferredSize());
	fProgressDetail->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL)
			.SetInsets(B_USE_WINDOW_SPACING)
			.AddGroup(B_HORIZONTAL, 0)
				.Add(fImageStep)
				.Add(fArrows[0])
				.Add(fDriveStep)
				.Add(fArrows[1])
				.Add(fWriteStep)
			.End()
			.AddStrut(be_control_look->DefaultItemSpacing())
			.AddGroup(B_HORIZONTAL)
				.Add(fProgress)
				.AddGroup(B_VERTICAL)
					.AddGlue()
					.Add(fCancelButton)
				.End()
			.End()
			.AddGroup(B_HORIZONTAL)
				.Add(fProgressDetail)
				.Add(link)
			.End()
		.End();

	fImageStep->Icon()->SetIcon(ResourceIcon("image", StepIconSize()));
	fDriveStep->Icon()->SetIcon(ResourceIcon("drive", StepIconSize()));
	fWriteStep->Icon()->SetIcon(ResourceIcon("BEOS:ICON", StepIconSize()));

	DriveRoster::StartWatching(BMessenger(this));
	_ScanDrives();
	_UpdateSteps();
	_ShowIdleStatus();
	// frame is the saved position (no size: the layout sets it), or an
	// invalid rectangle the first time.
	ResizeToPreferred();
	if (frame.IsValid())
		MoveOnScreen(B_MOVE_IF_PARTIALLY_OFFSCREEN);
	else
		CenterOnScreen();
}


MainWindow::~MainWindow()
{
	DriveRoster::StopWatching(BMessenger(this));
	_StopChecksum();
	if (fWriter != NULL) {
		fWriter->Stop();
		delete fWriter;
	}
	delete fImagePanel;
}


void
MainWindow::_BuildMenu(BView* view)
{
	BMenuBar* menuBar = static_cast<BMenuBar*>(view);
	BMenu* menu = new BMenu("File");
	menu->AddItem(new BMenuItem("Select image…", new BMessage(kMsgSelectImage),
		'O'));
	menu->AddItem(new BMenuItem("Select drive…", new BMessage(kMsgSelectDrive),
		'D'));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("About airImager",
		new BMessage(B_ABOUT_REQUESTED)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
	menuBar->AddItem(menu);

	menu = new BMenu("Help");
	menu->AddItem(new BMenuItem("Download air/OS images",
		new BMessage(kMsgOpenDownloads)));
	menu->AddItem(new BMenuItem("Writing an image (guide)",
		new BMessage(kMsgOpenGuide)));
	menuBar->AddItem(menu);
	for (int32 i = 0; i < menuBar->CountItems(); i++)
		menuBar->SubmenuAt(i)->SetTargetForItems(this);
	menuBar->SubmenuAt(0)->ItemAt(3)->SetTarget(be_app);
}


void
MainWindow::MessageReceived(BMessage* message)
{
	if (message->WasDropped() && message->HasRef("refs")) {
		entry_ref ref;
		message->FindRef("refs", &ref);
		BPath path(&ref);
		BString error;
		if (path.InitCheck() == B_OK && !SetImage(path.Path(), error)) {
			BAlert* alert = new BAlert("airImager", error, "OK", NULL, NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->Go(NULL);
		}
		return;
	}

	switch (message->what) {
		case kMsgSelectImage:
			_OpenImagePanel();
			break;

		case B_REFS_RECEIVED:
		case kMsgImageRefs:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) != B_OK)
				break;
			BPath path(&ref);
			BString error;
			if (path.InitCheck() == B_OK && !SetImage(path.Path(), error)) {
				BAlert* alert = new BAlert("airImager", error, "OK", NULL,
					NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
				alert->Go(NULL);
			}
			break;
		}

		case kMsgSelectDrive:
			_OpenDriveWindow();
			break;

		case kMsgDriveChosen:
			_DriveChosen(message->GetInt32("id", -1));
			break;

		case kMsgInternalConfirmed:
			if (message->GetInt32("which", 0) == 1) {
				fTargetID = message->GetInt32("id", -1);
				_UpdateSteps();
			}
			break;

		case kMsgWrite:
		{
			BString error;
			if (!StartWrite(true, error) && !error.IsEmpty()) {
				BAlert* alert = new BAlert("airImager", error, "OK", NULL,
					NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
				alert->Go(NULL);
			}
			break;
		}

		case kMsgWriteConfirmed:
			if (message->GetInt32("which", 0) == 1)
				_ConfirmedWrite();
			break;

		case kMsgCancel:
			CancelWrite(true);
			break;

		case kMsgCancelConfirmed:
			if (message->GetInt32("which", 0) == 1 && fWriter != NULL)
				fWriter->Cancel();
			break;

		case kMsgQuitConfirmed:
			if (message->GetInt32("which", 0) == 1 && fWriter != NULL) {
				fQuitWhenStopped = true;
				fWriter->Cancel();
			}
			break;

		case kMsgVerifyChanged:
			break;

		case kMsgOpenDownloads:
			OpenURL(kDownloadsURL);
			break;

		case kMsgOpenGuide:
			OpenURL(kGuideURL);
			break;

		case kMsgJobProgress:
			if (message->GetInt32("job", -1) == fJob)
				_JobProgress(message);
			break;

		case kMsgJobFinished:
			if (message->GetInt32("job", -1) == fJob)
				_JobFinished(message);
			break;

		case kMsgCheckProgress:
			if (message->GetInt32("job", -1) == fJob)
				_CheckProgress(message);
			break;

		case kMsgCheckFinished:
			if (message->GetInt32("job", -1) == fJob)
				_CheckFinished(message);
			break;

		case B_DEVICE_UPDATE:
			// While writing, the drive list is left alone: unmounting the
			// target's volumes alone sends a few of these.
			if (fWriter == NULL) {
				_ScanDrives();
				_UpdateSteps();
				_UpdateDriveWindow();
			}
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


bool
MainWindow::QuitRequested()
{
	if (fWriter != NULL) {
		BAlert* alert = new BAlert("Quit", "A drive is being written. Stop"
			" writing and quit?\n\nA drive left half-written will not start"
			" a computer until it is written again.", "Keep writing",
			"Stop and quit", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		alert->Go(new BInvoker(new BMessage(kMsgQuitConfirmed), this));
		return false;
	}
	SaveSettings(Frame(), Verify(), fHaveImage ? fImage.path.c_str() : NULL);
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


bool
MainWindow::SetImage(const char* path, BString& error)
{
	if (fWriter != NULL) {
		error = "A drive is being written. Wait for it to finish first.";
		return false;
	}
	ImageInfo info;
	std::string inspectError;
	if (!InspectImage(path, info, inspectError)) {
		error.SetToFormat("\"%s\" cannot be written:\n\n%s",
			BPath(path).Leaf(), inspectError.c_str());
		return false;
	}

	_StopChecksum();
	fImage = info;
	fHaveImage = true;
	fCheckState = kNoCheck;
	fCheckMessage = "";
	fLastResult = "";

	// The SHA-256 file published next to the image, if it was downloaded.
	std::string checksumPath;
	if (FindChecksumFile(info.path, checksumPath)) {
		Sha256Digest digest;
		std::string checksumError;
		if (ReadChecksumFile(checksumPath, info.name, digest, checksumError)) {
			fCheckState = kChecking;
			fCheckPercent = 0;
			fChecker = new ChecksumWorker(BMessenger(this), ++fJob, info.path,
				digest);
			if (fChecker->Start("airImager checksum") != B_OK) {
				delete fChecker;
				fChecker = NULL;
				fCheckState = kNoCheck;
			}
		} else {
			fCheckState = kCheckUnreadable;
			fCheckMessage = checksumError.c_str();
		}
	}

	_ScanDrives();
	// A drive chosen before may now be too small, or hold the image.
	const DriveInfo* target = _Target();
	if (target != NULL && Assess(*target, fImage.imageSize)
			!= DriveState::kAvailable) {
		fTargetID = -1;
	}
	_UpdateSteps();
	_UpdateDriveWindow();
	_ShowIdleStatus();
	return true;
}


bool
MainWindow::SetTarget(const char* path, BString& error)
{
	if (fWriter != NULL) {
		error = "A drive is being written. Wait for it to finish first.";
		return false;
	}
	_ScanDrives();
	for (const DriveInfo& drive : fDrives) {
		if (drive.path != path)
			continue;
		DriveState state = Assess(drive, fHaveImage ? fImage.imageSize : 0);
		if (state != DriveState::kAvailable) {
			error.SetToFormat("%s cannot be written: %s.", path,
				StateLabel(state).c_str());
			return false;
		}
		fTargetID = drive.id;
		_UpdateSteps();
		return true;
	}
	error.SetToFormat("There is no drive %s.", path);
	return false;
}


bool
MainWindow::StartWrite(bool confirm, BString& error)
{
	if (fWriter != NULL) {
		error = "A drive is already being written.";
		return false;
	}
	if (!fHaveImage) {
		error = "Select an image first.";
		return false;
	}
	if (fCheckState == kChecking) {
		error = "The download is still being checked against its SHA-256"
			" file. Wait a moment.";
		return false;
	}
	if (fCheckState == kCheckFailed) {
		error = "The image does not match its SHA-256 file: the download is"
			" damaged or incomplete. Download it again.";
		return false;
	}

	// The drives may have changed since the target was chosen.
	_ScanDrives();
	_UpdateSteps();
	const DriveInfo* target = _Target();
	if (target == NULL) {
		error = "Select the drive to write to.";
		return false;
	}
	DriveState state = Assess(*target, fImage.imageSize);
	if (state != DriveState::kAvailable
		|| target->id == DriveRoster::BootDriveID()) {
		error.SetToFormat("%s cannot be written: %s.",
			DisplayName(*target).c_str(), StateLabel(state).c_str());
		return false;
	}

	if (!confirm) {
		_ConfirmedWrite();
		if (fWriter == NULL)
			error = fLastResult;
		return fWriter != NULL;
	}

	BString text;
	text.SetToFormat("Erase \"%s\" (%s) and write \"%s\" to it?\n\n"
		"Everything on the drive will be lost.",
		DisplayName(*target).c_str(), DriveSummary(*target).String(),
		fImage.name.c_str());
	if (!target->volumes.empty()) {
		text << " " << (target->volumes.size() == 1 ? "Its volume "
			: "Its volumes ") << VolumeList(*target) << " will be unmounted.";
	}
	if (IsInternal(*target)) {
		text << "\n\nThis is an internal drive. Make sure it holds nothing"
			" you need.";
	}
	BAlert* alert = new BAlert("Write image", text, "Cancel",
		"Erase and write", NULL, B_WIDTH_AS_USUAL, B_OFFSET_SPACING,
		B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	alert->Go(new BInvoker(new BMessage(kMsgWriteConfirmed), this));
	return true;
}


void
MainWindow::_ConfirmedWrite()
{
	// Check once more: the confirmation may have stood open while drives
	// came and went.
	_ScanDrives();
	const DriveInfo* target = _Target();
	BString error;
	if (target == NULL)
		error = "The drive is gone. Was it removed?";
	else if (Assess(*target, fImage.imageSize) != DriveState::kAvailable
		|| target->id == DriveRoster::BootDriveID()) {
		error = "The drive can no longer be written.";
	} else if (!target->volumes.empty())
		DriveRoster::UnmountAll(target->id, error);

	if (error.IsEmpty()) {
		fWriting = *target;
		fWriter = new WriteWorker(BMessenger(this), ++fJob, fImage, fWriting,
			Verify());
		if (fWriter->Start("airImager writer") != B_OK) {
			delete fWriter;
			fWriter = NULL;
			error = "Could not start writing.";
		}
	}
	if (!error.IsEmpty()) {
		fLastResult = BString("failed: ") << error;
		_UpdateSteps();
		_ShowIdleStatus();
		BAlert* alert = new BAlert("airImager", error, "OK", NULL, NULL,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go(NULL);
		return;
	}

	fPhase = -1;
	fLastResult = "";
	fStatus = "writing 0%";
	_SetBusy(true);
	_UpdateSteps();
}


void
MainWindow::CancelWrite(bool confirm)
{
	if (fWriter == NULL)
		return;
	if (!confirm) {
		fWriter->Cancel();
		return;
	}
	const char* text = fPhase == int32(Phase::kVerifying)
		? "Stop verifying? The image has been written, but it will not be"
			" checked."
		: "Stop writing? The drive will not start a computer until it is"
			" written again.";
	BAlert* alert = new BAlert("Cancel", text, "Continue", "Stop", NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	alert->Go(new BInvoker(new BMessage(kMsgCancelConfirmed), this));
}


void
MainWindow::_ScanDrives()
{
	fDrives = DriveRoster::Scan(fHaveImage ? fImage.path.c_str() : NULL);
	if (fTargetID >= 0 && _Target() == NULL)
		fTargetID = -1;
}


const DriveInfo*
MainWindow::_Target() const
{
	for (const DriveInfo& drive : fDrives) {
		if (drive.id == fTargetID)
			return &drive;
	}
	return NULL;
}


void
MainWindow::_UpdateSteps()
{
	bool busy = fWriter != NULL;

	// Image
	if (!fHaveImage) {
		fImageStep->SetLines("No image selected", ".img.xz, .iso.xz, .img"
			" or .iso", NULL);
		fImageButton->SetLabel("Select image…");
	} else {
		BString size;
		if (fImage.compression == Compression::kXz) {
			size << FormatBytes(fImage.imageSize).c_str() << " ("
				<< FormatBytes(fImage.fileSize).c_str() << " xz)";
		} else
			size << FormatBytes(fImage.imageSize).c_str();
		BString check;
		switch (fCheckState) {
			case kNoCheck:
				check = fImage.kind.c_str();
				break;
			case kChecking:
				check.SetToFormat("Checking download… %d%%",
					int(fCheckPercent));
				break;
			case kCheckPassed:
				check = "✓ SHA-256 checked";
				break;
			case kCheckFailed:
				check = "✗ SHA-256 mismatch";
				break;
			case kCheckUnreadable:
				check = "SHA-256 file not usable";
				break;
		}
		fImageStep->SetLines(fImage.name.c_str(), size, check);
		fImageStep->ResetLineColors();
		if (fCheckState == kCheckPassed)
			fImageStep->SetLineColor(2, kGood);
		else if (fCheckState == kCheckFailed)
			fImageStep->SetLineColor(2, kBad);
		else if (fCheckState == kCheckUnreadable)
			fImageStep->SetLineColor(2, kWarn);
		fImageButton->SetLabel("Change…");
	}

	// Drive
	const DriveInfo* target = busy ? &fWriting : _Target();
	if (target == NULL) {
		int available = 0;
		for (const DriveInfo& drive : fDrives) {
			if (!IsInternal(drive) && Assess(drive, fHaveImage
					? fImage.imageSize : 0) == DriveState::kAvailable) {
				available++;
			}
		}
		BString count;
		if (available == 0)
			count = "Insert a USB stick or SD card";
		else if (available == 1)
			count = "1 drive available";
		else
			count.SetToFormat("%d drives available", available);
		fDriveStep->SetLines("No drive selected", count, NULL);
		fDriveStep->Icon()->SetIcon(ResourceIcon("drive", StepIconSize()));
		fDriveButton->SetLabel("Select drive…");
	} else {
		BString volumes;
		if (!target->volumes.empty() && !busy)
			volumes << "Erases " << VolumeList(*target);
		else
			volumes = target->path.c_str();
		fDriveStep->SetLines(DisplayName(*target).c_str(),
			DriveSummary(*target), volumes);
		BBitmap* icon = DriveRoster::DriveIcon(*target, StepIconSize());
		fDriveStep->Icon()->SetIcon(icon != NULL ? icon
			: ResourceIcon("drive", StepIconSize()));
		fDriveButton->SetLabel("Change…");
	}

	// Write
	bool ready = fHaveImage && target != NULL && fCheckState != kChecking
		&& fCheckState != kCheckFailed;
	fWriteButton->SetEnabled(ready && !busy);
	fWriteStep->Icon()->SetDimmed(!ready && !busy);
	if (busy)
		fWriteStep->SetLines("Writing", NULL, NULL);
	else if (ready)
		fWriteStep->SetLines("Ready to write", NULL, NULL);
	else if (fCheckState == kChecking)
		fWriteStep->SetLines("Waiting for the check", NULL, NULL);
	else if (fCheckState == kCheckFailed)
		fWriteStep->SetLines("Download damaged", NULL, NULL);
	else
		fWriteStep->SetLines("Select an image and a drive", NULL, NULL);

	fArrows[0]->SetActive(fHaveImage);
	fArrows[1]->SetActive(fHaveImage && target != NULL);
}


void
MainWindow::_UpdateDriveWindow()
{
	BLooper* looper;
	if (fDriveWindow.Target(&looper) == NULL || looper == NULL)
		return;
	DriveWindow* window = dynamic_cast<DriveWindow*>(looper);
	if (window != NULL && window->LockLooperWithTimeout(500000) == B_OK) {
		window->SetDrives(fDrives, fHaveImage ? fImage.imageSize : 0);
		window->UnlockLooper();
	}
}


void
MainWindow::_OpenImagePanel()
{
	if (fWriter != NULL)
		return;
	if (fImagePanel == NULL) {
		BMessenger target(this);
		fImagePanel = new BFilePanel(B_OPEN_PANEL, &target, NULL, B_FILE_NODE,
			false, new BMessage(kMsgImageRefs), new ImageFilter(), true, true);
		fImagePanel->Window()->SetTitle("airImager: Select image");
		fImagePanel->SetButtonLabel(B_DEFAULT_BUTTON, "Select");
		// Start where the last image was, or in Downloads.
		BPath folder;
		BString last = LastImagePath();
		if (fHaveImage)
			folder.SetTo(fImage.path.c_str());
		else if (!last.IsEmpty())
			folder.SetTo(last);
		if (folder.InitCheck() == B_OK && folder.GetParent(&folder) == B_OK
			&& BEntry(folder.Path()).IsDirectory()) {
			fImagePanel->SetPanelDirectory(folder.Path());
		} else if (find_directory(B_USER_DIRECTORY, &folder) == B_OK) {
			BPath downloads(folder.Path(), "Downloads");
			fImagePanel->SetPanelDirectory(
				BEntry(downloads.Path()).IsDirectory() ? downloads.Path()
					: folder.Path());
		}
	}
	fImagePanel->Show();
}


void
MainWindow::_OpenDriveWindow()
{
	if (fWriter != NULL)
		return;
	BLooper* looper;
	if (fDriveWindow.Target(&looper) != NULL && looper != NULL) {
		BWindow* window = dynamic_cast<BWindow*>(looper);
		if (window != NULL)
			window->Activate();
		return;
	}
	_ScanDrives();
	DriveWindow* window = new DriveWindow(this, BMessenger(this),
		fHaveImage ? fImage.imageSize : 0, fTargetID);
	window->SetDrives(fDrives, fHaveImage ? fImage.imageSize : 0);
	fDriveWindow = BMessenger(window);
	window->Show();
}


void
MainWindow::_DriveChosen(int32 id)
{
	_ScanDrives();
	for (const DriveInfo& drive : fDrives) {
		if (drive.id != id)
			continue;
		if (Assess(drive, fHaveImage ? fImage.imageSize : 0)
				!= DriveState::kAvailable) {
			return;
		}
		if (!IsInternal(drive)) {
			fTargetID = id;
			_UpdateSteps();
			return;
		}
		BString text;
		text.SetToFormat("\"%s\" (%s) is an internal drive, not a USB stick"
			" or SD card.\n\nWriting an image to it erases everything on it."
			" Use it anyway?", DisplayName(drive).c_str(),
			DriveSummary(drive).String());
		BMessage* message = new BMessage(kMsgInternalConfirmed);
		message->AddInt32("id", id);
		BAlert* alert = new BAlert("Internal drive", text, "Cancel",
			"Use this drive", NULL, B_WIDTH_AS_USUAL, B_OFFSET_SPACING,
			B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		alert->Go(new BInvoker(message, this));
		return;
	}
}


void
MainWindow::_JobProgress(BMessage* message)
{
	int32 phase = message->GetInt32("phase", 0);
	uint64 done = message->GetInt64("done", 0);
	uint64 total = message->GetInt64("total", 1);
	bigtime_t now = system_time();
	if (phase != fPhase) {
		fPhase = phase;
		fPhaseStart = now;
		fMeter.Reset();
		fProgress->SetBarColor(phase == int32(Phase::kVerifying) ? kVerifyBar
			: ui_color(B_STATUS_BAR_COLOR));
	}
	fMeter.Add((now - fPhaseStart) / 1000000.0, done);

	float percent = total > 0 ? 100.0f * done / total : 100;
	BString label, trailing, detail;
	trailing.SetToFormat("%d%%", int(percent));
	switch (Phase(phase)) {
		case Phase::kWriting:
			label = "Writing…";
			fStatus.SetToFormat("writing %d%%", int(percent));
			break;
		case Phase::kFlushing:
			label = "Finishing the write…";
			trailing = "";
			fStatus = "flushing";
			break;
		case Phase::kVerifying:
			label = "Verifying…";
			fStatus.SetToFormat("verifying %d%%", int(percent));
			break;
	}
	if (Phase(phase) != Phase::kFlushing) {
		// Speed and time left go with the percentage, above the bar's end.
		double rate = fMeter.Rate();
		if (rate > 0) {
			trailing << " · " << FormatRate(rate).c_str();
			double left = fMeter.SecondsLeft(total);
			if (left >= 0)
				trailing << " · " << FormatDuration(left).c_str() << " left";
			fStatus << " " << FormatRate(rate).c_str();
		}
		detail << FormatBytes(done).c_str() << " of "
			<< FormatBytes(total).c_str()
			<< (Phase(phase) == Phase::kWriting ? " written" : " verified");
	} else
		detail = "Waiting for the drive to store the last data";
	fProgress->Reset(label, trailing);
	fProgress->SetMaxValue(100);
	fProgress->Update(percent);
	fProgressDetail->SetText(detail);
}


void
MainWindow::_JobFinished(BMessage* message)
{
	fWriter->Stop();
	delete fWriter;
	fWriter = NULL;
	_SetBusy(false);

	JobResult::Status status = JobResult::Status(message->GetInt32("status",
		JobResult::kFailed));
	BString text = message->GetString("message", "");
	double writeSeconds = message->GetDouble("write seconds", 0);
	double verifySeconds = message->GetDouble("verify seconds", 0);
	uint64 written = message->GetInt64("written", 0);
	BString drive = DisplayName(fWriting).c_str();

	const char* title;
	alert_type type = B_STOP_ALERT;
	BString alertText;
	fProgress->SetBarColor(status == JobResult::kSucceeded ? kVerifyBar
		: ui_color(B_STATUS_BAR_COLOR));
	switch (status) {
		case JobResult::kSucceeded:
		{
			bool verified = verifySeconds > 0;
			title = verified ? "Write complete" : "Write complete (not verified)";
			type = B_INFO_ALERT;
			BString speed;
			speed << "Written in " << FormatDuration(writeSeconds).c_str()
				<< " (" << FormatRate(written / std::max(writeSeconds, 0.001))
					.c_str() << ")";
			if (verified) {
				speed << ", verified in "
					<< FormatDuration(verifySeconds).c_str();
			}
			speed << ".";
			alertText.SetToFormat("\"%s\" was written to \"%s\"%s.\n\n%s\n\n"
				"You can remove the drive now.", fImage.name.c_str(),
				drive.String(), verified ? " and verified" : "",
				speed.String());
			fProgress->Reset(verified ? "Done: written and verified"
				: "Done: written", "100%");
			fProgress->SetMaxValue(100);
			fProgress->Update(100);
			fProgressDetail->SetText(speed);
			fLastResult = verified ? "done: verified" : "done: written";
			break;
		}
		case JobResult::kVerifyFailed:
			title = "Verification failed";
			alertText = text;
			fLastResult = BString("verify failed: ") << text;
			break;
		case JobResult::kCancelled:
			title = "Cancelled";
			type = B_WARNING_ALERT;
			alertText = text;
			fLastResult = "cancelled";
			break;
		case JobResult::kFailed:
		default:
			title = "Write failed";
			alertText = text;
			fLastResult = BString("failed: ") << text;
			break;
	}
	if (status != JobResult::kSucceeded) {
		fProgress->Reset(title);
		fProgressDetail->SetText(text);
	}
	fStatus = "";
	fPhase = -1;

	_ScanDrives();
	_UpdateSteps();
	_UpdateDriveWindow();

	if (fQuitWhenStopped) {
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
	if (!IsActive())
		_Notify(title, alertText);
	BAlert* alert = new BAlert(title, alertText, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, type);
	alert->SetTitle(title);
	alert->Go(NULL);
}


void
MainWindow::_CheckProgress(BMessage* message)
{
	uint64 done = message->GetInt64("done", 0);
	uint64 total = message->GetInt64("total", 1);
	fCheckPercent = total > 0 ? 100.0f * done / total : 100;
	_UpdateSteps();
}


void
MainWindow::_CheckFinished(BMessage* message)
{
	if (fChecker != NULL) {
		fChecker->Stop();
		delete fChecker;
		fChecker = NULL;
	}
	switch (ChecksumResult(message->GetInt32("result", 0))) {
		case ChecksumResult::kMatch:
			fCheckState = kCheckPassed;
			break;
		case ChecksumResult::kMismatch:
		{
			fCheckState = kCheckFailed;
			BString text;
			text.SetToFormat("\"%s\" does not match the SHA-256 checksum in"
				" \"%s.sha256\".\n\nThe download is damaged or incomplete."
				" Download the image again.", fImage.name.c_str(),
				fImage.name.c_str());
			BAlert* alert = new BAlert("Download damaged", text, "OK", NULL,
				NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->Go(NULL);
			break;
		}
		case ChecksumResult::kFailed:
			fCheckState = kCheckUnreadable;
			fCheckMessage = message->GetString("message", "");
			break;
		case ChecksumResult::kCancelled:
			fCheckState = kNoCheck;
			break;
	}
	_UpdateSteps();
	_ShowIdleStatus();
}


void
MainWindow::_StopChecksum()
{
	if (fChecker == NULL)
		return;
	fChecker->Stop();
	delete fChecker;
	fChecker = NULL;
	// Its last reports may still be queued; a new job number drops them.
	fJob++;
}


void
MainWindow::_SetBusy(bool busy)
{
	fImageButton->SetEnabled(!busy);
	fDriveButton->SetEnabled(!busy);
	fVerifyBox->SetEnabled(!busy);
	fCancelButton->SetEnabled(busy);
	BMenuBar* menuBar = KeyMenuBar();
	if (menuBar != NULL) {
		BMenu* file = menuBar->SubmenuAt(0);
		file->ItemAt(0)->SetEnabled(!busy);
		file->ItemAt(1)->SetEnabled(!busy);
	}
	if (busy) {
		fProgress->Reset("Preparing…");
		fProgressDetail->SetText("");
	}
}


void
MainWindow::_ShowIdleStatus()
{
	if (fWriter != NULL)
		return;
	const char* hint;
	if (!fHaveImage)
		hint = "Select an image to write.";
	else if (fCheckState == kChecking)
		hint = "Checking the download against its SHA-256 file…";
	else if (fCheckState == kCheckFailed)
		hint = "The download is damaged. Download the image again.";
	else if (_Target() == NULL)
		hint = "Select the drive to write to.";
	else
		hint = "Ready to write.";
	if (!fLastResult.IsEmpty() && fLastResult.StartsWith("done"))
		return;
	fProgress->SetBarColor(ui_color(B_STATUS_BAR_COLOR));
	fProgress->Reset(hint);
	fProgressDetail->SetText(fCheckState == kCheckUnreadable
		? BString("The SHA-256 file next to the image could not be used: ")
			<< fCheckMessage : BString());
}


void
MainWindow::_Notify(const char* title, const char* text)
{
	BNotification notification(B_INFORMATION_NOTIFICATION);
	notification.SetGroup("airImager");
	notification.SetTitle(title);
	notification.SetContent(text);
	notification.Send();
}


BString
MainWindow::ImagePath() const
{
	return fHaveImage ? BString(fImage.path.c_str()) : BString();
}


BString
MainWindow::TargetPath() const
{
	const DriveInfo* target = fWriter != NULL ? &fWriting : _Target();
	return target != NULL ? BString(target->path.c_str()) : BString();
}


BString
MainWindow::Status() const
{
	if (fWriter != NULL)
		return fStatus;
	if (fCheckState == kChecking) {
		BString status;
		status.SetToFormat("checking %d%%", int(fCheckPercent));
		return status;
	}
	if (fCheckState == kCheckFailed)
		return "checksum mismatch";
	return fLastResult.IsEmpty() ? BString("idle") : fLastResult;
}


BString
MainWindow::DriveList() const
{
	BString list;
	for (const DriveInfo& drive : fDrives) {
		DriveState state = Assess(drive, fHaveImage ? fImage.imageSize : 0);
		list << drive.path.c_str() << "\t" << DisplayName(drive).c_str()
			<< "\t" << FormatBytes(drive.size).c_str() << "\t"
			<< BusLabel(drive.bus).c_str() << "\t"
			<< (state == DriveState::kAvailable ? "available"
				: StateLabel(state).c_str())
			<< (IsInternal(drive) ? " (internal)" : "") << "\n";
	}
	return list;
}


bool
MainWindow::Verify() const
{
	return fVerifyBox->Value() == B_CONTROL_ON;
}


void
MainWindow::SetVerify(bool verify)
{
	if (fWriter == NULL)
		fVerifyBox->SetValue(verify ? B_CONTROL_ON : B_CONTROL_OFF);
}
