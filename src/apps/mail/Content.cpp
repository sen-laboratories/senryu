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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <Alert.h>
#include <Beep.h>
#include <Clipboard.h>
#include <ControlLook.h>
#include <Debug.h>
#include <E-mail.h>
#include <Input.h>
#include <Locale.h>
#include <Looper.h>
#include <MenuItem.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <Roster.h>
#include <ScrollView.h>
#include <UTF8.h>

#include <MailMessage.h>
#include <MailAttachment.h>

#include "MailApp.h"
#include "MailSupport.h"
#include "Messages.h"
#include "Content.h"

#define B_TRANSLATION_CONTEXT "Mail"

int32
diff_mode(char c)
{
	if (c == '+')
		return 2;
	if (c == '-')
		return 1;
	if (c == '@')
		return 3;
	if (c == ' ')
		return 0;

	// everything else ends the diff mode
	return -1;
}


bool
is_quote_char(char c)
{
	return c == '>' || c == '|';
}

/*!	Fills the specified text_run_array with the correct values for the
	specified text.
	If "view" is NULL, it will assume that "line" lies on a line break,
	if not, it will correctly retrieve the number of quotes the current
	line already has.
*/
void
FillInQuoteTextRuns(BTextView* view, quote_context* context, const char* line,
	int32 length, const BFont& font, text_run_array* style, int32 maxStyles)
{
	text_run* runs = style->runs;
	int32 index = style->count;
	bool begin;
	int32 pos = 0;
	int32 diffMode = 0;
	bool inDiff = false;
	bool wasDiff = false;
	int32 level = 0;

	// get index to the beginning of the current line

	if (context != NULL) {
		level = context->level;
		diffMode = context->diff_mode;
		begin = context->begin;
		inDiff = context->in_diff;
		wasDiff = context->was_diff;
	} else if (view != NULL) {
		int32 start, end;
		view->GetSelection(&end, &end);

		begin = view->TextLength() == 0
			|| view->ByteAt(view->TextLength() - 1) == '\n';

		// the following line works only reliable when text wrapping is set to
		// off; so the complicated version actually used here is necessary:
		// start = view->OffsetAt(view->CurrentLine());

		const char *text = view->Text();

		if (!begin) {
			// if the text is not the start of a new line, go back
			// to the first character in the current line
			for (start = end; start > 0; start--) {
				if (text[start - 1] == '\n')
					break;
			}
		}

		// get number of nested qoutes for current line

		if (!begin && start < end) {
			begin = true;
				// if there was no text in this line, there may come
				// more nested quotes

			diffMode = diff_mode(text[start]);
			if (diffMode == 0) {
				for (int32 i = start; i < end; i++) {
					if (is_quote_char(text[i]))
						level++;
					else if (text[i] != ' ' && text[i] != '\t') {
						begin = false;
						break;
					}
				}
			} else
				inDiff = true;

			if (begin) {
				// skip leading spaces (tabs & newlines aren't allowed here)
				while (line[pos] == ' ')
					pos++;
			}
		}
	} else
		begin = true;

	// set styles for all qoute levels in the text to be inserted

	for (int32 pos = 0; pos < length;) {
		int32 next;
		if (begin && is_quote_char(line[pos])) {
			begin = false;

			while (pos < length && line[pos] != '\n') {
				// insert style for each quote level
				level++;

				bool search = true;
				for (next = pos + 1; next < length; next++) {
					if ((search && is_quote_char(line[next]))
						|| line[next] == '\n')
						break;
					else if (search && line[next] != ' ' && line[next] != '\t')
						search = false;
				}

				runs[index].offset = pos;
				runs[index].font = font;
				runs[index].color = level > 0 ? mix_color(ui_color(B_PANEL_TEXT_COLOR),
					kQuoteColors[level % kNumQuoteColors], 120) : ui_color(B_PANEL_TEXT_COLOR);

				pos = next;
				if (++index >= maxStyles)
					break;
			}
		} else {
			if (begin) {
				if (!inDiff) {
					inDiff = !strncmp(&line[pos], "--- ", 4);
					wasDiff = false;
				}
				if (inDiff) {
					diffMode = diff_mode(line[pos]);
					if (diffMode < 0) {
						inDiff = false;
						wasDiff = true;
					}
				}
			}

			runs[index].offset = pos;
			runs[index].font = font;
			if (wasDiff)
				runs[index].color = kDiffColors[diff_mode('@') - 1];
			else if (diffMode <= 0) {
				runs[index].color = level > 0 ? mix_color(ui_color(B_PANEL_TEXT_COLOR),
					kQuoteColors[level % kNumQuoteColors], 120) : ui_color(B_PANEL_TEXT_COLOR);
			} else
				runs[index].color = kDiffColors[diffMode - 1];

			begin = false;

			for (next = pos; next < length; next++) {
				if (line[next] == '\n') {
					begin = true;
					wasDiff = false;
					break;
				}
			}

			pos = next;
			index++;
		}

		if (pos < length)
			begin = line[pos] == '\n';

		if (begin) {
			pos++;
			level = 0;
			wasDiff = false;

			// skip one leading space (tabs & newlines aren't allowed here)
			if (!inDiff && pos < length && line[pos] == ' ')
				pos++;
		}

		if (index >= maxStyles)
			break;
	}
	style->count = index;

	if (context) {
		// update context for next run
		context->level = level;
		context->diff_mode = diffMode;
		context->begin = begin;
		context->in_diff = inDiff;
		context->was_diff = wasDiff;
	}
}


