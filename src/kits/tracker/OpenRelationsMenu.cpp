/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
*/
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
		case SEN_RELATIONS_GET_COMPATIBLE: {
			relationType = "COMPATIBLE";
			message.AddBool(SEN_MSG_CONFIGS, true);
			break;
		}
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
		case SEN_RELATIONS_GET_COMPATIBLE:
			PRINT(("building compatible relations menu.\n"));
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
	if (fRelationsReply.FindString(SEN_RELATION_SOURCE_ID, &srcId) != B_OK) {
		srcId.SetTo("");
	}

	// get relation configs for storing in menu items later
	BMessage relationConfigs;
	status_t result = fRelationsReply.FindMessage(SEN_RELATION_CONFIG_MAP, &relationConfigs);
	if (result != B_OK) {
		PRINT(("no relation config found, continuing with defaults.\n"));
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
	bool buildAssocRelations = false;

	// only sent for compatible relation types
	if (relationFilter == SEN_MSG_FILTER_COMPATIBLE) {
		propertyName = SEN_RELATION_TARGET_TYPE;	// adapt message processing to parse association relations below
		buildAssocRelations = true;
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
		if (buildAssocRelations) {
			message->AddString(SEN_RELATION_TYPE, SEN_ASSOC_RELATION_TYPE);
			message->AddString(SEN_RELATION_TARGET_TYPE, typeName);
		}
		else {
			message->AddString(SEN_RELATION_TYPE, typeName);
			message->AddBool(SEN_ID_TO_REF_MAP, true);	// param for internal processing to send back the id_ref-map
		}

		BMessage *openRelationTargetsMsg = new BMessage(SEN_OPEN_RELATION_TARGET_VIEW);
        openRelationTargetsMsg->AddRef(SEN_RELATION_SOURCE_REF, sourceRef);
		openRelationTargetsMsg->AddString(SEN_RELATION_SOURCE_ID, srcId);
		openRelationTargetsMsg->AddMessage(SEN_RELATION_CONFIG_MAP, &relationConfigs);

		if (buildAssocRelations) {
			openRelationTargetsMsg->AddString(SEN_RELATION_TYPE, SEN_ASSOC_RELATION_TYPE);
		}
		else {
			openRelationTargetsMsg->AddString(SEN_RELATION_TYPE, typeName);
		}

		// get label from relation config
		BString  label;
		BMessage relationConf;
		result = relationConfigs.FindMessage(typeName.String(), &relationConf);

		if (result == B_OK) {
			label = relationConf.GetString(SEN_RELATION_NAME);
		}
		if (label.IsEmpty()) {
			PRINT(("could not get relation config for type %s, falling back to FQN: %s.\n",
				typeName.String(), strerror(result) ));
			label = typeName;
		}

        BMenuItem* item = new IconMenuItem(
            new OpenRelationTargetsMenu(label.String(), message, fParentWindow, fTrackerMessenger),
										openRelationTargetsMsg, typeName);

		// redirect open relation targets message to Tracker app directly
		item->SetTarget(be_app_messenger);

		AddItem(item);
		countRelations++;
    }

	PRINT(("got %d compatible relation items.\n", countRelations));

	// store SEN:ID and relationType also in root relation menu item message itself,
	// so we can use it for the top-level relation-view
	BMenuItem *openRelationsItem = Supermenu()->FindItem(kOpenRelations);
	ASSERT(openRelationsItem != NULL);
	BMessage  *openRelationsItemMsg = openRelationsItem->Message();
	ASSERT(openRelationsItemMsg != NULL);

	// always replace any previous data as the parent menu is reused!
	openRelationsItemMsg->RemoveData(SEN_RELATION_SOURCE_REF);
	openRelationsItemMsg->RemoveData(SEN_RELATION_SOURCE_ID);
	openRelationsItemMsg->RemoveData(SEN_RELATION_CONFIG_MAP);

	openRelationsItemMsg->AddRef(SEN_RELATION_SOURCE_REF, sourceRef);
	openRelationsItemMsg->AddString(SEN_RELATION_SOURCE_ID, srcId);
    openRelationsItemMsg->AddMessage(SEN_RELATION_CONFIG_MAP, &relationConfigs);

	return countRelations;
}

