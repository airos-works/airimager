// Views.cpp - see Views.h.
#include "Views.h"

#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <ControlLook.h>
#include <Cursor.h>
#include <IconUtils.h>
#include <LayoutBuilder.h>
#include <Resources.h>
#include <Roster.h>

#include "DriveRoster.h"
#include "engine/Format.h"

using namespace imager;

namespace {

float
FontScale()
{
	return be_plain_font->Size() / 12.0f;
}

}  // namespace


BBitmap*
ResourceIcon(const char* name, float size)
{
	BResources* resources = be_app->AppResources();
	if (resources == NULL)
		return NULL;
	size_t length;
	const void* data = resources->LoadResource('VICN', name, &length);
	if (data == NULL)
		return NULL;
	BBitmap* icon = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	if (BIconUtils::GetVectorIcon(static_cast<const uint8*>(data), length,
			icon) != B_OK) {
		delete icon;
		return NULL;
	}
	return icon;
}


float
StepIconSize()
{
	return floorf(64 * FontScale());
}


// #pragma mark - IconView


IconView::IconView(float size)
	:
	BView("icon", B_WILL_DRAW),
	fSize(size),
	fIcon(NULL),
	fDimmed(false)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetFlags(Flags() | B_TRANSPARENT_BACKGROUND);
}


IconView::~IconView()
{
	delete fIcon;
}


void
IconView::SetIcon(BBitmap* icon)
{
	delete fIcon;
	fIcon = icon;
	Invalidate();
}


void
IconView::SetDimmed(bool dimmed)
{
	if (fDimmed == dimmed)
		return;
	fDimmed = dimmed;
	Invalidate();
}


void
IconView::Draw(BRect updateRect)
{
	if (fIcon == NULL)
		return;
	SetDrawingMode(B_OP_ALPHA);
	if (fDimmed) {
		SetHighColor(0, 0, 0, 90);
		SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
	} else
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
	BRect bounds = Bounds();
	BRect frame(0, 0, fSize - 1, fSize - 1);
	frame.OffsetBy(floorf((bounds.Width() - fSize) / 2),
		floorf((bounds.Height() - fSize) / 2));
	DrawBitmap(fIcon, fIcon->Bounds(), frame, B_FILTER_BITMAP_BILINEAR);
	SetDrawingMode(B_OP_COPY);
}


BSize
IconView::MinSize()
{
	return BSize(fSize, fSize);
}


BSize
IconView::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, fSize);
}


BSize
IconView::PreferredSize()
{
	return BSize(fSize, fSize);
}


// #pragma mark - StepView


StepView::StepView(const char* title, BButton* button, float width)
	:
	BGroupView(title, B_VERTICAL, 0)
{
	fIcon = new IconView(StepIconSize());
	BStringView* titleView = new BStringView("title", title);
	BFont font(be_bold_font);
	font.SetSize(be_bold_font->Size() * 1.15f);
	titleView->SetFont(&font);
	titleView->SetAlignment(B_ALIGN_CENTER);
	titleView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	button->SetExplicitMinSize(BSize(width * 0.8f, B_SIZE_UNSET));
	button->SetExplicitMaxSize(BSize(width * 0.8f, B_SIZE_UNSET));

	float spacing = be_control_look->DefaultItemSpacing();
	BLayoutBuilder::Group<>(this)
		.Add(fIcon)
		.AddStrut(spacing * 0.5f)
		.Add(titleView)
		.AddStrut(spacing)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(button)
			.AddGlue()
		.End()
		.AddStrut(spacing * 0.5f);

	for (int i = 0; i < 3; i++) {
		fLines[i] = new BStringView(NULL, "");
		fLines[i]->SetAlignment(B_ALIGN_CENTER);
		fLines[i]->SetTruncation(B_TRUNCATE_MIDDLE);
		fLines[i]->SetExplicitMinSize(BSize(width, B_SIZE_UNSET));
		fLines[i]->SetExplicitMaxSize(BSize(width, B_SIZE_UNSET));
		AddChild(fLines[i]);
	}
	// The first line, the image's or drive's name, stands out.
	fLines[0]->SetFont(be_bold_font);
	GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
	SetExplicitMinSize(BSize(width, B_SIZE_UNSET));
	SetExplicitMaxSize(BSize(width, B_SIZE_UNLIMITED));
}


