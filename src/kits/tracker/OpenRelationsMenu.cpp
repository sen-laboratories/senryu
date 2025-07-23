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

	//TODO: support multi-selection - when SEN is adapted
	fEntriesToOpen.FindRef("refs", &fSourceRef);
}

bool
OpenRelationsMenu::StartBuildingItemList()
{
    if (! be_roster->IsRunning(SEN_SERVER_SIGNATURE)) {
        PRINT(("failed to reach SEN server, please start process '%s' first.\n", SEN_SERVER_SIGNATURE));
        return false;
    }
	fSenMessenger = BMessenger(SEN_SERVER_SIGNATURE);

	// get relations for all refs received, 'what' field contains relation type (ALL or SELF)
	BMessage message(fEntriesToOpen);

	// use SEN action as new message type for passing on to SEN
	status_t result = message.FindUInt32(SEN_ACTION_CMD, &fSenCmd);

	if (result != B_OK) {
		fSenCmd = SEN_RELATIONS_GET_ALL;
		PRINT(("failed to get SEN ActionCmd, fall back to GetAllRelations: %s\n", strerror(result) ));
	}
	message.what = fSenCmd;

	BEntry entry(&fSourceRef);
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

	PRINT(("Tracker->SEN: getting %s relations for path '%s' with message:\n",
		relationType.String(),
		path.Path()));

	message.PrintToStream();

	fSenMessenger.SendMessage(new BMessage(message), &fRelationsReply);

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

	// check for desired relation type and build suitable items
	switch (fSenCmd) {
		case SEN_RELATIONS_GET:
			PRINT(("building relation targets menu.\n"));
			relationCount = AddRelationItems(&fSourceRef);
			break;
		case SEN_RELATIONS_GET_ALL:
			PRINT(("building relations menu.\n"));
			relationCount = AddRelationItems(&fSourceRef);
			break;
		case SEN_RELATIONS_GET_ALL_SELF:
			PRINT(("building contained relations menu.\n"));
			relationCount = AddSelfRelationItems(&fSourceRef);
			break;
		case SEN_RELATIONS_GET_COMPATIBLE:
			PRINT(("building compatible relations menu.\n"));
			relationCount = AddRelationItems(&fSourceRef);
			break;
		default:
			PRINT(("MISSING/UNEXPECTED command %u, building standard relations menu.\n", fSenCmd));
			relationCount = AddRelationItems(&fSourceRef);	// also handles new relation with compatible types
	}

	if (relationCount == 0) {
		BMenuItem* item = new BMenuItem("no relations found.", 0);
		item->SetEnabled(false);
		AddItem(item);
	} else {
		PRINT(("%u relation(s) added.\n", relationCount));
	}
}

