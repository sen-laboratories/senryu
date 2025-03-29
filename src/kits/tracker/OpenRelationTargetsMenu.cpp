#define DEBUG 1

#include "Attributes.h"
#include "AutoLock.h"
#include "Commands.h"
#include "FSUtils.h"
#include "IconMenuItem.h"
#include "OpenRelationTargetsMenu.h"
#include "Sensei.h"
#include "MimeTypes.h"
#include "StopWatch.h"
#include "Tracker.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Debug.h>
#include <GroupView.h>
#include <GridView.h>
#include <Locale.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Roster.h>
#include <SpaceLayoutItem.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>

OpenRelationTargetsMenu::OpenRelationTargetsMenu(const char* label, const BMessage* entriesToOpen,
	BWindow* parentWindow, BHandler* target)
	:
	BSlowMenu(label),
	fEntriesToOpen(*entriesToOpen),
	target(target),
	fParentWindow(parentWindow),
	fRelationTargetsReply(new BMessage())
{
	InitIconPreloader();

	SetFont(be_plain_font);

	// too long to have triggers
	SetTriggersEnabled(false);
}


OpenRelationTargetsMenu::OpenRelationTargetsMenu(const char* label, const BMessage* entriesToOpen,
	BWindow* parentWindow, const BMessenger &messenger)
	:
	BSlowMenu(label),
	fEntriesToOpen(*entriesToOpen),
	target(NULL),
	fMessenger(messenger),
	fParentWindow(parentWindow),
	fRelationTargetsReply(new BMessage())
{
	InitIconPreloader();

	SetFont(be_plain_font);
	SetTriggersEnabled(false);
}

bool
OpenRelationTargetsMenu::StartBuildingItemList()
{
	switch(fEntriesToOpen.what) {
		case SENSEI_MESSAGE_RESULT:	// we are a sub menu of self relations
		{
			PRINT(("building self relations submenu from SENSEI_MESSAGE_RESULT in existing sub item msg...\n"));
			fRelationTargetsReply->what = fEntriesToOpen.what;
			fRelationTargetsReply = &fEntriesToOpen;
			return true;
		}
		case SEN_RELATIONS_GET_SELF:
		{
			fRelationTargetsReply->what = fEntriesToOpen.what;
			PRINT(("building self relations submenu from SEN_RELATIONS_GET_SELF relation reply...\n"));
			break;
		}
		default:
		{
			PRINT(("relations submenu: start building items...\n"));
		}
	}

    if (be_roster->IsRunning(SEN_SERVER_SIGNATURE)) {
		BMessenger fSenMessenger(SEN_SERVER_SIGNATURE);
		if (! fSenMessenger.IsValid()) {
			PRINT(("failed to set up messenger for SEN server!\n"));
			return false;
		}

        status_t result = fSenMessenger.SendMessage(new BMessage(fEntriesToOpen), fRelationTargetsReply);
		if (result != B_OK) {
			PRINT(("failed to communicate with SEN server: %s\n", strerror(result)));
			return false;
		}
		return true;
    } else {
        PRINT(("failed to reach SEN server, is it running?\n"));
        return false;
    }
}

bool OpenRelationTargetsMenu::AddNextItem()
{
    // nothing to do here
    return false;
}

void OpenRelationTargetsMenu::ClearMenuBuildingState()
{
    //  empty;
    return;
}

void
OpenRelationTargetsMenu::DoneBuildingItemList()
{
	// target the menu
	if (target != NULL)
		SetTargetForItems(target);
	else
		SetTargetForItems(fMessenger);

	uint32 targets = 0;
	status_t result;

	switch(fRelationTargetsReply->what) {
		case SENSEI_MESSAGE_RESULT:
		{
			// resolve self relations from SENSEI reply
			result = AddSelfRelationTargetItems(&targets);
			break;
		}
		case SEN_RESULT_RELATIONS:
		{
			result = AddRelationTargetItems(&targets);
			break;
		}
		default:
		{
			PRINT(("relation RESULT: unsupported message type %u\n", fRelationTargetsReply->what));
			result = B_ERROR;
			break;
		}
	}
	if (result != B_OK) {
		PRINT(("error buiding relations menu: %s\n", strerror(result) ));
		return;
	}

	if (targets == 0) {
		PRINT(("no relation targets added.\n"));
		BMenuItem* item = new BMenuItem("no targets found.", 0);
		item->SetEnabled(false);
		AddItem(item);
	} else {
		PRINT(("%u targets added.\n", targets));
	}
}

