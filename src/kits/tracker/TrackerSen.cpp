/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
*/

#define DEBUG 1

#include <Debug.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Message.h>
#include <NodeInfo.h>
#include <Query.h>
#include <Roster.h>
#include <StringList.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <fs_attr.h>

#include "Commands.h"
#include "FSUtils.h"

#include "Sen.h"
#include "Sensei.h"
#include "Tracker.h"

bool
TTracker::HandleSenMessage(BMessage* message)
{
	if (message->what == SEN_OPEN_RELATION_TARGET_VIEW) {
		// Note: we need to differentiate between invoking the menu (to open targets in a Tracker relation view)
		//       vs invoking the relation from the menu itself
		if ((modifiers() & B_OPTION_KEY) != 0) {
			// handle as normal ref to be opened through SEN_OPEN_RELATION_TARGET
			// (intercepted to be enriched with SEN relation properties as args)
			message->what = SEN_OPEN_RELATION_TARGET;
			PRINT(("TrackerSen: switch from open menu -> open target.\n"));
		}
	}

	switch (message->what) {
		case kNewAssociation:
			PRINT(("TrackerSen::AssociateWith()  called\n"));
			break;
		case kOpenRelations:				// menu itself was invoked, adjust command for later processing below
		case kOpenSelfRelations: {			// fallthrough
			PRINT(("TrackerSen::Open top-level relation view.\n"));
			message->what = SEN_OPEN_RELATION_VIEW;
			break;
		}
		case SEN_OPEN_RELATION_TARGET: {
			PRINT(("TrackerSen::SEN_OPEN_RELATION_TARGET\n"));

			// get SEN relation type, if the msg comes from SEN it is required.
			BString relationType;
			status_t result = message->FindString(SEN_RELATION_TYPE, &relationType);

			if (result != B_OK) {
				PRINT(("could not find SEN relationType, aborting: %s",
					strerror(result) ));

				return true;	// we are done
			}

			entry_ref srcRef;
			entry_ref targetRef;
			entry_ref senHandlerRef;

			result = message->FindRef(SEN_RELATION_SOURCE_REF, &srcRef);
			if (result != B_OK) {
				PRINT(("could not find source ref, aborting: %s",
					strerror(result) ));

				return true;	// we are done
			}

			BMessage argsMsg;

			// get relation config - TODO: move to func
			BMessage relationConfigs, relationConf;
			result = message->FindMessage(SEN_RELATION_CONFIG, &relationConfigs);
			if (result == B_OK)
				result = relationConfigs.FindMessage(relationType.String(), &relationConf);

			if (result != B_OK) {
				PRINT(("could not get relation config for type %s: %s\n", relationType.String(), strerror(result) ));
				return true;	// abort
			}

			bool selfRelation = relationConf.GetBool(SEN_RELATION_IS_SELF, false);

			PRINT(("got relation config for type %s (is %s)\n",
				relationType.String(), (selfRelation ? "SELF" : "NORMAL") ));

			relationConf.PrintToStream();

			if (selfRelation) {
				PRINT(("resolving SELF relation...\n"));

				// pass on attributes from self relation properties
				result = message->FindMessage(SEN_RELATION_PROPERTIES, &argsMsg);
				if (result != B_OK) {
					if (result != B_NAME_NOT_FOUND) {
						PRINT(("error getting relf relation arguments from refs msg: %s\n", strerror(result)));
						return true;
					}
				}

				// get SEN relation handler for navigation from relation type's default app
				BMimeType senHandlerMime(relationType);
				if (! senHandlerMime.IsValid()) {
					PRINT(("error accessing MIME type for relation %s, opening as normal ref.\n", relationType.String()));
					return true;
				}

				char prefAppSig[B_MIME_TYPE_LENGTH];
				result = senHandlerMime.GetPreferredApp(prefAppSig);
				if (result != B_OK) {
					//todo: have SEN search for supporting plugins and let user choose, then set as preferred app
					//      like OpenWith behavior.
					PRINT(("could not find preferred app for handling relation %s: %s\n",
							relationType.String(), strerror(result)));

					return true;
				}

				result = be_roster->FindApp(prefAppSig, &senHandlerRef);
				if (result != B_OK) {
					PRINT(("could not resolve relation handler with signature %s, falling back to normal launch.\n",
							relationType.String()));

					return true;
				}

				// self relation has target == source ref
				targetRef = srcRef;
			} else {	// normal relation
				// and pass on parameters from attribute properties
				BString srcId, targetId;

				if (ResolveRelation(&srcRef, &srcId, &targetId)) {
					PRINT(("resolved SEN Relation target %s for ref %s\n", targetId.String(), srcRef.name));

					result = PrepareLaunchTarget(&srcRef, targetId.String(), &targetRef, &argsMsg);
					if (result != B_OK) {
						PRINT(("failed to resolve relation target for ref %s: %s\n", srcRef.name, strerror(result)));
						return true;
					}
					// get default app which should be a SEN relation navigator
					result = be_roster->FindApp(&srcRef, &senHandlerRef);
					if (result != B_OK) {
						PRINT(("failed to find default app for ref %s: %s\n", srcRef.name, strerror(result)));
						return true;
					}
				} else {
					PRINT(("could not resolve relation for ref %s.\n", srcRef.name));
				}
			}

			PRINT(("opening relation target %s for srcRef %s with SEN navigator %s\n",
				targetRef.name, srcRef.name, senHandlerRef.name));

			// open as normal refs with SEN relation handler and pass in targetRef as argument
			message->what = B_REFS_RECEIVED;
			message->RemoveName(SEN_RELATION_SOURCE_REF);
			message->AddRef("refs", &targetRef);

			TrackerLaunch(&senHandlerRef, message, true);

			return true;
		}
		case SEN_OPEN_RELATION_TARGET_VIEW:	{ // coming from the (sub)menu actions
			PRINT(("TrackerSen::Open (Self) Relations as target view.\n"));
			break;
		}
		case SEN_RELATIONS_GET_NEW_TARGET:
			PRINT(("TrackerSen::get NEW target template called.\n"));
			break;

		case SENSEI_CMD_EXTRACT:
			PRINT(("TrackerSen::SENSEI extract called.\n"));
			break;

		case SENSEI_CMD_ENRICH:
			PRINT(("TrackerSen::SENSEI enrich called.\n"));
			break;

		case SENSEI_CMD_IDENTIFY:
			PRINT(("TrackerSen::SENSEI identify called.\n"));
			break;

		case SENSEI_CMD_NAVIGATE:
			PRINT(("TrackerSen::SENSEI navigate called.\n"));
			break;

		default:
			// not a SEN command message, handle as normal
			return false;
	}

	// handle SEN command messages
	status_t result = B_OK;
	entry_ref relationDirRef;

	switch (message->what) {
		case SEN_OPEN_RELATION_VIEW:
		{
			result = PrepareRelationFolder(message, &relationDirRef);
			break;
		}
		case SEN_OPEN_RELATION_TARGET_VIEW:
		{
			result = PrepareRelationTargetFolder(message, &relationDirRef);
			break;
		}
		case SEN_RELATIONS_GET_NEW_TARGET:
		{
			BString relationType;
			result = message->FindString(SEN_RELATION_TYPE, &relationType);
			if (result != B_OK) {
				PRINT(("could not find relation type: %s\n", strerror(result) ));
				return true;	// abort
			}

			BString targetType;
			result = message->FindString(SEN_RELATION_TARGET_TYPE, &targetType);
			if (result != B_OK) {
				PRINT(("could not find target type: %s\n", strerror(result) ));
				return true;	// abort
			}

			entry_ref sourceRef;
			result = message->FindRef(SEN_RELATION_SOURCE_REF, &sourceRef);
			if (result != B_OK) {
				PRINT(("could not get source ref: %s\n", strerror(result) ));
				return true;	// abort
			}

			entry_ref targetRef;
			result = message->FindRef(SEN_RELATION_TARGET_REF, &targetRef);

			if (result != B_OK) {
				if (result == B_NAME_NOT_FOUND && targetType.StartsWith(SEN_CLASS_SUPERTYPE "/")) {
					result = CreateNewAssociationEntity(targetType.String(), &targetRef);

					if (result == B_OK) {
						PRINT(("adding new association entity instance '%s' for association '%s' of type '%s'\n",
						BPath(&targetRef).Path(), relationType.String(), targetType.String() ));

						// show in target location (SEN context config) in Tracker for editing name
						EditNewEntity(&targetRef);

						// add a relation to new or existing association meta entity
						PRINT(("adding relation '%s' to entity instance '%s' of type %s\n",
							relationType.String(), BPath(&targetRef).Path(), targetType.String() ));

						// send as SEN scripting message to add relation of desired type
						BMessage addRelationMsg(SEN_RELATION_ADD);
						addRelationMsg.AddString(SEN_RELATION_TYPE, relationType);
						addRelationMsg.AddString(SEN_RELATION_TARGET_TYPE, targetType);
						addRelationMsg.AddRef(SEN_RELATION_SOURCE_REF, &sourceRef);
						addRelationMsg.AddRef(SEN_RELATION_TARGET_REF, &targetRef);

						addRelationMsg.PrintToStream();

						BMessenger senMsgr(SEN_SERVER_SIGNATURE);
						if (senMsgr.IsValid()) {
							senMsgr.SendMessage(&addRelationMsg);
						} else {
							PRINT(("could not reach sen_server.\n"));
						}
					} else {
						return true;	// bail out
					}
				} else {
					PRINT(("could not get target ref: %s\n", strerror(result) ));
					return true;	// abort
				}
			} else { // new from template with existing targetRef
				// forward to Tracker like "New from Template"
				message->what = kNewEntryFromTemplate;
				message->AddRef("refs_template", &targetRef);

				// redirect new templates message and finish processing
				if (message->what == kNewEntryFromTemplate) {
					BMessenger poseViewMsgr;
					result = message->FindMessenger("TrackerViewToken", &poseViewMsgr);
					if (result == B_OK) {
						// redirect back to original PoseView to create new
						// relation target just like new file from Template
						// Note: relation has to be added there, as our targetRef
						//       is copied to a new file first.
						poseViewMsgr.SendMessage(message);
					} else {
						PRINT(("could not get PostView messenger, cannot forward as 'create new from temmplate': %s\n",
						strerror(result) ));
					}
				}
			}
			// done
			return true;
		}
		default:
		{
			PRINT(("unknown SEN message %u, ignoring.\n", message->what));
			return false;
		}
	}
	if (result == B_OK) {
		// done handling message, send as refs to Tracker for opening relation view
		BMessage* trackerOpenDir = new BMessage(B_REFS_RECEIVED);
		trackerOpenDir->AddRef("refs", &relationDirRef);

		PRINT(("open Tracker relation dir:\n"));
		trackerOpenDir->PrintToStream();

		be_app_messenger.SendMessage(trackerOpenDir);
	} else {
		PRINT(("error opening relation view: %s\n", strerror(result)));
	}

	// just indicates we are done and no further processing by the caller is needed.
	return true;
}

