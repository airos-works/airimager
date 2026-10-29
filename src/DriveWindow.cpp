// DriveWindow.cpp - see DriveWindow.h.
#include "DriveWindow.h"

#include <Button.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <ScrollView.h>
#include <StringView.h>

#include "Views.h"

using namespace imager;

namespace {

enum {
	kMsgSelectionChanged	= 'dsel',
	kMsgInvoked				= 'dinv',
	kMsgShowInternal		= 'dint',
	kMsgSelect				= 'dsok'
};


const char*
Reason(const DriveItem* item)
{
	switch (item->State()) {
		case DriveState::kBootDrive:
			return "air/OS is running from this drive. It cannot be written.";
		case DriveState::kHoldsImage:
			return "The image is on this drive. Copy it to another drive"
				" first.";
		case DriveState::kNoMedia:
			return "There is no card or disc in this drive.";
		case DriveState::kReadOnly:
			return "This drive is read-only (or write-protected).";
		case DriveState::kTooSmall:
			return "This drive is smaller than the image.";
		case DriveState::kAvailable:
			break;
	}
	return "";
}

}  // namespace


DriveWindow::DriveWindow(BWindow* parent, BMessenger target, uint64 imageSize,
		int32 selectedID)
	:
	BWindow(BRect(0, 0, 100, 100), "Select drive", B_TITLED_WINDOW_LOOK,
		B_MODAL_SUBSET_WINDOW_FEEL, B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fTarget(target),
	fImageSize(imageSize),
	fSelectedID(selectedID)
{
	AddToSubset(parent);

	fList = new BListView("drives");
	fList->SetSelectionMessage(new BMessage(kMsgSelectionChanged));
	fList->SetInvocationMessage(new BMessage(kMsgInvoked));
	BScrollView* scroll = new BScrollView("scroll", fList, 0, false, true);
	float em = be_plain_font->Size();
	scroll->SetExplicitMinSize(BSize(em * 34, em * 16));

	fShowInternal = new BCheckBox("internal", "Show internal drives",
		new BMessage(kMsgShowInternal));
	fShowInternal->SetToolTip("Internal drives hold your data and systems."
		" They are hidden unless you really mean to write to one.");
	fHint = new BStringView("hint", "");
	fHint->SetTruncation(B_TRUNCATE_END);
	fHint->SetExplicitMinSize(BSize(em * 20, B_SIZE_UNSET));
	BButton* cancel = new BButton("cancel", "Cancel",
		new BMessage(B_QUIT_REQUESTED));
	fSelectButton = new BButton("select", "Select", new BMessage(kMsgSelect));
	fSelectButton->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(new BStringView("title", "Choose the drive to write the image"
			" to. Everything on it will be erased."))
		.Add(scroll)
		.Add(fHint)
		.AddGroup(B_HORIZONTAL)
			.Add(fShowInternal)
			.AddGlue()
			.Add(cancel)
			.Add(fSelectButton)
		.End();

	if (parent != NULL) {
		CenterIn(parent->Frame());
		MoveOnScreen();
	}
}


void
DriveWindow::SetDrives(const std::vector<DriveInfo>& drives, uint64 imageSize)
{
	fDrives = drives;
	fImageSize = imageSize;
	_Rebuild();
}


void
DriveWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSelectionChanged:
			_SelectionChanged();
			break;
		case kMsgInvoked:
		case kMsgSelect:
			_Choose();
			break;
		case kMsgShowInternal:
			_Rebuild();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


void
DriveWindow::_Rebuild()
{
	bool showInternal = fShowInternal->Value() == B_CONTROL_ON;
	int hidden = 0;
	for (int32 i = fList->CountItems() - 1; i >= 0; i--)
		delete fList->RemoveItem(i);

	// Drives that can be written first, then the others; within each, in
	// the order Haiku lists them.
	for (int pass = 0; pass < 2; pass++) {
		for (const DriveInfo& drive : fDrives) {
			DriveState state = Assess(drive, fImageSize);
			if ((state == DriveState::kAvailable) != (pass == 0))
				continue;
			// The boot drive is always listed when it is external (booted
			// from a USB stick or SD card), to show why it is not offered.
			if (IsInternal(drive) && !showInternal) {
				if (pass == 0)
					hidden++;
				continue;
			}
			fList->AddItem(new DriveItem(drive, state));
		}
	}

	int32 selectIndex = -1;
	for (int32 i = 0; i < fList->CountItems(); i++) {
		DriveItem* item = static_cast<DriveItem*>(fList->ItemAt(i));
		if (!item->Available())
			continue;
		if (item->Drive().id == fSelectedID || selectIndex < 0)
			selectIndex = i;
		if (item->Drive().id == fSelectedID)
			break;
	}
	if (selectIndex >= 0)
		fList->Select(selectIndex);
	_SelectionChanged();

	if (fList->CountItems() == 0 || selectIndex < 0) {
		BString hint = "Insert a USB stick or SD card.";
		if (hidden > 0 && !showInternal) {
			hint << " (" << hidden << (hidden == 1 ? " internal drive is"
				: " internal drives are") << " hidden.)";
		}
		fHint->SetText(hint);
	}
}


void
DriveWindow::_SelectionChanged()
{
	int32 index = fList->CurrentSelection();
	DriveItem* item = index >= 0
		? static_cast<DriveItem*>(fList->ItemAt(index)) : NULL;
	if (item != NULL && !item->Available()) {
		// Disabled items cannot stay selected; say why instead.
		fHint->SetText(Reason(item));
		fList->DeselectAll();
		item = NULL;
	} else if (item != NULL) {
		fSelectedID = item->Drive().id;
		fHint->SetText(IsInternal(item->Drive())
			? "This is an internal drive. Make sure it is the right one."
			: "");
	}
	fSelectButton->SetEnabled(item != NULL);
}


void
DriveWindow::_Choose()
{
	int32 index = fList->CurrentSelection();
	if (index < 0)
		return;
	DriveItem* item = static_cast<DriveItem*>(fList->ItemAt(index));
	if (!item->Available())
		return;
	BMessage message(kMsgDriveChosen);
	message.AddInt32("id", item->Drive().id);
	fTarget.SendMessage(&message);
	PostMessage(B_QUIT_REQUESTED);
}
