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
#include "TemplatesMenu.h"
#include "TemplateUtils.h"
#include "Tracker.h"

bool
TTracker::HandleSenMessage(BMessage* message)
{
	switch (message->what) {
		case kNewAssociation:
			PRINT(("TrackerSen::AssociateWith()  called\n"));
			break;
		case kOpenRelations:
		case kOpenSelfRelations:
		case SEN_OPEN_RELATION_TARGET_VIEW:	// fallthrough
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

	// handle modifier for differentiating between open relation view and invoke target (dynamic/self relations)
	// todo: move this to Shortcuts and handle like 'Enrich (Overwrite)'
	if ((modifiers() & B_SHIFT_KEY) != 0) {
		if (message->what == SEN_OPEN_RELATION_TARGET_VIEW) {
			PRINT(("switching from open target view to open ref.\n"));
			message->what = B_REFS_RECEIVED;
			return false;	// handle as normal refs received message
		}
	}

	// handle SEN command messages
	status_t result = B_OK;
	RelationInfo relationInfo;

	switch (message->what) {
		case SEN_OPEN_RELATION_VIEW:
		{
			result = PrepareRelationWindow(message, &relationInfo);
			break;
		}
		case SEN_OPEN_RELATION_TARGET_VIEW:
		{
			result = PrepareRelationTargetWindow(message, &relationInfo);
			break;
		}
		case kOpenRelations:
		case kOpenSelfRelations:	//fallthrough
		{
			PRINT(("open (self) relations top level view not yet implemented. Please check back later.\n"));
			break;
		}
		case kNewAssociation:
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
				if (result == B_NAME_NOT_FOUND && targetType.StartsWith(SEN_META_SUPERTYPE "/")) {
					result = CreateNewAssociationEntity(targetType.String(), &targetRef);
					if (result == B_OK) {
						// show in target location (SEN context config) in Tracker for editing name
						EditNewEntity(&targetRef);

						// add new ref and continue to add association relation below as in normal case
						message->AddRef(SEN_RELATION_TARGET_REF, &targetRef);
					} else {
						return true;	// bail out
					}
				} else {
					PRINT(("could not get target ref: %s\n", strerror(result) ));
					return true;	// abort
				}
			}

			// add a relation to new or existing association meta entity
			PRINT(("adding relation to association entity instance '%s' for association %s of type %s\n",
				BPath(&targetRef).Path(), relationType.String(), targetType.String() ));

			// adapt and send as SEN scripting message to add relation of desired type
			message->what = SEN_RELATION_ADD;
			message->PrintToStream();

			BMessenger senMsgr(SEN_SERVER_SIGNATURE);
			if (senMsgr.IsValid()) {
				senMsgr.SendMessage(message);
			} else {
				PRINT(("could not reach sen_server.\n"));
			}

			// we are done here
			// todo: clean up 'if' branch below for better uniform handling
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
TTracker::PrepareRelationWindow(BMessage *message, RelationInfo* relationInfo)
{
	//status_t result;
	return B_NOT_SUPPORTED;	// not yet implemented
}

status_t
TTracker::PrepareRelationTargetWindow(BMessage *message, RelationInfo* relationInfo)
{
	status_t result;
	if ((result = PrepareRelationDirectory(message, relationInfo) != B_OK)) {
		PRINT(("could not create relation target view: %s\n", strerror(result)));
		return result;
	}

	BDirectory relationDir(&relationInfo->relationDirRef);

	// get fresh relation targets from SEN server
	BMessenger senMessenger(SEN_SERVER_SIGNATURE);
	BMessage relationTargetsMsg(SEN_RELATIONS_GET);
    relationTargetsMsg.AddRef(SEN_RELATION_SOURCE_REF, &relationInfo->srcRef);
    relationTargetsMsg.AddString(SEN_RELATION_TYPE, relationInfo->relationType);

	BMessage reply;
	senMessenger.SendMessage(&relationTargetsMsg, &reply);

    // check relations
	BMessage relations;
	if (reply.FindMessage(SEN_RELATIONS, &relations) != B_OK) {
		PRINT(("no relations for type %s and source %s to show.\n",
			relationInfo->relationType.String(), relationInfo->srcRef.name ));
			return B_OK;
	}

	PRINT(("got relations for type %s for source %s:\n", relationInfo->relationType.String(), relationInfo->srcRef.name) );
	relations.PrintToStream();

	// get MIME type for target relation
	BMessage attrInfo;
	if ((result = GetRelationTypeAttributeInfo(relationInfo->relationType, &attrInfo)) != B_OK) {
		return result;
	}

	BMessage relationProperties;
	result = relations.FindMessage("properties", &relationProperties);
	if (result != B_OK) {
		ERROR("no relation properties found for source %s: %s\n", relationInfo->srcRef.name, strerror(result));
		// still continue and just create blank relation files
	}

	PRINT(("got relation properties:\n"));
	relationProperties.PrintToStream();

	BStringList targetIds;
    result = relations.FindStrings(SEN_TO_ATTR, &targetIds);
	if (result != B_OK) {
		ERROR("could not get relation targetIds for source %s: %s\n", relationInfo->srcRef.name, strerror(result));
		return result;
	}

	// get ID lookup map
	BMessage idToRefMap;
	result = relations.FindMessage(SEN_ID_TO_REF_MAP, &idToRefMap);
	if (result != B_OK) {
		ERROR("could not get ref mapping for source %s: %s\n", relationInfo->srcRef.name, strerror(result));
		return result;
	}

	// iterate through all target IDs and get relation properties for each, then populate relation targets dir
    // with matching target refs, creating files with relation properties in file attributes.
    for (int32 targetIndex = 0; targetIndex < targetIds.CountStrings(); targetIndex++) {
		entry_ref ref;
		const char* targetId = targetIds.StringAt(targetIndex).String();
		result = idToRefMap.FindRef(targetId, &ref);
		if (result != B_OK) {
			PRINT(("no ref found for targetId %s.\n", targetId));
			continue;	// better luck next time?
		}
        PRINT(("handling properties for target %s ...\n", targetId));

		// properties for individual relation to that target
		// Note: there might be more than one relation to the same target with different properties!
        BMessage properties;
        int32 propertiesIndex = 0;

        while (relationProperties.FindMessage(targetId, propertiesIndex, &properties) == B_OK) {
            // create a file for each set of properties for all targets
            PRINT(("writing property attrs #%d into ref %s:\n", propertiesIndex, ref.name));
            properties.PrintToStream();

            BString fileName(ref.name);
			if (BEntry(fileName).Exists()) {
				fileName.Append(" #") << propertiesIndex + 2; // human readable 1-based index, first relation is #1
			}

            BFile relationTarget(&relationDir, fileName, B_READ_WRITE | B_CREATE_FILE);
            result = relationTarget.InitCheck();
            if (result != B_OK) {
                ERROR("error creating relation target %s: %s\n", ref.name, strerror(result));
                return result;
            }

            BNode relationNode(relationTarget);
            BNodeInfo relationNodeInfo(&relationNode);
            if (result == B_OK) result = relationNodeInfo.InitCheck();
            if (result == B_OK) result = relationNodeInfo.SetType(relationInfo->relationType);
            if (result == B_OK) result = relationNode.WriteAttrString(SEN_RELATION_SOURCE_ATTR,
											&relationInfo->srcId);
            if (result == B_OK) result = relationNode.WriteAttrString(SEN_RELATION_TARGET_ATTR,
                                            new BString(targetIds.StringAt(targetIndex)));
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
            relationTarget.Unset();

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

	PRINT(("got attrInfo:\n"));
	attrInfo->PrintToStream();

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
	result = attrInfo->Append(attrInfoSuperType);
/* FIXME: works but check returns 'Bad argument type passed to function' ?!
	if (result != B_OK) {
		PRINT(("failed to construct attribute info for relation type and supertype: %s\n", strerror(result) ));
		return result;
	}
 */
	return B_OK;
}

status_t
TTracker::PrepareRelationDirectory(BMessage *message, RelationInfo* relationInfo)
{
	status_t result = B_OK;

	BString srcId, relationType, relationLabel;
	entry_ref srcRef;

	result = message->FindRef(SEN_RELATION_SOURCE_REF, &srcRef);
	if (result == B_OK) result = message->FindString(SEN_RELATION_SOURCE_ATTR, &srcId);
	if (result == B_OK) result = message->FindString(SEN_RELATION_TYPE, &relationType);
	if (result == B_OK) result = message->FindString(SEN_RELATION_LABEL, &relationLabel);

	if (result != B_OK) {
		ERROR("PrepareRelationDirectory: could not get required parameter, aborting: %s\n", strerror(result));
		return result;
	}

	BString relationName(relationType);
	if (relationName.StartsWith(SEN_RELATION_SUPERTYPE "/")) {
		relationName.Remove(0, sizeof(SEN_RELATION_SUPERTYPE));
	}

	BPath relationsDirPath;
	if ((result = find_directory(B_SYSTEM_TEMP_DIRECTORY, &relationsDirPath)) != B_OK) {
		PRINT(("could not find temp directory: %s\n", strerror(result) ));
		return result;
	}
	BString relationsDirName(relationsDirPath.Path());
	relationsDirName.Append("/sen/") << srcId << "/relations/" << relationName
		<< "/" << srcRef.name << "\u2192" << relationLabel << " relations";

	if ((result = create_directory(relationsDirName, B_READ_WRITE) != B_OK)) {
		PRINT(("failed to create temp dir at %s: %s\n", relationsDirName.String(), strerror(result) ));
		return result;
	}

	// populate result param
	BDirectory relationsDir(relationsDirName.String());
	BEntry entry;
	relationsDir.GetEntry(&entry);
	entry.GetRef(&relationInfo->relationDirRef);

	relationInfo->relationType = relationType;
	relationInfo->srcRef = srcRef;
	relationInfo->srcId = srcId;

	return result;
}

status_t TTracker::ConvertAttributesToMessage(const entry_ref* ref, BMessage* params) {
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
