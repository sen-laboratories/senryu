/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/


#include "TextWidget.h"

#include <string.h>
#include <stdlib.h>

#include <Alert.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Clipboard.h>
#include <Debug.h>
#include <DateTime.h>
#include <Directory.h>
#include <MessageFilter.h>
#include <ScrollView.h>
#include <TextView.h>
#include <Volume.h>
#include <Window.h>

#include <sen/SenAttrValues.h>
#include <sen/SenAttributes.h>
#include <sen/SenFilterMenu.h>
#include <sen/SenRecurrence.h>
#include <sen/SenTimeAttributes.h>

#include "Attributes.h"
#include "ContainerWindow.h"
#include "Commands.h"
#include "FSUtils.h"
#include "PoseView.h"
#include "SenDatePicker.h"
#include "Utilities.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TextWidget"


const float kWidthMargin = 20;

// SEN: the pause between a click on an attribute cell and the edit (a double click still opens the file when it is faster)
const bigtime_t kCellEditDelay = 200000;


//	#pragma mark - BTextWidget


BTextWidget::BTextWidget(Model* model, BColumn* column, BPoseView* view)
	:
	fText(WidgetAttributeText::NewWidgetText(model, column, view)),
	fAttrHash(column->AttrHash()),
	fAlignment(column->Alignment()),
	fEditable(column->Editable()),
	fVisible(true),
	fActive(false),
	fSymLink(model->IsSymLink()),
	fArmed(false),
	fMaxWidth(0),
	fFilterMenu(NULL),
	fLastClickedTime(0),
	fEditDelay(0)
{
}


BTextWidget::~BTextWidget()
{
	if (fLastClickedTime != 0)
		fParams.poseView->SetTextWidgetToCheck(NULL, this);

	delete fFilterMenu;
	delete fText;
}


int
BTextWidget::Compare(const BTextWidget& with, BPoseView* view) const
{
	return fText->Compare(*with.fText, view);
}


const char*
BTextWidget::Text(const BPoseView* view) const
{
	StringAttributeText* textAttribute = dynamic_cast<StringAttributeText*>(fText);
	if (textAttribute == NULL)
		return NULL;

	return textAttribute->ValueAsText(view);
}


float
BTextWidget::TextWidth(const BPoseView* pose) const
{
	return fText->Width(pose);
}


float
BTextWidget::PreferredWidth(const BPoseView* pose) const
{
	return fText->PreferredWidth(pose);
}


BRect
BTextWidget::ColumnRect(BPoint poseLoc, const BColumn* column,
	const BPoseView* view)
{
	if (view->ViewMode() != kListMode) {
		// ColumnRect only makes sense in list view, return
		// CalcRect otherwise
		return CalcRect(poseLoc, column, view);
	}

	BRect rect;
	rect.left = column->Offset() + poseLoc.x;
	rect.right = rect.left + column->Width();
	rect.bottom = poseLoc.y + roundf((view->ListElemHeight() + view->FontHeight()) / 2.f);
	rect.top = rect.bottom - view->FontHeight();

	return rect;
}


BRect
BTextWidget::CalcRectCommon(BPoint poseLoc, const BColumn* column,
	const BPoseView* view, float textWidth)
{
	BRect rect;
	float viewWidth;

	poseLoc.x = roundf(poseLoc.x);
	poseLoc.y = roundf(poseLoc.y);

	if (view->ViewMode() == kListMode) {
		viewWidth = roundf(std::min(column->Width(), textWidth));

		poseLoc.x += column->Offset();

		switch (fAlignment) {
			case B_ALIGN_LEFT:
				rect.left = poseLoc.x;
				rect.right = rect.left + viewWidth - 1;
				break;

			case B_ALIGN_CENTER:
				rect.left = poseLoc.x + roundf((column->Width() - viewWidth) / 2.f);
				if (rect.left < 0)
					rect.left = 0;

				rect.right = rect.left + viewWidth - 1;
				break;

			case B_ALIGN_RIGHT:
				rect.right = poseLoc.x + column->Width();
				rect.left = rect.right - viewWidth + 1;
				if (rect.left < 0)
					rect.left = 0;
				break;

			default:
				TRESPASS();
				break;
		}

		rect.bottom = poseLoc.y + roundf((view->ListElemHeight() + view->FontHeight()) / 2.f);
		rect.top = rect.bottom - view->FontHeight() + 1;
	} else {
		float iconSize = (float)view->IconSizeInt();
		if (view->ViewMode() == kIconMode) {
			// icon mode
			viewWidth = roundf(std::min(view->StringWidth("M") * 30, textWidth));

			rect.left = poseLoc.x + roundf((iconSize - viewWidth) / 2.f);
			rect.bottom = poseLoc.y + ceilf(view->IconPoseHeight());
		} else {
			// mini icon mode
			viewWidth = roundf(textWidth);

			rect.left = poseLoc.x + iconSize + kMiniIconSeparator;
			rect.bottom = poseLoc.y + roundf((iconSize + view->FontHeight()) / 2.f);
		}

		rect.top = rect.bottom - view->FontHeight() + 1;
		rect.right = rect.left + viewWidth - 1;
	}

	return rect;
}


