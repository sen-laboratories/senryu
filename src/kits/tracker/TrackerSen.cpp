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
#include "TrackerSenRelations.h"

#include <sen/Sen.h>
#include <sen/Sensei.h>
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

			BMessage relationProperties;

			// relation properties act as arguments for launch app
			result = message->FindMessage(SEN_RELATION_PROPERTIES, &relationProperties);
			if (result != B_OK) {
				if (result != B_NAME_NOT_FOUND) {
					PRINT(("error getting relation properties from refs msg: %s\n", strerror(result)));
					return true;
				}
			}

			PRINT(("got relation properties:\n"));
			relationProperties.PrintToStream();

			// get selected relation config from map
			BMessage relationConfig;

			result = message->FindMessage(SEN_RELATION_CONFIG, &relationConfig);
			if (result != B_OK) {
				PRINT(("could not get relation config for type %s: %s\n", relationType.String(), strerror(result) ));
				return true;	// abort
			}

			bool selfRelation = relationConfig.GetBool(SEN_RELATION_IS_SELF, false);

			PRINT(("got relation config for type %s (is %s)\n",
				relationType.String(), (selfRelation ? "SELF" : "NORMAL") ));

			PRINT(("relationConfig is:\n"));
			relationConfig.PrintToStream();

			if (selfRelation) {
				PRINT(("resolving SELF relation...\n"));

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

				if (TrackerSenRelations::ResolveRelation(&srcRef, &srcId, &targetId)) {
					PRINT(("resolved SEN Relation target %s for ref %s\n", targetId.String(), srcRef.name));

					result = PrepareLaunchTarget(&srcRef, targetId.String(), &targetRef, &relationProperties);
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

status_t TTracker::PrepareLaunchTarget(
	const entry_ref* srcRef, const char* targetId, entry_ref* targetRef, BMessage* params)
{
	// get relation target by ID from SEN server
	BMessenger senMessenger(SEN_SERVER_SIGNATURE);
	BMessage queryTargetRefMsg(SEN_QUERY_REF_FOR_ID);
	queryTargetRefMsg.AddString(SEN_ID_ATTR, targetId);

	BMessage reply;
	senMessenger.SendMessage(&queryTargetRefMsg, &reply);

	status_t result = reply.FindRef("ref", targetRef);
	if (result == B_OK) {
		PRINT(("got target ref %s\n", targetRef->name));
		result = TrackerSenRelations::ConvertAttributesToMessage(srcRef, params);
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
	result = message->FindMessage(SEN_RELATION_CONFIG_MAP, &relationConfigs);
	if (result != B_OK) {
		PRINT(("could not get relation config: %s\n", strerror(result) ));
		return result;
	}

	// generate unique relation folder name
	// we can safely take the inode as folderId here since it's volume-bound anyway (e.g. to /tmp)
	BString srcId;
	result = TrackerSenRelations::GetInodeForRef(&srcRef, &srcId);
	if (result != B_OK) {
		PRINT(("failed to create relation folder: %s\n", strerror(result) ));
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
		result = TrackerSenRelations::CreateRelationDirectory(srcId.String(), relationType, &relationConf, relationDirRef);
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

	PRINT(("PrepareRelationTargetFolder: got relation target view message:\n"));
	message->PrintToStream();

	// get config for all relations in the result
	BMessage relationConfigs;
	// holds selected config
	BMessage relationConf;

	result = message->FindMessage(SEN_RELATION_CONFIG_MAP, &relationConfigs);
	if (result == B_OK)
		result = relationConfigs.FindMessage(relationType, &relationConf);

	if (result != B_OK) {
		PRINT(("could not get relation config for type %s: %s\n", relationType, strerror(result) ));
		return result;
	}

	const char* relationName = relationConf.GetString(SEN_RELATION_NAME);
	bool isSelf    = relationConf.GetBool(SEN_RELATION_IS_SELF);
	bool isDynamic = relationConf.GetBool(SEN_RELATION_IS_DYNAMIC);

	PRINT(("selected relation '%s' of type '%s' is %s and %s.\n",
		relationName,
		relationType,
		isSelf ? "reflexive" : "normal",
		isDynamic ? "dynamic": "static"));

	// get SEN:ID of source for normal relations, or the inode for self relations
	BString srcId;
	message->GetString(SEN_RELATION_SOURCE_ID, srcId);

	if (srcId.IsEmpty()) {	// e.g. for self relations
		result = TrackerSenRelations::GetInodeForRef(&srcRef, &srcId);
		if (result != B_OK) {
			PRINT(("failed to create relation folder: %s\n", strerror(result) ));
			return result;
		}

		// save ref along with relation files later, since we have no way to lookup the source by inode alone
		relationConf.AddRef(SEN_RELATION_SOURCE_REF, &srcRef);

		if (isSelf) {	// add source as target ref, too, as they are the same
			relationConf.AddRef(SEN_RELATION_TARGET_REF, &srcRef);
		}
	}
	// add to config for proccessing
	relationConf.AddString(SEN_RELATION_SOURCE_ID, srcId);

	BMessage relations;

	if (isSelf) {
		// get all relations for creating complete relation structure, but open only selected relation view later
		BMessage* relationRoot;
		result = message->FindPointer(SEN_RELATION_ROOT, reinterpret_cast<void**>(&relationRoot));

		if (result == B_OK) {
			relations = *relationRoot;
			if (relations.IsEmpty()) {
				PRINT(("  ? no relations contained in result root.\n"));
				return B_OK;
			}
		} else {
			PRINT(("  X failed to get relation ROOT: %s\n", strerror(result) ));
			return result;
		}

		PRINT(("  * got relation ROOT, generating self relation view....\n"));

		// get selected item ID from selected relation properties
		const char* itemId = relationProperties.GetString(SEN_RELATION_ITEM_ID, "");
		if (strlen(itemId) == 0) {
			PRINT(("  x got no item ID from selection, check.\n"));
		} else {
			PRINT(("  * got item ID %s.\n", itemId));
		}

		relationConf.AddString(SEN_RELATION_ITEM_ID, itemId);
		relationConf.AddString(SEN_RELATION_TYPE, relationType);

	} else {
		message->FindMessage(SEN_RELATIONS, &relations);
		PRINT(("got relations for type %s for source %s:\n", relationType, srcRef.name));
	}

	// create top-level relation dir for src relation
	// TODO: pass in all configs and handle mixed types properly
	result = TrackerSenRelations::CreateRelationDirectory(srcId.String(), relationType, &relationConf, relationDirRef);
	if ((result != B_OK)) {
		PRINT(("could not create relation target folder: %s\n", strerror(result)));
		return result;
	}

	// reusable optionally recursive part
	result = TrackerSenRelations::WriteTargetRelations(&relations, &relationConf, NULL, relationDirRef);
	if (result != B_OK) {
		PRINT(("could not write relation targets to folder %s: %s\n", relationDirRef->name, strerror(result) ));
		return result;
	}

	PRINT(("finished preparing relation targets folder (struct) with starting path '%s'.\n",
		BPath(relationDirRef).Path() ));

	return result;
}
