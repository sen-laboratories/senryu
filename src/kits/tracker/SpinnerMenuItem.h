/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <MenuItem.h>

/**
 * A menu item with a spinner in front of its label: spokes in a circle, the leading one in the colour of the text and the ones behind it fading, one step at a time. For an item that
 * says that something is being fetched. The size follows the font of the menu, the colours are those of the item (disabled, selected), so it fits any theme.
 *
 * The item does not run a timer: whoever waits calls NextFrame() (the lazy menu that is asked again and again while its items are built does, see BSlowMenu::AddDynamicItem), from the
 * thread that tracks the menu. Nothing moves and nothing is reserved while it is not spinning.
 */
class SpinnerMenuItem : public BMenuItem {
public:
							SpinnerMenuItem(const char* label, BMessage* message = NULL);

	/** the spinner is shown and turns with NextFrame() */
			void			StartSpinning();
	/** the spinner is gone (the space in front of the label stays, so the menu does not change its width) */
			void			StopSpinning();
			bool			IsSpinning() const { return fSpinning; }
	/** one step of the spinner (one eighth of a turn); draws it again */
			void			NextFrame();

protected:
	virtual	void			GetContentSize(float* _width, float* _height);
	virtual	void			DrawContent();

private:
			float			_SpinnerSize() const;
			void			_Invalidate();

	static const int32		kSpokes = 8;

			int32			fStep;
			bool			fSpinning;
};
