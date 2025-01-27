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
#include <E-mail.h>
#include <File.h>
#include <Locale.h>
#include <MenuItem.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>
#include <Roster.h>
#include <iostream>

#include "Content.h"
#include "Messages.h"
#include "THtmlView.h"

#define B_TRANSLATION_CONTEXT "Mail"

THtmlView::THtmlView(TContentView *view, bool allowExternalRefs)
	:
	BView("mail_message_htmlview", B_WILL_DRAW | B_NAVIGABLE),

	fReady(false),
	fLastPosition(-1),
	fMail(NULL),
	fParent(view),
	fRaw(false),
	fAllowExternalRefs(false)
{
	// Hyperlink pop up menu
	fLinkMenu = new BPopUpMenu("Link", false, false);
	fLinkMenu->SetFont(be_plain_font);
	fLinkMenu->AddItem(new BMenuItem(B_TRANSLATE("Open this link"),
		new BMessage(M_OPEN)));
	fLinkMenu->AddItem(new BMenuItem(B_TRANSLATE("Copy link location"),
		new BMessage(M_COPY)));

	fHtmlView = new LiteHtmlView(Bounds(), "liteHtmlView");
	fHtmlView->StartWatching(this, M_HTML_RENDERED);
}


THtmlView::~THtmlView()
{
	delete fHtmlView;
	delete fLinkMenu;
}


void
THtmlView::AttachedToWindow()
{
	BView::AttachedToWindow();

	if (fMail != NULL) {
		LoadMessage(fMail);
	}
}


void
THtmlView::KeyDown(const char *key, int32 count)
{
	//uint32 mods = modifiers();
	BSize targetSize;
	fHtmlView->GetPreferredSize(&targetSize.width, &targetSize.height);
	BRect clientRect = fHtmlView->GetClientRect();
	BRect viewRect = Bounds();
	float maxScrollVertical = targetSize.Height() - viewRect.Height();

	switch (key[0]) {
		case B_HOME:
			fHtmlView->ScrollTo(0, 0);
			break;

		case B_END:
			fHtmlView->ScrollTo(0, maxScrollVertical);
			break;

		case B_PAGE_UP: {
			float y = clientRect.top - viewRect.Height();
			if (y < 0) {
				y = 0;
			}
			fHtmlView->ScrollTo(0, y);
			break;
		}
		case B_PAGE_DOWN: {
			float y = clientRect.top + viewRect.Height();
			if (y > maxScrollVertical) {
				y = maxScrollVertical;
			}
			fHtmlView->ScrollTo(0, y);
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
		case B_OBSERVER_NOTICE_CHANGE:
		{
			if (B_OK == msg->FindUInt32(B_OBSERVE_ORIGINAL_WHAT, &originalWhat))
			{
				switch (originalWhat)
				{
					case M_HTML_RENDERED:
					{
						std::cout << "HTML_RENDERED received" << std::endl;
                        //UpdateScrollBars();
					}
                    case M_ANCHOR_CLICKED:
                    {
                        const char *href = msg->FindString("href");
                        std::cout << "Anchor clicked received: " << href << std::endl;
                        // local anchor?
                        BPoint anchorPos;
                        status_t result = msg->FindPoint("fragmentPos", &anchorPos);
                        if (result == B_OK) {
                            // yes, scroll to position
                            std::cout << "   scrolling to anchor with y pos = " << anchorPos.y << std::endl;
                            fHtmlView->ScrollTo(0, anchorPos.y);
                        } else {
                           // no, open in external viewer (usually the default browser, at least for HTTP(S) links)
                            BUrl url(href);
                            const char* signature = url.PreferredApplication();
                            std::cout << "   opening external link with application " << signature << std::endl;
                            BRoster launchRoster;
                            launchRoster.Launch(signature);
                        }
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
THtmlView::MouseDown(BPoint where)
{
//	fHtmlView->MouseDown(where);
}


void
THtmlView::MouseMoved(BPoint where, uint32 code, const BMessage *msg)
{
//	fHtmlView->MouseMoved(where, code, msg);
}

void
THtmlView::Clear()
{
	// NOOP for now, will be reused for next Load()
	delete fMail;
}

// TODO: probably not needed at all
bool
THtmlView::IsEmpty()
{
	return (!fReady);
}


void
THtmlView::LoadMessage(BMailComponent* mailComponent)
{
	BMallocIO buffer;
	buffer.SetBlockSize(4096);
	mailComponent->GetDecodedData(&buffer);

	off_t size;
	buffer.GetSize(&size);
	char text[size];
	buffer.Read(&text, size);

	fHtmlView->RenderHtml(BString(text));
}


void
THtmlView::SetText(const BString* text)
{
	// TODO: handle in a better way
	Clear();
	fHtmlView->RenderHtml(*text);
}


void
THtmlView::SetText(BFile* file, int32 offset, int32 length)
{
	// TODO: handle in a better way
	Clear();
	char buffer[length];
	file->Read(buffer, length);
	fHtmlView->RenderHtml(BString(buffer));
}


void
THtmlView::WindowActivated(bool flag)
{
	if (!flag) {
		// TODO: check if redraw needed
	}
	BView::WindowActivated(flag);
}

