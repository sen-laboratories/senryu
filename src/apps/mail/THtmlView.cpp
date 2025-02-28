/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2001, Be Incorporated. All rights reserved.

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
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

BeMail(TM), Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or
registered trademarks of Be Incorporated in the United States and other
countries. Other brand product names are registered trademarks or trademarks
of their respective holders. All rights reserved.
*/

#include <Alert.h>
#include <Application.h>
#include <Beep.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <E-mail.h>
#include <File.h>
#include <Locale.h>
#include <MenuItem.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>
#include <Roster.h>

#include <iostream>

#include "Content.h"
#include "../../kits/tracker/MimeTypes.h"
#include "Messages.h"
#include "THtmlView.h"

#define B_TRANSLATION_CONTEXT "Mail"

THtmlView::THtmlView(TContentView *view, bool allowExternalRefs)
	:
	LiteHtmlView(view->Bounds(), "mail_message_htmlview"),
	fReady(false),
	fLastPosition(-1),
	fMail(NULL),
	fParent(view),
	fRaw(false),
	fAllowExternalRefs(allowExternalRefs)
{
	scrollViewHtml = new BScrollView("htmlMailScrollView", this, 0, true, true);
	//scrollViewHtml->SetBorders(BControlLook::B_TOP_BORDER);
	fParent->AddChild(scrollViewHtml);

	BSize min = scrollViewHtml->MinSize();
	BSize max = scrollViewHtml->MaxSize();
	scrollViewHtml->SetExplicitMinSize(min);
	scrollViewHtml->SetExplicitMaxSize(max);

	// Hyperlink pop up menu
	fLinkMenu = new BPopUpMenu("Link", false, false);
	fLinkMenu->SetFont(be_plain_font);
	fLinkMenu->AddItem(new BMenuItem(B_TRANSLATE("Open this link"), new BMessage(M_OPEN)));
	fLinkMenu->AddItem(new BMenuItem(B_TRANSLATE("Copy link location"), new BMessage(M_COPY)));
	fLinkMenu->SetTargetForItems(this);

	fScrollBarHorizontal = scrollViewHtml->ScrollBar(B_HORIZONTAL);
    fScrollBarVertical   = scrollViewHtml->ScrollBar(B_VERTICAL);

    StartWatchingAll(this);
}


THtmlView::~THtmlView()
{
	StopWatchingAll(this);
	Clear();
	delete fLinkMenu;
}


void
THtmlView::AttachedToWindow()
{
	BView::AttachedToWindow();

	if (fMail != NULL) {
		// todo: needed? LoadMessage(fMail);
		printf("THtmlView::AttachedToWindow() - LoadMessage not implemented here.\n");
	}
	ResizeToPreferred();
}

void
THtmlView::FrameResized(float newWidth, float newHeight)
{
	UpdateScrollBars();
}

void
THtmlView::UpdateScrollBars()
{
    BSize viewSize(scrollViewHtml->Bounds().Size());
    float width, height;
    GetPreferredSize(&width, &height);

	printf("UpdateScrollBars: viewSize = %f x %f, htmlSize = %f x %f\n",
		   viewSize.Width(), viewSize.Height(), width, height);

    fScrollBarHorizontal->SetRange(0,  width);
    fScrollBarHorizontal->SetSteps(10, width / 10);
    fScrollBarVertical->SetRange(0,  height);
    fScrollBarVertical->SetSteps(10, height / 10);
}

void
THtmlView::KeyDown(const char *key, int32 count)
{
	//uint32 mods = modifiers();
	BSize targetSize;
	GetPreferredSize(&targetSize.width, &targetSize.height);
	BRect clientRect = GetClientRect();
	BRect viewRect = Bounds();
	float maxScrollVertical = targetSize.Height() - viewRect.Height();

	switch (key[0]) {
		case B_HOME:
			ScrollTo(0, 0);
			break;

		case B_END:
			ScrollTo(0, maxScrollVertical);
			break;

		case B_PAGE_UP: {
			float y = clientRect.top - viewRect.Height();
			if (y < 0) {
				y = 0;
			}
			ScrollTo(0, y);
			break;
		}
		case B_PAGE_DOWN: {
			float y = clientRect.top + viewRect.Height();
			if (y > maxScrollVertical) {
				y = maxScrollVertical;
			}
			ScrollTo(0, y);
			break;
		}
		default:
			BView::KeyDown(key, count);
	}
}

void
THtmlView::MakeFocus(bool focus)
{
	BView::MakeFocus(focus);
	fParent->Focus(focus);
}

