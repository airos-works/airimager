// Views.h - the pieces of airImager's windows: an icon, one of the three
// steps (Image, Drive, Write), the arrow between steps, a web link, and a
// drive in the drive list.
#ifndef AIRIMAGER_VIEWS_H
#define AIRIMAGER_VIEWS_H

#include <GroupView.h>
#include <ListItem.h>
#include <StringView.h>
#include <View.h>

#include "engine/Drive.h"

class BBitmap;
class BButton;

// A vector icon from the application's resources ('VICN', by name; the
// application icon is "BEOS:ICON"), rendered at size pixels.
BBitmap* ResourceIcon(const char* name, float size);
// The icon size of the steps: 64 pixels at the standard 12 pt font.
float StepIconSize();


class IconView : public BView {
public:
						IconView(float size);
	virtual				~IconView();

			void		SetIcon(BBitmap* icon);		// takes ownership
			void		SetDimmed(bool dimmed);

	virtual	void		Draw(BRect updateRect);
	virtual	BSize		MinSize();
	virtual	BSize		MaxSize();
	virtual	BSize		PreferredSize();

private:
			float		fSize;
			BBitmap*	fIcon;
			bool		fDimmed;
};


class StepView : public BGroupView {
public:
						StepView(const char* title, BButton* button,
							float width);

			IconView*	Icon() const { return fIcon; }
			// Up to three lines under the button; NULL leaves a line empty.
			void		SetLines(const char* first, const char* second = NULL,
							const char* third = NULL);
			void		SetLineColor(int index, const rgb_color& color);
			void		ResetLineColors();
			void		AddExtra(BView* view);

private:
			IconView*	fIcon;
			BStringView* fLines[3];
};


// A chevron between two steps, highlighted once the step before is done.
class StepArrow : public BView {
public:
						StepArrow();

			void		SetActive(bool active);

	virtual	void		Draw(BRect updateRect);
	virtual	BSize		MinSize();
	virtual	BSize		MaxSize();

private:
			bool		fActive;
};


// Text that opens a web page when clicked.
class LinkView : public BStringView {
public:
						LinkView(const char* label, const char* url);

	virtual	void		AttachedToWindow();
	virtual	void		Draw(BRect updateRect);
	virtual	void		MouseDown(BPoint where);
	virtual	void		MouseMoved(BPoint where, uint32 transit,
							const BMessage* dragMessage);

private:
			BString		fURL;
};


// A drive in the target list: its icon, name, size and bus, and why it
// cannot be chosen, if it cannot.
class DriveItem : public BListItem {
public:
						DriveItem(const imager::DriveInfo& drive,
							imager::DriveState state);
	virtual				~DriveItem();

			const imager::DriveInfo& Drive() const { return fDrive; }
			imager::DriveState State() const { return fState; }
			bool		Available() const
							{ return fState == imager::DriveState::kAvailable; }

	virtual	void		DrawItem(BView* owner, BRect frame, bool complete);
	virtual	void		Update(BView* owner, const BFont* font);

private:
			imager::DriveInfo	fDrive;
			imager::DriveState	fState;
			BBitmap*	fIcon;
			float		fIconSize;
};

#endif
