/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file SenDatePicker.h
 * @brief The calendar that drops down from the field of a day or a time that is edited in the list of the file manager (header only).
 *
 * A window without a frame of its own, as the calendar of the Deskbar's clock: a month with its days to be picked from, the buttons for the month and the year and, for a time, the time of day
 * (hour and minute, with the box *All day* that leaves it out). It NEVER TAKES THE FOCUS (B_AVOID_FOCUS): the field that is edited keeps the keyboard, and the file manager ends an edit when
 * its window is deactivated. The one who opened it closes it (B_QUIT_REQUESTED) when the edit ends; picking a day ends it, too: the text `YYYY-MM-DD` (or `YYYY-MM-DD hh:mm`) is sent to the
 * target in a message with the command that was given, in the field "text".
 */

#include <Box.h>
#include <Button.h>
#include <CalendarView.h>
#include <CheckBox.h>
#include <DateTime.h>
#include <GroupLayout.h>
#include <Messenger.h>
#include <Screen.h>
#include <SpaceLayoutItem.h>
#include <Spinner.h>
#include <String.h>
#include <StringView.h>
#include <Window.h>

namespace sen {

class DatePickerWindow : public BWindow {
public:
	/**
	 * @param where the left and the bottom of the field, in screen coordinates
	 * @param withTime also the time of day (a day only: false)
	 * @param allDay the box *All day* is checked (the time of day is left out)
	 * @param picked the message that is sent to the target when a day is picked, with the text added in the field "text"
	 * @param allDayLabel the (translated) label of the box *All day*
	 * @param takeFocus false: the window never takes the focus (the field of an edit keeps the keyboard); true: it is a window like another and goes when it is deactivated
	 * @param changed the message that is sent (with the text in the field "text") when the time of day or *All day* is changed, so that the field has it at once: Enter or leaving the
	 * field completes the edit with it (without it only a pick of a day made the text)
	 */
	DatePickerWindow(BPoint where, const BDate& date, int32 hour, int32 minute, bool withTime, bool allDay, const BMessenger& target, const BMessage& picked,
			const char* allDayLabel, bool takeFocus = false, const BMessage* changed = NULL)
		:
		BWindow(BRect(0.0, 0.0, 100.0, 130.0), "", B_NO_BORDER_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL,
			B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS | (takeFocus ? 0 : B_AVOID_FOCUS) | B_NOT_MINIMIZABLE | B_NOT_MOVABLE | B_NOT_RESIZABLE
				| B_NOT_ZOOMABLE),
		fTarget(target),
		fPicked(picked),
		fTakesFocus(takeFocus),
		fChanged(changed != NULL ? *changed : BMessage((uint32)0)),
		fHour(NULL),
		fMinute(NULL),
		fAllDay(NULL)
	{
		fMonth = new BStringView("month", "");
		fYear = new BStringView("year", "");
		fView = new BPrivate::BCalendarView(Bounds(), "calendar", B_FOLLOW_ALL);
		fView->SetSelectionMessage(new BMessage(kDayPicked));
		fView->SetInvocationMessage(new BMessage(kDayPicked));

		BGroupLayout* outer = new BGroupLayout(B_VERTICAL, 0.0);
		outer->SetInsets(0, 0, 0, 0);
		SetLayout(outer);
		BBox* frame = new BBox("frame", B_WILL_DRAW, B_PLAIN_BORDER);
		BGroupLayout* layout = new BGroupLayout(B_VERTICAL, 5.0);
		layout->SetInsets(5.0, 5.0, 5.0, 5.0);
		frame->SetLayout(layout);
		outer->AddView(frame);

		BGroupLayout* bar = new BGroupLayout(B_HORIZONTAL, 5.0);
		layout->AddItem(bar);
		bar->AddView(_Button("-", kMonthDown));
		bar->AddView(fMonth);
		bar->AddView(_Button("+", kMonthUp));
		bar->AddItem(BSpaceLayoutItem::CreateGlue());
		bar->AddView(_Button("-", kYearDown));
		bar->AddView(fYear);
		bar->AddView(_Button("+", kYearUp));
		layout->AddView(fView);

		if (withTime) {
			BGroupLayout* row = new BGroupLayout(B_HORIZONTAL, 5.0);
			layout->AddItem(row);
			fHour = new BSpinner("hour", "", new BMessage(kTimeChanged));
			fHour->SetRange(0, 23);
			fHour->SetValue(hour);
			fMinute = new BSpinner("minute", "", new BMessage(kTimeChanged));
			fMinute->SetRange(0, 59);
			fMinute->SetValue(minute);
			fAllDay = new BCheckBox("allday", allDayLabel, new BMessage(kTimeChanged));
			fAllDay->SetValue(allDay ? B_CONTROL_ON : B_CONTROL_OFF);
			row->AddView(fHour);
			row->AddView(new BStringView("colon", ":"));
			row->AddView(fMinute);
			row->AddItem(BSpaceLayoutItem::CreateGlue());
			row->AddView(fAllDay);
			_UpdateEnabled();
		}

		MoveTo(where);
		_Show(date);
	}