status_t TTracker::CreateNewAssociationEntity(const char* associationEntityType, entry_ref* targetRef)
{
	// create a new association/meta entity instance for the given target (association) type
	BMessage msgGetClassEntity(SEN_CONFIG_CLASS_ADD);
	msgGetClassEntity.AddString(SEN_MSG_TYPE, associationEntityType);

	BString newClass("New ");
	BMimeType classMime(associationEntityType);
	status_t result = classMime.InitCheck();
	if (result == B_OK) {
		char label[B_MIME_TYPE_LENGTH];
		classMime.GetShortDescription(label);
		newClass << label;
	} else {
		PRINT(("could not get MIME type for target type %s: %s\n", associationEntityType, strerror(result) ));
		newClass << "(Unknown Classification Type)";
	}
	msgGetClassEntity.AddString(SEN_MSG_NAME, newClass);

	BMessage msgClassReply;
	BMessenger senMsgr(SEN_SERVER_SIGNATURE);

	result = senMsgr.SendMessage(&msgGetClassEntity, &msgClassReply);
	status_t status = msgClassReply.GetInt32("status", B_OK);

	if (result != B_OK || status != B_OK) {
		PRINT(("error creating new ref from type %s: %s, return status was: %s\n",
			associationEntityType, strerror(result), strerror(status) ));

		return true;
	}

	PRINT(("got reply from create classification:\n"));
	msgClassReply.PrintToStream();

	result = msgClassReply.FindRef("refs", targetRef);
	if (result == B_OK) {
		PRINT(("got new meta entity instance '%s' of type '%s' in %s.\n",
			targetRef->name, associationEntityType, BPath(targetRef).Path() ));
	} else {
		PRINT(("could not find ref for new classification entity in reply: %s\n", strerror(result) ));
	}

	return result;
}