//	#pragma mark -


TextRunArray::TextRunArray(size_t entries)
	:
	fNumEntries(entries)
{
	fArray = (text_run_array *)malloc(sizeof(int32) + sizeof(text_run) * entries);
	if (fArray != NULL)
		fArray->count = 0;
}


TextRunArray::~TextRunArray()
{
	free(fArray);
}


//	#pragma mark -


TContentView::TContentView(bool incoming, BFont* font,
	bool showHeader, bool coloredQuotes)
	:
	BView("m_content", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fFocus(false),
	fIncoming(incoming)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// we use a stacked card view for TEXT/HTML view to make switching simple,
	// but only set up the HTML view if needed, as it consumes more resources.
	fCardLayout = new BCardLayout();
	SetLayout(fCardLayout);

	// set up Text view
	fTextView = new TTextView(fIncoming, this, font, showHeader,
		coloredQuotes);

	BScrollView* scrollViewText = new BScrollView("textMailScrollView", fTextView, 0, true, true);
	scrollViewText->SetBorders(BControlLook::B_TOP_BORDER);
	AddChild(scrollViewText);

	// set up HTML view if enabled
	// TODO: only add if html is enabled
	// TODO: set externalRefs to app setting
	fHtmlView = new THtmlView(this, true);
	BScrollView* scrollViewHtml = new BScrollView("htmlMailScrollView", fHtmlView, 0, true, true);
	scrollViewHtml->SetBorders(BControlLook::B_TOP_BORDER);
	AddChild(scrollViewHtml);

	fCardLayout->SetVisibleItem(VIEW_HTML);	// TODO: set to configured default view
}

void TContentView::ShowView(MAIL_VIEW view)
{
	fCardLayout->SetVisibleItem(view);
}


void TContentView::Clear()
{
	fTextView->Clear();
	fHtmlView->Clear();
}

void TContentView::LoadMessage(BEmailMessage *mail, bool quoteIt, const char *text)
{
	fTextView->LoadMessage(mail, quoteIt, text);
	if (fHtmlView) {
		bool htmlPartFound = false;
		// check for HTML mail attachment
		for (int comp = 0; comp < mail->CountComponents(); comp++) {
			auto component = mail->GetComponent(comp);
			BMimeType mime;
			status_t mimeCheck = component->MIMEType(&mime);
			if (mimeCheck == B_OK) {
				if (mime == BMimeType("text/html")) {
					// read and render in separate View thread
					printf("Content::found HTML mail...\n");
					htmlPartFound = true;
					fHtmlView->LoadMessage(component);
					break;
				} else {
					printf("skipping mail component of MIME type %s\n", mime.Type());
				}
			}
		}
		if (!htmlPartFound) {
			printf("no HTML part found, falling back to text\n");
			fCardLayout->SetVisibleItem(VIEW_TEXT);
		} else {
			printf("HTML part found and rendered, switching to HTML view.\n");
			fCardLayout->SetVisibleItem(VIEW_HTML);
			fHtmlView->Invalidate();
		}
	}
}


// TODO: when is this called? how can we support HTML mail here?
void TContentView::SetText(const BString* body)
{
	fTextView->SetText(body->String());
	if (fHtmlView) {
		fHtmlView->Clear();
		fHtmlView->SetText(body);
	}
}


void TContentView::SetText(BFile* content, int32 offset, int32 length)
{
	if (length == -1) {
		off_t size;
		content->GetSize(&size);
		length = size;
	}

	fTextView->SetText(content, offset, length);
	if (fHtmlView) {
		fHtmlView->Clear();
		fHtmlView->SetText(content, offset, length);
	}
}


