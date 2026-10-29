// App.cpp - see App.h.
#include "App.h"

#include <Alert.h>
#include <AboutWindow.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <PropertyInfo.h>

#include "MainWindow.h"

const char* kAppSignature = "application/x-vnd.airos-airImager";

namespace {

property_info kProperties[] = {
	{ "Image", { B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"The image to write: the path of an .img.xz, .iso.xz, .img or .iso"
			" file.", 0, { B_STRING_TYPE } },
	{ "Target", { B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"The drive to write to: its raw device, as /dev/disk/usb/0/0/raw.",
		0, { B_STRING_TYPE } },
	{ "Drives", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"Every drive, one per line: path, name, size, bus and whether it can"
			" be written.", 0, { B_STRING_TYPE } },
	{ "Status", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"idle, checking N%, writing N%, flushing, verifying N%, done: ...,"
			" failed: ...", 0, { B_STRING_TYPE } },
	{ "Verify", { B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Verify after writing.", 0,
		{ B_BOOL_TYPE } },
	{ "Write", { B_EXECUTE_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"Writes the image to the target, without asking first.", 0, {} },
	{ "Cancel", { B_EXECUTE_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"Stops writing or verifying.", 0, {} },
	{ 0 }
};


status_t
SettingsPath(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status == B_OK)
		status = path.Append("airImager");
	return status;
}


bool
LoadSettings(BMessage& settings)
{
	BPath path;
	if (SettingsPath(path) != B_OK)
		return false;
	BFile file(path.Path(), B_READ_ONLY);
	return file.InitCheck() == B_OK && settings.Unflatten(&file) == B_OK;
}

}  // namespace


void
SaveSettings(BRect frame, bool verify, const char* lastImage)
{
	BMessage settings;
	settings.AddRect("frame", frame);
	settings.AddBool("verify", verify);
	if (lastImage != NULL)
		settings.AddString("last image", lastImage);
	BPath path;
	if (SettingsPath(path) != B_OK)
		return;
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		settings.Flatten(&file);
}


BString
LastImagePath()
{
	BMessage settings;
	if (!LoadSettings(settings))
		return BString();
	return settings.GetString("last image", "");
}


App::App()
	:
	BApplication(kAppSignature),
	fWindow(NULL)
{
}


void
App::ReadyToRun()
{
	_EnsureWindow();
}


void
App::_EnsureWindow()
{
	if (fWindow != NULL)
		return;
	BMessage settings;
	LoadSettings(settings);
	BRect frame = settings.GetRect("frame", BRect());
	fWindow = new MainWindow(frame.IsValid()
		? BRect(frame.LeftTop(), frame.LeftTop()) : BRect(0, 0, -1, -1),
		settings.GetBool("verify", true));
	fWindow->Show();
	if (!fPendingImage.IsEmpty()) {
		BString error;
		fWindow->LockLooper();
		bool ok = fWindow->SetImage(fPendingImage, error);
		fWindow->UnlockLooper();
		if (!ok) {
			BAlert* alert = new BAlert("airImager", error, "OK", NULL, NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->Go(NULL);
		}
		fPendingImage = "";
	}
}


void
App::ArgvReceived(int32 argc, char** argv)
{
	if (argc < 2)
		return;
	BPath path;
	const char* cwd = NULL;
	BMessage* message = CurrentMessage();
	if (message != NULL)
		message->FindString("cwd", &cwd);
	if (argv[1][0] != '/' && cwd != NULL)
		path.SetTo(cwd, argv[1]);
	else
		path.SetTo(argv[1]);
	if (path.InitCheck() != B_OK)
		return;
	if (fWindow == NULL) {
		fPendingImage = path.Path();
		return;
	}
	BMessage refs(B_REFS_RECEIVED);
	entry_ref ref;
	if (get_ref_for_path(path.Path(), &ref) == B_OK) {
		refs.AddRef("refs", &ref);
		fWindow->PostMessage(&refs);
	}
}


void
App::RefsReceived(BMessage* message)
{
	entry_ref ref;
	if (message->FindRef("refs", &ref) != B_OK)
		return;
	if (fWindow == NULL) {
		fPendingImage = BPath(&ref).Path();
		return;
	}
	fWindow->PostMessage(message);
}


void
App::AboutRequested()
{
	BAboutWindow* about = new BAboutWindow("airImager", kAppSignature);
	about->AddDescription("Writes air/OS images to USB sticks and SD cards,"
		" and verifies them.\n\nIt reads the .xz downloads from airos.works"
		" directly, as well as plain .iso and .img files, checks a download"
		" against the .sha256 file next to it, and never offers the drive"
		" air/OS is running from.");
	const char* authors[] = { "air/OS contributors", NULL };
	about->AddAuthors(authors);
	about->AddCopyright(2026, "air/OS contributors");
	about->AddText("Decompression: XZ Embedded, by Lasse Collin and the XZ"
		" Embedded contributors (0BSD).");
	about->Show();
}


BHandler*
App::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 what, const char* property)
{
	BPropertyInfo info(kProperties);
	if (info.FindMatch(message, index, specifier, what, property) >= 0)
		return this;
	return BApplication::ResolveSpecifier(message, index, specifier, what,
		property);
}


status_t
App::GetSupportedSuites(BMessage* data)
{
	data->AddString("suites", "suite/vnd.airos-airImager");
	BPropertyInfo info(kProperties);
	data->AddFlat("messages", &info);
	return BApplication::GetSupportedSuites(data);
}


void
App::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_GET_PROPERTY:
		case B_SET_PROPERTY:
		case B_EXECUTE_PROPERTY:
			if (_HandleScripting(message))
				return;
			break;
	}
	BApplication::MessageReceived(message);
}


bool
App::_HandleScripting(BMessage* message)
{
	int32 index;
	BMessage specifier;
	int32 what;
	const char* property;
	if (message->GetCurrentSpecifier(&index, &specifier, &what, &property)
			!= B_OK || fWindow == NULL) {
		return false;
	}

	BMessage reply(B_REPLY);
	BString error;
	bool handled = true;
	fWindow->LockLooper();
	BString name(property);
	if (message->what == B_GET_PROPERTY) {
		if (name == "Image")
			reply.AddString("result", fWindow->ImagePath());
		else if (name == "Target")
			reply.AddString("result", fWindow->TargetPath());
		else if (name == "Drives")
			reply.AddString("result", fWindow->DriveList());
		else if (name == "Status")
			reply.AddString("result", fWindow->Status());
		else if (name == "Verify")
			reply.AddBool("result", fWindow->Verify());
		else
			handled = false;
	} else if (message->what == B_SET_PROPERTY) {
		const char* value = NULL;
		bool flag;
		if (name == "Image" || name == "Target") {
			if (message->FindString("data", &value) != B_OK)
				error = "Expected a path.";
			else if (name == "Image")
				fWindow->SetImage(value, error);
			else
				fWindow->SetTarget(value, error);
		} else if (name == "Verify") {
			if (message->FindBool("data", &flag) != B_OK)
				error = "Expected true or false.";
			else
				fWindow->SetVerify(flag);
		} else
			handled = false;
	} else {
		if (name == "Write")
			fWindow->StartWrite(false, error);
		else if (name == "Cancel")
			fWindow->CancelWrite(false);
		else
			handled = false;
	}
	fWindow->UnlockLooper();

	if (!handled)
		return false;
	if (!error.IsEmpty()) {
		reply.what = B_MESSAGE_NOT_UNDERSTOOD;
		reply.AddString("message", error);
		reply.AddInt32("error", B_ERROR);
	} else
		reply.AddInt32("error", B_OK);
	message->SendReply(&reply);
	return true;
}


int
main()
{
	App app;
	app.Run();
	return 0;
}