BRect
BTextWidget::CalcRect(BPoint poseLoc, const BColumn* column, const BPoseView* view)
{
	return CalcRectCommon(poseLoc, column, view, fText->Width(view));
}


BRect
BTextWidget::CalcOldRect(BPoint poseLoc, const BColumn* column, const BPoseView* view)
{
	return CalcRectCommon(poseLoc, column, view, fText->CurrentWidth());
}


BRect
BTextWidget::CalcClickRect(BPoint poseLoc, const BColumn* column, const BPoseView* view)
{
	BRect rect = CalcRect(poseLoc, column, view);
	if (rect.Width() < kWidthMargin) {
		// if recting rect too narrow, make it a bit wider
		// for comfortable clicking
		if (column != NULL && column->Width() < kWidthMargin)
			rect.right = rect.left + column->Width();
		else
			rect.right = rect.left + kWidthMargin;
	}

	return rect;
}


BRect
BTextWidget::CalcCellRect(BPoint poseLoc, const BColumn* column, const BPoseView* view)
{
	BRect rect;
	rect.left = roundf(poseLoc.x) + column->Offset();
	rect.right = rect.left + column->Width() - 1;
	rect.top = roundf(poseLoc.y);
	rect.bottom = rect.top + view->ListElemHeight() - 1;
	return rect;
}


BRect
BTextWidget::CalcHitRect(BPoint poseLoc, const BColumn* column, const BPoseView* view)
{
	if (view->ViewMode() == kListMode && column != NULL && column != view->FirstColumn() && IsEditable() && IsVisible())
		return CalcCellRect(poseLoc, column, view);

	return CalcClickRect(poseLoc, column, view);
}


void
BTextWidget::CheckExpiration()
{
	if (fLastClickedTime > 0 && IsEditable() && fParams.pose->IsSelected()) {
		bigtime_t delta = system_time() - fLastClickedTime;

		if (delta >= fEditDelay) {
			// the pause elapsed with no other click
			fLastClickedTime = 0;
			BColumn* column = fParams.poseView->ColumnFor(fAttrHash);
			StartEdit(fParams.poseView, fParams.pose, column);
		}
	} else {
		CancelWait();
	}
}


void
BTextWidget::CancelWait()
{
	fLastClickedTime = 0;
	fParams.poseView->SetTextWidgetToCheck(NULL);
	// SEN: no edit is coming: the outline of the cell goes
	fParams.poseView->SetArmedWidget(NULL, NULL);
}