void TContentView::SetTextFrom(TContentView* srcView)
{
	int32 textLen = srcView->GetTextLength();
	char text[textLen];
	text_run_array style;
	srcView->GetStyledText(text, &style);

	fTextView->SetText(text, textLen, &style);
//	free(style);	// haha freestyle :-P
}


void TContentView::SetReply(const BString* preamble, int32 start, int32 finish, bool coloredQuotes)
{
	char* text = (char*)malloc(finish - start + 1);
	if (text == NULL)
		return;

	fTextView->GetText(start, finish - start, text);
	if (text[strlen(text) - 1] != '\n') {
		text[strlen(text)] = '\n';
		finish++;
	}
	fTextView->SetText(text, finish - start);
	free(text);

	finish = fTextView->CountLines();
	for (int32 loop = 0; loop < finish; loop++) {
		fTextView->GoToLine(loop);
		fTextView->Insert((const char*)QUOTE);
	}

	if (coloredQuotes) {
		const BFont* font = fTextView->Font();
		int32 length = fTextView->TextLength();

		TextRunArray style(length / 8 + 8);

		FillInQuoteTextRuns(fTextView, NULL,
			fTextView->Text(), length, font, &style.Array(),
			style.MaxEntries());

		fTextView->SetRunArray(0, length, &style.Array());
	}

	fTextView->GoToLine(0);
	if (preamble->Length() > 0)
		fTextView->Insert(preamble->String());
}


void TContentView::AddAsContent(BEmailMessage *mail, bool wrap, uint32 charset, mail_encoding encoding)
{
	if (fCardLayout->VisibleIndex() == VIEW_TEXT) {
		fTextView->AddAsContent(mail, wrap, charset, encoding);
	} else  {
		// TODO: not yet supported
	}
}


const char* TContentView::GetText()
{
	if (fCardLayout->VisibleIndex() == VIEW_TEXT) {
		return fTextView->Text();
	} else  {
		// TODO: not yet supported
		return "";
	}
}


void TContentView::GetStyledText(char* text, text_run_array* style)
{
	int32 textLen = fTextView->TextLength();
	fTextView->GetText(0, textLen, text);
	style = fTextView->RunArray(0, textLen);
}


void TContentView::GetSelection(int32* start, int32* end)
{
	if (fCardLayout->VisibleIndex() == VIEW_TEXT) {
		fTextView->GetSelection(start, end);
	} else  {
		// TODO: not yet supported
		*start = 0;
		*end = 0;
	}
}


int32 TContentView::GetTextLength()
{
	return fTextView->TextLength();
}


void TContentView::StopLoad()
{
	fTextView->StopLoad();
	// n/a for htmlView (yet)
}

void TContentView::SetReadingPos(float y)
{
	//TODO: move to TextView alltogether
	BScrollBar* scroll = fTextView->ScrollBar(B_VERTICAL);
	if (scroll == NULL)
		return;

	fTextView->LockLooper();
		scroll->SetValue(y);
	fTextView->UnlockLooper();
}

float TContentView::GetReadingPos()
{
	//TODO: move to TextView alltogether
	BScrollBar* scroll = fTextView->ScrollBar(B_VERTICAL);
	if (scroll == NULL) {
		return 0.0;
	} else {
		return scroll->Value();
	}
}

bool TContentView::IsFocus()
{
	return fTextView->IsFocus();
}


int32 TContentView::CountLines()
{
	return fTextView->CountLines();
}


bool TContentView::IsEmpty()
{
	return fTextView->IsEmpty();
}


void
TContentView::FindString(const char *str)
{
	int32	finish;
	int32	pass = 0;
	int32	start = 0;

	if (str == NULL)
		return;

	//
	//	Start from current selection or from the beginning of the pool
	//
	const char *text = fTextView->Text();
	int32 count = fTextView->TextLength();
	fTextView->GetSelection(&start, &finish);
	if (start != finish)
		start = finish;
	if (!count || text == NULL)
		return;

	//
	//	Do the find
	//
	while (pass < 2) {
		long found = -1;
		char lc = tolower(str[0]);
		char uc = toupper(str[0]);
		for (long i = start; i < count; i++) {
			if (text[i] == lc || text[i] == uc) {
				const char *s = str;
				const char *t = text + i;
				while (*s && (tolower(*s) == tolower(*t))) {
					s++;
					t++;
				}
				if (*s == 0) {
					found = i;
					break;
				}
			}
		}

		//
		//	Select the text if it worked
		//
		if (found != -1) {
			Window()->Activate();
			fTextView->Select(found, found + strlen(str));
			fTextView->ScrollToSelection();
			fTextView->MakeFocus(true);
			return;
		}
		else if (start) {
			start = 0;
			text = fTextView->Text();
			count = fTextView->TextLength();
			pass++;
		} else {
			beep();
			return;
		}
	}
}


