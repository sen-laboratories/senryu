/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <Locker.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <Node.h>
#include <String.h>

#include <map>
#include <set>
#include <vector>

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
 * The folders and files are registered when they are created. A looper of its own watches every registered folder with the node
 * monitor: one watch per folder with `B_WATCH_DIRECTORY | B_WATCH_CHILDREN | B_WATCH_ATTR` reports entries that are created, removed,
 * moved or renamed, and the attribute changes of all files directly in it, also of files that are created later. The folder
 * is watched whether or not a window shows it, and whoever changes it (Tracker, a shell, another program). The changes are sent to
 * the SEN server. Only the drop of files is handled by the pose view (see HandleDrop), because it is an interaction, not an event.
 * The event methods below are public so that they can be tested without a file system.
 * All methods may be called from any thread.
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
		bool		common = false;	///< the common view of several files (what they have in common): nothing is dropped into it, see "Open related (common)"
		bool		all = false;	///< the folder of all the relations of a file (of all types, in one folder): a dropped file is a generic relation that is shown in it
		bool		lazy = false;	///< the folder of a relation that a plugin works out (dynamic): its files are fetched when it is opened, again at every opening after the cache time, see FolderOpened()
		bool		fetching = false;	///< the files are being fetched (a second opening does not ask again)
		bigtime_t	fetchedAt = 0;	///< when the last fetch ended (0: not yet)
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
	/** Forget everything and stop watching (tests). */
	void		Clear();

	bool		IsRelationFolder(const node_ref& node) const;
	/** The folder of all the relation types of a file ("Open related..."): its items are the folders of the types. */
	bool		IsTopLevelFolder(const node_ref& node) const;
	/** the folder of all the relations of a file, with the relations of all types in it (not the one with a folder for each type) */
	bool		IsAllRelationsFolder(const node_ref& node) const;
	bool		IsRelationFile(const node_ref& node) const;

	// What happens in a relation folder, reported by the pose views. All return true if the event was about a relation.

	/**
	 * A window opens the folder. If it is the folder of a relation that a plugin works out (lazy: a spatial relation), the relations are asked for now, in a thread, with a file
	 * "Fetching relations..." meanwhile; what was there before is replaced when the answer is in. At every opening, but not while a fetch is going on and not within the cache time
	 * (kFetchAgainAfter) of the last one: a folder that is kept (a shortcut on the Desktop to "near my home") shows what the plugin finds now. Nothing for any other folder.
	 * @param ref where the folder is now: it may have been moved since it was made (the Desktop), then the registered ref is replaced.
	 * A folder of a relation of a plugin that is not registered (copied to a place that stays, from before Akita was started) registers itself from its attributes, see
	 * TrackerSenRelations::AdoptKeptFolder(), and is fetched at once.
	 */
	void		FolderOpened(const node_ref& folder, const entry_ref& ref);
	/** A fetch of the folder was started (by FetchInto): it is not started a second time. The watchers of the folder are told. */
	void		FetchStarted(const node_ref& folder);
	/** The fetch of the folder ended (whether it worked or not): the cache time runs from now. The watchers of the folder are told. */
	void		FetchFinished(const node_ref& folder);
	/** whether the files of the relation folder are being fetched now (the status bar of its window says that the relations are resolved) */
	bool		IsFetching(const node_ref& folder) const;
	/**
	 * A window that shows a relation folder wants to know when a fetch starts and ends: `target` gets kFetchStateChanged (`fetching`: bool) at both, from the thread that does it (it is a message,
	 * so the window handles it in its own). A target that is not valid any more is dropped when it is told.
	 */
	void		WatchFetches(const node_ref& folder, const BMessenger& target);
	void		StopWatchingFetches(const node_ref& folder, const BMessenger& target);

	/** the message that the watchers of a fetch get: `fetching` is true when it starts, false when it ended */
	static const uint32	kFetchStateChanged = 'RFfs';

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
	/** @param reverse the file is the source of the relation and the source of the folder its target (a book's folder of authorships takes the author) */
	status_t	AddRelation(const FolderInfo& folder, const entry_ref& target, bool reverse = false);
	status_t	MoveRelation(const FileInfo& info, const FolderInfo& to, const node_ref& fileNode);

	void		StartWatching(const node_ref& folder);
	void		_TellFetchWatchers(const node_ref& folder, bool fetching);

	class Watcher;
	Watcher*						fWatcher;
	mutable BLocker					fLock;
	std::map<node_ref, FolderInfo>	fFolders;
	std::map<node_ref, std::vector<BMessenger> >	fFetchWatchers;	///< the windows that show a folder, by the folder
	std::map<node_ref, FileInfo>	fFiles;
	std::map<node_ref, bool>		fWatched;
	std::set<node_ref>				fAdopting;	///< kept folders whose registration is on its way (a second opening does not start it again)
};
