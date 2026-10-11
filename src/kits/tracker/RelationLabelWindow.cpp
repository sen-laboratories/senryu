/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "RelationLabelWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <OS.h>
#include <TextControl.h>
#include <Window.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "RelationLabelWindow"


namespace {

const uint32 kConfirm = 'rlOK';


class RelationLabelWindow : public BWindow {
public:
	RelationLabelWindow(const char* title, const char* label)
		:
		BWindow(BRect(0, 0, 10, 10), title, B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
		fDone(create_sem(0, "relation label done")),
		fConfirmed(false),
		fFinished(false)
	{
		fText = new BTextControl("label", B_TRANSLATE("Label:"), label, NULL);
		fText->SetExplicitMinSize(BSize(300, B_SIZE_UNSET));
		BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"), new BMessage(B_QUIT_REQUESTED));
		BButton* ok = new BButton("ok", B_TRANSLATE("OK"), new BMessage(kConfirm));
		ok->MakeDefault(true);

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(fText)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(cancel)
				.Add(ok)
			.End()
		.End();
		CenterOnScreen();
	}

	~RelationLabelWindow()
	{
		delete_sem(fDone);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kConfirm) {
			fConfirmed = true;
			_Finish();
		} else
			BWindow::MessageReceived(message);
	}

	virtual bool QuitRequested()
	{
		// (Cancel and Esc: the window is closed by Go(), once the caller has the answer)
		_Finish();
		return false;
	}

	bool Go(BString* label)
	{
		Show();
		// (the window runs now: its views are only touched with the window locked)
		if (Lock()) {
			fText->MakeFocus(true);
			fText->TextView()->SelectAll();
			Unlock();
		}

		while (acquire_sem(fDone) == B_INTERRUPTED) {
		}

		if (Lock()) {
			if (fConfirmed)
				label->SetTo(fText->Text());
			Quit();
		}
		return fConfirmed;
	}

private:
	void _Finish()
	{
		if (!fFinished) {
			fFinished = true;
			release_sem(fDone);
		}
	}

	sem_id			fDone;
	BTextControl*	fText;
	bool			fConfirmed;
	bool			fFinished;
};

}	// namespace


bool
PromptForRelationLabel(const char* relationName, BString* label)
{
	BString title(B_TRANSLATE("Label of the relation"));
	if (relationName != NULL && relationName[0] != '\0')
		title << ": " << relationName;

	RelationLabelWindow* window = new RelationLabelWindow(title.String(), label->String());
	return window->Go(label);
}