void
BTextWidget::DoMouseUp(BPoseView* view, BPose* pose)
{
	// Register the time of that click. The edit starts when the pause has elapsed with no other click (a timer of the PoseView, see StartTextWidgetCheck()).

	view->SetTextWidgetToCheck(NULL);
	if (!IsEditable() || !pose->IsSelected() || view->ExtendSelection()) {
		fLastClickedTime = 0;
		return;
	}

	bigtime_t doubleClickSpeed;
	get_click_speed(&doubleClickSpeed);

	BColumn* column = view->ColumnFor(fAttrHash);
	if (view->ViewMode() == kListMode && column != NULL && column != view->FirstColumn()) {
		// SEN: an attribute cell: one click is enough (it selects the file and outlines the cell at once), and the edit follows after a short pause: a click that is part of a
		// double click, which opens the file, is a faster one
		fLastClickedTime = system_time();
		fEditDelay = std::min(doubleClickSpeed, kCellEditDelay);
	} else {
		// the name: the first click selects, a second one (after the double click time) renames
		if (fLastClickedTime == 0) {
			fLastClickedTime = system_time();
			if (fLastClickedTime - doubleClickSpeed < pose->SelectionTime())
				fLastClickedTime = 0;
		} else
			fLastClickedTime = 0;

		fEditDelay = doubleClickSpeed;
	}

	if (fLastClickedTime == 0)
		return;

	view->StartTextWidgetCheck(this, fEditDelay);

	fParams.pose = pose;
	fParams.poseView = view;
}


static filter_result
TextViewKeyDownFilter(BMessage* message, BHandler**, BMessageFilter* filter)
{
	uchar key;
	if (message->FindInt8("byte", (int8*)&key) != B_OK)
		return B_DISPATCH_MESSAGE;

	ThrowOnAssert(filter != NULL);

	BContainerWindow* window = dynamic_cast<BContainerWindow*>(
		filter->Looper());
	ThrowOnAssert(window != NULL);

	BPoseView* view = window->PoseView();
	ThrowOnAssert(view != NULL);

	if (key == B_RETURN || key == B_ESCAPE) {
		view->CommitActivePose(key == B_RETURN);
		return B_SKIP_MESSAGE;
	}

	if (key == B_TAB) {
		if (view->ActivePose()) {
			if (message->FindInt32("modifiers") & B_SHIFT_KEY)
				view->ActivePose()->EditPreviousWidget(view);
			else
				view->ActivePose()->EditNextWidget(view);
		}

		return B_SKIP_MESSAGE;
	}

	// the BTextView doesn't respect window borders when resizing itself;
	// we try to work-around this "bug" here.

	// find the text editing view
	BView* scrollView = view->FindView("BorderView");
	if (scrollView != NULL) {
		BTextView* textView = dynamic_cast<BTextView*>(
			scrollView->FindView("WidgetTextView"));
		if (textView != NULL) {
			ASSERT(view->ActiveTextWidget() != NULL);
			float maxWidth = view->ActiveTextWidget()->MaxWidth();
			bool tooWide = textView->TextRect().Width() > maxWidth;
			textView->MakeResizable(!tooWide, tooWide ? NULL : scrollView);
		}
	}

	return B_DISPATCH_MESSAGE;
}


static filter_result
TextViewPasteFilter(BMessage* message, BHandler**, BMessageFilter* filter)
{
	ThrowOnAssert(filter != NULL);

	BContainerWindow* window = dynamic_cast<BContainerWindow*>(
		filter->Looper());
	ThrowOnAssert(window != NULL);

	BPoseView* view = window->PoseView();
	ThrowOnAssert(view != NULL);

	// the BTextView doesn't respect window borders when resizing itself;
	// we try to work-around this "bug" here.

	// find the text editing view
	BView* scrollView = view->FindView("BorderView");
	if (scrollView != NULL) {
		BTextView* textView = dynamic_cast<BTextView*>(
			scrollView->FindView("WidgetTextView"));
		if (textView != NULL) {
			float textWidth = textView->TextRect().Width();

			// subtract out selected text region width
			int32 start, finish;
			textView->GetSelection(&start, &finish);
			if (start != finish) {
				BRegion selectedRegion;
				textView->GetTextRegion(start, finish, &selectedRegion);
				textWidth -= selectedRegion.Frame().Width();
			}

			// add pasted text width
			if (be_clipboard->Lock()) {
				BMessage* clip = be_clipboard->Data();
				if (clip != NULL) {
					const char* text = NULL;
					ssize_t length = 0;

					if (clip->FindData("text/plain", B_MIME_TYPE,
							(const void**)&text, &length) == B_OK) {
						textWidth += textView->StringWidth(text);
					}
				}

				be_clipboard->Unlock();
			}

			// check if pasted text is too wide
			ASSERT(view->ActiveTextWidget() != NULL);
			float maxWidth = view->ActiveTextWidget()->MaxWidth();
			bool tooWide = textWidth > maxWidth;

			if (tooWide) {
				// resize text view to max width

				// move scroll view if not left aligned
				float oldWidth = textView->Bounds().Width();
				float newWidth = maxWidth;
				float right = oldWidth - newWidth;

				if (textView->Alignment() == B_ALIGN_CENTER)
					scrollView->MoveBy(roundf(right / 2), 0);
				else if (textView->Alignment() == B_ALIGN_RIGHT)
					scrollView->MoveBy(right, 0);

				// resize scroll view
				float grow = newWidth - oldWidth;
				scrollView->ResizeBy(grow, 0);
			}

			textView->MakeResizable(!tooWide, tooWide ? NULL : scrollView);
		}
	}

	return B_DISPATCH_MESSAGE;
}


