/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
*/

#include <filesystem>
#define DEBUG 1

#include <Debug.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <MimeType.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <fs_attr.h>

#include <sen/Sen.h>
#include <sen/Sensei.h>

#include "TrackerSenRelations.h"

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

	if (result == B_OK) result = relationNode.ReadAttrString(SEN_RELATION_SOURCE_ATTR, srcId);
	if (result == B_NAME_NOT_FOUND) return false;

	if (result == B_OK) result = relationNode.ReadAttrString(SEN_RELATION_TARGET_ATTR, targetId);
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

	bool isDynamic = relationConf->GetBool(SEN_RELATION_IS_DYNAMIC);
	const char* shortName = relationConf->GetString(SEN_RELATION_NAME);
	// top-level relation type, may vary for individual relations (e.g. self / n-ary relations)
	const char* relationDefaultType = relationConf->GetString(SEN_RELATION_TYPE);

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

	// for dynamic relations, check if current working dir is the selected target we want to open later
	BString itemId, selectedId;

	if (isDynamic) {
		relations->FindString(SEN_RELATION_ITEM_ID, &itemId);

		if (! itemId.IsEmpty()) {
			relationConf->FindString(SEN_RELATION_ITEM_ID, &selectedId);

			PRINT(("  > check if path is the one to open: %s (current) <-> %s (selected)\n",
					itemId.String(), selectedId.String() ));

			if (itemId == selectedId) {
				PRINT(("  * found user selected relations path %s\n", relationDirPath.Path() ));
				// update open ref so caller knows what directory the user expects to enter
				*openDirRef = *workingDirRef;
			}
		}
	}

	const char* srcId = relationConf->GetString(SEN_RELATION_SOURCE_ID);
	if (srcId == NULL) {
		PRINT(("  x missing relation source, expected inode or SEN:ID\n"));
		return B_BAD_VALUE;
	}

	int32 countRelations = 0;
	relations->GetInfo(SEN_RELATIONS, NULL, &countRelations);

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
		result = relations->FindMessage(SEN_RELATIONS, relationIndex, &properties);

		if (result != B_OK) {
			PRINT(("  > could not get relation properties at %d: %s\n",
				relationIndex, strerror(result) ));
			continue;
		}

		PRINT(("got relation properties:\n"));
		properties.PrintToStream();

		targetId = properties.GetString(SEN_TO_ATTR);

		// used for self (and later also n-ary) relations
		bool hasRelations = properties.HasMessage(SEN_RELATIONS);

		// skip intermediate nodes (only contain sub nodes)
		const char* relationType = properties.GetString(SEN_RELATION_TYPE, relationDefaultType);

		// determine useful file name for relation
		// best fit: label, fallback: relation's shortname
		entryName = properties.GetString(SEN_RELATION_LABEL_ATTR);
		if (entryName == NULL) {
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
		mode_t readWriteMode;

		if (hasRelations) {
			readWriteMode = (isDynamic ? 0555 : 0777);	// ugo=rx if dynamic, else rwx
		} else {
			readWriteMode = (isDynamic ? 0444 : 0555);  // ugo=r  if dynamic, else rw
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

			result = GetSenIcon(relationType, SEN_RELATION_FOLDER_ICON, &iconBuf, &iconSize);
			if (result == B_OK) {
				ssize_t sizeWritten = relationNode.WriteAttr("BEOS:ICON", B_VECTOR_ICON_TYPE, 0, iconBuf, iconSize);
				if (sizeWritten < 0) {	// can be interpreted as an error code
					PRINT(("  x error writing folder icon %s to %s: %s\n",
							SEN_RELATION_FOLDER_ICON, entryName.String(), strerror(sizeWritten) ));
					// keep on going, not tragic
				}
			}

			// recurse to create complete relation structure for dynamic relations
			if (isDynamic) {
				result = subDirEntry.InitCheck();

				if (result == B_OK) {
					BMessage nestedRelations;
					result = properties.FindMessage(SEN_RELATIONS, &nestedRelations);

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
			result = relationNode.WriteAttrString(SEN_RELATION_SOURCE_ATTR, &srcIdStr);
		if (result == B_OK)
			result = relationNode.WriteAttrString(SEN_RELATION_TARGET_ATTR, &targetIdStr);

		// write refs if any
		ssize_t sizeRead, sizeWritten;

		if (result == B_OK) {
			const void* buffer;

			// we deliberately don't use FindRef since we need the raw buffer and size for writing the attribute below
			result = relationConf->FindData(SEN_RELATION_SOURCE_REF, B_REF_TYPE, &buffer, &sizeRead);
			if (result == B_OK) {
				sizeWritten = relationNode.WriteAttr(SEN_RELATION_SOURCE_REF_ATTR, B_REF_TYPE, 0, buffer, sizeRead);
				if (sizeWritten <= 0) {
					result = sizeWritten;
					ERROR("  x failed to write relation source ref: %s\n", strerror(result));
					return result;
				}
			} else if (result == B_NAME_NOT_FOUND) {
				result = B_OK;
			}
			// same for target ref
			result = relationConf->FindData(SEN_RELATION_TARGET_REF, B_REF_TYPE, &buffer, &sizeRead);
			if (result == B_OK) {
				sizeWritten = relationNode.WriteAttr(SEN_RELATION_TARGET_REF_ATTR, B_REF_TYPE, 0, buffer, sizeRead);
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

	} // relations loop

	return B_OK;
}


status_t
TrackerSenRelations::GetRelationAttributeInfo(const char* relationType, BMessage* attrInfo) {
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
TrackerSenRelations::CreateRelationDirectory(
	const char* folderId,
	const char* relationType,
	const BMessage* relationConfig,
	entry_ref* relationDirRef)
{
	const char* relationName = relationConfig->GetString(SEN_RELATION_NAME, relationType);	// fall back
	const char* relationLabel = relationConfig->GetString(SEN_RELATION_LABEL, relationName);

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
	relationsDirPath.Append(folderId);
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
	BString folderLabel(relationLabel);
	folderLabel << " → "  << relationLabel << " relations";
	BNode relationNode(&relationDirEntry);

	result = relationNode.InitCheck();

	// write file and meta type for relation dir
	if (result == B_OK) {
		BNodeInfo relationNodeInfo(&relationNode);

		if (result == B_OK) result = relationNodeInfo.InitCheck();
		if (result == B_OK) result = relationNodeInfo.SetType(relationType);
		// mark as relation folder for some special features in Tracker (e.g. add/remove relations)
		if (result == B_OK) result = relationNode.WriteAttrString("META:TYPE", new BString(SEN_RELATION_FOLDER_TYPE));
		// add relation properties so we can populate the folder later with proper relation targets
		// todo: move to SEN:ID for easier uniform handling here?
		if (result == B_OK) result = relationNode.WriteAttrString(SEN_RELATION_SOURCE_ATTR, new  BString(folderId));
		// TODO: also attach relation message for self relations
		if (result == B_OK) result = relationNode.WriteAttrString(META_FOLDER_NAME, &folderLabel);
	}

	// return ref
	relationDirEntry.GetRef(relationDirRef);

	return result;
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
	if (type == NULL || type != SEN_RELATION_FOLDER_ICON || type != "META:ICON") {
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

	ssize_t sizeRead = mimeNode.ReadAttr(SEN_RELATION_FOLDER_ICON, B_VECTOR_ICON_TYPE, 0, *iconBuffer, *iconSize);
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
	status_t result = message->FindRef(SEN_RELATION_SOURCE_REF, &srcRef);
	if (result == B_OK) {
		enrichedMessage->AddRef(SEN_RELATION_SOURCE_REF, &srcRef);
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
		if (BString(name).IStartsWith(SEN_ATTR_PREFIX) || BString(name).IStartsWith(SENSEI_ATTR_PREFIX)) {
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