status_t TTracker::EditNewEntity(const entry_ref* ref)
{
	// get parent dir of association entity in SEN config path
	BEntry classEntry(ref);
	BEntry classDirEntry;

	status_t result = classEntry.GetParent(&classDirEntry);

	if (result == B_OK) {
		// switch to new PoseView in context config of the newly created item
		if (result == B_OK) {
			entry_ref classDirRef;
			result = classDirEntry.GetRef(&classDirRef);

			node_ref nodeToSelect;
			if (result == B_OK)
				result = classEntry.GetNodeRef(&nodeToSelect);

			if (result == B_OK) {
				// send to Tracker and open the relevant context config directory
				BMessage message(B_REFS_RECEIVED);
				message.AddRef("refs", &classDirRef);

				// select and start editing the newly created item
				result = message.AddData("nodeRefToSelect", B_RAW_TYPE,
										(const void*) &nodeToSelect, sizeof(node_ref));
				// same node, easier to work with existing Tracker OpenRef this way
				if (result == B_OK)
					result = message.AddData("nodeRefToEdit", B_RAW_TYPE,
										(const void*) &nodeToSelect, sizeof(node_ref));

				if (result == B_OK) {
					// post to Tracker as refs_received
					return be_app->PostMessage(&message);
				}
			}
		}
	}

	return result;
}

bool TTracker::ResolveRelation(const entry_ref* ref, BString* srcId, BString* targetId)
{
	status_t result;
	BNode relationNode(ref);
	BNodeInfo relationNodeInfo(&relationNode);

	result = relationNodeInfo.InitCheck();
	if (result != B_OK) {
		PRINT(("error accessing nodeInfo for relation target %s: %s\n", ref->name, strerror(result)));
		return false;
	}

	if (result == B_OK) result = relationNode.ReadAttrString(SEN_RELATION_SOURCE_ATTR, srcId);
	if (result == B_NAME_NOT_FOUND) return false;

	if (result == B_OK) result = relationNode.ReadAttrString(SEN_RELATION_TARGET_ATTR, targetId);
	if (result == B_NAME_NOT_FOUND) return false;

	return (result == B_OK);
}

status_t TTracker::PrepareLaunchTarget(
	const entry_ref* srcRef, const char* targetId, entry_ref* targetRef, BMessage* params)
{
	// get relation target by ID from SEN server
	BMessenger senMessenger(SEN_SERVER_SIGNATURE);
	BMessage queryTargetIdMsg(SEN_QUERY_ID);
	queryTargetIdMsg.AddString(SEN_ID_ATTR, targetId);

	BMessage reply;
	senMessenger.SendMessage(&queryTargetIdMsg, &reply);

	status_t result = reply.FindRef("ref", targetRef);
	if (result == B_OK) {
		PRINT(("got target ref %s\n", targetRef->name));
		result = ConvertAttributesToMessage(srcRef, params);
	} else {
		ERROR("failed to get ref from reply: %s\n", strerror(result));
	}
	return result;
}

