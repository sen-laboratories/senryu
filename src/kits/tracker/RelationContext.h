/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <Entry.h>
#include <Locker.h>
#include <Message.h>
#include <String.h>
#include <StringList.h>

#include <memory>

/**
 * @brief What the relation menus of a file know: the configs of its relation types and what the server said.
 *
 * A menu of relations is built from the answers of the SEN server: the relation types and their configs, for relations that plugins resolve
 * the config of the plugin and the result of the extraction. Every item of the menu needs some of it when it is chosen: the config of the
 * relation to open its target, the result to show a whole tree of contained relations as folders.
 *
 * Instead of a copy in every item (a menu of a document with hundreds of bookmarks would hold hundreds), the data is kept here, once for a
 * build of the menus, and an item carries only the id of its context (sen::key::kRelationContext). The context is found again by that id when
 * the item is chosen: the message of the item is handled later by the application, when the menu may be gone (and with it everything that
 * a pointer from the item pointed to).
 *
 * The data is set before the context is registered and does not change after that, except for the result tree (SetRoot), which is set by
 * the menu thread when its answer arrives.
 */
class RelationContext {
public:
							RelationContext(const entry_ref& source, const BString& sourceId);

	/** The config of a relation type (the value for it in the config map); false if there is none. */
	bool					FindConfig(const char* relationType, BMessage* config) const;

	/** The result of the server for the self relations of the source: the tree of the contained relations. */
	void					SetRoot(const BMessage& root);
	bool					GetRoot(BMessage* root) const;

	entry_ref				sourceRef;
	BString					sourceId;
	/** the config of every relation type of the answer, by type (sen::key::kRelationConfigMap) */
	BMessage				relationConfigs;
	/** for contained relations: the plugins of the source type and what they map (sensei::key::kPluginConfig) */
	BMessage				pluginConfig;
	/** the relation types of the answer (sen::key::kRelations): what the top level view of all relations shows */
	BStringList				relations;

private:
	mutable BLocker			fLock;
	BMessage				fRoot;
	bool					fHasRoot;
};

typedef std::shared_ptr<RelationContext> RelationContextRef;


/**
 * The contexts of the menus that were built lately. Only a limited number is kept (the oldest is dropped): an item is chosen right after its
 * menu was built, and a menu that is built again makes a context of its own. All methods can be called from any thread.
 */
class RelationContexts {
public:
	/** Keep the context; the id to put into the messages of the items (sen::key::kRelationContext). */
	static int64			Add(const RelationContextRef& context);

	static RelationContextRef	Find(int64 id);
	/** The context that the message names, or none (an item whose context was dropped, or a message that has none). */
	static RelationContextRef	Find(const BMessage* message);

	/** Add the id of the context to the message of an item. */
	static void				Tag(BMessage* message, int64 id);
};
