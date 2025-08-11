/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/

#define DEBUG 1

#include <Debug.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Message.h>
#include <NodeInfo.h>
#include <Query.h>
#include <StringList.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <fs_attr.h>

#include "Commands.h"
#include "Sen.h"
#include "Tracker.h"

bool
TTracker::HandleSenMessage(BMessage* message)
{
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
		case SEN_OPEN_RELATION_TARGET_VIEW:	// coming from the (sub)menu actions
			PRINT(("TrackerSen::Open (Self) Relations called.\n"));
			break;

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
	RelationInfo relationInfo;

	switch (message->what) {
		case SEN_OPEN_RELATION_VIEW:
		{
			result = PrepareRelationFolder(message, &relationInfo);
			break;
		}
		case SEN_OPEN_RELATION_TARGET_VIEW:
		{
			result = PrepareRelationTargetFolder(message, &relationInfo);
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
		trackerOpenDir->AddRef("refs", &relationInfo.relationDirRef);

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
TTracker::PrepareRelationFolder(BMessage *message, RelationInfo* relationInfo)
{
	entry_ref srcRef;

	PRINT(("PrepareRelationFolder: got message:\n"));
	message->PrintToStream();

	status_t result = message->FindRef(SEN_RELATION_SOURCE_REF, &srcRef);
	if (result != B_OK) {
		ERROR("PrepareRelationFolder: could not get required parameter, aborting: %s\n", strerror(result));
		return result;
	}

    // check relations
	BStringList relations;
	if (message->FindStrings(SEN_RELATIONS, &relations) != B_OK) {
		// TODO: add default relations from MIME DB so users can add targets!
		PRINT(("no relations for source %s to show.\n", srcRef.name ));
		return B_OK;
	}

	int32 countRelations = relations.CountStrings();
	PRINT(("got %d relations for source %s:\n", countRelations, srcRef.name) );

	BString folderId;
	result = GetFolderIdFromSenIdOrInode(message, &srcRef, &folderId);
	if (result != B_OK) {
		PRINT(("PrepareRelationFolder: could not create relation folder for src '%s': %s\n",
				srcRef.name, strerror(result) ));
		return result;
	}

	// create SEN relation folders of relation type, relations are expected to be unique here
	for (int rel = 0; rel < countRelations; rel++) {
		const char* relationType = relations.StringAt(rel).String();
		result = CreateRelationDirectory(&srcRef, folderId.String(), relationType, relationInfo);
		if ((result != B_OK)) {
			PRINT(("could not create directory for relation %s: %s\n", relationType, strerror(result)));
			return result;
		}
	}

	// return parent dir that holds all relations
	BEntry relationDirEntry(&relationInfo->relationDirRef);
	BEntry rootRelationDirEntry;

	result = relationDirEntry.GetParent(&rootRelationDirEntry);
	if (result == B_OK) {
		result = rootRelationDirEntry.GetRef(&relationInfo->relationDirRef);
	}

	return result;
}

status_t
TTracker::PrepareRelationTargetFolder(BMessage *message, RelationInfo* relationInfo)
{
	entry_ref srcRef;
	BString relationType;
	bool createDirectory = false;	// for n-ary relations (LATER) or self relations with nested relations

	PRINT(("PrepareRelationTargetFolder: got message:\n"));
	message->PrintToStream();

	status_t result = message->FindRef(SEN_RELATION_SOURCE_REF, &srcRef);
	if (result == B_OK) {
		result = message->FindString(SEN_RELATION_TYPE, &relationType);
	}
	if (result != B_OK) {
		ERROR("could not get required parameter, aborting: %s\n", strerror(result));
		return result;
	}

	BString folderId;
	result = GetFolderIdFromSenIdOrInode(message, &srcRef, &folderId);
	if (result != B_OK) {
		PRINT(("could not get ID for src '%s': %s\n", srcRef.name, strerror(result) ));
		return result;
	}

	result = CreateRelationDirectory(&srcRef, folderId.String(), relationType.String(), relationInfo);
	if ((result != B_OK)) {
		PRINT(("could not create relation target folder: %s\n", strerror(result)));
		return result;
	}

	BDirectory relationDir(&relationInfo->relationDirRef);

    // get relations and their properties
	BMessage relations;

	if (message->FindMessage(SEN_RELATIONS, &relations) != B_OK) {
		PRINT(("no relations for type %s and source %s to show.\n", relationType.String(), srcRef.name ));
		return B_OK;
	}
	PRINT(("got relations for type %s for source %s:\n", relationType.String(), srcRef.name) );
	relations.PrintToStream();

	// get config for target relation
	RelationConfig relationConfig;
	if ((result = GetRelationConfig(relationType.String(), &relationConfig)) != B_OK) {
		return result;
	}

	if (relationConfig.isSelf) {
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

		result = ConvertSelfRelationsToCommon(folderId.String(), &relations, &typeMapping, &attrMapping);
		if (result != B_OK) {
			PRINT(("could not convert from plugin result to self relations: %s\n", strerror(result) ));
			return result;
		}

		PRINT(("successfully converted plugin result to self relations:\n"));
		relations.PrintToStream();
	}

	// get ID lookup map
	BMessage idToRefMap;
	result = message->FindMessage(SEN_ID_TO_REF_MAP, &idToRefMap);

	if (!relationConfig.isSelf) {
		if (result != B_OK) {
			ERROR("could not get ref mapping for source %s: %s\n", srcRef.name, strerror(result));
			return result;
		}
	} else {
		idToRefMap.AddRef(folderId.String(), &srcRef);
	}

	// all relations of a particular target
	BMessage targetRelations;

	// iterate through all target IDs and get relation properties for each, then populate relation targets dir
    // with matching target refs, creating files with relation properties in file attributes.
    for (int32 targetIndex = 0; targetIndex < relations.CountNames(B_MESSAGE_TYPE); targetIndex++) {
		entry_ref ref;
		char* targetId;
		int32 countRelations;	// there can be multiple relations for the same target

		result = relations.GetInfo(B_MESSAGE_TYPE, targetIndex, &targetId, NULL, &countRelations);
		if (result == B_OK) {
			result = idToRefMap.FindRef(targetId, &ref);
		} else {
			PRINT(("could not get info for target at index %d: %s.\n", targetIndex, strerror(result) ));
			continue;	// better luck next time?
		}
		if (result != B_OK) {
			PRINT(("no valid targetId found / unexpected type for targetId %s at index %d: %s.\n",
				targetId, targetIndex, strerror(result) ));
			continue;	// better luck next time?
		}

		PRINT(("creating relation files for source '%s' with targetId %s and %d relations...\n",
				ref.name, targetId, countRelations));

		// (optional) properties for individual relation to that target
		// Note: there might be more than one relation to the same target with different properties!
		BNode	  relationNode;
		BNodeInfo relationNodeInfo;
        BMessage  properties;

		// create individual relation targets for each relation of the current target
        for (int32 relationIndex = 0; relationIndex < countRelations; relationIndex++) {
			result = relations.FindMessage(targetId, relationIndex, &properties);
			if (result != B_OK) {
				PRINT(("  > could not get relation properties for target %s at %d: %s\n",
					targetId, relationIndex, strerror(result) ));
				continue;
			}

			// used for self (and later also n-ary) relations
			createDirectory = properties.HasMessage(SEN_RELATIONS);

			// use relation's short name as file name
			BString fileName(relationConfig.shortName);

			// number file names, they are not really important since we prefer the label anyway
			if (relationIndex > 0) {
				// human readable 1-based index, first relation is #1 (without number)
				fileName << " " << relationIndex + 2;
			}

			// create a file for each set of properties for all targets
			// since dynamic relations are generated, they cannot be changed
			mode_t readWriteMode = (relationConfig.isDynamic ? 0755 : 0777);

			// create file or dir with appropriate permissions
			if (createDirectory) {
				relationDir.CreateDirectory(fileName.String(), NULL);
			} else {
				BFile relationTarget(&relationDir, fileName.String(), readWriteMode | B_CREATE_FILE);
			}

			relationNode.SetTo(&relationDir, fileName.String());
			result = relationNode.InitCheck();
			if (result != B_OK) {
				ERROR("  > error creating relation target file %s: %s\n", fileName.String(), strerror(result));
				return result;
			}

			relationNode.SetPermissions(readWriteMode);
			BNodeInfo relationNodeInfo(&relationNode);

			result = relationNodeInfo.SetType(relationInfo->relationType);
			if (result == B_OK) result = relationNode.WriteAttrString(SEN_RELATION_SOURCE_ATTR, &relationInfo->srcId);
			if (result == B_OK) result = relationNode.WriteAttrString(SEN_RELATION_TARGET_ATTR, new BString(targetId));
			if (result != B_OK) {
				ERROR(("  > error writing common relation file attributes: %s"), strerror(result));
				return result;
			}

			PRINT(("* writing %d property attributes for relation #%d, target %s, into file %s:\n",
				   properties.CountNames(B_ANY_TYPE), relationIndex, targetId, fileName.String() ));
			properties.PrintToStream();

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

status_t
TTracker::GetRelationConfig(const char* relationType, RelationConfig* config) {
	status_t result;
	BMimeType relationMimeType(relationType);

	if (!relationMimeType.IsValid()) {
		PRINT(("invalid MIME type for relation %s.\n", relationType));
		return B_ERROR;
	}
	if (!relationMimeType.IsInstalled()) {
		PRINT(("MIME type for relation %s not installed, check ontology config.\n", relationType));
		return B_ERROR;
	}

	// get additional attributes from relation supertype
	BMimeType relationSuperType;
	relationMimeType.GetSupertype(&relationSuperType);	// must be installed if MimeType was OK above

	char description[B_MIME_TYPE_LENGTH];

	relationMimeType.GetShortDescription(description);
	config->shortName = description;
	config->typeName  = relationMimeType.Type();	// == relationType we got
	config->isAssociation = config->typeName.StartsWith(SEN_ASSOC_RELATION_TYPE);

	// get extra SEN relation config
	BMessage relationAttrInfo;
	result = GetRelationAttributeInfo(relationType, &relationAttrInfo);

	if (result != B_OK) {
		PRINT(("could not get SEN relation attrInfo for type %s: %s\n", relationType, strerror(result) ));
		return result;
	}
/*
	BMessage relationConfig;
	result = relationAttrInfo.FindMessage(SEN_RELATION_CONFIG, &relationConfig);
	if (result != B_OK) {
		if (result != B_NAME_NOT_FOUND) {	// optional
			PRINT(("could not get SEN relation config for type %s: %s\n", relationType, strerror(result) ));
			return result;
		}
	}
*/
	config->isBidir   = relationAttrInfo.GetBool(SEN_RELATION_IS_BIDIR);
	config->isDynamic = relationAttrInfo.GetBool(SEN_RELATION_IS_DYNAMIC);
	config->isSelf    = relationAttrInfo.GetBool(SEN_RELATION_IS_SELF);

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
	const char* srcId,
	const char* relationType,
	RelationInfo* relationInfo)
{
	status_t result = B_OK;

	BString relationName(relationType);
	if (relationName.StartsWith(SEN_RELATION_SUPERTYPE "/")) {
		relationName.Remove(0, sizeof(SEN_RELATION_SUPERTYPE));
	}

	BPath relationsDirPath;
	if ((result = find_directory(B_SYSTEM_TEMP_DIRECTORY, &relationsDirPath)) != B_OK) {
		PRINT(("could not find temp directory: %s\n", strerror(result) ));
		return result;
	}

	// generate unique relation name
	BString relationDirName(relationsDirPath.Path());
	relationDirName.Append("/sen/") << srcId << "/relations/" << relationName;

	// FIXME: remove relation dir and sub dirs, but there does not seem to be a native API call
	//        matching create_directory, which sucks. Keep an eye on Haiku forum:
	//	      https://discuss.haiku-os.org/t/missing-remove-directory-to-complement-create-directory/18022
	if ((result = create_directory(relationDirName, B_READ_WRITE) != B_OK)) {
		PRINT(("failed to create temp dir at %s: %s\n", relationDirName.String(), strerror(result) ));
		return result;
	}

	// TODO: prepare suitable default layout using archived relation attribute columns
	// see PoseView::SaveState() and ViewState::ArchiveToStream()
	// better yet, do this in BPoseView::SetupDefaultColumnsIfNeeded()

	// populate result param
	BDirectory relationDir(relationDirName.String());
	BEntry relationDirEntry;
	relationDir.GetEntry(&relationDirEntry);
	relationDirEntry.GetRef(&relationInfo->relationDirRef);

	// set a friendly display name from MIME Type's short desc
	BMimeType relationMime(relationType);
	result = relationMime.InitCheck();
	if (result == B_OK) {
		char shortDesc[B_MIME_TYPE_LENGTH];
		result = relationMime.GetShortDescription(shortDesc);
		if (result == B_OK) {
			relationInfo->relationLabel = shortDesc;
		}
	}
	if (result != B_OK) {
		PRINT(("could not get label for relation %s from short description: %s\n",
			relationType, strerror(result) ));
		// use type as fallback
		relationInfo->relationLabel = relationType;
	}

	// add a friendly display name
	BString folderLabel(srcRef->name);
	folderLabel << " \u2192 "  << relationInfo->relationLabel << " relations";
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
		if (result == B_OK) result = relationNode.WriteAttrString(SEN_RELATION_SOURCE_ATTR, new BString(srcId));
		// TODO: also attach relation message for self relations
		if (result == B_OK) result = relationNode.WriteAttrString(META_FOLDER_NAME, &folderLabel);
	}
	relationInfo->relationType = relationType;
	relationInfo->srcRef = *srcRef;
	relationInfo->srcId = srcId;

	return result;
}


status_t TTracker::ConvertSelfRelationsToCommon(
	const char* targetId, BMessage* relations,
	BMessage* typeMapping, BMessage* attrMapping)
{
	status_t result = B_OK;

	// get root node item message
	// Note: we need to include child nodes as shallow fields for correct rendering, but we don't
	//       deep copy their subtree, as we only display the current level in the relation view.
	BMessage itemMsg;
	result = relations->FindMessage(SENSEI_ITEM, &itemMsg);
	if (result != B_OK) {
		if (result != B_NAME_NOT_FOUND) {
			PRINT(("error looking for child node in message: %s\n", strerror(result)));
			return result;
		} else {
			// return an empty property message for the self relation
			PRINT(("no self relations received.\n"));
			relations->AddMessage(SEN_ID_SELF, &itemMsg);

			return B_OK;
		}
	}

	// convert each item msg from self relations to a targetId->Properties msg as used in the common
	// SEN:relations structure, mapping types to full MIME type and properties to MIME attribute names.
	// sanity checking has already been done in SEN SelfRelations implementation.
	char* name;
	int32 count = 0, propCount = 0;	// for consistency check, should always be equal
	int32 itemIndex, itemCount = 0;
	type_code typeCode;

	const char* defaultType = typeMapping->GetString(SENSEI_DEFAULT_TYPE_KEY);

	BStringList properties;	// collects item properties below

	// todo: move to common place, copied from OpenRelationTargetsMenu::GetItemMessageInfo
	BString itemPropertyName(SENSEI_ITEM);	// for comfy repeated checks below

	// first, collect all properties and handle nested items
	for (itemIndex = 0; result == B_OK; itemIndex++) {
		result = itemMsg.GetInfo(B_ANY_TYPE, itemIndex, &name,	&typeCode, &propCount);

		if (result != B_OK) {
			if (result == B_BAD_INDEX) {
				break;
			}
			PRINT(("failed to get message info for property item #%d: %s\n", itemIndex, strerror(result)));
			return result;
		}

		// the message contains item properties in an array that should always line up
		if (itemIndex > 0 && propCount != count) {
			PRINT(("mismatch in item structure detected: property '%s' has %d elements vs %d from last.\n",
					name, propCount, count));
			return B_BAD_INDEX;
		}
		count = propCount;

		properties.Add(name);
	}	// for

	itemCount = propCount;	// we convert to items collecting all properties at each property index

	BString  propertyName;
	BMessage childMsg;
	BMessage relationProperties;
	ssize_t  valueSize;

	// now collect all properties separately into individual relation property messages
	for (int32 itemIndex = 0; itemIndex < itemCount; itemIndex++) {
		PRINT(("* collecting %d properties for item #%d:\n", count, itemIndex));

		for (int32 propIndex = 0; propIndex < properties.CountStrings(); propIndex++) {
			propertyName = properties.StringAt(propIndex);
			PRINT((" * checking property %s at #%d of item %d...\n", propertyName.String(), propIndex, itemIndex));

			// handle nested item messages for mapping properties to a shallow child relations message
			if (propertyName == SENSEI_ITEM) {
				result = itemMsg.FindMessage(SENSEI_ITEM, itemIndex, &childMsg);
				if (result != B_OK || childMsg.IsEmpty()) {
					PRINT(("  > skipping empty placeholder child item for property '%s' for item #%d.\n",
						propertyName.String(), itemIndex));
					continue;
				}

				PRINT(("  > mapping nested item %s @[%d]...\n", propertyName.String(), itemIndex ));

				// add as nested self relations message so we can later open sub folders with the cached result
				relationProperties.what = SEN_RELATIONS_GET_SELF;
				relationProperties.AddMessage(SEN_RELATIONS, &childMsg);

				// just get matching label from the type with same index
				const char* label = itemMsg.GetString(SENSEI_LABEL, propIndex, "");

				// will be used as file name
				relationProperties.AddString(SEN_RELATION_TARGET_LABEL, label);

				// possibly different type for self relation targets (plugins can return whatever fits the use case)
				const char* type = itemMsg.GetString(SENSEI_TYPE, propIndex, defaultType);
				relationProperties.AddString(SEN_RELATION_TARGET_TYPE, type);

			} else {
				const void* value;
				result = itemMsg.GetInfo(propertyName.String(), &typeCode);
				if (result == B_OK)
					result = itemMsg.FindData(propertyName.String(), typeCode, itemIndex, &value, &valueSize);
				if (result != B_OK) {
					PRINT(("  > failed to get value for property '%s' [#%d]: %s\n",
							name, itemIndex, strerror(result)));
					return result;
				}

				// map property name to common attribute name as per attribute map
				// default is to keep the name, if no mapping was defined.
				const char *commonName = attrMapping->GetString(propertyName.String(), propertyName.String());

				PRINT(("  > mapping property #%d of item %d: %s -> %s...\n",
					propIndex, itemIndex, propertyName.String(), commonName ));

				result = relationProperties.AddData(commonName, typeCode, value, valueSize);
				if (result != B_OK) {
					PRINT(("failed to add message data '%s' as '%s' to item at [%d]: %s\n",
							propertyName.String(), commonName, itemIndex, strerror(result)));
					return result;
				}
			}
		}
		// add properties in standard SEN format
		relations->AddMessage(targetId, &relationProperties);

		// next run, collect new properties
		relationProperties.MakeEmpty();
	}

	// remove original item and all its data
	relations->RemoveName(SENSEI_ITEM);

	return B_OK;
}


status_t TTracker::ConvertAttributesToMessage(const entry_ref* ref, BMessage* params)
{
	status_t result;

	PRINT(("ConvertAttr2Msg: converting attributes of file %s...\n", ref->name));

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


status_t TTracker::GetFolderIdFromSenIdOrInode(const BMessage* message, const entry_ref* srcRef, BString* folderId)
{
	// get resolved SEN:ID from reply or fall back to inode if it is a self relation
	bool isSelfRelation = message->GetBool(SEN_RELATION_IS_SELF, false);
	status_t result = message->FindString(SEN_RELATION_SOURCE_ID, folderId);

	if (result == B_OK)
		return result;	// done

	// missind SEN sourceID - only allowed for self relations, where we don't want to create a SEN:ID
	if (! isSelfRelation) {
		ERROR("could not get SEN:ID for srcRef '%s', aborting: %s\n", srcRef->name, strerror(result));
		return result;
	} else {
		// get inode as folder ID instead of SEN:ID, no need to create one for now
		BEntry srcEntry(srcRef);
		result = srcEntry.InitCheck();

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
	}
	return result;
}