status_t
TTracker::PrepareRelationFolder(BMessage *message, entry_ref* relationDirRef)
{
	entry_ref srcRef;

	status_t result = message->FindRef(SEN_RELATION_SOURCE_REF, &srcRef);
	if (result != B_OK) {
		PRINT(("missing required parameter %s !\n", SEN_RELATION_SOURCE_REF ));
		return result;
	}

    // check relations
	BStringList relations;
	if (message->FindStrings(SEN_RELATIONS, &relations) != B_OK) {
		// TODO: add default relations from MIME DB so users can add targets!
		PRINT(("no relations to show.\n"));
		return B_OK;
	}

	int32 countRelations = relations.CountStrings();
	PRINT(("got %d relations\n", countRelations) );

	BMessage relationConfigs;
	result = message->FindMessage(SEN_RELATION_CONFIG, &relationConfigs);
	if (result != B_OK) {
		PRINT(("could not get relation config: %s\n", strerror(result) ));
		return result;
	}

	// create SEN relation folders of relation type, relations are expected to be unique here
	BMessage relationConf;

	for (int rel = 0; rel < countRelations; rel++) {
		const char* relationType = relations.StringAt(rel).String();

		// set name and possibly label from config
		result = relationConfigs.FindMessage(relationType, &relationConf);
		if (result != B_OK) {
			PRINT(("could not find relation config for type %s, skipping.\n", relationType));
			continue;
		}
		result = CreateRelationDirectory(&srcRef, relationType, &relationConf, relationDirRef);
		if ((result != B_OK)) {
			PRINT(("could not create directory for relation %s: %s\n", relationType, strerror(result)));
			return result;
		}
	}

	// return parent dir that holds all relations
	BEntry relationDirEntry(relationDirRef);
	BEntry rootRelationDirEntry;

	result = relationDirEntry.GetParent(&rootRelationDirEntry);
	if (result == B_OK) {
		result = rootRelationDirEntry.GetRef(relationDirRef);
	}

	return result;
}

status_t
TTracker::PrepareRelationTargetFolder(BMessage *message, entry_ref* relationDirRef)
{
	entry_ref srcRef;

	status_t result = message->FindRef(SEN_RELATION_SOURCE_REF, &srcRef);
	if (result != B_OK) {
		PRINT(("missing required parameter %s !\n", SEN_RELATION_SOURCE_REF ));
		return result;
	}

	const char* relationType;
	result = message->FindString(SEN_RELATION_TYPE, &relationType);
	if (result != B_OK) {
		PRINT(("missing required parameter %s !\n", SEN_RELATION_TYPE ));
		return result;
	}

	BMessage relationProperties;
	message->FindMessage(SEN_RELATION_PROPERTIES, &relationProperties);

	// get config for relation
	BMessage relationConf;
	result = message->FindMessage(SEN_RELATION_CONFIG, &relationConf);
	if (result != B_OK) {
		PRINT(("could not get relation config for type %s: %s\n", relationType, strerror(result) ));
		return result;
	}

	// add type to config for proccessing
	relationConf.AddString(SEN_RELATION_TYPE, relationType);

	bool isSelf    = relationConf.GetBool(SEN_RELATION_IS_SELF);
	bool isDynamic = relationConf.GetBool(SEN_RELATION_IS_DYNAMIC);

	PRINT(("relation %s is %s and %s.\n", relationType,
		isSelf ? "reflexive" : "normal",
		isDynamic ? "dynamic": "static"));

	// get SEN:ID of source for normal relations, or just a dummy SELF_ID for self relations
	BString srcId;

	if (isSelf) {
		srcId = SEN_ID_SELF;
	} else {
		srcId = message->GetString(SEN_RELATION_SOURCE_ID);
		if (srcId == NULL) {
			PRINT(("could not get relation srcId for type %s\n", relationType ));
			return B_BAD_DATA;
		}
	}
	// add to config for proccessing
	relationConf.AddString(SEN_RELATION_SOURCE_ID, srcId);

	BMessage relations;

	if (isSelf) {
		// get all relations for creating complete relation structure, but open only selected relation view later
		BMessage* relationRoot;
		result = message->FindPointer(SEN_RELATION_ROOT, reinterpret_cast<void**>(&relationRoot));
		if (result == B_OK && relationRoot != NULL) {
			PRINT(("  * got relation ROOT, generating view for ALL relations of source.\n"));
		} else {
			PRINT(("  X failed to get expected relation ROOT, aborting.\n"));
			if (result != B_OK)
				return result;
			else
				return B_BAD_VALUE;
		}

		// get plugin config for mappings
		BMessage pluginConfig;
		result = message->FindMessage(SENSEI_PLUGIN_CONFIG_KEY, &pluginConfig);
		if (result != B_OK) {
			PRINT(("could not get plugin config required for resolving self relations: %s\n", strerror(result) ));
			return result;
		}

		// convert to common relations map with targetId (all point to source for self relation) and
		// properties mapped to MIME type attribute names, using the type_mapping provided by SENSEI
		BMessage typeMapping;
		pluginConfig.FindMessage(SENSEI_TYPE_MAPPING, &typeMapping);

		// same for attributes (e.g. page -> SEN:REL:page)
		BMessage attrMapping;
		pluginConfig.FindMessage(SENSEI_ATTR_MAPPING, &attrMapping);

		// get root node item message
		result = relationRoot->FindMessage(SENSEI_ITEM, &relations);

		if (result != B_OK) {
			if (result != B_NAME_NOT_FOUND) {
				PRINT(("error looking for child node in message: %s\n", strerror(result)));
				return result;
			} else {
				// return an empty property message for the self relation
				PRINT(("no self relations received.\n"));
				relations.AddMessage(SEN_ID_SELF, &relations);

				return B_OK;
			}
		}

		// get selected item level
		const char* itemPath = message->GetString(SENSEI_PATH, "");
		PRINT(("* got item path %s.\n", itemPath));
		relationConf.AddString(SENSEI_PATH, itemPath);

		// self relations target points to source
		result = ConvertSelfRelationsToCommon(srcId.String(), &relations, &typeMapping, &attrMapping);
		if (result != B_OK) {
			PRINT(("could not convert from plugin result to self relations: %s\n", strerror(result) ));
			return result;
		}

		PRINT(("successfully converted plugin result to self relations:\n"));

	} else {
		message->FindMessage(SEN_RELATIONS, &relations);
		PRINT(("got relations for type %s for source %s:\n", relationType, srcRef.name) );
	}

	// get ID lookup map
	BMessage idToRefMap;
	result = message->FindMessage(SEN_ID_TO_REF_MAP, &idToRefMap);

	if (! isSelf) {
		if (result != B_OK) {
			ERROR("could not get ref mapping for source %s: %s\n", srcRef.name, strerror(result));
			return result;
		}
	} else {
		idToRefMap.AddRef(SEN_ID_SELF, &srcRef);
	}

	// create top-level relation dir for src relation
	result = CreateRelationDirectory(&srcRef, relationType, &relationConf, relationDirRef);
	if ((result != B_OK)) {
		PRINT(("could not create relation target folder: %s\n", strerror(result)));
		return result;
	}

	// reusable optionally recursive part
	result = WriteTargetRelations(&relations, &idToRefMap, &relationConf, NULL, relationDirRef);
	if (result != B_OK) {
		PRINT(("could not write relation targets to folder %s: %s\n", relationDirRef->name, strerror(result) ));
		return result;
	}

	return result;
}

