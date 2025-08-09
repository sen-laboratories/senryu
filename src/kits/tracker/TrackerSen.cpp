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
	BMessage relationProperties;

	// need some special care
	bool isSelfRelation = message->GetBool(SEN_RELATION_IS_SELF, false);
	// dynamic relations are not writable as they are created on the fly
	bool isDynamicRelation = message->GetBool(SEN_RELATION_IS_DYNAMIC, false);

	if (message->FindMessage(SEN_RELATIONS, &relations) != B_OK) {
		PRINT(("no relations for type %s and source %s to show.\n", relationType.String(), srcRef.name ));
		return B_OK;
	}
	PRINT(("got relations for type %s for source %s:\n", relationType.String(), srcRef.name) );
	relations.PrintToStream();

	if (! isSelfRelation) {
		// get optional relation properties for relation targets
		relations.FindMessage(SEN_RELATION_PROPERTIES, &relationProperties);
	}

	// get MIME type for target relation
	BMessage attrInfo;
	if ((result = GetRelationTypeAttributeInfo(relationType.String(), &attrInfo)) != B_OK) {
		return result;
	}

	if (isSelfRelation) {
		// convert to common relations map with targetId (all point to source for self relation) and
		// properties mapped to MIME type attribute names, using the type_mapping provided by SENSEI
		BMessage typeMapping;
		message->FindMessage(SENSEI_TYPE_MAPPING, &typeMapping);

		// same for attributes (e.g. page -> SEN:REL:page)
		BMessage attrMapping;
		message->FindMessage(SENSEI_ATTR_MAPPING, &attrMapping);

		result = ConvertSelfRelationsToCommon(folderId.String(), &relations, &typeMapping, &attrMapping);
		if (result != B_OK) {
			PRINT(("could not convert from plugin result to self relations: %s\n", strerror(result) ));
			return result;
		}

		PRINT(("successfully converted plugin result to self relations:\n"));
		relations.PrintToStream();

		createDirectory = relations.GetBool("hasChildren", false);
	}

	// get ID lookup map
	BMessage idToRefMap;
	result = message->FindMessage(SEN_ID_TO_REF_MAP, &idToRefMap);
	if (!isSelfRelation) {
		if (result != B_OK) {
			ERROR("could not get ref mapping for source %s: %s\n", srcRef.name, strerror(result));
			return result;
		}
	} else {
		idToRefMap.AddRef(folderId.String(), &srcRef);
	}

	// iterate through all target IDs and get relation properties for each, then populate relation targets dir
    // with matching target refs, creating files with relation properties in file attributes.
    for (int32 targetIndex = 0; targetIndex < relations.CountNames(B_MESSAGE_TYPE); targetIndex++) {
		entry_ref ref;
		char* targetId;

		result = relations.GetInfo(B_MESSAGE_TYPE, targetIndex, &targetId, NULL);
		if (result == B_OK) {
			result = idToRefMap.FindRef(targetId, &ref);
		} else {
			PRINT(("no targetId found at index %d.\n", targetIndex));
			continue;	// better luck next time?
		}
		if (result != B_OK) {
			PRINT(("no ref found for targetId %s.\n", targetId));
			continue;	// better luck next time?
		}

		// (optional) properties for individual relation to that target
		// Note: there might be more than one relation to the same target with different properties!
        BMessage properties;
        int32 propertiesIndex = 0;

		if (! relationProperties.HasMessage(targetId)) {
			// write an empty placeholder for our targetId for easier common processing below
			relationProperties.AddMessage(targetId, new BMessage());
		}

		BNode	  relationNode;
		BNodeInfo relationNodeInfo;

        while (relationProperties.FindMessage(targetId, propertiesIndex, &properties) == B_OK) {
            // create a file for each set of properties for all targets
            PRINT(("writing property attrs #%d into ref %s:\n", propertiesIndex, ref.name));
            properties.PrintToStream();

            BString fileName(ref.name);
			if (BEntry(fileName).Exists()) {
				fileName.Append(" #") << propertiesIndex + 2; // human readable 1-based index, first relation is #1
			}

			uint32 readWriteMode = (isDynamicRelation ? B_READ_ONLY : B_READ_WRITE);

			// create file or dir with appropriate permissions
			if (createDirectory) {
				relationDir.CreateDirectory(fileName, NULL);
				relationDir.SetPermissions(readWriteMode);
			} else {
				BFile relationTarget(&relationDir, fileName, readWriteMode | B_CREATE_FILE);
			}

			relationNode.SetTo(&relationDir, fileName);
			result = relationNode.InitCheck();
			if (result != B_OK) {
				ERROR("error creating relation target %s: %s\n", ref.name, strerror(result));
				return result;
			}

            BNodeInfo relationNodeInfo(&relationNode);

			if (result == B_OK) result = relationNodeInfo.InitCheck();
            if (result == B_OK) result = relationNodeInfo.SetType(relationInfo->relationType);
            if (result == B_OK) result = relationNode.WriteAttrString(SEN_RELATION_SOURCE_ATTR, &relationInfo->srcId);
            if (result == B_OK) result = relationNode.WriteAttrString(SEN_RELATION_TARGET_ATTR, new BString(targetId));
            if (result != B_OK) {
                ERROR(("error writing relation attributes: %s"), strerror(result));
                return result;
            }

            // write out relation properties as file attributes according to MIME type
            BString attrName;
            int32 attrType;
            int32 attrIndex = 0;

            while (attrInfo.FindString("attr:name", attrIndex, &attrName) == B_OK) {
                result = attrInfo.FindInt32("attr:type", attrIndex, &attrType);
                if (result == B_OK) {
                    PRINT(("handling attribute %s of type %d...\n", attrName.String(), attrType));

                    const void* data;
                    ssize_t size;
                    result = properties.FindData(attrName.String(), static_cast<type_code>(attrType), 0, &data, &size);
                    if (result == B_OK) {
						BString value("value ");
                        PRINT(("creating relation property attribute %s with %s\n",
							attrName.String(), (value << data).String()) );	// todo: check - was result

                        result = relationNode.WriteAttr(attrName.String(), attrType, 0, data, size);
                        if (result <= 0) {
                            ERROR(("failed to write relation property attribute %s.\n"), attrName.String());
                        }
                    }
                }
                attrIndex++;
            } // attributes loop

            relationNode.Sync();
            propertiesIndex++;
        } // properties loop
	}

	return B_OK;
}

