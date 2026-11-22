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

	/**
	 * A bool field of the files to open: some of them are items of a relation whose items stand for entities (the events of a calendar, see TrackerSenRelations::IsMaterialisableItem()).
	 * The relations of such an item are those of its entity, which is made (or found) when the menu is built, in its own thread: the files are the item files then, not their targets.
	 */
	static const char* const kMaterialiseItems;

private:
	virtual bool StartBuildingItemList();
	virtual bool AddNextItem();
	virtual void DoneBuildingItemList();
	virtual void ClearMenuBuildingState();

	/** Takes the entities in the place of the items among the files to open (see kMaterialiseItems); an item that has none stays what it was, its target. */
	void MaterialiseSources();

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