status_t TTracker::WriteTargetRelations(
	BMessage  *relations,
	BMessage  *idToRefMap,
	BMessage  *relationConf,
	entry_ref *workingDirRef,
	entry_ref *openDirRef)
{
	if (relations->IsEmpty() != B_OK) {
		PRINT(("no relations found, skipping.\n"));
		return B_OK;
	}

	if (workingDirRef == NULL) {	// start at root relation dir created before
		PRINT(("WriteTargetRelations: starting at ROOT relation path: %s\n", openDirRef->name ));
		workingDirRef = new entry_ref(*openDirRef);
	}

	bool isDynamic = relationConf->GetBool(SEN_RELATION_IS_DYNAMIC);
	const char* srcId = relationConf->GetString(SEN_RELATION_SOURCE_ID);
	const char* shortName = relationConf->GetString(SEN_RELATION_NAME);
	const char* relationType = relationConf->GetString(SEN_RELATION_TYPE);

	BDirectory relationDir(workingDirRef);
	BPath relationDirPath(workingDirRef);

	status_t result = relationDir.InitCheck();
	if (result == B_OK)
		result = relationDirPath.InitCheck();

	if (result != B_OK) {
		PRINT(("invalid directory for path %s: %s\n", relationDirPath.Path(), strerror(result) ));
		return result;
	}

	// for dynamic relations, check if current working dir is the selected target we want to open later
	BString currentPath;

	if (isDynamic) {
		relations->FindString(SENSEI_PATH, &currentPath);
		if (currentPath.IsEmpty()) {
			PRINT(("  * init root path."));
			currentPath = "/0";
		}

		BString openPath;
		relationConf->FindString(SENSEI_PATH, &openPath);

		PRINT(("  > check if path is the one to open: %s (current) <-> %s (selected)\n",
				currentPath.String(), openPath.String() ));

		if (currentPath == openPath) {
			// update open ref so caller knows what directory the user expects to enter
			*openDirRef = *workingDirRef;
		}
		// done, don't confuse later processing as it's just an internal property
		relations->RemoveName(SENSEI_PATH);
	}

	PRINT(("processing relation dir '%s'...\n", relationDirPath.Path() ));

	// all relations of a particular target (possibly nested)
	BMessage targetRelations;

	// iterate through all target IDs and get relation properties for each, then populate relation targets dir
    // with matching target refs, creating files with relation properties in file attributes.
    for (int32 targetIndex = 0; targetIndex < relations->CountNames(B_MESSAGE_TYPE); targetIndex++) {
		entry_ref ref;
		char* targetId;
		int32 countRelations;	// there can be multiple relations for the same target

		result = relations->GetInfo(B_MESSAGE_TYPE, targetIndex, &targetId, NULL, &countRelations);
		if (result == B_OK) {
			result = idToRefMap->FindRef(targetId, &ref);
		} else {
			PRINT(("could not get info for target at index %d: %s.\n", targetIndex, strerror(result) ));
			continue;	// better luck next time?
		}
		if (result != B_OK) {
			PRINT(("no valid targetId found / unexpected type for targetId %s at index %d: %s.\n",
				targetId, targetIndex, strerror(result) ));
			continue;	// better luck next time?
		}

		PRINT(("\n*** creating relation files for source '%s' with targetId %s and %d relations...\n",
				ref.name, targetId, countRelations));

		// get properties for individual relation to the loop's targetId
		// Note: there might be more than one relation to the same target with different properties!
		BNode	  relationNode;
		BNodeInfo relationNodeInfo;
        BMessage  properties;

		// create individual relation targets for each relation of the current target
        for (int32 relationIndex = 0; relationIndex < countRelations; relationIndex++) {
			result = relations->FindMessage(targetId, relationIndex, &properties);

			if (result != B_OK) {
				PRINT(("  > could not get relation properties for target %s at %d: %s\n",
					targetId, relationIndex, strerror(result) ));
				continue;
			}

			// used for self (and later also n-ary) relations
			bool createDirectory = properties.HasMessage(SEN_RELATIONS);

			// use relation's short name as file name
			BString fileName(shortName);

			// number file names, they are not really important since we prefer the label anyway
			if (relationIndex > 0) {
				// human readable 1-based index, following the first relation #0 (without index)
				fileName << " #" << relationIndex + 1;
			}

			// create a file for each set of properties for all targets
			// since dynamic relations are generated, they cannot be changed
			// Note: Haiku is single user so perms have to apply to root, too
			mode_t readWriteMode = (isDynamic ? 0555 : 0777);

			// create file or dir with appropriate permissions
			if (createDirectory) {
				BString subDirLoc(relationDirPath.Path());
				BString subDirName(fileName);    // must already be unique from indexing above, using as folder name only
				subDirName << " Relations";

				// add optional custom path fragment if there (for more structure e.g. with nested / self relations)
				if (! currentPath.IsEmpty()) {
					subDirLoc << currentPath.String();	// always leading '/' but no trailing '/'
				} else {
					// if not, just use relation file name from above as dir name
					subDirLoc << subDirName.String();
				}

				result = create_directory(subDirLoc.String(), readWriteMode);
				if (result != B_OK) {
					PRINT(("  > error creating relation subdir '%s'': %s\n",
							subDirLoc.String(), strerror(result) ));
					return result;
				}

				// recurse to create complete relation structure for dynamic relations
				if (isDynamic) {
					entry_ref subDirRef;
					BEntry subDirEntry(subDirLoc.String());
					subDirEntry.GetRef(&subDirRef);

					result = subDirEntry.InitCheck();
					if (result == B_OK) {
						BMessage nestedRelations;
						result = properties.FindMessage(SEN_RELATIONS, &nestedRelations);

						if (result == B_OK && ! nestedRelations.IsEmpty()) {
							PRINT(("  >> entering relation subdir %s...\n", subDirRef.name));

							// process nested relations in new subdir
							result = WriteTargetRelations(&nestedRelations, idToRefMap,
                                                           relationConf, &subDirRef, openDirRef);
							PRINT(("  << leaving relation subdir %s...\n", subDirRef.name));
						}
					}
					if (result != B_OK) {
						PRINT(("  > error writing to relation subdir '%s': %s\n",
								subDirLoc.String(), strerror(result) ));
						return result;
					}
				}

				relationNode.SetTo(subDirLoc.String());

			} else {
				BFile relationTarget(&relationDir, fileName.String(), readWriteMode | B_CREATE_FILE);
				relationNode.SetTo(&relationDir, fileName.String());

				PRINT(("writing to relation file %s...\n", fileName.String() ));
			}

			result = relationNode.InitCheck();
			if (result != B_OK) {
				PRINT(("  > error creating relation target file '%s': %s\n", fileName.String(), strerror(result) ));
				return result;
			}

			relationNode.SetPermissions(readWriteMode);
			BNodeInfo relationNodeInfo(&relationNode);

			// TODO: handle varying types (esp. for self relations with different entity types)
			result = relationNodeInfo.SetType(relationType);
			if (result == B_OK) result = relationNode.WriteAttrString(SEN_RELATION_SOURCE_ATTR, new BString(srcId));
			if (result == B_OK) result = relationNode.WriteAttrString(SEN_RELATION_TARGET_ATTR, new BString(targetId));
			if (result != B_OK) {
				ERROR(("  > error writing common relation file attributes: %s"), strerror(result));
				return result;
			}

			PRINT(("* writing %d property attributes for relation #%d, target %s, into file %s:\n",
				   properties.CountNames(B_ANY_TYPE), relationIndex, targetId, fileName.String() ));

			for (int32 propertyIndex = 0; propertyIndex < properties.CountNames(B_ANY_TYPE); propertyIndex++) {
				// write out relation properties as file attributes according to message field type (== MIME attr type)
				char*   propertyName;
				uint32  propertyType;
				const void*  data;
				ssize_t	     size;

				result = properties.GetInfo(B_ANY_TYPE, propertyIndex, &propertyName, &propertyType, NULL);
				if (result != B_OK) {
					PRINT(("  error retrieving propertyName #%d for relation %d of target %s: %s\n",
							propertyIndex, relationIndex, targetId, strerror(result) ));
					continue;
				}

				result = properties.FindData(propertyName, propertyType, &data, &size);
				if (result != B_OK) {
					PRINT(("  skipping property %s for relation %d of target %s, error: %s\n",
							propertyName, relationIndex, targetId, strerror(result) ));
					continue;
				}

				// relation property name and type == MIME type attr name and type
				PRINT(("  > writing relation property %s into attribute.\n", propertyName));

				result = relationNode.WriteAttr(propertyName, propertyType, 0, data, size);
				if (result < size) {
					ERROR(("  > failed to write relation property attribute %s: %s.\n"),
						propertyName, strerror(result));
				}
			}   // properties loop
			// sync after finished writing attributes
			relationNode.Sync();

        } // target relations loop
	}

	return B_OK;
}