void
THtmlView::MessageReceived(BMessage *msg)
{
	uint32 originalWhat;

	switch (msg->what) {
		case M_OPEN:
		{
			BMessage params;
			status_t status = msg->FindMessage("params", &params);
			if (status == B_OK) {
				OpenUrl(&params);
			} else {
				std::cout << "could not find expected message 'params' for opening link!" << std::endl;
			}
			break;
		}
		case M_COPY:
		{
			BMessage params;
			status_t status = msg->FindMessage("params", &params);

			if (status == B_OK) {
				BString href = msg->GetString("href");
				std::cout << "copy URL to clipboard: " << href << std::endl;

				if (href && be_clipboard->Lock()) {
					be_clipboard->Clear();

					BMessage *clip = be_clipboard->Data();
					clip->AddData(kPlainTextMimeType, B_MIME_TYPE, href.String(), href.Length());
					// TODO: how to add as Bookmark? - clip->AddData(B_BOOKMARK_MIMETYPE, B_MIME_TYPE, ??, ??);

					status = be_clipboard->Commit();
					if (status != B_OK) {
						fprintf(stderr, "could not commit data to clipboard.\n");
					}
					be_clipboard->Unlock();
				} else {
					fprintf(stderr, "could not copy to clipboard.\n");
				}
			} else {
				std::cout << "could not find expected message 'params' for copying link!" << std::endl;
			}
			break;
		}
		case B_OBSERVER_NOTICE_CHANGE:
		{
			if (B_OK == msg->FindUInt32(B_OBSERVE_ORIGINAL_WHAT, &originalWhat))
			{
				switch (originalWhat)
				{
					case M_HTML_RENDERED:
					{
						std::cout << "HTML_RENDERED received" << std::endl;
                        UpdateScrollBars();
						Invalidate();
						break;
					}
                    case M_ANCHOR_CLICKED:
                    {
						msg->PrintToStream();	// TEST get title

						uint32 buttons = msg->GetUInt32("buttons", B_PRIMARY_MOUSE_BUTTON);
						if (buttons & B_SECONDARY_MOUSE_BUTTON) {
							// adjust target messages with correct parameters from selected anchor
							for (int i = 0; i < fLinkMenu->CountItems(); i++) {
								BMessage* msg = fLinkMenu->ItemAt(i)->Message();
								// replace contained params with selected link params
								msg->ReplaceMessage("params", msg);
							}
							BPoint where = msg->GetPoint("where", Bounds().LeftTop());
							ConvertToScreen(&where);
							fLinkMenu->Go(where, true, false, true);
						} else {
							OpenUrl(msg);
						}
						break;
                    }
					default:
					{
						BView::MessageReceived(msg);
					}
				}
			}
			break;
		}
	}
}

void
THtmlView::OpenUrl(BMessage* urlMsg)
{
	const char *href = urlMsg->FindString("href");

	// local anchor?
	BPoint anchorPos;
	status_t result = urlMsg->FindPoint("fragmentPos", &anchorPos);

	if (result == B_OK) {
		// yes, scroll to position
		std::cout << "   scrolling to anchor with y pos = " << anchorPos.y << std::endl;
		ScrollTo(0, anchorPos.y);
	} else {
		// no, open in external viewer (usually the default browser, at least for HTTP(S) links)
		BUrl url(href);
		const char* signature = url.PreferredApplication();
		std::cout << "   opening external link with application " << signature << std::endl;

		BRoster launchRoster;
		const char* args[1];
		args[0] = href;

		launchRoster.Launch(signature, 1, args);
	}
}

void
THtmlView::MouseDown(BPoint where)
{
	MouseDown(where);
}


void
THtmlView::MouseMoved(BPoint where, uint32 code, const BMessage *msg)
{
	MouseMoved(where, code, msg);
}

void
THtmlView::Clear()
{
	// NOOP for now, will be reused for next Load()
}

// TODO: probably not needed at all
bool
THtmlView::IsEmpty()
{
	return (!fReady);
}

void
THtmlView::LoadMessage(const BString* htmlText)
{
	RenderHtml(*htmlText);
}

void
THtmlView::SetText(const BString* text)
{
	// TODO: handle in a better way
	Clear();
	RenderHtml(*text);
}

void
THtmlView::SetText(BFile* file, int32 offset, int32 length)
{
	// TODO: handle in a better way
	Clear();
	char buffer[length];
	file->Read(buffer, length);
	RenderHtml(BString(buffer));
}

void
THtmlView::WindowActivated(bool flag)
{
	BView::WindowActivated(flag);
}