uint32 OpenRelationsMenu::AddSelfRelationItems(const entry_ref* sourceRef) {
	BMessage pluginConfig, relationConfigs;
	int relationsAdded = 0;
    status_t result;

	// Note: self relations mostly have no sourceId yet since they are dynamically resolved

	// get relation config
	result = fRelationsReply.FindMessage(SEN_RELATION_CONFIG_MAP, &relationConfigs);
	if (result != B_OK) {
		PRINT(("no relation config map found, continuing with defaults.\n"));
	}

	// get plugin config
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
		PRINT(("no self relations found.\n"));
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
		// target types might vary, but the relation type for the menu is always the original default type
        message.AddString(SEN_RELATION_TYPE, defaultType);
		// add plugin needed to resolve this self relation
		message.AddString(SENSEI_PLUGIN_KEY, pluginName);
		// add plugin config with default type and type+attr mapping
		message.AddMessage(SENSEI_PLUGIN_CONFIG_KEY, &pluginConfig);
		// add relation config
		message.AddMessage(SEN_RELATION_CONFIG_MAP, &relationConfigs);

		// message for the relation menu itself
		BMessage openRelationTargetsMsg(SEN_OPEN_RELATION_TARGET_VIEW);

		// add only needed parts of SEN relation config as compact individual fields
        openRelationTargetsMsg.AddRef(SEN_RELATION_SOURCE_REF, sourceRef);
		// add relation config
		openRelationTargetsMsg.AddMessage(SEN_RELATION_CONFIG, &relationConfigs);
		openRelationTargetsMsg.AddString(SENSEI_PLUGIN_KEY, pluginName);
		openRelationTargetsMsg.AddMessage(SENSEI_PLUGIN_CONFIG_KEY, &pluginConfig);

		// get label from relation config
		BString  label;
		BMessage relationConf;
		result = relationConfigs.FindMessage(defaultType.String(), &relationConf);

		if (result == B_OK) {
			label = relationConf.GetString(SEN_RELATION_NAME);
		}
		if (label.IsEmpty()) {
			PRINT(("could not get relation config for type %s, falling back to type name: %s.\n",
				defaultType.String(), strerror(result) ));
			label = defaultType;
		}

        BMenuItem* item = new IconMenuItem(
            new OpenRelationTargetsMenu(label.String(), new BMessage(message), fParentWindow, be_app_messenger),
            new BMessage(openRelationTargetsMsg),
			defaultType
        );
		// redirect open relation targets message to Tracker app directly
		item->SetTarget(be_app_messenger);
		AddItem(item);

		relationsAdded++;
    }	// for index...itemCount

	// store relationType and config also in root relation menu item message itself,
	// so we can use it for the top-level relation-view
	BMenuItem *openSelfRelationsItem = Supermenu()->FindItem(kOpenSelfRelations);
	ASSERT(openSelfRelationsItem != NULL);
	BMessage  *openSelfRelationsItemMsg = openSelfRelationsItem->Message();
	ASSERT(openSelfRelationsItemMsg != NULL);

	// always replace any previous data as the parent menu is reused!
	openSelfRelationsItemMsg->RemoveData(SEN_RELATION_SOURCE_REF);
	openSelfRelationsItemMsg->RemoveData(SEN_RELATIONS);
	openSelfRelationsItemMsg->RemoveData(SEN_RELATION_CONFIG_MAP);

	openSelfRelationsItemMsg->AddRef(SEN_RELATION_SOURCE_REF, sourceRef);

	BStringList relations;
	result = fRelationsReply.FindStrings(SEN_RELATIONS, &relations);
	openSelfRelationsItemMsg->AddStrings(SEN_RELATIONS,  relations);	// add the emty message if some error occurred
    openSelfRelationsItemMsg->AddMessage(SEN_RELATION_CONFIG_MAP, &relationConfigs);

	return relationsAdded;
}