// SEN: the common values of the attribute of a column for the type of a file (and its semantic type META:TYPE), if the ontology has some
static bool
ValuesFor(BPose* pose, BColumn* column, ::sen::attrvalues::Values* values)
{
	Model* model = pose != NULL ? pose->TargetModel() : NULL;
	if (model == NULL || column == NULL)
		return false;

	BString semanticType;
	if (model->OpenNode() == B_OK) {
		model->Node()->ReadAttrString(::sen::attr::kType, &semanticType);
		model->CloseNode();
	}

	return (!semanticType.IsEmpty() && ::sen::attrvalues::Load(semanticType.String(), column->AttrName(), values))
		|| ::sen::attrvalues::Load(model->MimeType(), column->AttrName(), values);
}


void
BTextWidget::StartEdit(BPoseView* view, BPose* pose, BColumn* column)
{
	ASSERT(view != NULL);
	ASSERT(view->Window() != NULL);
	ASSERT(pose != NULL);

	view->SetTextWidgetToCheck(NULL, this);
	view->SetArmedWidget(NULL, NULL);
	if (!IsEditable() || IsActive())
		return;

	if (fText->IsCheckbox()) {
		// SEN: a boolean is a check box: the click turns it over and nothing is edited (a file on a volume that cannot be written keeps its value)
		if (!view->SelectedVolumeIsReadOnly() && fText->ToggleValue()) {
			const BPoint where = view->ViewMode() == kListMode
				? BPoint(0, view->IndexOfPose(pose) * view->ListElemHeight()) : pose->Location(view);
			CheckAndUpdate(where, column, view, true);
		}
		return;
	}

	view->SetActiveTextWidget(this);

	// The initial text color has to be set differently on Desktop
	rgb_color initialTextColor;
	if (view->IsDesktopView())
		initialTextColor = InvertColor(view->HighColor());
	else
		initialTextColor = view->HighColor();

	// The pose may have been dragged to a new location.
	BPoint poseLoc;
	if (view->ViewMode() == kListMode)
		poseLoc = BPoint(0, view->IndexOfPose(pose) * view->ListElemHeight());
	else
		poseLoc = pose->Location(view);

	BRect rect(CalcRect(poseLoc, column, view));
	rect.OffsetTo(roundf(rect.left), roundf(rect.top));

	BTextView* textView = new BTextView(rect.InsetByCopy(-2, -2), "WidgetTextView",
		rect.OffsetToCopy(2, 2), be_plain_font, &initialTextColor, B_FOLLOW_ALL, B_WILL_DRAW);

	textView->SetWordWrap(false);
	textView->SetInsets(2, 2, 2, 2);
	DisallowMetaKeys(textView);
	fText->SetupEditing(textView);

	if (view->IsDesktopView()) {
		// force text view colors to be inverse of Desktop text color, white or black
		rgb_color backColor = view->HighColor();
		rgb_color textColor = InvertColor(backColor);
		backColor = tint_color(backColor,
			view->SelectedVolumeIsReadOnly() ? ReadOnlyTint(backColor) : B_NO_TINT);

		textView->SetViewColor(backColor);
		textView->SetLowColor(backColor);
		textView->SetHighColor(textColor);
	} else {
		// document colors or tooltip colors on Open with... window
		textView->SetViewUIColor(view->ViewUIColor());
		textView->SetLowUIColor(view->LowUIColor());
		textView->SetHighUIColor(view->HighUIColor());
	}

	if (view->SelectedVolumeIsReadOnly()) {
		textView->MakeEditable(false);
		textView->MakeSelectable(true);
	}

	textView->AddFilter(new BMessageFilter(B_KEY_DOWN, TextViewKeyDownFilter));
	if (!view->SelectedVolumeIsReadOnly())
		textView->AddFilter(new BMessageFilter(B_PASTE, TextViewPasteFilter));

	// truncated text bounds
	BRect bounds(rect);

	// get full text length
	rect.right = rect.left + textView->LineWidth() - 1;
	rect.bottom = rect.top + textView->LineHeight() - 1;

	if (view->ViewMode() == kListMode) {
		// limit max width to column width in list mode
		BColumn* column = view->ColumnFor(fAttrHash);
		ASSERT(column != NULL);
		fMaxWidth = column->Width();
	} else {
		// limit max width to 30em in icon and mini icon mode
		fMaxWidth = textView->StringWidth("M") * 30;

		// center under the icon if text is longer than truncated label width
		if (view->ViewMode() == kIconMode && rect.Width() > bounds.Width()) {
			float newWidth = std::min(fMaxWidth, rect.Width());
			rect.OffsetBy(roundf((bounds.Width() - newWidth) / 2), 0);
			textView->MoveTo(rect.left - 2, rect.top - 2);
		}
	}

	// SEN: a field with a list of values is as wide as its widest value and the arrow, even when the text is short or empty (it would be a small box with an arrow)
	bool valued = false;
	{
		::sen::attrvalues::Values values;
		if (ValuesFor(pose, column, &values)) {
			valued = true;
			float widest = 0;
			for (size_t i = 0; i < values.texts.size(); i++)
				widest = std::max(widest, textView->StringWidth(values.texts[i].c_str()));
			const float wanted = std::min(fMaxWidth, widest + ceilf(textView->LineHeight() * 1.1f) + 12);
			if (rect.Width() < wanted)
				rect.right = rect.left + wanted - 1;
		}
	}

	// SEN: a day or a time has the arrow of its calendar: room for a time that is written out
	if (!valued && fText->TimeKind() != 0 && !fText->IsReadOnlyFile() && !view->SelectedVolumeIsReadOnly()) {
		valued = true;
		const float wanted = std::min(fMaxWidth, textView->StringWidth("0000-00-00 00:00") + ceilf(textView->LineHeight() * 1.1f) + 12);
		if (rect.Width() < wanted)
			rect.right = rect.left + wanted - 1;
	}

	// SEN: the field of an attribute fills its cell (an empty value would be a field that is hard to see and to hit)
	if (view->ViewMode() == kListMode && column != NULL && column != view->FirstColumn()) {
		const BRect cell(CalcCellRect(poseLoc, column, view));
		if (cell.Width() - 3 > rect.Width()) {
			rect.left = cell.left + 2;
			rect.right = cell.right - 2;
		}
	}

	// resize textView
	textView->ResizeTo(std::min(fMaxWidth, rect.Width()) + 4, rect.Height() + 4);
	textView->SetTextRect(rect.OffsetToCopy(2, 2));

	// set alignment before adding textView so it doesn't redraw
	switch (view->ViewMode()) {
		case kIconMode:
			textView->SetAlignment(B_ALIGN_CENTER);
			break;

		case kMiniIconMode:
			textView->SetAlignment(B_ALIGN_LEFT);
			break;

		case kListMode:
			textView->SetAlignment(fAlignment);
			break;
	}

	BScrollView* scrollView
		= new BScrollView("BorderView", textView, 0, 0, false, false, B_PLAIN_BORDER);
	view->AddChild(scrollView);

	// (a field with a list of values keeps its width while the text changes, as one that is too wide does: the key filter looks at MaxWidth() after every key)
	if (valued)
		fMaxWidth = 0;
	bool tooWide = textView->TextRect().Width() > fMaxWidth;
	textView->MakeResizable(!tooWide, tooWide ? NULL : scrollView);

	view->SetActivePose(pose);
		// tell view about pose
	SetActive(true);
		// for widget

	// SEN: an attribute with common values (an enum in the schema, kept with the MIME type) gets the list that follows the text below the field
	StartValueList(textView, view, pose, column);
	StartDatePicker(textView, view, column);

	textView->SelectAll();
	textView->ScrollToSelection();
		// scroll to beginning so that text is visible
	textView->MakeFocus();

	// make this text widget invisible while we edit it
	SetVisible(false);

	// force immediate redraw so TextView appears instantly
	view->Window()->UpdateIfNeeded();
}


