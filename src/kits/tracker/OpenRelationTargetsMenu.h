/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once
#include <String.h>

#include <sen/Sen.h>
#include "SlowMenu.h"

class OpenRelationTargetsMenu : public BSlowMenu {
public:
	OpenRelationTargetsMenu(const char* label, const BMessage* entriesToOpen,
							BWindow* parentWindow, const BMessenger &target);
	~OpenRelationTargetsMenu();

private:
	virtual bool StartBuildingItemList();
	virtual bool AddNextItem();
	virtual void DoneBuildingItemList();
	virtual void ClearMenuBuildingState();

	status_t	 AddRelationTargetItems(uint32* targetCount);
	status_t	 AddCompatibleRelationTargetItems(uint32* targetCount);
	status_t	 AddSelfRelationTargetItems(uint32* targetCount);

	BMessage	fEntriesToOpen;
	BMessenger	fMessenger;
	BMessenger* fSenMessenger;
	BWindow*	fParentWindow;

	BMessage*	fRelationTargetsReply;
	BMessage*   fRelationRoot;
	BString 	fDefaultType;

	typedef BSlowMenu _inherited;
};