void
StepView::SetLines(const char* first, const char* second, const char* third)
{
	const char* lines[3] = { first, second, third };
	for (int i = 0; i < 3; i++) {
		fLines[i]->SetText(lines[i] != NULL ? lines[i] : "");
		fLines[i]->SetToolTip(lines[i] != NULL && lines[i][0] != '\0'
			? lines[i] : NULL);
	}
}


void
StepView::SetLineColor(int index, const rgb_color& color)
{
	fLines[index]->SetHighColor(color);
	fLines[index]->Invalidate();
}


void
StepView::ResetLineColors()
{
	for (int i = 0; i < 3; i++) {
		fLines[i]->SetHighUIColor(B_PANEL_TEXT_COLOR);
		fLines[i]->Invalidate();
	}
}


void
StepView::AddExtra(BView* view)
{
	BLayoutBuilder::Group<>(this)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(view)
			.AddGlue()
		.End();
}


// #pragma mark - StepArrow


StepArrow::StepArrow()
	:
	BView("arrow", B_WILL_DRAW),
	fActive(false)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


void
StepArrow::SetActive(bool active)
{
	if (fActive == active)
		return;
	fActive = active;
	Invalidate();
}


void
StepArrow::Draw(BRect updateRect)
{
	// Level with the middle of the step icons.
	float scale = FontScale();
	float y = floorf(StepIconSize() / 2);
	BRect bounds = Bounds();
	float left = bounds.left + 4 * scale;
	float right = bounds.right - 4 * scale;
	rgb_color color = fActive ? ui_color(B_CONTROL_HIGHLIGHT_COLOR)
		: tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT);
	SetHighColor(color);
	SetPenSize(2 * scale);
	SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
	StrokeLine(BPoint(left, y), BPoint(right, y));
	float head = 6 * scale;
	StrokeLine(BPoint(right - head, y - head), BPoint(right, y));
	StrokeLine(BPoint(right - head, y + head), BPoint(right, y));
	SetPenSize(1);
}


BSize
StepArrow::MinSize()
{
	return BSize(36 * FontScale(), StepIconSize());
}


BSize
StepArrow::MaxSize()
{
	return BSize(36 * FontScale(), B_SIZE_UNLIMITED);
}


// #pragma mark - LinkView


LinkView::LinkView(const char* label, const char* url)
	:
	BStringView("link", label),
	fURL(url)
{
	SetToolTip(url);
}


void
LinkView::AttachedToWindow()
{
	BStringView::AttachedToWindow();
	SetHighUIColor(B_LINK_TEXT_COLOR);
}


void
LinkView::Draw(BRect updateRect)
{
	BStringView::Draw(updateRect);
	// Underlined, as links are.
	font_height height;
	GetFontHeight(&height);
	float width = StringWidth(Text());
	BRect bounds = Bounds();
	float x = 0;
	if (Alignment() == B_ALIGN_CENTER)
		x = floorf((bounds.Width() - width) / 2);
	else if (Alignment() == B_ALIGN_RIGHT)
		x = bounds.right - width;
	float baseline = ceilf(height.ascent) + floorf((bounds.Height()
		- ceilf(height.ascent + height.descent)) / 2) + 1;
	StrokeLine(BPoint(x, baseline), BPoint(x + width, baseline));
}


void
LinkView::MouseDown(BPoint where)
{
	const char* args[] = { fURL.String(), NULL };
	be_roster->Launch("application/x-vnd.Be.URL.https", 1, args);
}


void
LinkView::MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage)
{
	if (transit == B_ENTERED_VIEW) {
		BCursor cursor(B_CURSOR_ID_FOLLOW_LINK);
		SetViewCursor(&cursor);
	} else if (transit == B_EXITED_VIEW)
		SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);
}


// #pragma mark - DriveItem