status_t TTracker::GetRelationAttributeInfo(const char* relationType, BMessage* attrInfo) {
	// get defined attributes for MIME type of relation, same for all targets of this relation
	BMimeType relationMimeType(relationType);
	status_t result = relationMimeType.GetAttrInfo(attrInfo);
	if (result != B_OK) {
		PRINT(("error reading attribute info for relation with type %s: %s\n", relationType, strerror(result)));
		return result;
	}

	// get additional attributes from relation supertype
	BMimeType relationSuperType;
	relationMimeType.GetSupertype(&relationSuperType);

	if (!relationSuperType.IsInstalled()) {
		PRINT(("MIME supertype for relation %s is not installed, check SEN installation!\n", SEN_RELATION_SUPERTYPE));
		return B_ERROR;
	}

	BMessage attrInfoSuperType;
	result = relationSuperType.GetAttrInfo(&attrInfoSuperType);
	if (result != B_OK) {
		PRINT(("error reading attribute info for relation super type: %s\n", strerror(result) ));
		return result;
	}

	// merge with attributes from supertype (relation)
	// TODO: merge relationConfig
	return attrInfo->Append(attrInfoSuperType);
}


status_t
TTracker::CreateRelationDirectory(
	const entry_ref* srcRef,
	const char* relationType,
	const BMessage* relationConfig,
	entry_ref* relationDirRef)
{
	const char* relationName = relationConfig->GetString(SEN_RELATION_NAME, relationType);	// fall back
	const char* relationLabel = relationConfig->GetString(SEN_RELATION_LABEL, relationName);

	PRINT(("creating relation dir for relation '%s' with label '%s'...\n", relationName, relationLabel));

	status_t result;
	BPath relationsDirPath;

	result = find_directory(B_SYSTEM_TEMP_DIRECTORY, &relationsDirPath);
	if (result != B_OK) {
		PRINT(("could not find temp directory: %s\n", strerror(result) ));
		return result;
	}

	// generate unique relation folder name
	// we can safely take the inode as folderId here since it's volume-bound anyway (e.g. to /tmp)
	BString srcId;
	result = GetFolderIdFromInode(srcRef, &srcId);
	if (result != B_OK) {
		PRINT(("failed to create relation folder: %s\n", strerror(result) ));
		return result;
	}

	result = relationsDirPath.Append("sen");
	relationsDirPath.Append(srcId.String());
	relationsDirPath.Append("relations");
	relationsDirPath.Append(relationName);

	PRINT(("creating relation temp dir at: %s\n", relationsDirPath.Path() ));

	// FIXME: remove old relation dir contents (only current relation dir)
	result = create_directory(relationsDirPath.Path(), B_READ_WRITE);
	if (result != B_OK) {
		PRINT(("failed to create temp relations dir at %s: %s\n", relationsDirPath.Path(), strerror(result) ));
		return result;
	}

	// TODO: prepare suitable folder attribute layout using archived relation attribute columns
	// see PoseView::SaveState() and ViewState::ArchiveToStream()
	// better yet, do this in BPoseView::SetupDefaultColumnsIfNeeded()

	BDirectory relationDir(relationsDirPath.Path());
	BEntry relationDirEntry;
	relationDir.GetEntry(&relationDirEntry);

	// add a friendly display name
	BString folderLabel(relationLabel);
	folderLabel << " \u2192 "  << relationLabel << " relations";
	BNode relationNode(&relationDirEntry);

	result = relationNode.InitCheck();

	// write file and meta type for relation dir
	if (result == B_OK) {
		BNodeInfo relationNodeInfo(&relationNode);

		if (result == B_OK) result = relationNodeInfo.InitCheck();
		if (result == B_OK) result = relationNodeInfo.SetType(relationType);
		if (result == B_OK) result = relationNode.WriteAttrString("META:TYPE", new BString(SEN_RELATION_FOLDER_TYPE));
		// add relation properties so we can populate the folder later with proper relation targets
		// todo: move to SEN:ID for easier uniform handling here?
		if (result == B_OK) result = relationNode.WriteAttrString(SEN_RELATION_SOURCE_ATTR, &srcId);
		// TODO: also attach relation message for self relations
		if (result == B_OK) result = relationNode.WriteAttrString(META_FOLDER_NAME, &folderLabel);
	}

	// return ref
	relationDirEntry.GetRef(relationDirRef);

	return result;
}


