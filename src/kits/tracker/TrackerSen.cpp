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
			result = message->FindString("type", &targetType);
			if (result != B_OK) {
				PRINT(("could not find target type: %s\n", strerror(result) ));
				return true;	// abort
			}

			entry_ref sourceRef;
			result = message->FindRef("refs", &sourceRef);
			if (result != B_OK) {
				PRINT(("could not get source ref: %s\n", strerror(result) ));
				return true;	// abort
			}

			entry_ref targetRef;
			BMimeType filterType(SEN_ENTITY_SUPERTYPE);
			BMessage  templateTypeToRef;
			result =  TemplateUtils::GetInstalledTemplates(NULL, &filterType, &templateTypeToRef);

			if (result == B_OK) {
				PRINT(("got templates:\n"));
				templateTypeToRef.PrintToStream();

				result =  templateTypeToRef.FindRef(targetType.String(), &targetRef);
				if (result == B_NAME_NOT_FOUND) {
					PRINT(("could not find template for type %s, creating a temporary one...\n",
						targetType.String() ));
					// no template for type, let's create an empty one on the fly
					result = TemplateUtils::GetTemplateForType(targetType.String(), &targetRef);
				} else {
					PRINT(("got target template ref %s for creating new type %s.\n",
						targetRef.name, targetType.String() ));
				}
			}
			if (result != B_OK) {
				PRINT(("could not resolve template for target type %s: %s\n",
						targetType.String(), strerror(result) ));
				return true;	// abort
			}

			// associations are handled the same until here, where we never create a new target in that case
			if (relationType == SEN_LABEL_RELATION_TYPE) {
				// just add a relation to the existing association meta entity
				PRINT(("adding relation to META entity for association %s of type %s\n",
					targetRef.name, targetType.String() ));

				// send SEN scripting message to add relation of desired type
				BMessage senAddRelationMsg(SEN_RELATION_ADD);
				senAddRelationMsg.AddRef(SEN_RELATION_SOURCE_REF, new entry_ref(sourceRef));
				senAddRelationMsg.AddRef(SEN_RELATION_TARGET_REF, new entry_ref(targetRef));
				senAddRelationMsg.AddString(SEN_RELATION_TYPE, relationType);

				senAddRelationMsg.PrintToStream();

				BMessenger senMsgr(SEN_SERVER_SIGNATURE);
				if (senMsgr.IsValid()) {
					senMsgr.SendMessage(&senAddRelationMsg);
				} else {
					PRINT(("could not reach sen_server."));
				}

				return true;	// done
			}

			BMessage msgCreateNewFromTemplate(kNewEntryFromTemplate);
			msgCreateNewFromTemplate.AddString(SEN_RELATION_TYPE, relationType);
			msgCreateNewFromTemplate.AddRef(SEN_RELATION_SOURCE_REF, &sourceRef);
			msgCreateNewFromTemplate.AddRef("refs_template", &targetRef);
			msgCreateNewFromTemplate.AddString("name", targetRef.name);

			BMessenger trackerMessenger;
			result = message->FindMessenger("TrackerViewToken", &trackerMessenger);

			if (result == B_OK) {
				result = trackerMessenger.SendMessage(&msgCreateNewFromTemplate);
			}
			if (result != B_OK) {
				PRINT(("failed to send newFromTemplate message to Tracker: %s\n", strerror(result) ));
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
		// done handling message, send as refs to Tracker for opening as normal
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
	if (result == B_NAME_NOT_FOUND) {
		// todo: check for self relation
		return false;
	}
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
	relationTargetsMsg.AddString(SEN_RELATION_SOURCE, relationInfo->source);
    relationTargetsMsg.AddString(SEN_RELATION_TYPE, relationInfo->relationType);

	BMessage reply;
	senMessenger.SendMessage(&relationTargetsMsg, &reply);

    // check relations
	BMessage relations;
	if (reply.FindMessage(SEN_RELATIONS, &relations) != B_OK) {
		PRINT(("no relations for type %s and source %s to show.\n",
			relationInfo->relationType.String(), relationInfo->source.String() ));
			return B_OK;
	}

	PRINT(("got relations for type %s for source %s:\n", relationInfo->relationType.String(), relationInfo->source.String()) );
	relations.PrintToStream();

	// get MIME type for target relation
	BMessage attrInfo;
	if ((result = GetRelationTypeAttributeInfo(relationInfo->relationType, &attrInfo)) != B_OK) {
		return result;
	}

	BMessage relationProperties;
	result = relations.FindMessage("properties", &relationProperties);
	if (result != B_OK) {
		ERROR("no relation properties found for source %s: %s\n", relationInfo->source.String(), strerror(result));
		// still continue and just create blank relation files
	}

	PRINT(("got relation properties:\n"));
	relationProperties.PrintToStream();

	BStringList targetIds;
    result = relations.FindStrings(SEN_TO_ATTR, &targetIds);
	if (result != B_OK) {
		ERROR("could not get relation targetIds for source %s: %s\n", relationInfo->source.String(), strerror(result));
		return result;
	}

	// get ID lookup map
	BMessage idToRefMap;
	result = relations.FindMessage(SEN_ID_TO_REF_MAP, &idToRefMap);
	if (result != B_OK) {
		ERROR("could not get ref mapping for source %s: %s\n", relationInfo->source.String(), strerror(result));
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

	PRINT(("got attrInfo for supertype:\n"));
	attrInfoSuperType.PrintToStream();

	// merge with attributes from supertype (relation)
	result = attrInfo->Append(attrInfoSuperType);
/* fixme: works but check returns 'Bad argument type passed to function' ?!
	if (result != B_OK) {
		PRINT(("failed to construct attribute info for relation type and supertype: %s\n", strerror(result) ));
		return result;
	}
 */
	PRINT(("got attributeInfo for type %s and supertype %s:\n", relationType, relationSuperType.Type()) );
	attrInfo->PrintToStream();

	return B_OK;
}

status_t
TTracker::PrepareRelationDirectory(BMessage *message, RelationInfo* relationInfo)
{
	status_t result = B_OK;

	BString src, srcId, relationType, relationLabel;

	result = message->FindString(SEN_RELATION_SOURCE, &src);
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
		<< "/" << BPath(src).Leaf() << "\u2192" << relationLabel << " relations";

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
	relationInfo->source = src;
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
