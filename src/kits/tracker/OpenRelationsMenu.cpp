#define DEBUG 1

#include "Attributes.h"
#include "AutoLock.h"
#include "Commands.h"
#include "FSUtils.h"
#include "IconMenuItem.h"
#include "OpenRelationsMenu.h"
#include "OpenRelationTargetsMenu.h"
#include "MimeTypes.h"
#include "Sen.h"
#include "Sensei.h"
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

OpenRelationsMenu::OpenRelationsMenu(const char* label, const BMessage* entriesToOpen,
	BWindow* parentWindow, const BMessenger& target)
	:
	BSlowMenu(label),
	fEntriesToOpen(*entriesToOpen),
	fTrackerMessenger(target),
	fParentWindow(parentWindow)
{
	InitIconPreloader();

	SetFont(be_plain_font);

	// too long to have triggers
	SetTriggersEnabled(false);
}


bool
OpenRelationsMenu::StartBuildingItemList()
{
    if (! be_roster->IsRunning(SEN_SERVER_SIGNATURE)) {
        PRINT(("failed to reach SEN server, please start process '%s' first.\n", SEN_SERVER_SIGNATURE));
        return false;
    }
	fSenMessenger = BMessenger(SEN_SERVER_SIGNATURE);

	// get all relations for refs received, what field contains relation type (ALL or SELF)
	BMessage message(fEntriesToOpen);

	// use SEN action as new message type for passing on to SEN
	status_t result = message.FindUInt32(SEN_ACTION_CMD, &fSenCmd);

	if (result != B_OK) {
		fSenCmd = SEN_RELATIONS_GET_ALL;
		PRINT(("failed to get SEN ActionCmd, fall back to GetAllRelations: %s\n", strerror(result) ));
	}
	message.what = fSenCmd;

	//TODO: support multi-selection - when SEN is adapted
	entry_ref ref;
	fEntriesToOpen.FindRef("refs", &ref);

	BEntry entry(&ref);
	BPath path;
	entry.GetPath(&path);

	BString relationType;
	switch(fSenCmd) {
		case SEN_RELATIONS_GET:
			relationType = "EXISTING";
			break;
		case SEN_RELATIONS_GET_ALL:
			relationType = "ALL";
			break;
		case SEN_RELATIONS_GET_ALL_SELF:
			relationType = "SELF";
			break;
		case SEN_RELATIONS_GET_COMPATIBLE:
			relationType = "COMPATIBLE";
			break;
		default:
			relationType = "UNKNOWN/UNEXPECTED";
	}
	PRINT(("Tracker->SEN: getting %s relations for path %s\n",
		relationType.String(),
		path.Path()));

	// TODO: remove string param and only use ref
	message.AddString(SEN_RELATION_SOURCE, (new BString(path.Path()))->String());
	message.PrintToStream();

	fSenMessenger.SendMessage(new BMessage(message), &fRelationsReply);

	fRelationsReply.AddRef("refs", new entry_ref(ref));
	// also add source to reply for later use in building relation target menu
	// todo: migrate to native refs internally and use source path only for external scripting!
	fRelationsReply.AddString(SEN_RELATION_SOURCE, path.Path());

	PRINT(("SEN->Tracker: received reply:\n"));
	fRelationsReply.PrintToStream();

	return true;
}

bool OpenRelationsMenu::AddNextItem()
{
    // nothing to do here
    return false;
}

void OpenRelationsMenu::ClearMenuBuildingState()
{
    //  empty;
    return;
}

void
OpenRelationsMenu::DoneBuildingItemList()
{
    // bail out if SEN server is not running
    if (! be_roster->IsRunning(SEN_SERVER_SIGNATURE)) {
        BMenuItem* item = new BMenuItem("n/a, SEN server not running.", 0);
		item->SetEnabled(false);
		AddItem(item);

        return;
    }

	// target the menu
	SetTargetForItems(fTrackerMessenger);

	int32 relationCount = 0;
	BString source;

	status_t result = fRelationsReply.FindString(SEN_RELATION_SOURCE, &source);
	if (result != B_OK) {
		PRINT(("error parsing SEN relation reply: %s\n", strerror(result) ));
		// still continue and build empty relations menu for a more consistent behavior
	}

	// check for desired relation type and build suitable items
	switch (fSenCmd) {
		case SEN_RELATIONS_GET:
			PRINT(("building relation targets menu.\n"));
			relationCount = AddRelationItems(&source);
			break;
		case SEN_RELATIONS_GET_ALL:
			PRINT(("building relations menu.\n"));
			relationCount = AddRelationItems(&source);
			break;
		case SEN_RELATIONS_GET_ALL_SELF:
			PRINT(("building contained relations menu.\n"));
			relationCount = AddSelfRelationItems(&source);
			break;
		case SEN_RELATIONS_GET_COMPATIBLE:
			PRINT(("building compatible relations menu.\n"));
			relationCount = AddRelationItems(&source);
			break;
		default:
			PRINT(("MISSING/UNEXPECTED command %u, building standard relations menu.\n", fSenCmd));
			relationCount = AddRelationItems(&source);	// also handles new relation with compatible types
	}
	if (relationCount == 0) {
		BMenuItem* item = new BMenuItem("no relations found.", 0);
		item->SetEnabled(false);
		AddItem(item);
	} else {
		PRINT(("%u relation(s) added.\n", relationCount));
	}
}