// TODO: the array format is only needed for the PDF-Extractor to keep the order of TOC items and any children;
//       we need to have a flag isOrdered in relationConf to switch between this and the easier
//       default format for relations with nested messages and individual property bags.
status_t TTracker::ConvertSelfRelationsToCommon(
	const char* targetId, BMessage* relations,
	BMessage* typeMapping, BMessage* attrMapping)
{
	status_t result = B_OK;

	// convert each item msg from self relations to a targetId->Properties msg as used in the common
	// SEN:relations structure, mapping types to full MIME type and properties to MIME attribute names.
	// sanity checking has already been done in SEN SelfRelations implementation.
	char* name;
	type_code typeCode;
	int32 propCount = 0;
	int32 itemCount = relations->CountNames(B_ANY_TYPE);

	if (itemCount == 0) {
		PRINT(("  - got emtpy message, bailing out.\n"));
		return B_OK;
	}

	// get property count - same for all properties of an item in our case
	result = relations->GetInfo(B_ANY_TYPE, 0, &name, &typeCode, &propCount);
	if (result != B_OK) {
		PRINT(("  X failed to get property count for item msg: %s\n", strerror(result) ));
		return result;
	}

	PRINT(("collecting %d properties for %d items...\n", propCount, itemCount));

	BStringList  handledProperties;	// garbage collector for transformed properties, to be cleared in one swoop later
	BMessage     itemProperties;
	const void*  value;
	ssize_t      valueSize;

	// now collect all properties for every item separately into individual relation property messages
	for (int32 propertyIndex = 0; propertyIndex < propCount; propertyIndex++) {
		PRINT(("round %d for %d items and %d properties...\n", propertyIndex, itemCount, propCount));

		for (int32 itemIndex = 0; itemIndex < itemCount; itemIndex++) {

			// get value for each property at the current itemIndex
			result = relations->GetInfo(B_ANY_TYPE, itemIndex, &name, &typeCode, NULL);
			if (result != B_OK) {
				PRINT(("  X failed to get name/value for item #%d, property '%s' [%d]: %s\n",
					itemIndex, name, propertyIndex, strerror(result) ));
				continue;
			}

			PRINT(("inspecting property %s at %d for item %d.\n", name, propertyIndex, itemIndex));

			// handle nested item messages for mapping properties to a shallow child relations message
			if (strncmp(name, SENSEI_ITEM, strlen(SENSEI_ITEM)) == 0) {
				BMessage  childMsg;
				result = relations->FindMessage(SENSEI_ITEM, propertyIndex, &childMsg);

				if (result != B_OK) {
					if (result == B_NAME_NOT_FOUND) {
						PRINT(("  x failed to get subitem msg: %s\n", strerror(result) ));
						continue;
					}
				}

				if (childMsg.IsEmpty()) {
					PRINT(("  > skipping empty placeholder child item for property '%s' for item #%d.\n",
							name, itemIndex));
					continue;
				}

				PRINT(("  > mapping nested item %s @[%d]...\n", name, itemIndex ));

                // build outline in recursion (has to match algo in OpenRelationTargetsMenu!)
                BString path;

                // get any existing path (possibly inherited from parent)
                path << relations->GetString(SENSEI_PATH, "") << "/" << propertyIndex;

                childMsg.AddString(SENSEI_PATH, path);

				// recursively convert child message so we can later create the entire structure from the root node
				result = ConvertSelfRelationsToCommon(targetId, &childMsg, typeMapping, attrMapping);
				if (result != B_OK) {
					PRINT(("  X error mapping nested item %s @[%d]: %s\n",
							name, itemIndex, strerror(result) ));
					continue;	// skip
				}

				PRINT(("  < DONE mapping nested item %s @[%d]...\n", name, itemIndex ));

				itemProperties.AddMessage(SEN_RELATIONS, &childMsg);

			} else {    // map flat properties
                // special handling for outline, just handed through until processed in ITEM above (don't remove below)
                if (strncmp(name, SENSEI_PATH, strlen(SENSEI_PATH)) == 0) {
                    continue;
				}

				result = relations->FindData(name, typeCode, propertyIndex, &value, &valueSize);
				if (result == B_OK) {
					// map property name to common attribute name as per attribute map
					// default is to keep the name, if no mapping was defined.
					const char *commonName = attrMapping->GetString(name, name);
					PRINT(("  * adding property '%s' ('%s') [%d] for item #%d:\n",
							name, commonName, propertyIndex, itemIndex));

					itemProperties.AddData(commonName, typeCode, value, valueSize);
				} else {
					PRINT(("  X failed to add name/value for item %d, property %s [%d]: %s\n",
						itemIndex, name, propertyIndex, strerror(result) ));
					continue;
				}
			}   // if (propertyName == SENSEI_ITEM)

			// remember properties (only needs to be done oce, as we process properties in an array)
			if (propertyIndex == 0) {
				handledProperties.Add(name);
			}
		} // item loop

		// remove original item properties and all their data
		for (int i = 0; i < handledProperties.CountStrings(); i++) {
			itemProperties.RemoveName(handledProperties.StringAt(i).String());
		}

		// add properties in standard SEN format
		relations->AddMessage(targetId, &itemProperties);

		// next run, collect new properties
		itemProperties.MakeEmpty();
	}  // property loop

	// remove original item properties and all their data from root, including _item's themselves
	handledProperties.Add(SENSEI_ITEM);
	for (int i = 0; i < handledProperties.CountStrings(); i++) {
		relations->RemoveName(handledProperties.StringAt(i).String());
	}

	return B_OK;
}