void
BTextWidget::StartValueList(BTextView* textView, BPoseView* view, BPose* pose, BColumn* column)
{
	::sen::attrvalues::Values values;
	if (!ValuesFor(pose, column, &values))
		return;

	fFilterMenu = new ::sen::FilterPopUpMenu(column->AttrName(), new BMessage());
	fFilterMenu->SetValues(values);
	// one Enter picks the value and ends the edit as it ends the edit of any text
	fFilterMenu->SetEnterAfterPick(true);
	// how a thing repeats: the list ends with `Custom...`, a window for every N weeks, the weekdays and how long (it writes the words and the rule)
	if (::sen::recurrence::IsWordsAttribute(column->AttrName()) && !fText->IsReadOnlyFile())
		fFilterMenu->SetExtraItem(B_TRANSLATE("Custom" B_UTF8_ELLIPSIS), BMessenger(view), BMessage(kMsgOpenRepeatPicker));
	fFilterMenu->AttachTo(textView);
}


void
BTextWidget::StartDatePicker(BTextView* textView, BPoseView* view, BColumn* column)
{
	// (a field with a list of values has its arrow already; a file that cannot be written has no calendar to change it with)
	if (fFilterMenu != NULL || fText->TimeKind() == 0 || fText->IsReadOnlyFile() || view->SelectedVolumeIsReadOnly())
		return;

	fFilterMenu = new ::sen::FilterPopUpMenu(column->AttrName(), new BMessage());
	fFilterMenu->SetArrowMessage(BMessenger(view), BMessage(kMsgOpenDatePicker));
	fFilterMenu->AttachTo(textView);
}


