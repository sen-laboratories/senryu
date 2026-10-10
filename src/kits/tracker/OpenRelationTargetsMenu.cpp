/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#define DEBUG 1

#include "ContainerWindow.h"

#include "Attributes.h"
#include "AutoLock.h"
#include "Commands.h"
#include "FSUtils.h"
#include "IconMenuItem.h"
#include "OpenRelationTargetsMenu.h"
#include "RelationContext.h"
#include <sen/Sen.h>
#include <sen/Sensei.h>
#include "MimeTypes.h"
#include "StopWatch.h"
#include "Tracker.h"
#include "TemplateUtils.h"

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
#include <vector>

OpenRelationTargetsMenu::OpenRelationTargetsMenu(const char* label, const BMessage* entriesToOpen,
	BWindow* parentWindow, const BMessenger &messenger)
	:
	BSlowMenu(label),
	fEntriesToOpen(*entriesToOpen),
	fMessenger(messenger),
	fSenMessenger(NULL),
	fParentWindow(parentWindow),
	fRelationTargetsReply(&fOwnReply)
{
	InitIconPreloader();

	SetFont(be_plain_font);
	SetTriggersEnabled(false);

	if (be_roster->IsRunning(sen::kServerSignature)) {
		fSenMessenger = new BMessenger(sen::kServerSignature);
		if (! fSenMessenger->IsValid()) {
			PRINT(("failed to set up messenger for SEN server!\n"));
		}
	}

}

OpenRelationTargetsMenu::~OpenRelationTargetsMenu()
{
	if (fSenMessenger)
		delete fSenMessenger;
}