status_t TTracker::ConvertAttributesToMessage(const entry_ref* ref, BMessage* params)
{
	status_t result;

	BNode node(ref);
	BPath path(ref);

	if ((result = node.InitCheck()) != B_OK) {
		ERROR("failed to init node for ref %s: %s\n", path.Path(), strerror(result));
		return result;
	}

	char attrName[B_ATTR_NAME_LENGTH];
	int32 attrCount = 0;
	attr_info attrInfo;

	// iterate through relation target attributes and convert based on the attrInfo
	while ((result = node.GetNextAttrName(attrName)) == B_OK) {
	    if ((result = node.GetAttrInfo(attrName, &attrInfo)) != B_OK) {
		    ERROR("error reading attr_info of attribute %s of ref %s: %s\n", attrName, path.Path(), strerror(result));
		    return result;
        }
		if (! BString(attrName).StartsWith(SEN_ATTR_PREFIX)) {
			PRINT(("skipping non-managed attribute '%s' of file %s...\n", attrName, path.Leaf()) );
			continue;
		}
		PRINT(("adding attribute %s of file %s...\n", attrName, path.Leaf()) );
		const void *data[attrInfo.size];
		ssize_t bytesRead = node.ReadAttr(attrName, attrInfo.type, 0, data, attrInfo.size);

		if (bytesRead <= 0) {
			ERROR("failed to read attribute value of attribute %s from file %s: %s\n",
				attrName, path.Path(), strerror(result));
			return result;
		}
		// now add to message as typed field
		params->AddData(attrName, attrInfo.type, data, bytesRead);
		attrCount++;
	}
	if (result != B_ENTRY_NOT_FOUND) {
		ERROR("failed to read attributes of ref %s: %s\n", path.Path(), strerror(result));
		return result;
	}
	PRINT(("converted %d attribute(s) for file %s\n", attrCount, path.Leaf()) );
	params->PrintToStream();

	return B_OK;
}


// get resolved SEN:ID from reply or fall back to inode if it is a self relation
status_t TTracker::GetFolderIdFromInode(const entry_ref* srcRef, BString* folderId)
{
	// get inode as folder ID instead of SEN:ID, no need to create one for now
	BEntry srcEntry(srcRef);
	status_t result = srcEntry.InitCheck();

	if (result == B_OK) {
		struct stat srcStat;
		result = srcEntry.GetStat(&srcStat);

		if (result == B_OK) {
			*folderId << srcStat.st_ino;
		}
	}
	if (result != B_OK) {
		PRINT(("WARNING: could not get inode for srcRef %s: %s\n", srcRef->name, strerror(result) ));
		// fall back
		*folderId << srcRef->device << "_" << srcRef->directory << "_" << srcRef->name;
	}

	return result;
}
