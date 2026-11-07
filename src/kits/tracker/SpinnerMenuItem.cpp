/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SpinnerMenuItem.h"

#include <Menu.h>

#include <math.h>

namespace {

/** the space between the spinner and the label, in pixels */
const float kGap = 6;

}	// namespace


SpinnerMenuItem::SpinnerMenuItem(const char* label, BMessage* message)
	:
	BMenuItem(label, message),
	fStep(0),
	fSpinning(false)
{
}


void
SpinnerMenuItem::StartSpinning()
{
	fSpinning = true;
	_Invalidate();
}


void
SpinnerMenuItem::StopSpinning()
{
	fSpinning = false;
	fStep = 0;
	_Invalidate();
}


void
SpinnerMenuItem::NextFrame()
{
	if (!fSpinning)
		return;
	fStep = (fStep + 1) % kSpokes;
	_Invalidate();
}


float
SpinnerMenuItem::_SpinnerSize() const
{
	// as high as the text
	font_height height;
	if (Menu() == NULL)
		return 14;
	Menu()->GetFontHeight(&height);
	return ceilf(height.ascent + height.descent);
}


void
SpinnerMenuItem::GetContentSize(float* _width, float* _height)
{
	BMenuItem::GetContentSize(_width, _height);
	if (_width != NULL)
		*_width += _SpinnerSize() + kGap;
}


void
SpinnerMenuItem::DrawContent()
{
	BMenu* menu = Menu();
	if (menu == NULL)
		return;

	const float size = _SpinnerSize();
	// the pen is at the top left of the content: the spinner is drawn there and the label after it
	const BPoint origin = menu->PenLocation();
	const rgb_color base = menu->HighColor();		// (the colour of the label of this item: the theme, disabled, selected)

	menu->MovePenTo(origin.x + size + kGap, origin.y);
	BMenuItem::DrawContent();
	menu->MovePenTo(origin);

	if (!fSpinning)
		return;

	const float radius = size * 0.45f;
	const float inner = radius * 0.45f;
	const BPoint centre(origin.x + size / 2, origin.y + size / 2);

	menu->PushState();
	menu->SetDrawingMode(B_OP_ALPHA);
	menu->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	menu->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
	menu->SetPenSize(max_c(1.5f, size / 9));

	for (int32 spoke = 0; spoke < kSpokes; spoke++) {
		const float angle = (float)spoke * (2 * M_PI / kSpokes) - M_PI / 2;		// (the first spoke points up, they go clockwise)
		// how far behind the leading spoke this one is: the trail fades
		const int32 behind = (fStep - spoke + kSpokes) % kSpokes;
		rgb_color colour = base;
		colour.alpha = (uint8)(255 - behind * (215 / (kSpokes - 1)));
		menu->SetHighColor(colour);

		const float cosine = cosf(angle), sine = sinf(angle);
		menu->StrokeLine(BPoint(centre.x + inner * cosine, centre.y + inner * sine), BPoint(centre.x + radius * cosine, centre.y + radius * sine));
	}

	menu->PopState();
}


void
SpinnerMenuItem::_Invalidate()
{
	BMenu* menu = Menu();
	if (menu != NULL && menu->LockLooper()) {
		menu->Invalidate(Frame());
		menu->UnlockLooper();
	}
}