uint32 OpenRelationsMenu::AddRelationItems(const BString* source) {
	BString srcId;
	if (fRelationsReply.FindString(SEN_ID_ATTR, &srcId) != B_OK) {
		srcId.SetTo("");
	}

	// update command for followup action in relation menu items
	uint32 msgCmd;
	switch (fSenCmd) {
		case SEN_RELATIONS_GET_COMPATIBLE:
			msgCmd = SEN_RELATIONS_GET_COMPATIBLE_TYPES;
			break;
		case SEN_RELATIONS_GET_ALL: {
			msgCmd = SEN_RELATIONS_GET;
			break;
		}
		default:
			msgCmd = fSenCmd;
	}

	BString relationType(SEN_RELATIONS);	// handle normal relations

	// handle meta relations
	BString relation;
    int index = 0;
	if (fRelationsReply.FindString(SEN_RELATION_TYPE, &relation) == B_OK) {
		if (relation == SEN_LABEL_RELATION_TYPE) {
			relationType = "types";		// use the meta relation types as relations here
		}
	}

    while (fRelationsReply.FindString(relationType.String(), index++, &relation) == B_OK) {
		// only show associations for existing relations, not for new ones (handled separately)
		/*
		if (relation == SEN_LABEL_RELATION_TYPE) {
			if (fSenCmd != SEN_RELATIONS_GET && fSenCmd != SEN_RELATIONS_GET_COMPATIBLE_TYPES) {
				PRINT(("  > skipping Association relation.\n"));
				continue;
			}
		}
		*/
		// message for relation menu items
        BMessage* message = new BMessage(msgCmd);
        message->AddString(SEN_RELATION_SOURCE, BString(*source));
        message->AddString(SEN_RELATION_TYPE, BString(relation));

		// message for the relation menu itself (to open targets in separate Tracker window)
		BMimeType mime(relation.String());
		if (!mime.IsInstalled()) {
			ERROR("skipping relation with unavailable MIME type %s...\n", relation.String());
			continue;
		}

		char label[B_ATTR_NAME_LENGTH];
		if (mime.GetShortDescription(label) != B_OK) {
			PRINT(("could not get MIME type for relation %s", relation.String()));
			relation.CopyInto(label, 0, relation.Length());
		}

		BMessage *openRelationTargetsMsg = new BMessage(SEN_OPEN_RELATION_TARGET_VIEW);
        openRelationTargetsMsg->AddString(SEN_RELATION_SOURCE, source->String());
		// todo: add new srcId if adding first relation via Tracker later!
		openRelationTargetsMsg->AddString(SEN_RELATION_SOURCE_ATTR, BString(srcId));
		openRelationTargetsMsg->AddString(SEN_RELATION_TYPE, BString(relation));
		openRelationTargetsMsg->AddString(SEN_RELATION_LABEL, label);

        BMenuItem* item = new IconMenuItem(
            new OpenRelationTargetsMenu(label, message, fParentWindow, fTrackerMessenger),
            openRelationTargetsMsg,
			(new BString(relation))->String()
        );
		// redirect open relation targets message to Tracker app directly
		item->SetTarget(be_app_messenger);
		AddItem(item);
    }

	return index;
}

