/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
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
#include "TrackerSenLog.h"
#include "RelationContext.h"
#include "RelationFolders.h"

bool
TTracker::HandleSenMessage(BMessage* message)
{
	if (message->what == sen::cmd::kOpenRelationTargetView) {
		// Note: we need to differentiate between invoking the menu (to open targets in a Tracker relation view)
		//       vs invoking the relation from the menu itself
		if ((modifiers() & B_OPTION_KEY) != 0) {
			// handle as normal ref to be opened through sen::cmd::kOpenRelationTarget
			// (intercepted to be enriched with SEN relation properties as args)
			message->what = sen::cmd::kOpenRelationTarget;
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
			message->what = sen::cmd::kOpenRelationView;
			break;
		}
		case sen::cmd::kOpenRelationTarget: {
			PRINT(("TrackerSen::sen::cmd::kOpenRelationTarget\n"));

			// get SEN relation type, if the msg comes from SEN it is required.
			BString relationType;
			status_t result = message->FindString(sen::key::kRelationType, &relationType);

			if (result != B_OK) {
				PRINT(("could not find SEN relationType, aborting: %s",
					strerror(result) ));

				return true;	// we are done
			}

			entry_ref srcRef;
			entry_ref targetRef;
			entry_ref senHandlerRef;

			result = message->FindRef(sen::key::kSourceRef, &srcRef);
			if (result != B_OK) {
				PRINT(("could not find source ref, aborting: %s",
					strerror(result) ));

				return true;	// we are done
			}

			BMessage relationProperties;

			// relation properties act as arguments for launch app
			result = message->FindMessage(sen::key::kRelationProperties, &relationProperties);
			if (result != B_OK) {
				if (result != B_NAME_NOT_FOUND) {
					PRINT(("error getting relation properties from refs msg: %s\n", strerror(result)));
					return true;
				}
			}

			PRINT(("got relation properties:\n"));
			relationProperties.PrintToStream();

			// the config of the relation: in the context of the menu that this item was made in (and so are all the others of the menu),
			// or, for an item that has no context, brought by the item
			BMessage relationConfig;

			RelationContextRef context = RelationContexts::Find(message);
			if (context != NULL && context->FindConfig(relationType.String(), &relationConfig))
				result = B_OK;
			else
				result = message->FindMessage(sen::key::kRelationConfig, &relationConfig);
			if (result != B_OK) {
				PRINT(("could not get relation config for type %s: %s\n", relationType.String(), strerror(result) ));
				return true;	// abort
			}

			bool selfRelation = relationConfig.GetBool(sen::conf::kSelf, false);

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
					// the navigator of the relation type if it has one (not Tracker: that is what opens any file),
					// else the default app of the target, which should be a SEN relation navigator
					char navigatorSig[B_MIME_TYPE_LENGTH];
					result = B_ERROR;
					if (BMimeType(relationType.String()).GetPreferredApp(navigatorSig) == B_OK
							&& strcasecmp(navigatorSig, kTrackerSignature) != 0) {
						result = be_roster->FindApp(navigatorSig, &senHandlerRef);
					}
					if (result != B_OK)
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
			message->RemoveName(sen::key::kSourceRef);
			message->AddRef("refs", &targetRef);

			TrackerLaunch(&senHandlerRef, message, true);

			return true;
		}
		case sen::cmd::kOpenRelationTargetView:	{ // coming from the (sub)menu actions
			PRINT(("TrackerSen::Open (Self) Relations as target view.\n"));
			break;
		}
		case sen::cmd::kRelationsGetNewTarget:
			PRINT(("TrackerSen::get NEW target template called.\n"));
			break;

		case sensei::cmd::kExtract:
			PRINT(("TrackerSen::SENSEI extract called.\n"));
			break;

		case sensei::cmd::kEnrich:
			PRINT(("TrackerSen::SENSEI enrich called.\n"));
			break;

		case sensei::cmd::kIdentify:
			PRINT(("TrackerSen::SENSEI identify called.\n"));
			break;

		case sensei::cmd::kNavigate:
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
		case sen::cmd::kOpenRelationView:
		{
			result = PrepareRelationFolder(message, &relationDirRef);
			break;
		}
		case sen::cmd::kOpenRelationTargetView:
		{
			result = PrepareRelationTargetFolder(message, &relationDirRef);
			break;
		}
		case sen::cmd::kRelationsGetNewTarget:
		{
			BString relationType;
			result = message->FindString(sen::key::kRelationType, &relationType);
			if (result != B_OK) {
				PRINT(("could not find relation type: %s\n", strerror(result) ));
				return true;	// abort
			}

			BString targetType;
			result = message->FindString(sen::key::kTargetType, &targetType);
			if (result != B_OK) {
				PRINT(("could not find target type: %s\n", strerror(result) ));
				return true;	// abort
			}

			entry_ref sourceRef;
			result = message->FindRef(sen::key::kSourceRef, &sourceRef);
			if (result != B_OK) {
				PRINT(("could not get source ref: %s\n", strerror(result) ));
				return true;	// abort
			}

			entry_ref targetRef;
			result = message->FindRef(sen::key::kTargetRef, &targetRef);

			if (result != B_OK) {
				if (result == B_NAME_NOT_FOUND && targetType.StartsWith(sen::mime::kClassificationPrefix)) {
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
						BMessage addRelationMsg(sen::cmd::kRelationAdd);
						addRelationMsg.AddString(sen::key::kRelationType, relationType);
						addRelationMsg.AddString(sen::key::kTargetType, targetType);
						addRelationMsg.AddRef(sen::key::kSourceRef, &sourceRef);
						addRelationMsg.AddRef(sen::key::kTargetRef, &targetRef);

						addRelationMsg.PrintToStream();

						BMessenger senMsgr(sen::kServerSignature);
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
	BMessage msgGetClassEntity(sen::cmd::kClassificationAdd);
	msgGetClassEntity.AddString(sen::key::kType, associationEntityType);

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
	msgGetClassEntity.AddString(sen::key::kName, newClass);

	BMessage msgClassReply;
	BMessenger senMsgr(sen::kServerSignature);

	result = senMsgr.SendMessage(&msgGetClassEntity, &msgClassReply);
	status_t status = msgClassReply.GetInt32(sen::key::kResult, B_OK);

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
	BMessenger senMessenger(sen::kServerSignature);
	BMessage queryTargetRefMsg(sen::cmd::kQueryRefForId);
	queryTargetRefMsg.AddString(sen::attr::kId, targetId);

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

	status_t result = message->FindRef(sen::key::kSourceRef, &srcRef);
	if (result != B_OK) {
		PRINT(("missing required parameter %s !\n", sen::key::kSourceRef ));
		return result;
	}

	// the relation types and their configs were kept by the menu that made this item (see RelationContext)
	RelationContextRef context = RelationContexts::Find(message);
	if (context == NULL) {
		PRINT(("the context of the menu is gone: open the menu again.\n"));
		return B_NAME_NOT_FOUND;
	}

	// check relations
	BStringList relations(context->relations);
	if (relations.IsEmpty()) {
		// TODO: add default relations from MIME DB so users can add targets!
		PRINT(("no relations to show.\n"));
		return B_OK;
	}

	int32 countRelations = relations.CountStrings();
	PRINT(("got %d relations\n", countRelations) );

	BMessage relationConfigs(context->relationConfigs);

	// every view has its own folder (a TSID): the inode of the source is not unique across volumes, and two views
	// of the same file must not overwrite each other
	BString viewId;
	TrackerSenRelations::NewViewId(&viewId);

	// the source is known by its SEN:ID, files without one by their inode
	BString srcId;
	BNode srcNode(&srcRef);
	if (srcNode.ReadAttrString(sen::attr::kId, &srcId) != B_OK || srcId.IsEmpty()) {
		result = TrackerSenRelations::GetInodeForRef(&srcRef, &srcId);
		if (result != B_OK) {
			PRINT(("failed to create relation folder: %s\n", strerror(result) ));
			return result;
		}
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
		// stored relations are shown as files, one per relation; relations of plugins are resolved at run time
		// when their menu is used, their folder stays a placeholder here
		result = TrackerSenRelations::MaterializeType(srcRef, viewId.String(), relationType, relationDirRef);
		if (result == B_NOT_SUPPORTED) {
			result = TrackerSenRelations::CreateRelationDirectory(viewId.String(), srcId.String(), TrackerSenRelations::DisplayNameOf(srcRef).String(), relationType,
				&relationConf, relationDirRef);
		}
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

	// the folder of all types takes dropped files as generic relations to them
	if (result == B_OK) {
		RelationFolders::FolderInfo root;
		if (rootRelationDirEntry.GetNodeRef(&root.node) == B_OK) {
			root.ref = *relationDirRef;
			root.sourceRef = srcRef;
			root.sourceId = srcId;
			root.viewId = viewId;
			RelationFolders::Instance().RegisterFolder(root);
		}
	}

	return result;
}

status_t
TTracker::PrepareRelationTargetFolder(BMessage *message, entry_ref* relationDirRef)
{
	entry_ref srcRef;

	status_t result = message->FindRef(sen::key::kSourceRef, &srcRef);
	if (result != B_OK) {
		PRINT(("missing required parameter %s !\n", sen::key::kSourceRef ));
		return result;
	}

	const char* relationType;
	result = message->FindString(sen::key::kRelationType, &relationType);
	if (result != B_OK) {
		PRINT(("missing required parameter %s !\n", sen::key::kRelationType ));
		return result;
	}

	BMessage relationProperties;
	message->FindMessage(sen::key::kRelationProperties, &relationProperties);

	PRINT(("PrepareRelationTargetFolder: got relation target view message:\n"));
	message->PrintToStream();

	// the configs of all relations of the menu, in its context (kept by the menu, see RelationContext), and the selected one
	RelationContextRef context = RelationContexts::Find(message);
	if (context == NULL) {
		PRINT(("the context of the menu is gone: open the menu again.\n"));
		return B_NAME_NOT_FOUND;
	}
	BMessage relationConf;
	if (!context->FindConfig(relationType, &relationConf)) {
		PRINT(("could not get relation config for type %s\n", relationType));
		return B_NAME_NOT_FOUND;
	}

	const char* relationName = relationConf.GetString(sen::key::kRelationName);
	bool isSelf    = relationConf.GetBool(sen::conf::kSelf);
	bool isDynamic = relationConf.GetBool(sen::conf::kDynamic);

	PRINT(("selected relation '%s' of type '%s' is %s and %s.\n",
		relationName,
		relationType,
		isSelf ? "reflexive" : "normal",
		isDynamic ? "dynamic": "static"));

	// get SEN:ID of source for normal relations, or the inode for self relations
	BString srcId;
	message->GetString(sen::key::kSourceId, srcId);

	if (srcId.IsEmpty()) {	// e.g. for self relations
		result = TrackerSenRelations::GetInodeForRef(&srcRef, &srcId);
		if (result != B_OK) {
			PRINT(("failed to create relation folder: %s\n", strerror(result) ));
			return result;
		}

		// save ref along with relation files later, since we have no way to lookup the source by inode alone
		relationConf.AddRef(sen::key::kSourceRef, &srcRef);

		if (isSelf) {	// add source as target ref, too, as they are the same
			relationConf.AddRef(sen::key::kTargetRef, &srcRef);
		}
	}
	// add to config for proccessing
	relationConf.AddString(sen::key::kSourceId, srcId);

	BMessage relations;

	if (isSelf) {
		// get all relations for creating complete relation structure, but open only selected relation view later.
		// The tree is in the context, set by the menu when the answer of the server arrived. A click can be faster than the menu (it is
		// built in a thread of its own): then the server is asked now.
		if (!context->GetRoot(&relations)) {
			BMessage request(sen::cmd::kRelationsGetSelf);
			request.AddRef(sen::key::kSourceRef, &srcRef);
			request.AddString(sen::key::kRelationType, relationType);
			const char* plugin;
			if (message->FindString(sensei::key::kPlugin, &plugin) == B_OK)
				request.AddString(sensei::key::kPlugin, plugin);
			if (!context->pluginConfig.IsEmpty())
				request.AddMessage(sensei::key::kPluginConfig, &context->pluginConfig);

			BMessenger server(sen::kServerSignature);
			BMessage answer;
			result = server.IsValid() ? server.SendMessage(&request, &answer) : B_ERROR;
			if (result != B_OK) {
				PRINT(("  X failed to get relation ROOT: %s\n", strerror(result) ));
				return result;
			}
			context->SetRoot(answer);
			relations = answer;
		}
		if (relations.IsEmpty()) {
			PRINT(("  ? no relations contained in result root.\n"));
			return B_OK;
		}

		PRINT(("  * got relation ROOT, generating self relation view....\n"));

		// get selected item ID from selected relation properties
		const char* itemId = relationProperties.GetString(sen::key::kItemId, "");
		if (strlen(itemId) == 0) {
			PRINT(("  x got no item ID from selection, check.\n"));
		} else {
			PRINT(("  * got item ID %s.\n", itemId));
		}

		relationConf.AddString(sen::key::kItemId, itemId);
		relationConf.AddString(sen::key::kRelationType, relationType);

	} else {
		// a stored relation: the view is made as for the menu of all relations (the files of the relations, which can be edited)
		BString storedViewId;
		TrackerSenRelations::NewViewId(&storedViewId);
		result = TrackerSenRelations::MaterializeType(srcRef, storedViewId.String(), relationType, relationDirRef);
		if (result != B_NOT_SUPPORTED)
			return result;
		// resolved at run time (by a plugin): handed over with the menu
		message->FindMessage(sen::key::kRelations, &relations);
		PRINT(("got relations for type %s for source %s:\n", relationType, srcRef.name));
	}

	// create top-level relation dir for src relation
	// TODO: pass in all configs and handle mixed types properly
	BString viewId;
	TrackerSenRelations::NewViewId(&viewId);
	result = TrackerSenRelations::CreateRelationDirectory(viewId.String(), srcId.String(), TrackerSenRelations::DisplayNameOf(srcRef).String(), relationType, &relationConf, relationDirRef);
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