status_t
TTracker::GetRelationTypeAttributeInfo(const char* relationType, BMessage* attrInfo) {
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

	// get defined attributes for MIME type of relation, same for all targets of this relation
	result = relationMimeType.GetAttrInfo(attrInfo);
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
	attrInfo->Append(attrInfoSuperType);

	return B_OK;
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
	int32 count;
	int32 itemIndex = 0;
	type_code typeCode;
	const char* defaultType = typeMapping->GetString(SENSEI_DEFAULT_TYPE_KEY);

	BMessage properties;	// collects item properties below

	// todo: move to common place, copied from OpenRelationTargetsMenu::GetItemMessageInfo
	BString itemPropertyName(SENSEI_ITEM);	// for comfy repeated checks below
	BStringList senseiProps;
	senseiProps.Add(SENSEI_ITEM);
	senseiProps.Add(SENSEI_LABEL);
	senseiProps.Add(SENSEI_TYPE);

	while (result == B_OK) {
		result = itemMsg.GetInfo(B_ANY_TYPE, itemIndex, &name,	&typeCode, &count);
		if (result != B_OK) {
			if (result == B_BAD_INDEX) {
				break;
			}
			PRINT(("failed to get message info for property item #%d: %s\n", itemIndex, strerror(result)));
			return result;
		}

		// handle SENSEI properties to map to SEN:relations properly

		// handle nested item messages and adapt to map properties to a shallow child relations message
		if (itemPropertyName == name) {
			PRINT(("  > mapping nested item %s @[%d]...\n", name, itemIndex ));

			// add as nested *stub* to indicate hierarchy, so we can handle this properly and create a directory later
			properties.what = SEN_RELATIONS_GET_SELF;

			// just get matching label from the type with same index
			const char* label = itemMsg.GetString(SENSEI_LABEL, itemIndex, "");

			// will be used as file name
			properties.AddString(SEN_RELATION_TARGET_LABEL, label);

			// possibly different type for self relation targets (plugins can return whatever fits the use case)
			const char* type = itemMsg.GetString(SENSEI_TYPE, itemIndex, defaultType);
			properties.AddString(SEN_RELATION_TARGET_TYPE, type);
		}

		// skip standard properties for remaining custom properties
		if (! senseiProps.HasString(BString(name), true) ) {
			PRINT(("properties at index %d with name %s and count %d:\n", itemIndex, name, count));

			const void* data;
			ssize_t size;

			if ((result = itemMsg.FindData(name, typeCode, itemIndex, &data, &size))!= B_OK) {
				PRINT(("failed to get message data for property '%s' [#%d]: %s\n",
						name, itemIndex, strerror(result)));
				return result;
			}

			// map property name to common attribute name as per attribute map
			// default is to keep the name, if no mapping was defined.
			const char *commonName = attrMapping->GetString(name, name);

			if ((result = properties.AddData(commonName, typeCode, data, size)) != B_OK) {
				PRINT(("failed to add message data '%s' as '%s' to item at [%d]: %s\n",
						name, commonName, itemIndex, strerror(result)));
				return result;
			}
		}
		itemIndex++;
	}	// while

	// add properties in standard SEN format
	relations->AddMessage(SEN_ID_SELF, &properties);

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