uint32 OpenRelationsMenu::AddRelationItems(const entry_ref* sourceRef) {
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

	BString propertyName(SEN_RELATIONS);	// default: handle normal relations
	BString relationFilter = fRelationsReply.GetString(SEN_MSG_FILTER);

	// only sent for compatible relation types
	if (relationFilter == SEN_MSG_FILTER_COMPATIBLE) {
		propertyName = SEN_RELATION_COMPATIBLE_TYPES;		// use the meta relation types for association relations
	}

	PRINT(("getting compatible relation items for relations using property %s...\n", propertyName.String() ));

	// get any type filters passed in
	BStringList mimeExcludes;
	fEntriesToOpen.FindStrings(SEN_EXCLUDE_TYPES, &mimeExcludes);

    int32 index = 0, countRelations = 0;
	BString typeName;
    while (fRelationsReply.FindString(propertyName.String(), index++, &typeName) == B_OK) {
		// check if associated MIME type is installed (needed for Tracker display)
		BMimeType mime(typeName.String());
		if (!mime.IsInstalled()) {
			ERROR("  > skipping relation with unavailable MIME type %s...\n", typeName.String());
			continue;
		}

		// check for excluded types
		if (mimeExcludes.HasString(typeName)) {
			PRINT(("  > skipping excluded relation %s.\n", typeName.String()));
			continue;
		}
		// message for relation menu items
        BMessage* message = new BMessage(msgCmd);
        message->AddRef(SEN_RELATION_SOURCE_REF, sourceRef);
		// add relevant message properties for compatible or ALL relations
		if (propertyName == SEN_RELATION_COMPATIBLE_TYPES) {
			message->AddString(SEN_RELATION_TYPE, SEN_LABEL_RELATION_TYPE);
			message->AddString(SEN_RELATION_TARGET_TYPE, typeName);
		}
		else {
			message->AddString(SEN_RELATION_TYPE, typeName);
			message->AddBool(SEN_ID_TO_REF_MAP, true);	// will be sent back as message under the same name
		}

		char label[B_ATTR_NAME_LENGTH];
		if (mime.GetShortDescription(label) != B_OK) {
			// this should never happen, since the MIME registry does not allow a Type without a short description
			PRINT(("could not get short description for relation %s, falling back to type name.\n", typeName.String()));
			typeName.CopyInto(label, 0, typeName.CountBytes(0, typeName.Length()));
		}

		BMessage *openRelationTargetsMsg = new BMessage(SEN_OPEN_RELATION_TARGET_VIEW);
        openRelationTargetsMsg->AddRef(SEN_RELATION_SOURCE_REF, sourceRef);
		openRelationTargetsMsg->AddString(SEN_RELATION_SOURCE_ATTR, srcId);
		openRelationTargetsMsg->AddString(SEN_RELATION_LABEL, label);

		if (propertyName == SEN_RELATION_COMPATIBLE_TYPES) {
			openRelationTargetsMsg->AddString(SEN_RELATION_TYPE, SEN_LABEL_RELATION_TYPE);
		}
		else {
			openRelationTargetsMsg->AddString(SEN_RELATION_TYPE, typeName);
		}

        BMenuItem* item = new IconMenuItem(
            new OpenRelationTargetsMenu(label, message, fParentWindow, fTrackerMessenger),
										openRelationTargetsMsg, typeName);

		// redirect open relation targets message to Tracker app directly
		item->SetTarget(be_app_messenger);

		AddItem(item);
		countRelations++;
    }
	PRINT(("got %d compatible relation items.\n", countRelations));

	return countRelations;
}

// TODO: move to SEN and reuse AddRelationItems with just different SEN command
uint32 OpenRelationsMenu::AddSelfRelationItems(const entry_ref* sourceRef) {
	BMessage pluginConfig;
	int relationsAdded = 0;
    status_t result;

	result = fRelationsReply.FindMessage(SENSEI_PLUGIN_CONFIG_KEY, &pluginConfig);
	if (result != B_OK) {
		PRINT(("no plugin config found / unexpected reply.\n"));
		return 0;
	}

	if (pluginConfig.IsEmpty()) {
		PRINT(("empty plugin config, skipping.\n"));
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
		message.AddRef(SEN_RELATION_SOURCE_REF, sourceRef);
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
        openRelationTargetsMsg.AddRef(SEN_RELATION_SOURCE_REF, sourceRef);
		openRelationTargetsMsg.AddString(SEN_RELATION_SOURCE_ATTR, srcId);
		openRelationTargetsMsg.AddString(SEN_RELATION_TYPE, defaultType);
		openRelationTargetsMsg.AddString(SEN_RELATION_LABEL, label);
		openRelationTargetsMsg.AddString(SENSEI_PLUGIN_KEY, pluginName);

        BMenuItem* item = new IconMenuItem(
            new OpenRelationTargetsMenu(label, new BMessage(message), fParentWindow, be_app_messenger),
            new BMessage(openRelationTargetsMsg),
			defaultType
        );
		// redirect open relation targets message to Tracker app directly
		item->SetTarget(be_app_messenger);
		AddItem(item);

		relationsAdded++;
    }
	return relationsAdded;
}
