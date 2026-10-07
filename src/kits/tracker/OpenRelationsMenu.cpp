/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
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
#include <sen/Sen.h>
#include <sen/Sensei.h>
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
#include "TrackerSenLog.h"

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
    if (! be_roster->IsRunning(sen::kServerSignature)) {
        PRINT(("failed to reach SEN server, please start process '%s' first.\n", sen::kServerSignature));
        return false;
    }
	fSenMessenger = BMessenger(sen::kServerSignature);

	// get relations for all refs received, 'what' field contains relation type (ALL or SELF)
	BMessage message(fEntriesToOpen);

	// use SEN action as new message type for passing on to SEN
	status_t result = message.FindUInt32(sen::key::kAction, &fSenCmd);

	if (result != B_OK) {
		fSenCmd = sen::cmd::kRelationsGetAll;
		PRINT(("failed to get SEN ActionCmd, fall back to GetAllRelations: %s\n", strerror(result) ));
	}
	message.what = fSenCmd;

	BEntry entry(&fSourceRef);
	BPath path;
	entry.GetPath(&path);

	BString relationType;
	switch(fSenCmd) {
		case sen::cmd::kRelationsGet:
			relationType = "EXISTING";
			break;
		case sen::cmd::kRelationsGetAll:
			relationType = "ALL";
			break;
		case sen::cmd::kRelationsGetAllSelf:
			relationType = "SELF";
			break;
		case sen::cmd::kRelationsGetCompatible: {
			relationType = "COMPATIBLE";
			message.AddBool(sen::key::kWithConfigs, true);
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
    if (! be_roster->IsRunning(sen::kServerSignature)) {
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
		case sen::cmd::kRelationsGet:
			PRINT(("building relation targets menu.\n"));
			relationCount = AddRelationItems(&fSourceRef);
			break;
		case sen::cmd::kRelationsGetCompatible:
			PRINT(("building compatible relations menu.\n"));
			relationCount = AddRelationItems(&fSourceRef);
			break;
		case sen::cmd::kRelationsGetAll:
			PRINT(("building relations menu.\n"));
			relationCount = AddRelationItems(&fSourceRef);
			break;
		case sen::cmd::kRelationsGetAllSelf:
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

BString
OpenRelationsMenu::ResolveRelationLabel(const BMessage& relationConfigs, const BString& typeName)
{
	BString label;
	BMessage relationConf;

	if (relationConfigs.FindMessage(typeName.String(), &relationConf) == B_OK) {
		label = relationConf.GetString(sen::key::kRelationName);
	}
	if (label.IsEmpty()) {
		PRINT(("could not get relation config for type %s, falling back to type name.\n",
			typeName.String() ));
		label = typeName;
	}
	return label;
}

uint32 OpenRelationsMenu::AddRelationItems(const entry_ref* sourceRef) {
	BString srcId;
	if (fRelationsReply.FindString(sen::key::kSourceId, &srcId) != B_OK) {
		srcId.SetTo("");
	}

	// get relation configs for storing in menu items later
	BMessage relationConfigs;
	status_t result = fRelationsReply.FindMessage(sen::key::kRelationConfigMap, &relationConfigs);
	if (result != B_OK) {
		PRINT(("no relation config found, continuing with defaults.\n"));
	}

	// update command for followup action in relation menu items
	uint32 msgCmd;
	switch (fSenCmd) {
		case sen::cmd::kRelationsGetCompatible:
			msgCmd = sen::cmd::kRelationsGetCompatibleTypes;
			break;
		case sen::cmd::kRelationsGetAll: {
			msgCmd = sen::cmd::kRelationsGet;
			break;
		}
		default:
			msgCmd = fSenCmd;
	}

	BString propertyName(sen::key::kRelations);	// default: handle normal relations
	BString relationFilter = fRelationsReply.GetString(sen::key::kFilter);
	bool buildAssocRelations = false;

	// only sent for compatible relation types
	if (relationFilter == sen::filter::kCompatible) {
		propertyName = sen::key::kTargetType;	// adapt message processing to parse association relations below
		buildAssocRelations = true;
	}

	PRINT(("getting compatible relation items for relations using property %s...\n", propertyName.String() ));

	// get any type filters passed in
	BStringList mimeExcludes;
	fEntriesToOpen.FindStrings(sen::key::kExcludeTypes, &mimeExcludes);

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
        message->AddRef(sen::key::kSourceRef, sourceRef);

		// add relevant message properties for compatible or ALL relations
		if (buildAssocRelations) {
			message->AddString(sen::key::kRelationType, sen::mime::kAssociationRelation);
			message->AddString(sen::key::kTargetType, typeName);
		}
		else {
			message->AddString(sen::key::kRelationType, typeName);
			message->AddBool(sen::key::kIdToRefMap, true);	// param for internal processing to send back the id_ref-map
		}

		BMessage *openRelationTargetsMsg = new BMessage(sen::cmd::kOpenRelationTargetView);
        openRelationTargetsMsg->AddRef(sen::key::kSourceRef, sourceRef);
		openRelationTargetsMsg->AddString(sen::key::kSourceId, srcId);
		openRelationTargetsMsg->AddMessage(sen::key::kRelationConfigMap, &relationConfigs);

		if (buildAssocRelations) {
			openRelationTargetsMsg->AddString(sen::key::kRelationType, sen::mime::kAssociationRelation);
		}
		else {
			openRelationTargetsMsg->AddString(sen::key::kRelationType, typeName);
		}

		// get label from relation config
		BString label = ResolveRelationLabel(relationConfigs, typeName);

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
	openRelationsItemMsg->RemoveData(sen::key::kSourceRef);
	openRelationsItemMsg->RemoveData(sen::key::kSourceId);
	openRelationsItemMsg->RemoveData(sen::key::kRelationConfigMap);

	openRelationsItemMsg->AddRef(sen::key::kSourceRef, sourceRef);
	openRelationsItemMsg->AddString(sen::key::kSourceId, srcId);
    openRelationsItemMsg->AddMessage(sen::key::kRelationConfigMap, &relationConfigs);

	return countRelations;
}

uint32 OpenRelationsMenu::AddSelfRelationItems(const entry_ref* sourceRef) {
	BMessage pluginConfig, relationConfigs;
	int relationsAdded = 0;
    status_t result;

	// Note: self relations mostly have no sourceId yet since they are dynamically resolved

	// get relation config
	result = fRelationsReply.FindMessage(sen::key::kRelationConfigMap, &relationConfigs);
	if (result != B_OK) {
		PRINT(("no relation config map found, continuing with defaults.\n"));
	}

	// get plugin config
	result = fRelationsReply.FindMessage(sensei::key::kPluginConfig, &pluginConfig);
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
	result = pluginConfig.FindString(sensei::key::kDefaultType, &defaultType);
	if (result != B_OK) {
		PRINT(("failed to look up default type in plugin config: %s\n", strerror(result)));
		return 0;
	}

	BMessage typesPlugins;
	result = pluginConfig.FindMessage(sensei::key::kTypesPlugins, &typesPlugins);
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
        BMessage message(sen::cmd::kRelationsGetSelf);
		message.AddRef(sen::key::kSourceRef, sourceRef);
		// target types might vary, but the relation type for the menu is always the original default type
        message.AddString(sen::key::kRelationType, defaultType);
		// add plugin needed to resolve this self relation
		message.AddString(sensei::key::kPlugin, pluginName);
		// add plugin config with default type and type+attr mapping
		message.AddMessage(sensei::key::kPluginConfig, &pluginConfig);
		// add relation config
		message.AddMessage(sen::key::kRelationConfigMap, &relationConfigs);

		// message for the relation menu itself
		BMessage openRelationTargetsMsg(sen::cmd::kOpenRelationTargetView);

		// add only needed parts of SEN relation config as compact individual fields
        openRelationTargetsMsg.AddRef(sen::key::kSourceRef, sourceRef);
		openRelationTargetsMsg.AddString(sen::key::kRelationType, defaultType);
		// add relation config - PrepareRelationTargetFolder() looks this up by
		// sen::key::kRelationConfigMap, same as the non-self relation path above.
		openRelationTargetsMsg.AddMessage(sen::key::kRelationConfigMap, &relationConfigs);
		openRelationTargetsMsg.AddString(sensei::key::kPlugin, pluginName);
		openRelationTargetsMsg.AddMessage(sensei::key::kPluginConfig, &pluginConfig);

		// get label from relation config
		BString label = ResolveRelationLabel(relationConfigs, defaultType);

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
	openSelfRelationsItemMsg->RemoveData(sen::key::kSourceRef);
	openSelfRelationsItemMsg->RemoveData(sen::key::kRelations);
	openSelfRelationsItemMsg->RemoveData(sen::key::kRelationConfigMap);

	openSelfRelationsItemMsg->AddRef(sen::key::kSourceRef, sourceRef);

	BStringList relations;
	result = fRelationsReply.FindStrings(sen::key::kRelations, &relations);
	openSelfRelationsItemMsg->AddStrings(sen::key::kRelations,  relations);	// add the emty message if some error occurred
    openSelfRelationsItemMsg->AddMessage(sen::key::kRelationConfigMap, &relationConfigs);

	return relationsAdded;
}
