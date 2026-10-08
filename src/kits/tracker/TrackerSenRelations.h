/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
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
	/**
	 * Create the folder that shows the relations of a type, below a folder of the view: `<temp>/sen/<viewId>/<relation type>`.
	 * Every view has its own folder (a TSID), so that several views of the same file, or of files of different volumes (whose
	 * inodes are not unique), never share or overwrite one.
	 * @param viewId   the folder of the view, see NewViewId()
	 * @param sourceId the SEN:ID of the source of the relations (or its inode, where it has no ID), kept in the folder
	 * @param sourceName the name of the source file, shown in the title of the folder ("<source> → <relation> relations")
	 */
	static status_t	CreateRelationDirectory(const char* viewId, const char* sourceId, const char* sourceName,
						const char* relationType,
						const BMessage* relationConfig,
						entry_ref* relationDirRef);
	/** A new, unique name for the folder of a view: a TSID. */
	static void		NewViewId(BString* viewId);
	/**
	 * Show the stored relations of a type as files: ask the SEN server, make the folder of the type and a file for each
	 * relation (named after its target, the properties are its attributes). The files are registered with RelationFolders,
	 * which makes them editable. Dynamic relations are not stored and not handled here.
	 * @param sourceRef    the file whose relations are shown
	 * @param viewId       the folder of the view, see NewViewId()
	 * @param typeDirRef   receives the folder of the type
	 */
	static status_t	MaterializeType(const entry_ref& sourceRef, const char* viewId,
						const char* relationType, entry_ref* typeDirRef);
	/**
	 * Convert the relations of the server (target ID -> properties) into the list that WriteTargetRelations() expects: one
	 * item per relation with the target ID, the name of the target (from the resolved targets) and the properties.
	 */
	static void		RelationsToList(const BMessage& relations, const BMessage& idToRef, const BMessage& idToName, BMessage* list);
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