status_t
OpenRelationTargetsMenu::AddRelationTargetItems(uint32* targetCount)
{
	entry_ref ref;
	BEntry entry;
	BPath path;
	int32 index = 0;
	status_t result;

	PRINT(("adding relation menu target items...\n"));

	while ((result = fRelationTargetsReply->FindRef("refs", index, &ref)) == B_OK) {
		entry.SetTo(&ref);
		entry.GetPath(&path);

		// this will create a menu item similar to folder items and launch with the preferred app,
		// which in this case is the associated relation handler.
		// todo: add message with arguments from relation properties!
		ModelMenuItem* item = new ModelMenuItem(new Model(&ref, true, true), path.Leaf(), NULL);
		AddItem(item);

		index++;
		(*targetCount)++;
	}
	// check for normal end state of refs processing
	if (result != B_NAME_NOT_FOUND && result != B_BAD_INDEX) {
		PRINT(("failed to resolve refs: %s\n", strerror(result)));
		return result;
	}

	return B_OK;
}

status_t
OpenRelationTargetsMenu::AddSelfRelationTargetItems(uint32* targetCount)
{
	status_t result;
	BMessage itemMsg, childMsg;
	BString defaultType;

	// get default item type from original msg received from parent menu
	result = fEntriesToOpen.FindString(SENSEI_DEFAULT_TYPE_KEY, &defaultType);
	if (result != B_OK) {
		if (result != B_NAME_NOT_FOUND) {
			PRINT(("error reading message from OpenRelationsMenu: %s\n", strerror(result)));
			return result;
		}	// still continue and hope we get the type from items
	} else {
		PRINT(("adding self relation menu target items with default type %s...\n", defaultType.String()));
		fDefaultType = defaultType;
	}

	// get root node
	result = fRelationTargetsReply->FindMessage("item", &itemMsg);
	if (result != B_OK) {
		PRINT(("failed to build self relations menu, could not find root node, unexpected message reply.\n"));
		return result;
	}

	// get ref to self from refs received (source of relation)
	// Note: we expect only 1 ref for self relations, but let's keep it consistent across all relation types
	entry_ref ref;
	result = fRelationTargetsReply->FindRef("refs", &ref);
	if (result != B_OK) {
		PRINT(("failed to resolve self relation to source: %s\n", strerror(result)));
		return result;
	}

	PRINT(("iterating through self relation target message...\n"));
	int32 index = 0;
	BMessage propertiesMsg;
	BString label, type;

	while ((result = GetItemMessageInfo(&itemMsg, &childMsg, &propertiesMsg, index) == B_OK)) {
		IconMenuItem* item;

		propertiesMsg.FindString("label", &label);
		propertiesMsg.FindString("type", &type);
		PRINT(("got properties for item %s [%d]:\n", label.String(), index));
		#ifdef DEBUG
			propertiesMsg.PrintToStream();
		#endif

		// add as menu if there is a child node, else add as a plain menu item
		if (childMsg.IsEmpty()) {
			PRINT(("adding self relation item [%d] '%s' of type '%s'.\n", index, label.String(), type.String() ));

			// create new self relation item with self ref msg
			BMessage openRelationTargetItemMsg(B_REFS_RECEIVED);
			openRelationTargetItemMsg.AddRef("refs", new entry_ref(ref));
			openRelationTargetItemMsg.AddBool(SEN_RELATION_IS_SELF, true);
			openRelationTargetItemMsg.AddBool(SEN_RELATION_IS_DYNAMIC, true);
			openRelationTargetItemMsg.AddString(SENSEI_DEFAULT_TYPE_KEY, (new BString(defaultType))->String());
			openRelationTargetItemMsg.AddString(SEN_RELATION_LABEL, (new BString(label))->String());
			openRelationTargetItemMsg.AddString(SEN_RELATION_TYPE, (new BString(type))->String());

			// add all properties from this item (e.g. page, position,...)
			openRelationTargetItemMsg.AddMessage(SEN_OPEN_RELATION_ARGS_KEY, new BMessage(propertiesMsg));

			#ifdef DEBUG
				PRINT(("openRelationTargetItemMsg is:\n"));
				openRelationTargetItemMsg.PrintToStream();
			#endif
			item = new IconMenuItem(label.String(), new BMessage(openRelationTargetItemMsg), type.String());
		} else {
			PRINT(("adding self relation menu [%d] '%s' of type '%s'.\n", index, label.String(), type.String() ));

			BMessage openRelationTargetsMsg(SEN_OPEN_RELATION_TARGET_VIEW);
			openRelationTargetsMsg.AddBool(SEN_RELATION_IS_SELF, true);
			openRelationTargetsMsg.AddBool(SEN_RELATION_IS_DYNAMIC, true);
			openRelationTargetsMsg.AddString(SENSEI_DEFAULT_TYPE_KEY, (new BString(defaultType))->String());
			openRelationTargetsMsg.AddString(SEN_RELATION_LABEL, (new BString(label))->String());
			openRelationTargetsMsg.AddString(SEN_RELATION_TYPE, (new BString(type))->String());
			// add all properties of this item as arguments
			openRelationTargetsMsg.AddMessage(SEN_OPEN_RELATION_ARGS_KEY, new BMessage(propertiesMsg));

			// transparently handle just like a normal message result above, but for the subtree
			childMsg.what = SENSEI_MESSAGE_RESULT;
			childMsg.AddRef("refs", new entry_ref(ref));
			childMsg.AddString(SENSEI_DEFAULT_TYPE_KEY, (new BString(defaultType))->String());

			#ifdef DEBUG
				PRINT(("openRelationTargetsMsg is:\n"));
				openRelationTargetsMsg.PrintToStream();
				PRINT(("childMsg is:\n"));
				childMsg.PrintToStream();
			#endif

			item = new IconMenuItem(
				new OpenRelationTargetsMenu(label.String(), new BMessage(childMsg), fParentWindow, be_app_messenger),
				new BMessage(openRelationTargetsMsg),
				type.String()	// this is the MIME type of the relation, so take its icon
			);

			childMsg.MakeEmpty();
		}
		item->SetTarget(be_app_messenger);
		AddItem(item);

		propertiesMsg.MakeEmpty();
		index++;
		(*targetCount)++;
	}
	return B_OK;
}