	virtual void Show()
	{
		// (it opens where it is asked to, and does not go off the screen)
		BRect screen(BScreen().Frame());
		BRect frame = Frame();
		const BSize size = GetLayout()->PreferredSize();
		BPoint where = frame.LeftTop();
		if (where.x + size.width > screen.right)
			where.x = screen.right - size.width;
		if (where.y + size.height > screen.bottom)
			where.y = frame.top - size.height - 30;
		MoveTo(where);
		BWindow::Show();
	}

	virtual void WindowActivated(bool active)
	{
		if (!active && fTakesFocus)
			PostMessage(B_QUIT_REQUESTED);
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kDayPicked:
			{
				int32 day, month, year;
				if (message->FindInt32("day", &day) != B_OK || message->FindInt32("month", &month) != B_OK || message->FindInt32("year", &year) != B_OK)
					break;
				const BString text = _Text(year, month, day);
				BMessage picked(fPicked);
				picked.RemoveName("text");
				picked.AddString("text", text.String());
				fTarget.SendMessage(&picked);
				PostMessage(B_QUIT_REQUESTED);
				break;
			}
			case kTimeChanged:
				_UpdateEnabled();
				if (fChanged.what != 0) {
					// (the day that is shown: the one that was picked last, else the one that the field had)
					const BDate date = fView->Date();
					if (date.IsValid()) {
						BMessage changed(fChanged);
						changed.RemoveName("text");
						changed.AddString("text", _Text(date.Year(), date.Month(), date.Day()).String());
						fTarget.SendMessage(&changed);
					}
				}
				break;
			case kMonthDown:
			case kMonthUp:
			{
				BDate date = fView->Date();
				date.AddMonths(message->what == kMonthDown ? -1 : 1);
				_Show(date);
				break;
			}
			case kYearDown:
			case kYearUp:
			{
				BDate date = fView->Date();
				date.AddYears(message->what == kYearDown ? -1 : 1);
				_Show(date);
				break;
			}
			default:
				BWindow::MessageReceived(message);
		}
	}

private:
	enum { kDayPicked = 'dpDy', kMonthDown = 'dpMd', kMonthUp = 'dpMu', kYearDown = 'dpYd', kYearUp = 'dpYu', kTimeChanged = 'dpTc' };

	BButton* _Button(const char* label, uint32 what)
	{
		BButton* button = new BButton(label, label, new BMessage(what));
		float width, height;
		fMonth->GetPreferredSize(&width, &height);
		button->SetExplicitMinSize(BSize(height + 6, height + 2));
		button->SetExplicitMaxSize(BSize(height + 6, height + 2));
		return button;
	}

	/** the text of a day, with the time of day unless *All day* is checked */
	BString _Text(int32 year, int32 month, int32 day) const
	{
		BString text;
		text.SetToFormat("%04d-%02d-%02d", (int)year, (int)month, (int)day);
		if (fHour != NULL && fAllDay->Value() != B_CONTROL_ON)
			text << BString().SetToFormat(" %02d:%02d", (int)fHour->Value(), (int)fMinute->Value());
		return text;
	}

	/** the time of day is greyed with *All day* */
	void _UpdateEnabled()
	{
		if (fAllDay == NULL)
			return;
		const bool enabled = fAllDay->Value() != B_CONTROL_ON;
		fHour->SetEnabled(enabled);
		fMinute->SetEnabled(enabled);
	}

	void _Show(const BDate& date)
	{
		if (!date.IsValid())
			return;
		fView->SetDate(date);
		BString year;
		year << date.Year();
		fYear->SetText(year.String());
		fMonth->SetText(date.LongMonthName(date.Month()).String());
	}

	BMessenger					fTarget;
	BMessage					fPicked;
	bool						fTakesFocus;
	BMessage					fChanged;
	BStringView*				fMonth;
	BStringView*				fYear;
	BPrivate::BCalendarView*	fView;
	BSpinner*					fHour;
	BSpinner*					fMinute;
	BCheckBox*					fAllDay;
};

}	// namespace sen
