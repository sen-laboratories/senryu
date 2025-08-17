/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _OPEN_RELATION_TARGETS_WINDOW_H
#define _OPEN_RELATION_TARGETS_WINDOW_H

#include <String.h>

#include "Sen.h"
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
	status_t	 GetItemMessageInfo(const BMessage* itemMsg, BMessage* childMsg, BMessage* properties, int32 index = 0);

	BMessage	fEntriesToOpen;
	BMessenger	fMessenger;
	BMessenger* fSenMessenger;
	BWindow*	fParentWindow;

	BMessage*	fRelationTargetsReply;
	BMessage*   frelationRoot;
	BString 	fDefaultType;

	typedef BSlowMenu _inherited;
};

#endif	// _OPEN_RELATION_TARGETS_WINDOW_H