status_t OpenRelationTargetsMenu::GetItemMessageInfo(
	const BMessage* itemMsg,
	BMessage* childMsg,
	BMessage* properties,
	int32 index)
{
	// get required properties
	// get label
	BString label;
	status_t result = itemMsg->FindString("label", index, &label);
	if (result != B_OK) {
		if (result == B_BAD_INDEX && index > 0) {
			return result;	// this will abort without error in calling loop
		}
		PRINT(("failed to get item label [%d]: %s\n", index, strerror(result) ));
		return result;
	}
	// add to result
	properties->AddString("label", (new BString(label))->String());

	// get type
	BString type;
	result = itemMsg->FindString("type", index, &type);
	if (result != B_OK) {
		if (result == B_NAME_NOT_FOUND) {
			// this is only allowed if there is a default type
			if (! fDefaultType.IsEmpty()) {
				type = fDefaultType;
			} else {
				PRINT(("no type specified and no defaultType provided, aborting.\n"));
				return B_BAD_VALUE;
			}
		} else {
			PRINT(("failed to get item type: %s\n", strerror(result) ));
			return B_ERROR;
		}
	}
	// add to result
	properties->AddString("type", (new BString(type))->String());

	// get optional child node message (empty or another node)
	BMessage subItemMsg;
	result = itemMsg->FindMessage("item", index, &subItemMsg);
	if (result != B_OK) {
		if (result != B_NAME_NOT_FOUND) {
			PRINT(("error looking for child node in message: %s\n", strerror(result)));
		}
	} else {
		if (! subItemMsg.IsEmpty()) {
			childMsg->AddMessage("item", new BMessage(subItemMsg));
		}
	}

	// add all properties from this item at this index (e.g. page, position,...)
	char* name;
	int32 count;
	int32 itemIndex = 0;
	type_code typeCode;

	while (result == B_OK) {
		result = itemMsg->GetInfo(B_ANY_TYPE, itemIndex, &name,	&typeCode, &count);
		if (result != B_OK) {
			if (result == B_BAD_INDEX) {
				break;
			}
			PRINT(("failed to get message info for item %s[%d]: %s\n", label.String(), itemIndex, strerror(result)));
			return result;
		}
		// omit already parsed or non-property fields including item child nodes
		if (strncmp(name, "label", 5) != 0 && strncmp(name, "type", 4) != 0 && strncmp(name, "item", 4) != 0) {
			PRINT(("properties at index %d with name %s and count %d:\n", index, name, count));
			if (index > count-1) {
				PRINT(("got only %d '%s' items but need #%d\n", count, name, index));
				break;
			}
			// add remaining property fields
			const void* data;
			ssize_t size;

			if ((result = itemMsg->FindData(name, typeCode, index, &data, &size))!= B_OK) {
				PRINT(("failed to get message data '%s' for item %s[%d]: %s\n", name, label.String(), index, strerror(result)));
				return result;
			}
			if ((result = properties->AddData(name, typeCode, data, size)) != B_OK) {
				PRINT(("failed to add message data '%s' for item %s[%d]: %s\n", name, label.String(), index, strerror(result)));
				return result;
			}
		}
		itemIndex++;
	}

	return B_OK;
}
