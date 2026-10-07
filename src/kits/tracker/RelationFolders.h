/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <Locker.h>
#include <Message.h>
#include <Node.h>
#include <String.h>

#include <map>

/**
 * @file RelationFolders.h
 * @brief Relation folders as live editors of relations.
 *
 * A relation is shown as a folder of files (see TrackerSenRelations::MaterializeType): one file per relation, its properties are
 * the attributes of the file. The usual operations on files are then the operations on the relation:
 *
 *  - delete a file (or move it out of the folder, e.g. to the Trash): the relation is removed,
 *  - edit an attribute of a file: the properties of the relation change,
 *  - drop a file into the folder of a relation type: a relation of that type to the file is created,
 *  - drop a file into the top level (the folder of all types): a generic relation to it is created,
 *  - move a relation file to the folder of another type of the same source: the relation changes its type.
 *
 * Dynamic relations (made by plugins, not stored) are read-only: they are not registered and nothing is sent.
 *
 * The folders and files are registered when they are created. The pose views report what happens to them (this class does
 * not depend on the pose view, so that it can be tested with a real server and real files) and the changes are sent to the
 * SEN server. All methods may be called from any window thread.
 */
class RelationFolders {
public:
	/** a folder that Tracker created to show relations */
	struct FolderInfo {
		node_ref	node;
		entry_ref	ref;
		entry_ref	sourceRef;
		BString		sourceId;
		BString		relationType;	///< empty for the top level (the folder of all types)
		BString		viewId;			///< the folder (TSID) that holds this view: the top level and the folders of the types
		BMessage	relationConfig;
	};

	/** a file that stands for a stored relation */
	struct FileInfo {
		entry_ref	ref;
		node_ref	folder;
		entry_ref	sourceRef;
		BString		sourceId;
		BString		targetId;
		BString		relationType;
		BString		relationId;		///< only if there are several relations of this type to the target
		bigtime_t	registeredAt;
	};

	static RelationFolders&	Instance();

	void		RegisterFolder(const FolderInfo& folder);
	/** Register the file of a stored relation. Not for dynamic relations: they are read-only. */
	void		RegisterFile(const node_ref& file, const FileInfo& info);
	/** Forget everything (tests). */
	void		Clear();

	bool		IsRelationFolder(const node_ref& node) const;
	bool		IsRelationFile(const node_ref& node) const;

	// What happens in a relation folder, reported by the pose views. All return true if the event was about a relation.

	/** The file is gone: the relation is removed. */
	bool		EntryRemoved(const node_ref& file);
	/** The file was moved to another directory: out of the folder it is a removal (Trash), into another relation folder of the
	 *  same source it changes the type of the relation. */
	bool		EntryMoved(const node_ref& file, const node_ref& newDirectory);
	/** The file was renamed in its folder (the name is not a property of the relation). */
	bool		EntryRenamed(const node_ref& file, const char* name);
	/** An attribute of the file changed: the properties of the relation are sent to the server. */
	bool		AttributesChanged(const node_ref& file, const char* attribute);
	/** Files were dropped on the folder (message with "refs"): relations are created, or moved from another folder. Returns
	 *  true if the folder is a relation folder (the drop is handled, Tracker must not copy or move the files). */
	bool		HandleDrop(const BMessage& drop, const node_ref& folder);

	/** the properties of a relation file: all its attributes in the vocabularies of the relations, without its identity */
	static status_t	ReadProperties(const entry_ref& file, BMessage* properties);

private:
				RelationFolders();

	status_t	Send(BMessage* message, BMessage* reply) const;
	status_t	AddRelation(const FolderInfo& folder, const entry_ref& target);
	status_t	MoveRelation(const FileInfo& info, const FolderInfo& to, const node_ref& fileNode);

	mutable BLocker					fLock;
	std::map<node_ref, FolderInfo>	fFolders;
	std::map<node_ref, FileInfo>	fFiles;
};
