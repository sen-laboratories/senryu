/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
*/
#define DEBUG 1

#include "ContainerWindow.h"

#include "Attributes.h"
#include "AutoLock.h"
#include "Commands.h"
#include "FSUtils.h"
#include "IconMenuItem.h"
#include "OpenRelationTargetsMenu.h"
#include "Sen.h"
#include "Sensei.h"
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
	fParentWindow(parentWindow),
	fRelationTargetsReply(new BMessage())
{
	InitIconPreloader();

	SetFont(be_plain_font);
	SetTriggersEnabled(false);

	if (be_roster->IsRunning(SEN_SERVER_SIGNATURE)) {
		fSenMessenger = new BMessenger(SEN_SERVER_SIGNATURE);
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
		case SENSEI_MESSAGE_RESULT:	// we are a sub menu of self relations
		{
			PRINT(("  > building self relations submenu from SENSEI_MESSAGE_RESULT in existing sub item msg...\n"));
			fRelationTargetsReply = &fEntriesToOpen;
			return true;
		}
		case SEN_RELATIONS_GET_SELF:
		{
			PRINT(("building self relations submenu from SEN_RELATIONS_GET_SELF relation reply...\n"));
			break;
		}
		case SEN_RELATIONS_GET_COMPATIBLE_TYPES:
		{
			PRINT(("building relation targets submenu for compatible targets...\n"));
			break;
		}
		case SEN_RELATIONS_GET:
		{
			PRINT(("building relation targets submenu for existing targets...\n"));
			break;
		}
		default:
		{
			PRINT(("relation targets submenu: UNKNOWN/UNEXPECTED type, start building items...\n"));
		}
	}

	// prepare items with relation targets result from SEN
	status_t result = fSenMessenger->SendMessage(new BMessage(fEntriesToOpen), fRelationTargetsReply);
	if (result != B_OK) {
		PRINT(("failed to communicate with SEN server: %s\n", strerror(result)));
		return false;
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
		// fixme: this should be handled as SEN_RESULT_RELATIONS but check there for self relation!
		case SENSEI_MESSAGE_RESULT:
		{
			// resolve self relations from SENSEI reply
			result = AddSelfRelationTargetItems(&targets);
			break;
		}
		case SEN_RESULT_RELATIONS:
		{
			if (fEntriesToOpen.what == SEN_RELATIONS_GET_COMPATIBLE_TYPES)
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

	result = fEntriesToOpen.FindRef(SEN_RELATION_SOURCE_REF, &srcRef);
	if (result != B_OK) {
		PRINT(("failed to retrieve relation source ref: %s\n", strerror(result)));
		return result;
	}

	BString relationType;
	result = fEntriesToOpen.FindString(SEN_RELATION_TYPE, &relationType);
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
	if (relationType == SEN_ASSOC_RELATION_TYPE) {
		BString targetType;
		result = fEntriesToOpen.FindString(SEN_RELATION_TARGET_TYPE, &targetType);

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
		BMessage* newAssociationMsg = new BMessage(SEN_RELATIONS_GET_NEW_TARGET);
		newAssociationMsg->AddRef(SEN_RELATION_SOURCE_REF, &srcRef);
		newAssociationMsg->AddString(SEN_RELATION_TYPE, relationType);
		newAssociationMsg->AddString(SEN_RELATION_TARGET_TYPE, targetType);
		newAssociationMsg->AddMessenger("TrackerViewToken", fMessenger);

		BString newAssocLabel("New" B_UTF8_ELLIPSIS);

		IconMenuItem* newAssociationItem = new IconMenuItem(newAssocLabel.String(),
											   newAssociationMsg,
											   targetType);

		newAssociationItem->SetTarget(be_app_messenger);
		AddItem(newAssociationItem);

		AddSeparatorItem();

		senAddRelationMsg.what = SEN_RELATION_ADD;
		targetMessenger = BMessenger(SEN_SERVER_SIGNATURE);

		// retrieve suitable classifications from SEN config
		BMessage senFindClassMsg(SEN_CONFIG_CLASS_FIND);
		senFindClassMsg.AddString(SEN_MSG_TYPE, targetType);

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

		senAddRelationMsg.what = SEN_RELATIONS_GET_NEW_TARGET;
		senAddRelationMsg.AddMessenger("TrackerViewToken", fMessenger);
		targetMessenger = be_app_messenger;

		mimeIncludes.Add(targetTypes);
		mimeExcludes.Add(SEN_CLASS_SUPERTYPE);

		// here we got a list of compatible target types
		result = fRelationTargetsReply->FindStrings(SEN_RELATION_COMPATIBLE_TYPES, &targetTypes);
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
				itemMsg->AddRef(SEN_RELATION_SOURCE_REF, &srcRef);
				itemMsg->AddRef(SEN_RELATION_TARGET_REF, &destRef);
				itemMsg->AddString(SEN_RELATION_TYPE, relationType);
				itemMsg->AddString(SEN_RELATION_TARGET_TYPE, targetType);

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
	result = fEntriesToOpen.FindString(SEN_RELATION_TYPE, &relationType);
	if (result != B_OK) {
		PRINT(("failed to retrieve relation type: %s\n", strerror(result)));
		return result;
	}

	BMessage relations;
	result = fRelationTargetsReply->FindMessage(SEN_RELATIONS, &relations);
	if (result != B_OK) {
		PRINT(("failed to retrieve relations from result: %s\n", strerror(result)));
		return result;
	}

	BMessage idToRef;
	result = fRelationTargetsReply->FindMessage(SEN_ID_TO_REF_MAP, &idToRef);
	if (result != B_OK) {
		PRINT(("failed to retrieve ID/ref mapping for relations: %s\n", strerror(result)));
		return result;
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
				BMessage itemMessage(SEN_OPEN_RELATION_TARGET);
				itemMessage.AddRef(SEN_RELATION_SOURCE_REF, &ref);
				itemMessage.AddString(SEN_RELATION_TYPE, relationType);

				BMessage itemProps;
				result = relations.FindMessage(idKey, &itemProps);
				if (result == B_OK) {
					// add as arguments like with ARGV_RECEIVED but typed
					itemMessage.AddMessage(SEN_RELATION_PROPERTIES, &itemProps);
				}

				PRINT(("adding item message for relationt target with ID %s:\n", idKey));
				itemMessage.PrintToStream();

				ModelMenuItem* item = new ModelMenuItem(new Model(&ref, true, true), ref.name, new BMessage(itemMessage));
				item->SetTarget(be_app_messenger);
				AddItem(item);

				(*targetCount)++;
			}
		}
		if (result != B_OK) {
			PRINT(("error adding target ref for relation: %s\n", strerror(result) ));
		}
	}	// for id_ref ...

	// cache relations in oparent pen relations menu item message itself,
	// so we can reuse it for the relation target view
	BMenuItem *openRelationTargetsItem = Supermenu()->FindItem(SEN_OPEN_RELATION_TARGET_VIEW);
	ASSERT(openRelationTargetsItem != NULL);
	BMessage  *openRelationTargetsItemMsg = openRelationTargetsItem->Message();
	ASSERT(openRelationTargetsItemMsg != NULL);

	openRelationTargetsItemMsg->AddMessage(SEN_RELATIONS, &relations);

	PRINT(("openRelationTargetsItemMsg is:\n"));
	openRelationTargetsItemMsg->PrintToStream();

	return result;
}

status_t
OpenRelationTargetsMenu::AddSelfRelationTargetItems(uint32* targetCount)
{
	status_t result;

	// get relation configs for storing in menu items later
	BMessage relationConfigs;
	result = fEntriesToOpen.FindMessage(SEN_RELATION_CONFIG_MAP, &relationConfigs);

	if (result != B_OK) {
		PRINT(("no relation config found, continuing with defaults.\n"));
	}

	// get plug config for default property type from original msg received from parent menu
	BMessage pluginConfig;
	result = fEntriesToOpen.FindMessage(SENSEI_PLUGIN_CONFIG_KEY, &pluginConfig);

	if (result != B_OK) {
		// at least the attrMapping msg with common label mapping must be there
		PRINT(("could not get plugin config required for resolving self relations: %s\n", strerror(result) ));
		return result;
	}

	// get optional default type from pluginConfig
	BString defaultType;
	result = pluginConfig.FindString(SENSEI_DEFAULT_TYPE_KEY, &defaultType);

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
	if (! fRelationTargetsReply->HasMessage(SEN_RELATIONS)) {
		PRINT(("could not find any items in reply, skipping.\n"));
		return result;
	}

	// get ref to self from original refs received (source of relation)
	// Note: we expect only 1 ref for self relations, but let's keep it consistent across all relation types
	entry_ref ref;
	result = fEntriesToOpen.FindRef(SEN_RELATION_SOURCE_REF, &ref);
	if (result != B_OK) {
		PRINT(("failed to resolve self relation to source: %s\n", strerror(result)));
		return result;
	}

    // get number of data (i.e. relation) items in this message
	int32 relationCount;
	result = fRelationTargetsReply->GetInfo(SEN_RELATIONS, NULL, &relationCount);

	PRINT(("* building self relation target menu items for %d relations...\n", relationCount));

	// process all items of this level
	for (int32 itemIndex = 0; itemIndex < relationCount; itemIndex++) {
		BString label, type;

		BMessage relationProperties;
		result = fRelationTargetsReply->FindMessage(SEN_RELATIONS, itemIndex, &relationProperties);
		if (result != B_OK) {
			PRINT(("  x could not display item %d, skipping.\n", itemIndex));
			continue;	// try next
		}

		result = relationProperties.FindString(SEN_RELATION_LABEL_ATTR, &label);
		if (result == B_OK || result == B_NAME_NOT_FOUND) {
			if (label.IsEmpty()) {
				PRINT(("could not get label for item %d: %s\n", itemIndex, strerror(result) ));
				label = "<no label>";	// should always be present but allow easier debugging
			}

			result  = relationProperties.FindString(SEN_RELATION_TYPE, &type);	// optional
			if (result == B_OK || result == B_NAME_NOT_FOUND) {
				if (type.IsEmpty())
					type = fDefaultType;
			}

			result = B_OK;
		}

		IconMenuItem* item;

		// build menu item msg
		BMessage openRelationItemMsg;

		// add common relation properties
		openRelationItemMsg.AddRef(SEN_RELATION_SOURCE_REF, &ref);	// use standard refs as expected by Tracker
		openRelationItemMsg.AddString(SEN_RELATION_TYPE, type);

		// add all properties of this relation item to be used as potential args by receiver
		openRelationItemMsg.AddMessage(SEN_RELATION_PROPERTIES, &relationProperties);

		// add as menu if there is a child node, else add as a plain menu item
		if (! relationProperties.HasMessage(SEN_RELATIONS)) {
			PRINT(("    > adding self relation item [%d] '%s' of type '%s' with %d properties.\n",
					itemIndex, label.String(), type.String(), relationProperties.CountNames(B_ANY_TYPE) ));

			// will open the target item as SEN enriched ref in Tracker
			openRelationItemMsg.what = SEN_OPEN_RELATION_TARGET;
			// here we know which config we need, so pass only selected type
			BMessage selectedConfig;
			result = relationConfigs.FindMessage(type, &selectedConfig);
			if (result == B_OK) {
				relationProperties.AddMessage(SEN_RELATION_CONFIG, &selectedConfig);
			} else {
				if (result != B_NAME_NOT_FOUND) {
					PRINT(("    x failed to inspect relation configs: %s\n", strerror(result) ));
				}
				result = B_OK;
			}

			item = new IconMenuItem(label.String(), new BMessage(openRelationItemMsg), type.String());

		} else {

			PRINT(("  * adding self relation menu [%d] '%s' of type '%s' with %d properties.\n",
					itemIndex, label.String(), type.String(), relationProperties.CountNames(B_ANY_TYPE) ));

			openRelationItemMsg.what = SEN_OPEN_RELATION_TARGET_VIEW;

			// keep track of the relations root for building entire structure (i.e. in Tracker folder view)
			BMessage* relationRoot;

			// root is handed through via menu message
			result = Superitem()->Message()->FindPointer(SEN_RELATION_ROOT, reinterpret_cast<void**>(&relationRoot));

			if (result == B_OK && relationRoot != NULL) {
				PRINT(("  * got relation ROOT, handing down.\n"));
				openRelationItemMsg.AddPointer(SEN_RELATION_ROOT, relationRoot);
			}
			else {
				PRINT(("  * SET relation ROOT.\n"));
				openRelationItemMsg.AddPointer(SEN_RELATION_ROOT, reinterpret_cast<void*>(fRelationTargetsReply));

				// save for later below
				relationRoot = fRelationTargetsReply;
			}

			// get child node
			BMessage childNode;
			result = relationProperties.FindMessage(SEN_RELATIONS, &childNode);
			ASSERT(result == B_OK);		// already checked above

			// conveniently add selected item from properties directly, too
			openRelationItemMsg.AddString(SEN_RELATION_ITEM_ID, relationProperties.GetString(SENSEI_ITEM_ID));

			// now adapt childMsg for adding to menu below:
			// transparently handle just like a normal message result above, but for the subtree
			// so the submenu will have a SEN:relations and SEN_RELATIONS from the childMsg subtree
			childNode.what = SENSEI_MESSAGE_RESULT;
			childNode.AddRef(SEN_RELATION_SOURCE_REF, &ref);
			childNode.AddString(SEN_RELATION_TYPE, type.String());
			childNode.AddPointer(SEN_RELATION_ROOT, reinterpret_cast<void*>(relationRoot));

			childNode.AddMessage(SENSEI_PLUGIN_CONFIG_KEY, &pluginConfig);
			// we need all configs here to select in subtree
			// TODO: optimize by constraining to single relation per menu or use AddPointer to root config!
			childNode.AddMessage(SEN_RELATION_CONFIG_MAP, &relationConfigs);

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
