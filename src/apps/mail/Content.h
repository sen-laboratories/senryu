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
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

BeMail(TM), Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/
#ifndef _CONTENT_H
#define _CONTENT_H

#include <CardLayout.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <Font.h>
#include <MailMessage.h>
#include <View.h>
#include <fs_attr.h>
#include <Point.h>
#include <Rect.h>
#include <MessageFilter.h>
#include "TTextView.h"
#include "THtmlView.h"

#define DEBUG_SPELLCHECK 0
#if DEBUG_SPELLCHECK
#	define DSPELL(x) x
#else
#	define DSPELL(x) ;
#endif

#define MESSAGE_TEXT		"Message:"
#define MESSAGE_TEXT_H		 16
#define MESSAGE_TEXT_V		 5
#define MESSAGE_FIELD_H		 59
#define MESSAGE_FIELD_V		 11

#define CONTENT_TYPE		"content-type: "
#define CONTENT_ENCODING	"content-transfer-encoding: "
#define CONTENT_DISPOSITION	"Content-Disposition: "
#define MIME_TEXT			"text/"
#define MIME_MULTIPART		"multipart/"

class TMailWindow;
class TScrollView;
class TTextView;
class BFile;
class BList;
class BPopUpMenu;

struct text_run_array;

void Unicode2UTF8(int32 c, char **out);
inline bool
IsInitialUTF8Byte(uchar b)
{
	return ((b & 0xC0) != 0x80);
}

const rgb_color kSpellTextColor = {255, 0, 0, 255};
const rgb_color kHeaderColor = {72, 72, 72, 255};

const rgb_color kQuoteColors[] = {
	{0, 0, 0xff, 0},		// 3rd, 6th, ... quote level color (blue)
	{0, 0xff, 0, 0},		// 1st, 4th, ... quote level color (green)
	{0xff, 0, 0, 0}			// 2nd, 5th, ... quote level color (red)
};
const int32 kNumQuoteColors = 3;

const rgb_color kDiffColors[] = {
	{0xb0, 0, 0, 0},		// '-', red
	{0, 0x90, 0, 0},		// '+', green
	{0x6a, 0x6a, 0x6a, 0}	// '@@', dark grey
};

typedef struct {
	bool header;
	bool raw;
	bool quote;
	bool incoming;
	bool close;
	bool mime;
	TTextView *view;
	BEmailMessage *mail;
	BList *enclosures;
	sem_id *stop_sem;
} reader_info;

enum MAIL_VIEW {
	VIEW_TEXT = 0,
	VIEW_HTML
};

enum ENCLOSURE_TYPE {
	TYPE_ENCLOSURE = 100,
	TYPE_BE_ENCLOSURE,
	TYPE_URL,
	TYPE_MAILTO
};

struct hyper_text {
	int32 type;
	char *name;
	char *content_type;
	char *encoding;
	int32 text_start;
	int32 text_end;
	BMailComponent *component;
	bool saved;
	bool have_ref;
	entry_ref ref;
	node_ref node;
};

class TSavePanel;


class TContentView : public BView {
public:
								TContentView(bool incoming, BFont* font,
									bool showHeader, bool coloredQuotes);

			void				FindString(const char *);
			void				Focus(bool);

			void				Clear();
			bool				IsEmpty();
			int32				CountLines();
			void				LoadMessage(BEmailMessage *mail, bool quoteIt = false, const char *text = NULL);
			void				AddAsContent(BEmailMessage *mail, bool wrap, uint32 charset, mail_encoding encoding);
			void				SetText(const BString* body);
			void				SetText(BFile* content, int32 offset = 0, int32 length = -1);
			void				SetTextFrom(TContentView* srcView);
			void				SetReply(const BString* preamble, int32 start, int32 finish, bool coloredQuotes = false);
			void				GetStyledText(char* text, text_run_array* style);
			int32				GetTextLength();
			const char*			GetText();
			void				GetSelection(int32* start, int32* end);
			void				StopLoad();
			void				SetReadingPos(float y);
			float				GetReadingPos();
			float				GetPreferredHeight(BRect outerFrame);
			void				ShowView(MAIL_VIEW view);

	virtual	void				MessageReceived(BMessage* message);
			bool				IsFocus();

private:
			BCardLayout*		fCardLayout;
			TTextView*			fTextView;
			THtmlView*			fHtmlView;
			bool				fFocus;
			bool				fIncoming;
			float				fOffset;
};


//====================================================================

class TSavePanel : public BFilePanel {
	public:
		TSavePanel(hyper_text*, TTextView*);
		virtual void SendMessage(const BMessenger*, BMessage*);
		void SetEnclosure(hyper_text*);

	private:
		hyper_text *fEnclosure;
		TTextView *fView;
};

#endif	/* #ifndef _CONTENT_H */
