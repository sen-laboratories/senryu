/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _OPEN_RELATIONS_WINDOW_H
#define _OPEN_RELATIONS_WINDOW_H

#include <String.h>

#include "ContainerWindow.h"
#include "EntryIterator.h"
#include "NodeWalker.h"
#include "PoseView.h"
#include "Query.h"
#include "SlowMenu.h"
#include "Utilities.h"

class OpenRelationsMenu : public BSlowMenu {
public:
	OpenRelationsMenu(const char* label, const BMessage* entriesToOpen,
		BWindow* parentWindow, const BMessenger& target);

private:
	virtual bool StartBuildingItemList();
	virtual bool AddNextItem();
	virtual void DoneBuildingItemList();
	virtual void ClearMenuBuildingState();

	uint32 AddRelationItems(const BString* source);
	uint32 AddSelfRelationItems(const BString* source);

	BMessage    fEntriesToOpen;
	BMessenger  fTrackerMessenger;
	BWindow*    fParentWindow;

    BMessenger  fSenMessenger;
    BMessage    fRelationsReply;
	uint32		fSenCmd;

	typedef BSlowMenu _inherited;
};

#endif	// _OPEN_RELATIONS_WINDOW_H
