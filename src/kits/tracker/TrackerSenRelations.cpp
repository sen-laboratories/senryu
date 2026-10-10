/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include <filesystem>
#define DEBUG 1

#include <Debug.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <MimeType.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <fs_attr.h>

#include <sen/Sen.h>
#include <sen/SenOntoCore.h>
#include <sen/Sensei.h>

#include "TrackerSenRelations.h"
#include "RelationFolders.h"
#include "TrackerSenLog.h"

bool
TrackerSenRelations::ResolveRelation(const entry_ref* ref, BString* srcId,
	BString* targetId)
{
	status_t result;
	BNode relationNode(ref);
	BNodeInfo relationNodeInfo(&relationNode);

	result = relationNodeInfo.InitCheck();
	if (result != B_OK) {
		PRINT(("error accessing nodeInfo for relation target %s: %s\n", ref->name, strerror(result)));
		return false;
	}

	if (result == B_OK) result = relationNode.ReadAttrString(sen::attr::kRelationSource, srcId);
	if (result == B_NAME_NOT_FOUND) return false;

	if (result == B_OK) result = relationNode.ReadAttrString(sen::attr::kRelationTarget, targetId);
	if (result == B_NAME_NOT_FOUND) return false;

	return (result == B_OK);
}


status_t
TrackerSenRelations::WriteTargetRelations(
	BMessage  *relations,
	BMessage  *relationConf,
	entry_ref *workingDirRef,
	entry_ref *openDirRef)
{
	if (relations->IsEmpty()) {
		PRINT(("no relations found, skipping.\n"));
		return B_OK;
	}

	if (workingDirRef == NULL) {	// start at root relation dir created before
		PRINT(("WriteTargetRelations: starting at ROOT relation path: %s\n", openDirRef->name ));
		workingDirRef = new entry_ref(*openDirRef);
	}

	bool isDynamic = relationConf->GetBool(sen::conf::kDynamic);
	const char* shortName = relationConf->GetString(sen::key::kRelationName);
	// top-level relation type, may vary for individual relations (e.g. self / n-ary relations)
	const char* relationDefaultType = relationConf->GetString(sen::key::kRelationType);

	BDirectory relationDir(workingDirRef);
	BPath relationDirPath(workingDirRef);

	status_t result = relationDir.InitCheck();
	if (result == B_OK)
		result = relationDirPath.InitCheck();

	if (result != B_OK) {
		PRINT(("invalid directory for path %s: %s\n", relationDirPath.Path(), strerror(result) ));
		return result;
	}

	PRINT(("  * working dir is now: '%s'...\n", relationDirPath.Path() ));

	// the item that the user chose to open (of a dynamic relation): the view shows what is below it, as the folder of it would
	const char* selectedId = isDynamic ? relationConf->GetString(sen::key::kItemId, "") : "";

	const char* srcId = relationConf->GetString(sen::key::kSourceId);
	if (srcId == NULL) {
		PRINT(("  x missing relation source, expected inode or SEN:ID\n"));
		return B_BAD_VALUE;
	}

	int32 countRelations = 0;
	relations->GetInfo(sen::key::kRelations, NULL, &countRelations);

	PRINT(("  o processing relation dir '%s' with %d relations...\n", relationDirPath.Path(), countRelations ));

	// iterate through all target relations and get relation properties for each, then populate relation targets dir
    // with matching target refs, creating files with relation properties in file attributes.
	// Note: there can be multiple relations for the same target with different properties
	for (int32 relationIndex = 0; relationIndex < countRelations; relationIndex++) {
		const char*	  targetId;
        BMessage  properties;
		BNode	  relationNode;
		BString   entryName;

		// create individual relation targets for each relation of the current target
		result = relations->FindMessage(sen::key::kRelations, relationIndex, &properties);

		if (result != B_OK) {
			PRINT(("  > could not get relation properties at %d: %s\n",
				relationIndex, strerror(result) ));
			continue;
		}

		PRINT(("got relation properties:\n"));
		properties.PrintToStream();

		targetId = properties.GetString(sen::attr::kTo);

		// used for self (and later also n-ary) relations
		bool hasRelations = properties.HasMessage(sen::key::kRelations);

		// skip intermediate nodes (only contain sub nodes)
		const char* relationType = properties.GetString(sen::key::kRelationType, relationDefaultType);

		// determine useful file name for relation
		// best fit: label, fallback: relation's shortname
		// a relation to a target is named after the target, else after the label of the relation
		const char* targetName = properties.GetString(sensei::key::kName, "");
		entryName = targetName;
		if (entryName.IsEmpty())
			entryName = properties.GetString(sen::attr::kRelationLabel, "");
		if (entryName.IsEmpty()) {
			PRINT(("  x WARN: expected label not found, falling back to short name.\n"));
			entryName = shortName;
		}

		// file/dir name must be valid
		entryName.ReplaceAll('/', '\\');

		// ensure file/dir name is unique
		if (BEntry(&relationDir, entryName.String()).Exists()) {
			// human readable 1-based index, following the first relation #0 (without index)
			entryName << " #" << relationIndex + 1;
		}

		// since dynamic relations are generated, they cannot be changed
		// Note: Haiku is single user so perms have to apply to root, too
		// TODO: there seems to be a bug in Haiku's storage kit or Tracker bc you can still delete read-only files!
		// a relation that is read-only (the relations of an ontology to its types) is shown like one of a plugin
		bool readOnly = isDynamic || properties.GetBool(sen::attr::kRelationReadOnly, false);
		mode_t readWriteMode;

		if (hasRelations) {
			readWriteMode = (readOnly ? 0555 : 0777);	// ugo=rx if read-only, else rwx
		} else {
			readWriteMode = (readOnly ? 0444 : 0555);  // ugo=r  if read-only, else rw
		}

		// create file or dir with appropriate permissions
		if (hasRelations) {
			BPath subDirLoc;
			BDirectory subDir;
			BEntry subDirEntry;
			entry_ref subDirRef;

			result = relationDir.CreateDirectory(entryName, &subDir);
			if (result != B_OK) {
				PRINT(("  > error creating relation subdir '%s'': %s\n",
						entryName.String(), strerror(result) ));
				return result;
			}

			subDir.GetEntry(&subDirEntry);
			subDirEntry.GetPath(&subDirLoc);
			subDirEntry.GetRef(&subDirRef);
			relationNode.SetTo(&subDirRef);

			// write relation dir icon if available
			void*  iconBuf;
			size_t iconSize;

			// a relation folder inside a relation folder is a "contains": it gets the icon of that relation, not of
			// the relation type of its content (e.g. a bookmark)
			result = GetSenIcon(sen::onto::core::mime::kContains, "META:ICON", &iconBuf, &iconSize);
			if (result == B_OK) {
				ssize_t sizeWritten = relationNode.WriteAttr("BEOS:ICON", B_VECTOR_ICON_TYPE, 0, iconBuf, iconSize);
				delete[] (char*)iconBuf;
				if (sizeWritten < 0) {	// can be interpreted as an error code
					PRINT(("  x error writing folder icon %s to %s: %s\n",
							sen::attr::kRelationFolderIcon, entryName.String(), strerror(sizeWritten) ));
					// keep on going, not tragic
				}
			}

			// recurse to create complete relation structure for dynamic relations
			if (isDynamic) {
				// the folder of the chosen item is the one to open (an item is found by its own id, not by the ones of its children)
				const char* itemId = properties.GetString(sen::key::kItemId, "");
				if (strlen(selectedId) > 0 && strcmp(itemId, selectedId) == 0) {
					PRINT(("  * found user selected relations path %s\n", relationDirPath.Path() ));
					*openDirRef = subDirRef;
				}

				result = subDirEntry.InitCheck();

				if (result == B_OK) {
					BMessage nestedRelations;
					result = properties.FindMessage(sen::key::kRelations, &nestedRelations);

					if (result == B_OK && ! nestedRelations.IsEmpty()) {
						PRINT(("  >> entering relation subdir %s...\n", subDirRef.name));

						// process nested relations in new subdir
						result = WriteTargetRelations(&nestedRelations, relationConf, &subDirRef, openDirRef);
						if (result != B_OK) {
							PRINT(("  x error writing target relations, aborting.\n"));
							return result;
						}

						PRINT(("  << leaving relation subdir %s...\n", subDirRef.name));
					}
				}
				if (result != B_OK) {
					PRINT(("  x error writing to relation subdir '%s': %s\n",
							subDirLoc.Path(), strerror(result) ));
					return result;
				}
			}
		} else {
			BFile relationTarget(&relationDir, entryName.String(), readWriteMode | B_CREATE_FILE);
			relationNode.SetTo(&relationDir, entryName.String());

			PRINT(("  > writing to relation file %s...\n", entryName.String() ));
		}

		result = relationNode.InitCheck();
		if (result != B_OK) {
			PRINT(("  x error creating relation target file/dir '%s': %s\n", entryName.String(), strerror(result) ));
			return result;
		}

		relationNode.SetPermissions(readWriteMode);
		BNodeInfo relationNodeInfo(&relationNode);

		result = relationNodeInfo.SetType(relationType);

		// write relation src/target IDs
		BString srcIdStr(srcId);
		BString targetIdStr(targetId);
		if (result == B_OK)
			result = relationNode.WriteAttrString(sen::attr::kRelationSource, &srcIdStr);
		if (result == B_OK)
			result = relationNode.WriteAttrString(sen::attr::kRelationTarget, &targetIdStr);

		// write refs if any
		ssize_t sizeRead, sizeWritten;

		if (result == B_OK) {
			const void* buffer;

			// we deliberately don't use FindRef since we need the raw buffer and size for writing the attribute below
			result = relationConf->FindData(sen::key::kSourceRef, B_REF_TYPE, &buffer, &sizeRead);
			if (result == B_OK) {
				sizeWritten = relationNode.WriteAttr(sen::attr::kRelationSourceRef, B_REF_TYPE, 0, buffer, sizeRead);
				if (sizeWritten <= 0) {
					result = sizeWritten;
					ERROR("  x failed to write relation source ref: %s\n", strerror(result));
					return result;
				}
			} else if (result == B_NAME_NOT_FOUND) {
				result = B_OK;
			}
			// same for target ref
			result = relationConf->FindData(sen::key::kTargetRef, B_REF_TYPE, &buffer, &sizeRead);
			if (result == B_OK) {
				sizeWritten = relationNode.WriteAttr(sen::attr::kRelationTargetRef, B_REF_TYPE, 0, buffer, sizeRead);
				if (sizeWritten <= 0) {
					result = sizeWritten;
					ERROR("  x failed to write relation target ref: %s\n", strerror(result));
					return result;
				}
			} else if (result == B_NAME_NOT_FOUND) {
				result = B_OK;
			}
		}

		if (result != B_OK) {
			ERROR("  x error writing common relation file attributes: %s\n", strerror(result));
			return result;
		}

		int32 countProperties = properties.CountNames(B_ANY_TYPE);

		PRINT(("  * writing %d property attributes for relation #%d, target %s, into file %s:\n",
			   properties.CountNames(B_ANY_TYPE), relationIndex, targetId, entryName.String() ));

		// create a file for each set of properties for all targets
		for (int32 propertyIndex = 0; propertyIndex < countProperties; propertyIndex++) {
			// write out relation properties as file attributes according to message field type (== MIME attr type)
			char*        propertyName;
			type_code    propertyType;
			const void*  data;
			ssize_t	     size;

			result = properties.GetInfo(B_ANY_TYPE, propertyIndex, &propertyName, &propertyType, NULL);
			if (result != B_OK) {
				PRINT(("  x error retrieving propertyName #%d for relation %d of target %s: %s\n",
						propertyIndex, relationIndex, targetId, strerror(result) ));
				continue;
			}

			// the target is in SEN:REL:TO; as SEN:TO the file would be taken for a file that links to the target
			if (strcmp(propertyName, sen::attr::kTo) == 0)
				continue;

			// don't write attributes for internal properties (starting with "_"),
			// they are only temporary or have already been translated to full names.
			if (strncmp(propertyName, "_", 1) == 0) {
				PRINT(("  - skipping internal attribute %s.\n", propertyName));
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

			sizeWritten = relationNode.WriteAttr(propertyName, propertyType, 0, data, size);
			if (sizeWritten < size) {
				if (result < 0)
					result = sizeWritten;
				else
					result = B_BAD_VALUE;

				ERROR(("  x failed to write relation property attribute %s: %s.\n"),
					propertyName, strerror(result));

				return result;
			}
		}   // properties loop
		// sync after finished writing attributes
		relationNode.Sync();

		// a stored relation can be edited by working with its file: delete, change attributes, move
		entry_ref storedSourceRef;
		if (!readOnly && !hasRelations && relationConf->FindRef(sen::key::kSourceRef, &storedSourceRef) == B_OK) {
			RelationFolders::FileInfo info;
			BEntry fileEntry(&relationDir, entryName.String());
			node_ref fileNode;
			if (fileEntry.GetRef(&info.ref) == B_OK && fileEntry.GetNodeRef(&fileNode) == B_OK
					&& relationDir.GetNodeRef(&info.folder) == B_OK) {
				info.sourceRef = storedSourceRef;
				info.sourceId = srcId;
				info.targetId = targetId;
				info.relationType = relationType;
				info.relationId = properties.GetString(sen::key::kRelationId, "");
				RelationFolders::Instance().RegisterFile(fileNode, info);
			}
		}

	} // relations loop

	return B_OK;
}


status_t
TrackerSenRelations::GetRelationAttributeInfo(const char* relationType, BMessage* attrInfo) {
	// get defined attributes for MIME type of relation, same for all targets of this relation
	BMimeType relationMimeType(relationType);
	status_t result = relationMimeType.GetAttrInfo(attrInfo);
	if (result != B_OK) {
		// a type without attributes of its own still has the ones of all relations
		PRINT(("no attribute info for relation with type %s: %s\n", relationType, strerror(result)));
		attrInfo->MakeEmpty();
	}

	// get additional attributes from relation supertype
	BMimeType relationSuperType;
	relationMimeType.GetSupertype(&relationSuperType);

	if (!relationSuperType.IsInstalled()) {
		PRINT(("MIME supertype for relation %s is not installed, check SEN installation!\n", sen::mime::kRelationSupertype));
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
TrackerSenRelations::CreateRelationDirectory(
	const char* viewId,
	const char* sourceId,
	const char* sourceName,
	const char* relationType,
	const BMessage* relationConfig,
	entry_ref* relationDirRef)
{
	const char* relationName = relationConfig->GetString(sen::key::kRelationName, relationType);	// fall back
	const char* relationLabel = relationConfig->GetString(sen::key::kRelationLabel, relationName);

	PRINT(("* creating relation dir for relation '%s' with name '%s' and label '%s'...\n",
			relationType, relationName, relationLabel));

	status_t result;
	BPath relationsDirPath;

	result = find_directory(B_SYSTEM_TEMP_DIRECTORY, &relationsDirPath);
	if (result != B_OK) {
		PRINT(("could not find temp directory: %s\n", strerror(result) ));
		return result;
	}

	result = relationsDirPath.Append("sen");
	relationsDirPath.Append(viewId);
	// Note: since relations are required to be a subtype of relation,
	//       this will automatically create a relation subdir, handy:)
	relationsDirPath.Append(relationType);

	// TODO: prepare suitable folder attribute layout using archived relation attribute columns
	// see PoseView::SaveState() and ViewState::ArchiveToStream()
	// better yet, do this in BPoseView::SetupDefaultColumnsIfNeeded()

	BDirectory relationDir(relationsDirPath.Path());
	BEntry relationDirEntry;
	relationDir.GetEntry(&relationDirEntry);

	if (relationDirEntry.Exists()) {
		// sadly there is no Haiku native wrapper for this (yet?)
		// see https://discuss.haiku-os.org/t/missing-remove-directory-to-complement-create-directory
		PRINT(("* removing existing temp relations dir %s\n", relationsDirPath.Path() ));

		std::filesystem::path path(relationsDirPath.Path());
		uint32 entriesDeleted = std::filesystem::remove_all(path);

		if (entriesDeleted <= 0) {
			PRINT(("failed to remove existing temp relations dir %s\n", relationsDirPath.Path() ));
		}
	} else {
		PRINT(("creating relation temp dir at: %s\n", relationsDirPath.Path() ));
	}

	result = create_directory(relationsDirPath.Path(), B_READ_WRITE);
	if (result != B_OK) {
		PRINT(("failed to create temp relations dir at %s: %s\n", relationsDirPath.Path(), strerror(result) ));
		return result;
	}

	// update Dir and ref
	relationDir.SetTo(relationsDirPath.Path());	// force update
	relationDir.GetEntry(&relationDirEntry);

	// add a friendly display name
	BString folderLabel(sourceName);
	folderLabel << " → "  << relationLabel << " relations";
	BNode relationNode(&relationDirEntry);

	result = relationNode.InitCheck();

	// write file and meta type for relation dir
	if (result == B_OK) {
		BNodeInfo relationNodeInfo(&relationNode);

		if (result == B_OK) result = relationNodeInfo.InitCheck();
		if (result == B_OK) result = relationNodeInfo.SetType(relationType);
		// mark as relation folder for some special features in Tracker (e.g. add/remove relations)
		if (result == B_OK) result = relationNode.WriteAttrString("META:TYPE", new BString(sen::mime::kRelationFolder));
		// add relation properties so we can populate the folder later with proper relation targets
		// todo: move to SEN:ID for easier uniform handling here?
		if (result == B_OK) result = relationNode.WriteAttrString(sen::attr::kRelationSource, new BString(sourceId));
		// TODO: also attach relation message for self relations
		if (result == B_OK) result = relationNode.WriteAttrString(sen::attr::kFolderName, &folderLabel);
	}

	// return ref
	relationDirEntry.GetRef(relationDirRef);

	return result;
}


/** The vocabularies (attribute name prefixes) that relation properties are named with, see the ontologies of SEN. */
static bool
IsRelationPropertyAttribute(const char* name)
{
	static const char* const kVocabularies[] = {"SEN:", "schema:", "oa:", "be:", "dc:", "dcterms:", "foaf:"};
	for (const char* prefix : kVocabularies) {
		if (strncmp(name, prefix, strlen(prefix)) == 0)
			return true;
	}
	return false;
}


status_t
TrackerSenRelations::ConvertAttributesToMessage(const entry_ref* ref, BMessage* params)
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
		// relation properties are named by the vocabularies of the relation types (SEN:REL:*, schema:*, oa:*, be:*, ...);
		// everything else (BEOS:TYPE, ...) belongs to the file
		if (! IsRelationPropertyAttribute(attrName)) {
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


BString
TrackerSenRelations::DisplayNameOf(const entry_ref& ref)
{
	BNode node(&ref);
	BString name;
	if (node.InitCheck() == B_OK) {
		for (const char* attribute : {"dc:title", "META:S:DESC"}) {
			if (node.ReadAttrString(attribute, &name) == B_OK && !name.IsEmpty())
				return name;
		}
	}
	return BString(ref.name);
}


static bool
ReadTargetRef(BNode& node, entry_ref* target)
{
	attr_info info;
	if (node.GetAttrInfo(sen::attr::kRelationTargetRef, &info) != B_OK || info.type != B_REF_TYPE || info.size <= 0)
		return false;

	// the ref is stored as a message holds it: flattened
	bool found = false;
	char* buffer = new char[info.size];
	if (node.ReadAttr(sen::attr::kRelationTargetRef, B_REF_TYPE, 0, buffer, info.size) == info.size) {
		BMessage holder;
		entry_ref ref;
		if (holder.AddData("ref", B_REF_TYPE, buffer, info.size, false) == B_OK && holder.FindRef("ref", &ref) == B_OK
				&& BEntry(&ref).Exists()) {
			*target = ref;
			found = true;
		}
	}
	delete[] buffer;
	return found;
}


entry_ref
TrackerSenRelations::RelationTargetOrSelf(const entry_ref* ref)
{
	// a folder is a nested (n-ary) relation: what is done with it is done with that relation
	BEntry entry(ref);
	if (entry.InitCheck() != B_OK || entry.IsDirectory())
		return *ref;

	BNode node(ref);
	entry_ref target;
	if (node.InitCheck() != B_OK || !ReadTargetRef(node, &target))
		return *ref;
	return target;
}


bool
TrackerSenRelations::SelfRelationNode(const entry_ref* ref, entry_ref* source, BMessage* relations)
{
	BNode node(ref);
	attr_info info;
	// the nodes of the view of a plugin have the id of their item (stored relations have none)
	if (node.InitCheck() != B_OK || node.GetAttrInfo(sen::key::kItemId, &info) != B_OK || !ReadTargetRef(node, source))
		return false;

	relations->MakeEmpty();
	if (node.GetAttrInfo(sen::key::kRelations, &info) == B_OK && info.type == B_MESSAGE_TYPE && info.size > 0) {
		char* buffer = new char[info.size];
		if (node.ReadAttr(sen::key::kRelations, B_MESSAGE_TYPE, 0, buffer, info.size) == info.size)
			relations->Unflatten(buffer);
		delete[] buffer;
	}
	return true;
}


// copied from SEN SelfRelationHandler
status_t
TrackerSenRelations::GetInodeForRef(const entry_ref* srcRef, BString* inode)
{
	// get inode as folder ID instead of SEN:ID, no need to create one for now
	BEntry srcEntry(srcRef);
	status_t result = srcEntry.InitCheck();

	if (result == B_OK) {
		struct stat srcStat;
		result = srcEntry.GetStat(&srcStat);

		if (result == B_OK) {
			*inode << srcStat.st_ino;
		}
	}
	if (result != B_OK) {
		PRINT(("WARNING: could not get inode for srcRef %s: %s\n", srcRef->name, strerror(result) ));
		// fall back
		*inode << srcRef->device << "_" << srcRef->directory << "_" << srcRef->name;
	}

	return result;
}


// todo: move to library or SEN core later
status_t
TrackerSenRelations::GetSenIcon(const char* mimeType, const char* iconType, void** iconBuffer, size_t* iconSize) {
	status_t result;

	// check for supported icon types
	BString type(iconType);
	if (type != sen::attr::kRelationFolderIcon && type != "META:ICON") {
		return B_BAD_VALUE;
	}

	BPath path;
	result = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (result != B_OK) {
		fprintf(stderr, "could not find user settings directory: %s\n", strerror(result));
		return result;
	}

	path.Append("mime_db");

	BDirectory mimeDir(path.Path());
	BEntry mimeEntry(&mimeDir, mimeType);

	result = mimeEntry.InitCheck();
	if (result != B_OK) {
		fprintf(stderr, "error accessing MIME type %s in MIME DB: %s\n", mimeType, strerror(result));
		return result;
	}
	if (! mimeEntry.Exists()) {
		fprintf(stderr, "could not find MIME type %s in MIME DB: %s\n", mimeType, strerror(result));
		return B_ENTRY_NOT_FOUND;
	}

	// read icon attribute from MIME type file directly
	BNode mimeNode(&mimeEntry);
	if (mimeNode.InitCheck() != B_OK) {
		fprintf(stderr, "error accessing MIME DB file %s: %s\n", mimeType, strerror(result));
		return result;
	}

	// get size for icon attribute
	attr_info attrInfo;
	result = mimeNode.GetAttrInfo(iconType, &attrInfo);
	if (result != B_OK) {
		PRINT(("  x failed to get attr_info of type '%s' for icon attribute '%s': %s\n",
			mimeType, iconType, strerror(result) ));
		return result;
	}

	*iconBuffer = new char[attrInfo.size];
	*iconSize   = attrInfo.size;

	ssize_t sizeRead = mimeNode.ReadAttr(iconType, B_VECTOR_ICON_TYPE, 0, *iconBuffer, *iconSize);
	if (sizeRead < 0) {
		result = sizeRead;	// can be interpreted as error code

		fprintf(stderr, "error reading %s icon from MIME DB type %s: %s\n",
				iconType, mimeType, strerror(result));
		return result;
	}

	return result;
}


// moved from BPoseView::ExtractSenParams (PoseViewSen.cpp) - operates only on
// the passed-in messages, no pose/selection state involved.
status_t
TrackerSenRelations::ExtractSenParams(const BMessage* message, BMessage* enrichedMessage)
{
	entry_ref srcRef;
	status_t result = message->FindRef(sen::key::kSourceRef, &srcRef);
	if (result == B_OK) {
		enrichedMessage->AddRef(sen::key::kSourceRef, &srcRef);
	}

	// add all properties from this item at this index (e.g. page, position,...)
	char* name;
	int32 count;
	int32 index = 0;
	type_code typeCode;
	result = B_OK;	// could be B_NAME_NOT_FOUND from above!

	while (result == B_OK) {
		result = message->GetInfo(B_ANY_TYPE, index, &name,	&typeCode, &count);
		if (result != B_OK) {
			if (result == B_BAD_INDEX) {
				break;	// end of line, done
			}
			PRINT(("failed to get message info at index #%d: %s\n", index, strerror(result)));
			return result;
		}

		// add message property only if it comes from SEN
		if (BString(name).IStartsWith(sen::attr::kPrefix) || BString(name).IStartsWith(sen::attr::kPrefix)) {
			PRINT(("adding SEN properties at index %d with name %s and count %d:\n", index, name, count));
		}
		const void* data;
		ssize_t size;

		result = message->FindData(name, typeCode, index, &data, &size);
		if (result != B_OK) {
			PRINT(("failed to get message data for property %s[%d]: %s\n",
					name, index, strerror(result)));
			return result;
		}

		// finally copy over data to keep
		result = enrichedMessage->AddData(name, typeCode, data, size);
		if (result != B_OK) {
			PRINT(("failed to add message data '%s' at index %d: %s\n",
					name, index, strerror(result)));
			return result;
		}

		index++;
	}

	return B_OK;	// all params are optional for now
}


void
TrackerSenRelations::NewViewId(BString* viewId)
{
	viewId->SetTo(sen::id::New().c_str());
}


void
TrackerSenRelations::RelationsToList(const BMessage& relations, const BMessage& idToRef, const BMessage& idToName,
	BMessage* list)
{
	char* targetId;
	type_code type;
	int32 count;
	for (int32 i = 0; relations.GetInfo(B_MESSAGE_TYPE, i, &targetId, &type, &count) == B_OK; i++) {
		for (int32 set = 0; set < count; set++) {
			BMessage properties;
			if (relations.FindMessage(targetId, set, &properties) != B_OK)
				continue;

			properties.RemoveName(sen::attr::kTo);
			properties.AddString(sen::attr::kTo, targetId);

			entry_ref target;
			BString name;
			if (idToRef.FindRef(targetId, &target) == B_OK) {
				// the title of the target, not its file name (the name of a MIME type is not what a user knows it by)
				if (idToName.FindString(targetId, &name) != B_OK || name.IsEmpty())
					name = target.name;
				// the file of the relation stands for it: the menus of the relation are about the target
				properties.RemoveName(sen::attr::kRelationTargetRef);
				properties.AddRef(sen::attr::kRelationTargetRef, &target);
			} else {
				// a relation to a file that does not exist (any more)
				name = properties.GetString(sen::attr::kRelationLabel, targetId);
				name << " (missing)";
			}
			properties.RemoveName(sensei::key::kName);
			properties.AddString(sensei::key::kName, name);

			list->AddMessage(sen::key::kRelations, &properties);
		}
	}
}


status_t
TrackerSenRelations::MaterializeType(const entry_ref& sourceRef, const char* viewId, const char* relationType,
	entry_ref* typeDirRef)
{
	BMessage ask(sen::cmd::kRelationsGet);
	ask.AddRef(sen::key::kSourceRef, &sourceRef);
	ask.AddString(sen::key::kRelationType, relationType);
	ask.AddBool(sen::key::kIdToRefMap, true);

	BMessenger server(sen::kServerSignature);
	BMessage reply;
	status_t result = server.IsValid() ? server.SendMessage(&ask, &reply, 5000000, 5000000) : B_ERROR;
	if (result == B_OK)
		result = reply.GetInt32(sen::key::kResult, B_ERROR);
	if (result != B_OK) {
		ERROR("could not get the relations of type %s from the SEN server: %s\n", relationType, strerror(result));
		return result;
	}

	BMessage relations, idToRef, idToName, configs, relationConfig;
	reply.FindMessage(sen::key::kRelations, &relations);
	reply.FindMessage(sen::key::kIdToRefMap, &idToRef);
	reply.FindMessage(sen::key::kIdToNameMap, &idToName);
	if (reply.FindMessage(sen::key::kRelationConfigMap, &configs) != B_OK
			|| configs.FindMessage(relationType, &relationConfig) != B_OK) {
		return B_NAME_NOT_FOUND;
	}

	// relations of plugins are resolved at run time, not stored: they cannot be edited and are shown by their menus
	if (relationConfig.GetBool(sen::conf::kDynamic, false) || relationConfig.GetBool(sen::conf::kSelf, false))
		return B_NOT_SUPPORTED;

	BString sourceId(reply.GetString(sen::key::kSourceId, ""));
	if (sourceId.IsEmpty())
		GetInodeForRef(&sourceRef, &sourceId);

	result = CreateRelationDirectory(viewId, sourceId.String(), DisplayNameOf(sourceRef).String(), relationType, &relationConfig, typeDirRef);
	if (result != B_OK)
		return result;

	BMessage list;
	RelationsToList(relations, idToRef, idToName, &list);

	BMessage config(relationConfig);
	config.AddString(sen::key::kSourceId, sourceId);
	config.AddRef(sen::key::kSourceRef, &sourceRef);
	config.AddString(sen::key::kRelationType, relationType);

	entry_ref workingRef(*typeDirRef), openRef(*typeDirRef);
	result = WriteTargetRelations(&list, &config, &workingRef, &openRef);
	if (result != B_OK)
		return result;

	RelationFolders::FolderInfo folder;
	BEntry folderEntry(typeDirRef);
	if (folderEntry.GetNodeRef(&folder.node) == B_OK) {
		folder.ref = *typeDirRef;
		folder.sourceRef = sourceRef;
		folder.sourceId = sourceId;
		folder.relationType = relationType;
		folder.viewId = viewId;
		folder.relationConfig = relationConfig;
		RelationFolders::Instance().RegisterFolder(folder);
	}
	return B_OK;
}