DriveItem::DriveItem(const DriveInfo& drive, DriveState state)
	:
	BListItem(),
	fDrive(drive),
	fState(state),
	fIcon(NULL),
	fIconSize(floorf(32 * FontScale()))
{
	fIcon = DriveRoster::DriveIcon(drive, fIconSize);
	if (fIcon == NULL)
		fIcon = ResourceIcon("drive", fIconSize);
	SetEnabled(Available());
}


DriveItem::~DriveItem()
{
	delete fIcon;
}


void
DriveItem::Update(BView* owner, const BFont* font)
{
	BListItem::Update(owner, font);
	font_height height;
	font->GetHeight(&height);
	float lines = 2 * ceilf(height.ascent + height.descent + height.leading);
	float spacing = be_control_look->DefaultItemSpacing();
	SetHeight(std::max(lines, fIconSize) + spacing);
}


void
DriveItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	float spacing = be_control_look->DefaultItemSpacing();
	rgb_color background = IsSelected()
		? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
		: ui_color(B_LIST_BACKGROUND_COLOR);
	rgb_color text = IsSelected() ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
		: ui_color(B_LIST_ITEM_TEXT_COLOR);
	owner->SetLowColor(background);
	owner->SetHighColor(background);
	owner->FillRect(frame);

	BPoint iconPoint(frame.left + spacing,
		frame.top + floorf((frame.Height() - fIconSize) / 2));
	if (fIcon != NULL) {
		owner->SetDrawingMode(B_OP_ALPHA);
		if (Available()) {
			owner->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
		} else {
			owner->SetHighColor(0, 0, 0, 100);
			owner->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
		}
		owner->DrawBitmap(fIcon, fIcon->Bounds(),
			BRect(iconPoint, iconPoint + BPoint(fIconSize - 1, fIconSize - 1)),
			B_FILTER_BITMAP_BILINEAR);
		owner->SetDrawingMode(B_OP_COPY);
	}

	if (!Available())
		text = tint_color(background, B_DISABLED_LABEL_TINT);
	BFont bold(be_bold_font);
	BFont plain(be_plain_font);
	font_height height;
	plain.GetHeight(&height);
	float lineHeight = ceilf(height.ascent + height.descent + height.leading);
	float textLeft = iconPoint.x + fIconSize + spacing;
	float top = frame.top + floorf((frame.Height() - 2 * lineHeight) / 2);

	// The reason a drive cannot be chosen, at the right.
	std::string state = StateLabel(fState);
	float stateWidth = 0;
	if (!state.empty()) {
		owner->SetFont(&bold);
		stateWidth = owner->StringWidth(state.c_str());
		rgb_color warning = fState == DriveState::kBootDrive
			? make_color(200, 40, 40) : text;
		owner->SetHighColor(warning);
		owner->DrawString(state.c_str(), BPoint(frame.right - spacing
			- stateWidth, top + lineHeight + ceilf(height.ascent) / 2));
	}
	float textWidth = frame.right - spacing - textLeft
		- (stateWidth > 0 ? stateWidth + spacing : 0);

	owner->SetHighColor(text);
	owner->SetFont(&bold);
	BString name(DisplayName(fDrive).c_str());
	owner->TruncateString(&name, B_TRUNCATE_END, textWidth);
	owner->DrawString(name, BPoint(textLeft, top + ceilf(height.ascent)));

	owner->SetFont(&plain);
	BString details;
	details << FormatBytes(fDrive.size).c_str() << " · "
		<< BusLabel(fDrive.bus).c_str();
	for (size_t i = 0; i < fDrive.volumes.size(); i++) {
		details << (i == 0 ? " · " : ", ") << "\""
			<< fDrive.volumes[i].c_str() << "\"";
	}
	details << " · " << fDrive.path.c_str();
	owner->TruncateString(&details, B_TRUNCATE_END, textWidth);
	// The details are a step quieter than the name, in light and dark
	// themes alike.
	owner->SetHighColor(Available() ? mix_color(background, text, 190) : text);
	owner->DrawString(details, BPoint(textLeft,
		top + lineHeight + ceilf(height.ascent)));
}