/** Opens the calendar below the field (the arrow opens it, a second click on the arrow closes it again), at the day and the time that the text says, else now. */
void
BTextWidget::ShowDatePicker(BPoseView* view)
{
	if (fDatePicker.IsValid()) {
		fDatePicker.SendMessage(B_QUIT_REQUESTED);
		fDatePicker = BMessenger();
		return;
	}

	BView* scrollView = view->FindView("BorderView");
	BTextView* textView = scrollView != NULL ? dynamic_cast<BTextView*>(scrollView->FindView("WidgetTextView")) : NULL;
	if (textView == NULL)
		return;

	BDate date = BDate::CurrentDate(B_LOCAL_TIME);
	time_t now = time(NULL);
	struct tm parts;
	int32 hour = 0, minute = 0;
	if (localtime_r(&now, &parts) != NULL) {
		hour = parts.tm_hour;
		minute = parts.tm_min;
	}
	bool allDay = false;

	::sen::edtf::Value value;
	if (::sen::edtf::Parse(textView->Text(), &value, ::sen::timeattr::LocalOffsetAt) && value.valid) {
		const time_t moment = (time_t)value.earliest;
		// (an exact time is on the local clock, a day is the moment that it begins at in UTC)
		const bool found = value.IsExact() ? localtime_r(&moment, &parts) != NULL : gmtime_r(&moment, &parts) != NULL;
		if (found) {
			date.SetDate(parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday);
			hour = value.IsExact() ? parts.tm_hour : 0;
			minute = value.IsExact() ? parts.tm_min : 0;
			allDay = !value.IsExact();
		}
	}

	const BPoint where = view->ConvertToScreen(scrollView->Frame().LeftBottom() + BPoint(0, 2));
	const BMessage changed(kMsgDateChanged);
	::sen::DatePickerWindow* picker = new ::sen::DatePickerWindow(where, date, hour, minute,
		fText->TimeKind() == 2, allDay, BMessenger(view), BMessage(kMsgDatePicked), B_TRANSLATE("All day"), false, &changed);
	fDatePicker = BMessenger(picker);
	picker->Show();
}


