/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
*/

#pragma once

#include <Entry.h>
#include <Message.h>
#include <String.h>
#include <SupportDefs.h>

// Stateless SEN relation attribute/filesystem I/O, factored out of TTracker
// so it can be exercised without a live Tracker instance (see TemplateUtils
// for the reference pattern).
class TrackerSenRelations {
public:
	static bool		ResolveRelation(const entry_ref* ref, BString* srcId,
						BString* targetId);
	static status_t	GetSenIcon(const char* mimeType, const char* iconType,
						void** icon, size_t* iconSize);
	static status_t	GetInodeForRef(const entry_ref* srcRef, BString* inode);
	static status_t	WriteTargetRelations(BMessage* relations,
						BMessage* relationConf, entry_ref* workingDirRef,
						entry_ref* openDirRef);
	static status_t	CreateRelationDirectory(const char* folderId,
						const char* relationType,
						const BMessage* relationConfig,
						entry_ref* relationDirRef);
	static status_t	ConvertAttributesToMessage(const entry_ref* ref,
						BMessage* params);
	static status_t	GetRelationAttributeInfo(const char* relationType,
						BMessage* attrInfo);

	// used by BPoseView::HandleSenMessage; doesn't touch pose/selection
	// state, only the passed-in messages.
	static status_t	ExtractSenParams(const BMessage* message,
						BMessage* enrichedMessage);

	// only static access allowed
	TrackerSenRelations() = delete;
};
