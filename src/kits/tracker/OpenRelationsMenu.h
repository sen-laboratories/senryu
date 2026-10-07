/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once
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

	uint32 AddRelationItems(const entry_ref* sourceRef);
	uint32 AddSelfRelationItems(const entry_ref* sourceRef);

	// looks up sen::key::kRelationName for typeName in relationConfigs, falling
	// back to typeName itself if there's no config or name entry.
	static BString ResolveRelationLabel(const BMessage& relationConfigs,
		const BString& typeName);

	BMessage    fEntriesToOpen;
	BMessenger  fTrackerMessenger;
	BWindow*    fParentWindow;

    BMessenger  fSenMessenger;
	entry_ref   fSourceRef;			// todo: support multi refs
    BMessage    fRelationsReply;
	uint32		fSenCmd;

	typedef BSlowMenu _inherited;
};