void
BTextWidget::CloseDatePicker()
{
	if (fDatePicker.IsValid())
		fDatePicker.SendMessage(B_QUIT_REQUESTED);
	fDatePicker = BMessenger();
}


void
BTextWidget::StopEdit(bool saveChanges, BPoint poseLoc, BPoseView* view,
	BPose* pose, int32 poseIndex)
{
	view->SetActiveTextWidget(NULL);

	// find the text editing view
	BView* scrollView = view->FindView("BorderView");
	ASSERT(scrollView != NULL);
	if (scrollView == NULL)
		return;

	BTextView* textView = dynamic_cast<BTextView*>(scrollView->FindView("WidgetTextView"));
	ASSERT(textView != NULL);
	if (textView == NULL)
		return;

	BColumn* column = view->ColumnFor(fAttrHash);
	ASSERT(column != NULL);
	if (column == NULL)
		return;

	// (the list goes first: it follows the text view)
	CloseDatePicker();
	delete fFilterMenu;
	fFilterMenu = NULL;

	if (saveChanges && fText->CommitEditedText(textView)) {
		// we have an actual change, re-sort
		view->CheckPoseSortOrder(pose, poseIndex);
	}

	// make text widget visible again
	SetVisible(true);
	view->Invalidate(ColumnRect(poseLoc, column, view));

	// force immediate redraw so TEView disappears
	scrollView->RemoveSelf();
	delete scrollView;

	ASSERT(view->Window() != NULL);
	view->Window()->UpdateIfNeeded();
	view->MakeFocus();

	SetActive(false);
}


void
BTextWidget::CheckAndUpdate(BPoint loc, const BColumn* column, BPoseView* view, bool visible)
{
	BRect oldRect;
	if (view->ViewMode() != kListMode)
		oldRect = CalcOldRect(loc, column, view);

	if (fText->CheckAttributeChanged() && fText->CheckViewChanged(view) && visible) {
		BRect invalidRect(ColumnRect(loc, column, view));
		if (view->ViewMode() != kListMode)
			invalidRect = invalidRect | oldRect;

		view->Invalidate(invalidRect);
	}
}


void
BTextWidget::SelectAll(BPoseView* view)
{
	BTextView* text = dynamic_cast<BTextView*>(view->FindView("WidgetTextView"));
	if (text != NULL)
		text->SelectAll();
}


