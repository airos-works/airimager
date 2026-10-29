// DriveWindow.h - choosing the drive to write to.
//
// USB drives, SD cards and disk image files are listed; internal drives only
// when "Show internal drives" is checked. Drives that cannot be written (the
// air/OS boot drive above all) are shown, greyed out with the reason, and
// cannot be selected. The list follows drives being plugged in and removed.
#ifndef AIRIMAGER_DRIVE_WINDOW_H
#define AIRIMAGER_DRIVE_WINDOW_H

#include <Messenger.h>
#include <Window.h>

#include <vector>

#include "engine/Drive.h"

class BButton;
class BCheckBox;
class BListView;
class BStringView;

// To the target: "id" (int32), the chosen drive.
const uint32 kMsgDriveChosen = 'dchs';


class DriveWindow : public BWindow {
public:
						DriveWindow(BWindow* parent, BMessenger target,
							uint64 imageSize, int32 selectedID);

			// New drive information (called with the window locked).
			void		SetDrives(const std::vector<imager::DriveInfo>& drives,
							uint64 imageSize);

	virtual	void		MessageReceived(BMessage* message);

private:
			void		_Rebuild();
			void		_SelectionChanged();
			void		_Choose();

			BMessenger	fTarget;
			std::vector<imager::DriveInfo> fDrives;
			uint64		fImageSize;
			int32		fSelectedID;
			BListView*	fList;
			BCheckBox*	fShowInternal;
			BStringView* fHint;
			BButton*	fSelectButton;
};

#endif