bool
OpenRelationTargetsMenu::StartBuildingItemList()
{
	switch(fEntriesToOpen.what) {
		case sensei::cmd::kResult:	// we are a sub menu of self relations
		{
			PRINT(("  > building self relations submenu from sensei::cmd::kResult in existing sub item msg...\n"));
			fRelationTargetsReply = &fEntriesToOpen;
			return true;
		}
		case sen::cmd::kRelationsGetSelf:
		{
			PRINT(("building self relations submenu from sen::cmd::kRelationsGetSelf relation reply...\n"));
			break;
		}
		case sen::cmd::kRelationsGetCompatibleTypes:
		{
			PRINT(("building relation targets submenu for compatible targets...\n"));
			break;
		}
		case sen::cmd::kRelationsGet:
		{
			PRINT(("building relation targets submenu for existing targets...\n"));
			break;
		}
		default:
		{
			PRINT(("relation targets submenu: UNKNOWN/UNEXPECTED type, start building items...\n"));
		}
	}

	if (fSenMessenger == NULL || !fSenMessenger->IsValid()) {
		PRINT(("failed to reach the SEN server.\n"));
		return false;
	}

	// prepare items with relation targets result from SEN
	BMessage request(fEntriesToOpen);
	RelationContextRef context = RelationContexts::Find(&fEntriesToOpen);
	if (context != NULL && fEntriesToOpen.what == sen::cmd::kRelationsGetSelf && !context->pluginConfig.IsEmpty()) {
		// the server need not look for the plugins again
		request.AddMessage(sensei::key::kPluginConfig, &context->pluginConfig);
	}
	status_t result = fSenMessenger->SendMessage(&request, fRelationTargetsReply);
	if (result != B_OK) {
		PRINT(("failed to communicate with SEN server: %s\n", strerror(result)));
		return false;
	}
	if (context != NULL && fEntriesToOpen.what == sen::cmd::kRelationsGetSelf) {
		// (the children of a node of a view, not the whole tree of the document)
		context->RestrictToNode(fRelationTargetsReply);
		// the whole tree of the contained relations, for the views that the items of this menu open
		context->SetRoot(*fRelationTargetsReply);
	}
	#ifdef DEBUG
		PRINT(("< got relation targets reply:\n"));
		fRelationTargetsReply->PrintToStream();
	#endif

	return true;
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
	SetTargetForItems(fMessenger);

	uint32 targets = 0;
	status_t result;

	switch(fRelationTargetsReply->what) {
		// fixme: this should be handled as sen::cmd::kReplyRelations but check there for self relation!
		case sensei::cmd::kResult:
		{
			// resolve self relations from SENSEI reply
			result = AddSelfRelationTargetItems(&targets);
			break;
		}
		case sen::cmd::kReplyRelations:
		{
			if (fEntriesToOpen.what == sen::cmd::kRelationsGetCompatibleTypes)
				result = AddCompatibleRelationTargetItems(&targets);
			else
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
	if (result != B_OK && result != B_NAME_NOT_FOUND) {	// ignore empty replies / missing relation items
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

// used for associations and creating new relation targets
status_t OpenRelationTargetsMenu::AddCompatibleRelationTargetItems(uint32* targetCount)
{
	status_t result;
	entry_ref srcRef;

	result = fEntriesToOpen.FindRef(sen::key::kSourceRef, &srcRef);
	if (result != B_OK) {
		PRINT(("failed to retrieve relation source ref: %s\n", strerror(result)));
		return result;
	}

	BString relationType;
	result = fEntriesToOpen.FindString(sen::key::kRelationType, &relationType);
	if (result != B_OK) {
		PRINT(("failed to retrieve relation type: %s\n", strerror(result)));
		return result;
	}

	// get matching meta template types and refs
	BMessage 	senAddRelationMsg;
	BMessenger  targetMessenger;
	BMessage 	matchingRefs;
	BStringList targetTypes;
	// for filtering out meta types / generic entitiy types below
	BStringList mimeIncludes;
	BStringList mimeExcludes;

	PRINT(("collecting compatible target types for relation %s...\n", relationType.String() ));

	// handle meta relations for associations
	// relation type is the classification relation and targetType is some classification type
	if (relationType == sen::mime::kAssociationRelation) {
		BString targetType;
		result = fEntriesToOpen.FindString(sen::key::kTargetType, &targetType);

		if (result != B_OK) {
			// targetType param is optional
			if (result != B_NAME_NOT_FOUND) {
				PRINT(("failed to retrieve target type: %s\n", strerror(result)));
				return result;
			}
		} else {
			targetTypes.Add(targetType);
		}

		PRINT(("collecting association targets for type %s...\n", targetType.String() ));

		// add a shortcut New <AssociationType> on top
		BMessage* newAssociationMsg = new BMessage(sen::cmd::kRelationsGetNewTarget);
		newAssociationMsg->AddRef(sen::key::kSourceRef, &srcRef);
		newAssociationMsg->AddString(sen::key::kRelationType, relationType);
		newAssociationMsg->AddString(sen::key::kTargetType, targetType);
		newAssociationMsg->AddMessenger("TrackerViewToken", fMessenger);

		BString newAssocLabel("New" B_UTF8_ELLIPSIS);

		IconMenuItem* newAssociationItem = new IconMenuItem(newAssocLabel.String(),
											   newAssociationMsg,
											   targetType);

		newAssociationItem->SetTarget(be_app_messenger);
		AddItem(newAssociationItem);

		AddSeparatorItem();

		senAddRelationMsg.what = sen::cmd::kRelationAdd;
		targetMessenger = BMessenger(sen::kServerSignature);

		// retrieve suitable classifications from SEN config
		BMessage senFindClassMsg(sen::cmd::kClassificationFind);
		senFindClassMsg.AddString(sen::key::kType, targetType);

		BMessage senFindClassReply;
		result = fSenMessenger->SendMessage(&senFindClassMsg, &senFindClassReply);

		if (result == B_OK) {
			PRINT(("got %d matching classification types from SEN:\n", senFindClassReply.CountNames(B_REF_TYPE) ));
			senFindClassReply.PrintToStream();

			entry_ref classRef;
			BString   classType;

			for (int i = 0; i < senFindClassReply.CountNames(B_REF_TYPE); i++) {
				classType = senFindClassReply.GetString("types", i, "");
				if (! classType.IsEmpty()) {
					result = senFindClassReply.FindRef("refs", i, &classRef);
					if (result == B_OK) {
						PRINT(("adding ref %s @%d.\n", classRef.name, i));
						matchingRefs.AddRef(classType.String(), &classRef);
					} else {
						PRINT(("  > error getting ref @%d: %s\n", i, strerror(result) ));
					}
				} else {
					PRINT(("  > could not get type for entry @%d, skipping\n", i));
				}
			}
		} else {
			PRINT(("failed to send find classification msg to SEN: %s\n", strerror(result)));
		}
	} else {
		// else, handle new relations from compatible templates
		PRINT(("%s is a normal relation, collecting all compatible templates except for META entities for relation.\n",
			relationType.String() ));

		senAddRelationMsg.what = sen::cmd::kRelationsGetNewTarget;
		senAddRelationMsg.AddMessenger("TrackerViewToken", fMessenger);
		targetMessenger = be_app_messenger;

		mimeIncludes.Add(targetTypes);
		mimeExcludes.Add(sen::mime::kClassificationSupertype);

		// here we got a list of compatible target types
		result = fRelationTargetsReply->FindStrings(sen::key::kTargetType, &targetTypes);
		if (result != B_OK) {
			if (result != B_NAME_NOT_FOUND) {	// param is optional
				PRINT(("failed to retrieve target types for relation: %s\n", strerror(result)));
				return result;
			}
		}

		int32 templatesCount = TemplateUtils::GetInstalledTemplates(NULL, &mimeIncludes, &mimeExcludes, &matchingRefs);

		if (templatesCount >= 0) {	// ok to find nothing
			PRINT(("got %d matching templates for relation %s:\n", templatesCount, relationType.String()));
			matchingRefs.PrintToStream();
		} else {
			PRINT(("failed to retrieve relation target refs for type %s: %s\n",
				relationType.String(), strerror(result)));

			return result;
		}
	}

	// for meta relations, we only need to add the association relation and are done
	// we got all matching meta entities for the given relation MIME type here, so just get the refs
	entry_ref destRef;
	int32	  refCount;
	char*     targetType;

	PRINT(("adding %d refs to OpenRelated target items...\n", matchingRefs.CountNames(B_REF_TYPE) ));

	for (int32 i = 0; i < matchingRefs.CountNames(B_REF_TYPE); i++) {
		result = matchingRefs.GetInfo(B_REF_TYPE, i, &targetType, NULL, &refCount);

		if (result == B_OK) {
			for (int32 r = 0; r < refCount; r++) {
				matchingRefs.FindRef(targetType, r, &destRef);

				PRINT(("adding ref %s for type %s at %d\n", destRef.name, targetType, r));

				BMessage* itemMsg = new BMessage(senAddRelationMsg);
				itemMsg->AddRef(sen::key::kSourceRef, &srcRef);
				itemMsg->AddRef(sen::key::kTargetRef, &destRef);
				itemMsg->AddString(sen::key::kRelationType, relationType);
				itemMsg->AddString(sen::key::kTargetType, targetType);

				IconMenuItem* item = new IconMenuItem(destRef.name,
													  itemMsg,
													  targetType);

				item->SetTarget(targetMessenger);
				AddItem(item);

				(*targetCount)++;
			}
		} else {
			PRINT(("could not resolve type #%d for source type %s: %s\n",
				i, targetType, strerror(result) ));
		}
	}
	return result;
}

status_t OpenRelationTargetsMenu::AddRelationTargetItems(uint32* targetCount)
{
	status_t result;
	PRINT(("adding relation menu target items...\n"));

	BString relationType;
	result = fEntriesToOpen.FindString(sen::key::kRelationType, &relationType);
	if (result != B_OK) {
		PRINT(("failed to retrieve relation type: %s\n", strerror(result)));
		return result;
	}

	BMessage relations;
	result = fRelationTargetsReply->FindMessage(sen::key::kRelations, &relations);
	if (result == B_NAME_NOT_FOUND) {
		// nothing to show: a document without bookmarks has no contained relations, that is not an error
		PRINT(("no relation targets in the result.\n"));
		return B_OK;
	}
	if (result != B_OK) {
		PRINT(("failed to retrieve relations from result: %s\n", strerror(result)));
		return result;
	}

	BMessage idToRef;
	result = fRelationTargetsReply->FindMessage(sen::key::kIdToRefMap, &idToRef);
	if (result != B_OK) {
		PRINT(("failed to retrieve ID/ref mapping for relations: %s\n", strerror(result)));
		return result;
	}
	// the names to show for the targets (optional: else the names of the files)
	BMessage idToName;
	fRelationTargetsReply->FindMessage(sen::key::kIdToNameMap, &idToName);

	// the items name the context of the menus, which has the config of the relation (kept once): if it is gone (it cannot be, the menu was
	// built from it a moment ago) the item brings its own
	BMessage 	relationConfigMap, relationConfig;
	int64 contextId = 0;
	bool hasContext = fEntriesToOpen.FindInt64(sen::key::kRelationContext, &contextId) == B_OK
		&& RelationContexts::Find(contextId) != NULL;
	if (!hasContext) {
		result = fRelationTargetsReply->FindMessage(sen::key::kRelationConfigMap, &relationConfigMap);
		if (result == B_OK)
			result = relationConfigMap.FindMessage(relationType.String(), &relationConfig);
	}

	char*       idKey;
    type_code   typeCode;
    int32       refCount;
	entry_ref 	ref;

	for (int i = 0; i < idToRef.CountNames(B_REF_TYPE); i++) {
		result = idToRef.GetInfo(B_REF_TYPE, i, &idKey, &typeCode, &refCount);
        if (result == B_OK && typeCode == B_REF_TYPE) {
			result = idToRef.FindRef(idKey, &ref);
			if (result == B_OK) {
				// this will create a menu item similar to folder items and launch with the preferred app,
				// which in this case is the associated relation handler.

				// add message with target ref and relation properties for the given ID
				BMessage itemMessage(sen::cmd::kOpenRelationTarget);
				itemMessage.AddRef(sen::key::kSourceRef, &ref);
				itemMessage.AddString(sen::key::kRelationType, relationType);

				// the config applicable to this item
				if (hasContext)
					RelationContexts::Tag(&itemMessage, contextId);
				else
					itemMessage.AddMessage(sen::key::kRelationConfig, &relationConfig);

				BMessage itemProps;
				result = relations.FindMessage(idKey, &itemProps);
				if (result == B_OK) {
					// add as arguments like with ARGV_RECEIVED but typed
					itemMessage.AddMessage(sen::key::kRelationProperties, &itemProps);
				}

				PRINT(("adding item message for relationt target with ID %s:\n", idKey));
				itemMessage.PrintToStream();

				BString name;
				if (idToName.FindString(idKey, &name) != B_OK || name.IsEmpty())
					name = ref.name;
				ModelMenuItem* item = new ModelMenuItem(new Model(&ref, true, true), name.String(), new BMessage(itemMessage));
				item->SetTarget(be_app_messenger);
				AddItem(item);

				(*targetCount)++;
			}
		}
		if (result != B_OK) {
			PRINT(("error adding target ref for relation: %s\n", strerror(result) ));
		}
	}	// for id_ref ...

	// cache relations in parent open relations menu item message itself,
	// so we can reuse it for the relation target view
	// (the item of this menu: FindItem() by the command would find the first of all relation types)
	BMenuItem* openRelationTargetsItem = Superitem();
	BMessage* openRelationTargetsItemMsg = openRelationTargetsItem != NULL ? openRelationTargetsItem->Message() : NULL;
	if (openRelationTargetsItemMsg != NULL) {
		openRelationTargetsItemMsg->RemoveName(sen::key::kRelations);	// the menu is reused
		openRelationTargetsItemMsg->AddMessage(sen::key::kRelations, &relations);

		PRINT(("openRelationTargetsItemMsg is:\n"));
		openRelationTargetsItemMsg->PrintToStream();
	}

	return result;
}

status_t
OpenRelationTargetsMenu::AddSelfRelationTargetItems(uint32* targetCount)
{
	status_t result;

	// the relation configs and the plugin config (for the default property type, with the attr mapping) are in the context that the parent
	// menu made; the items only name it
	RelationContextRef context = RelationContexts::Find(&fEntriesToOpen);
	if (context == NULL) {
		PRINT(("the context of the menu is gone, no relations to show.\n"));
		return B_NAME_NOT_FOUND;
	}
	int64 contextId;
	fEntriesToOpen.FindInt64(sen::key::kRelationContext, &contextId);

	BMessage relationConfigs(context->relationConfigs);
	BMessage pluginConfig(context->pluginConfig);

	if (pluginConfig.IsEmpty()) {
		// at least the attrMapping msg with common label mapping must be there
		PRINT(("could not get plugin config required for resolving self relations.\n"));
		return B_NAME_NOT_FOUND;
	}

	// get optional default type from pluginConfig
	BString defaultType;
	result = pluginConfig.FindString(sensei::key::kDefaultType, &defaultType);

	if (result != B_OK) {
		if (result != B_NAME_NOT_FOUND) {
			PRINT(("error reading message from OpenRelationsMenu: %s\n", strerror(result)));
			return result;
		}	// still continue and hope we get the type from items
	} else {
		PRINT(("adding SELF relation menu target items with default type %s...\n", defaultType.String()));
		fDefaultType = defaultType;
	}

	// check for expected relation nodes
	if (! fRelationTargetsReply->HasMessage(sen::key::kRelations)) {
		PRINT(("could not find any items in reply, skipping.\n"));
		return result;
	}

	// get ref to self from original refs received (source of relation)
	// Note: we expect only 1 ref for self relations, but let's keep it consistent across all relation types
	entry_ref ref;
	result = fEntriesToOpen.FindRef(sen::key::kSourceRef, &ref);
	if (result != B_OK) {
		PRINT(("failed to resolve self relation to source: %s\n", strerror(result)));
		return result;
	}

    // get number of data (i.e. relation) items in this message
	int32 relationCount;
	result = fRelationTargetsReply->GetInfo(sen::key::kRelations, NULL, &relationCount);

	PRINT(("* building self relation target menu items for %d relations...\n", relationCount));

	// process all items of this level
	for (int32 itemIndex = 0; itemIndex < relationCount; itemIndex++) {
		BString label, type;

		BMessage relationProperties;
		result = fRelationTargetsReply->FindMessage(sen::key::kRelations, itemIndex, &relationProperties);
		if (result != B_OK) {
			PRINT(("  x could not display item %d, skipping.\n", itemIndex));
			continue;	// try next
		}

		result = relationProperties.FindString(sen::attr::kRelationLabel, &label);
		if (result == B_OK || result == B_NAME_NOT_FOUND) {
			if (label.IsEmpty()) {
				PRINT(("could not get label for item %d: %s\n", itemIndex, strerror(result) ));
				label = "<no label>";	// should always be present but allow easier debugging
			}

			result  = relationProperties.FindString(sen::key::kRelationType, &type);	// optional
			if (result == B_OK || result == B_NAME_NOT_FOUND) {
				if (type.IsEmpty())
					type = fDefaultType;
			}

			result = B_OK;
		}

		IconMenuItem* item;

		// An item can be of a type of its own (the attributes that a type contains are of the type of an attribute), which is
		// not a relation and has no config: the relation is the one of the menu, the type only gives the icon.
		BString relationOfItem = relationConfigs.HasMessage(type.String()) || fDefaultType.IsEmpty() ? type : fDefaultType;

		// build menu item msg
		BMessage openRelationItemMsg;

		// add common relation properties
		openRelationItemMsg.AddRef(sen::key::kSourceRef, &ref);	// use standard refs as expected by Tracker
		openRelationItemMsg.AddString(sen::key::kRelationType, relationOfItem);
		RelationContexts::Tag(&openRelationItemMsg, contextId);

		// add as menu if there is a child node, else add as a plain menu item
		if (! relationProperties.HasMessage(sen::key::kRelations)) {
			PRINT(("    > adding self relation item [%d] '%s' of type '%s' with %d properties.\n",
					itemIndex, label.String(), type.String(), relationProperties.CountNames(B_ANY_TYPE) ));

			// will open the target item as SEN enriched ref in Tracker
			openRelationItemMsg.what = sen::cmd::kOpenRelationTarget;

			// (the config that is needed is the one of relationOfItem in the context: TrackerSen::HandleSenMessage looks it up)

			// add all properties of this relation item to be used as potential args by receiver
			openRelationItemMsg.AddMessage(sen::key::kRelationProperties, &relationProperties);

			item = new IconMenuItem(label.String(), new BMessage(openRelationItemMsg), type.String());

		} else {

			PRINT(("  * adding self relation menu [%d] '%s' of type '%s' with %d properties.\n",
					itemIndex, label.String(), type.String(), relationProperties.CountNames(B_ANY_TYPE) ));

			openRelationItemMsg.what = sen::cmd::kOpenRelationTargetView;

			// (the tree of all the relations that the view of this one is made from is in the context, found by its id)

			// get child node
			BMessage childNode;
			result = relationProperties.FindMessage(sen::key::kRelations, &childNode);
			ASSERT(result == B_OK);		// already checked above

			// conveniently add selected item from properties directly, too
			// TODO: handle sen::key::kItemId, too
			openRelationItemMsg.AddString(sen::key::kItemId, relationProperties.GetString(sensei::key::kItemId));

			// add all properties of this relation menu item to be used as potential args by receiver
			openRelationItemMsg.AddMessage(sen::key::kRelationProperties, &relationProperties);

			// now adapt childMsg for adding to menu below:
			// transparently handle just like a normal message result above, but for the subtree
			// so the submenu will have a SEN:relations and sen::key::kRelations from the childMsg subtree
			childNode.what = sensei::cmd::kResult;
			childNode.AddRef(sen::key::kSourceRef, &ref);
			childNode.AddString(sen::key::kRelationType, relationOfItem.String());
			// the configs and the tree are in the context, whatever the depth: a sub menu is only a node of the tree
			RelationContexts::Tag(&childNode, contextId);

			item = new IconMenuItem(
				new OpenRelationTargetsMenu(
					label.String(),
					new BMessage(childNode),
					fParentWindow,
					be_app_messenger),
				new BMessage(openRelationItemMsg),
				type.String()	// this is the MIME type of the relation, so take its icon
			);
		}  // for

		item->SetTarget(be_app_messenger);
		AddItem(item);

		(*targetCount)++;
	}	// for

	return B_OK;
}