// TODO: move to SEN and reuse AddRelationItems with just different SEN command
uint32 OpenRelationsMenu::AddSelfRelationItems(const BString* source) {
	BMessage pluginConfig;
	int relationsAdded = 0;
    status_t result;

	result = fRelationsReply.FindMessage(SENSEI_PLUGIN_CONFIG_KEY, &pluginConfig);
	if (result != B_OK) {
		PRINT(("no plugin config found / unexpected reply.\n"));
		return 0;
	}

	// store default type for use in self relation targets menu later
	BString defaultType;
	result = pluginConfig.FindString(SENSEI_DEFAULT_TYPE_KEY, &defaultType);
	if (result != B_OK) {
		PRINT(("failed to look up default type in plugin config: %s\n", strerror(result)));
		return 0;
	}

	BMessage typesPlugins;
	result = pluginConfig.FindMessage(SENSEI_TYPES_PLUGINS_KEY, &typesPlugins);
	if (result != B_OK) {
		PRINT(("could not get type->plugin mapping in config received: %s\n", strerror(result)));
		return 0;
	}

	int32 itemCount = typesPlugins.CountNames(B_STRING_TYPE);
	if (itemCount == 0) {
		PRINT(("no relations found.\n"));
		return 0;
	}

	PRINT(("got SELF relations config:\n"));
	pluginConfig.PrintToStream();

	int32 pluginCount;
	char *fileType[B_MIME_TYPE_LENGTH];
	BString pluginName;

    for (int32 index = 0; index < itemCount; index++) {
		result = typesPlugins.GetInfo(B_STRING_TYPE, index, fileType, NULL, &pluginCount);
		if (result != B_OK || *fileType == NULL) {
			PRINT(("failed to parse self relations (%d added): %s\n", relationsAdded, strerror(result)));
			return relationsAdded;
		}
		PRINT(("adding menu item for self relation %s with %u plugins...\n", *fileType, pluginCount));

		result = typesPlugins.FindString(*fileType, &pluginName);
		if (result != B_OK || ! BMimeType(pluginName.String()).IsValid()) {
			PRINT(("failed to get valid plugin (got '%s') to resolve filetype %s: %s",
				pluginName.String(), *fileType, strerror(result)) );

			return relationsAdded;
		}
		PRINT(("got plugin %s to resolve filetype %s\n", pluginName.String(), *fileType));

		// message for relation menu items, sent to SEN server for resolving
        BMessage message(SEN_RELATIONS_GET_SELF);
		message.AddString(SEN_RELATION_SOURCE, (new BString(*source))->String());
        message.AddString(SEN_RELATION_TYPE, defaultType);	// the actual self relation
		// add plugin needed to resolve this self relation
		message.AddString(SENSEI_PLUGIN_KEY, pluginName);
		// add default type (the self relation type shown in the menu)
		message.AddString(SENSEI_DEFAULT_TYPE_KEY, defaultType);

		// message for the relation menu itself (to open targets in separate Tracker window)
		BString srcId;
		if (fRelationsReply.FindString(SEN_ID_ATTR, &srcId) != B_OK) {
			srcId.SetTo(SEN_RELATION_IS_SELF);	// todo: align with SEN core
		}

		BMimeType mime(defaultType.String());
		if (!mime.IsInstalled()) {
			PRINT(("skipping relation with unavailable MIME type %s...\n", defaultType.String()));
			index++;
			continue;
		} else {
			BMessage attrInfoMsg;
			mime.GetAttrInfo(&attrInfoMsg);
			bool isDynamic;
			bool isSelf;
			attrInfoMsg.FindBool(SEN_RELATION_IS_DYNAMIC, &isDynamic);
			attrInfoMsg.FindBool(SEN_RELATION_IS_SELF, &isSelf);

			PRINT(("got MIME type %s, relation is %s and %s:\n",
				defaultType.String(),
				isDynamic ? "dynamic" : "static",
				isSelf ? "inward" : "outward"));
		}
		char label[B_ATTR_NAME_LENGTH];
		if (mime.GetShortDescription(label) != B_OK) {
			PRINT(("could not get short description for relation MIME type %s, falling back to type name.\n", defaultType.String()));
			strcpy(label, defaultType.String());
		}

		BMessage openRelationTargetsMsg(SEN_OPEN_RELATION_TARGET_VIEW);
        openRelationTargetsMsg.AddString(SEN_RELATION_SOURCE, (new BString(*source))->String());
		openRelationTargetsMsg.AddString(SEN_RELATION_SOURCE_ATTR, srcId);
		openRelationTargetsMsg.AddString(SEN_RELATION_TYPE, defaultType);
		openRelationTargetsMsg.AddString(SEN_RELATION_LABEL, label);
		openRelationTargetsMsg.AddString(SENSEI_PLUGIN_KEY, pluginName);

        BMenuItem* item = new IconMenuItem(
            new OpenRelationTargetsMenu(label, new BMessage(message), fParentWindow, be_app_messenger),
            new BMessage(openRelationTargetsMsg),
			(new BString(defaultType))->String()
        );
		// redirect open relation targets message to Tracker app directly
		item->SetTarget(be_app_messenger);
		AddItem(item);

		relationsAdded++;
    }
	return relationsAdded;
}