void
BTextWidget::Draw(BRect eraseRect, BRect textRect, BPoseView* view, BView* drawView,
	bool selected, uint32 clipboardMode, BPoint offset)
{
	ASSERT(view != NULL);
	ASSERT(view->Window() != NULL);
	ASSERT(drawView != NULL);

	textRect.OffsetBy(offset);

	BRegion textRegion(textRect);
	drawView->ConstrainClippingRegion(&textRegion);

	// We are only concerned with setting the correct text color.

	// For active views the selection is drawn as inverse text
	// (background color for the text, solid black for the background).
	// For inactive windows the text is drawn normally, then the
	// selection rect is alpha-blended on top. This all happens in
	// BPose::Draw before and after calling this function.

	bool direct = drawView == view;
	bool dragging = false;
	if (!direct && view->Window() != NULL && view->Window()->CurrentMessage() != NULL)
		dragging = view->Window()->CurrentMessage()->what == kMsgMouseDragged;
	bool drawOutlines = view->WidgetTextOutline() && !selected && (direct || dragging);
	bool drawCut = clipboardMode == kMoveSelectionTo;

	if (selected) {
		if (dragging) {
			drawView->SetDrawingMode(B_OP_ALPHA);
			drawView->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
		} else {
			// erase selection rect background
			drawView->SetDrawingMode(B_OP_COPY);
		}

		drawView->FillRect(textRect, B_SOLID_LOW);

		// High color is set to inverted low, then the whole thing is
		// inverted again so that the background color "shines through".
		drawView->SetHighColor(InvertColorSmart(drawView->LowColor()));
	} else {
		if (view->IsDesktopView())
			drawView->SetHighColor(view->HighColor());
		else
			drawView->SetHighUIColor(view->HighUIColor());
	}

	if (drawOutlines || dragging || (direct && drawCut)) {
		drawView->SetDrawingMode(B_OP_ALPHA);
		drawView->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
	} else {
		drawView->SetDrawingMode(B_OP_OVER);
	}

	// high color and drawing mode are set to draw text

	BPoint location(textRect.left, roundf(textRect.top) + view->FontInfo().ascent + 1);
		// let's determine the baseline position

	const char* fittingText = fText->FittingText(view);

	if (fText->IsCheckbox()) {
		// SEN: a boolean is a check box, greyed where it cannot be changed (the attribute is read-only, or the volume cannot be written)
		const font_height& height = view->FontInfo();
		const float size = std::min(ceilf(height.ascent + height.descent), textRect.Height() - 1);
		BRect box(textRect.left + 1, roundf(textRect.top + (textRect.Height() - size) / 2), 0, 0);
		box.right = box.left + size - 1;
		box.bottom = box.top + size - 1;

		uint32 flags = fText->IsChecked() ? BControlLook::B_ACTIVATED : 0;
		if (!IsEditable() || fText->IsReadOnlyFile() || view->SelectedVolumeIsReadOnly())
			flags |= BControlLook::B_DISABLED;

		drawView->SetDrawingMode(B_OP_COPY);
		be_control_look->DrawCheckBox(drawView, box, textRect, ui_color(B_LIST_BACKGROUND_COLOR), flags);
		drawView->ConstrainClippingRegion(NULL);
		return;
	}

	// Draw text outline if enabled unless selected or column resizing.
	if (drawOutlines) {
		// draw a halo around the text by using the "false bold"
		// feature for text rendering. Either black or white is used for
		// the glow (whatever acts as contrast) with a some alpha value,
		BFont font;
		drawView->GetFont(&font);

		rgb_color textColor = drawView->HighColor();
		if (textColor.IsDark()) {
			// dark text on light outline
			rgb_color glowColor = ui_color(B_SHINE_COLOR);

			font.SetFalseBoldWidth(2.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);
			glowColor.alpha = 30;
			drawView->SetHighColor(glowColor);

			drawView->DrawString(fittingText, location);

			font.SetFalseBoldWidth(1.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);
			glowColor.alpha = 65;
			drawView->SetHighColor(glowColor);

			drawView->DrawString(fittingText, location);

			font.SetFalseBoldWidth(0.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);
		} else {
			// light text on dark outline
			rgb_color outlineColor = kBlack;

			font.SetFalseBoldWidth(1.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);
			outlineColor.alpha = 30;
			drawView->SetHighColor(outlineColor);

			drawView->DrawString(fittingText, location);

			font.SetFalseBoldWidth(0.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);

			outlineColor.alpha = 200;
			drawView->SetHighColor(outlineColor);

			drawView->DrawString(fittingText, location + BPoint(1, 1));
		}

		drawView->SetHighColor(textColor);
	}

	// (not applicable: the attribute is none of the type of the file: n/a in the grey of a disabled label. The pen of the view is shared with the cells that are drawn next: it is put back)
	const rgb_color penColor = drawView->HighColor();
	const bool notApplicable = fText->IsNotApplicable();
	if (notApplicable)
		drawView->SetHighColor(disable_color(penColor, drawView->LowColor()));

	drawView->DrawString(fittingText, location);

	if (notApplicable)
		drawView->SetHighColor(penColor);

	if (fSymLink && (fAttrHash == view->FirstColumn()->AttrHash())) {
		// TODO:
		// this should be exported to the WidgetAttribute class, probably
		// by having a per widget kind style
		BRect lineRect(textRect);
		lineRect.OffsetTo(location.x, std::min(location.y + 2, textRect.bottom));
			// underline goes 2px under the baseline
		lineRect.InsetBy(roundf(textRect.Width() - fText->Width(view)), 0);
			// only underline text part
		drawView->StrokeLine(lineRect.LeftTop(), lineRect.RightTop(), B_MIXED_COLORS);
	}

	drawView->ConstrainClippingRegion(NULL);
}