void
TContentView::Focus(bool focus)
{
	if (fFocus != focus) {
		fFocus = focus;
		Draw(Frame());
	}
}


void
TContentView::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case M_VIEW_TEXT:
		{
			fCardLayout->SetVisibleItem(VIEW_TEXT);
			break;
		}
		case M_VIEW_HTML:
		{
			fCardLayout->SetVisibleItem(VIEW_HTML);
			break;
		}
		case M_HEADER:
		{
			be_app_messenger.SendMessage(msg, fTextView);
			break;
		}
		case M_RAW:
		{
			be_app_messenger.SendMessage(msg, fTextView);
			break;
		}
		case CHANGE_FONT:
		{
			BFont *font;
			msg->FindPointer("font", (void **)&font);
			fTextView->UpdateFont(font);
			fTextView->Invalidate(Bounds());
			break;
		}
		case M_ADD_QUOTE_LEVEL:
		{
			int32 start, finish;
			fTextView->GetSelection(&start, &finish);
			fTextView->AddQuote(start, finish);
			break;
		}
		case M_SUB_QUOTE_LEVEL:
		{
			int32 start, finish;
			fTextView->GetSelection(&start, &finish);
			fTextView->RemoveQuote(start, finish);
			break;
		}
		case M_SIGNATURE:
		{
			if (fTextView->IsReaderThreadRunning()) {
				// Do not add the signature until the reader thread
				// is finished. Resubmit the message for later processing
				Window()->PostMessage(msg);
				break;
			}

			entry_ref ref;
			msg->FindRef("ref", &ref);

			BFile file(&ref, B_READ_ONLY);
			if (file.InitCheck() == B_OK) {
				int32 start, finish;
				fTextView->GetSelection(&start, &finish);

				off_t size;
				file.GetSize(&size);
				if (size > 32768)	// safety against corrupt signatures
					break;

				char *signature = (char *)malloc(size);
				if (signature == NULL)
					break;
				ssize_t bytesRead = file.Read(signature, size);
				if (bytesRead < B_OK) {
					free (signature);
					break;
				}

				const char *text = fTextView->Text();
				int32 length = fTextView->TextLength();

				// reserve some empty lines before the signature
				const char* newLines = "\n\n\n\n";
				if (length && text[length - 1] == '\n')
					newLines++;

				fTextView->Select(length, length);
				fTextView->Insert(newLines, strlen(newLines));
				length += strlen(newLines);

				// append the signature
				fTextView->Select(length, length);
				fTextView->Insert(signature, bytesRead);
				fTextView->Select(length, length + bytesRead);
				fTextView->ScrollToSelection();

				// set the editing cursor position
				fTextView->Select(length - 2 , length - 2);
				fTextView->ScrollToSelection();
				free (signature);
			} else {
				beep();
				BAlert* alert = new BAlert("",
					B_TRANSLATE("An error occurred trying to open this "
						"signature."), B_TRANSLATE("Sorry"));
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
			}
			break;
		}
		case M_CHECK_SPELLING:
		{
			bool enable = msg->GetBool("enable", false);
			fTextView->EnableSpellCheck(enable);
			break;
		}
		case M_FIND:
			FindString(msg->FindString("findthis"));
			break;

		default:
			BView::MessageReceived(msg);
	}
}


float TContentView::GetPreferredHeight(BRect outerFrame)
{
	return max_c(fTextView->CountLines(), 20)
		* fTextView->LineHeight(0)
		+ (outerFrame.Height()
		- fTextView->Bounds().Height());
}

//====================================================================
//	#pragma mark -


TSavePanel::TSavePanel(hyper_text *enclosure, TTextView *view)
	: BFilePanel(B_SAVE_PANEL)
{
	fEnclosure = enclosure;
	fView = view;
	if (enclosure->name)
		SetSaveText(enclosure->name);
}


void
TSavePanel::SendMessage(const BMessenger * /* messenger */, BMessage *msg)
{
	const char	*name = NULL;
	BMessage	save(M_SAVE);
	entry_ref	ref;

	if ((!msg->FindRef("directory", &ref)) && (!msg->FindString("name", &name))) {
		save.AddPointer("enclosure", fEnclosure);
		save.AddString("name", name);
		save.AddRef("directory", &ref);
		fView->Window()->PostMessage(&save, fView);
	}
}


void
TSavePanel::SetEnclosure(hyper_text *enclosure)
{
	fEnclosure = enclosure;
	if (enclosure->name)
		SetSaveText(enclosure->name);
	else
		SetSaveText("");

	if (!IsShowing())
		Show();
	Window()->Activate();
}
