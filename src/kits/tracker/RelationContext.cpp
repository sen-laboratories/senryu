/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "RelationContext.h"

#include <Autolock.h>

#include <sen/Sen.h>

#include <deque>
#include <utility>


RelationContext::RelationContext(const entry_ref& source, const BString& id)
	:
	sourceRef(source),
	sourceId(id),
	fHasRoot(false),
	fIsNode(false)
{
}


bool
RelationContext::FindConfig(const char* relationType, BMessage* config) const
{
	return relationConfigs.FindMessage(relationType, config) == B_OK;
}


void
RelationContext::SetRoot(const BMessage& root)
{
	BAutolock locker(fLock);
	fRoot = root;
	fHasRoot = true;
}


void
RelationContext::SetNode(const BMessage& relations)
{
	fNodeRelations = relations;
	fIsNode = true;
}


void
RelationContext::RestrictToNode(BMessage* reply) const
{
	if (!fIsNode)
		return;
	reply->RemoveName(sen::key::kRelations);
	BMessage item;
	for (int32 index = 0; fNodeRelations.FindMessage(sen::key::kRelations, index, &item) == B_OK; index++)
		reply->AddMessage(sen::key::kRelations, &item);
}


bool
RelationContext::GetRoot(BMessage* root) const
{
	BAutolock locker(fLock);
	if (!fHasRoot)
		return false;
	*root = fRoot;
	return true;
}


// ---- the registry

namespace {

const size_t kMaxContexts = 64;

struct Registry {
	BLocker	lock;
	int64	nextId;
	std::deque<std::pair<int64, RelationContextRef> >	contexts;

	Registry() : lock("relation contexts"), nextId(1) {}
};


Registry&
registry()
{
	static Registry instance;
	return instance;
}

}	// namespace


int64
RelationContexts::Add(const RelationContextRef& context)
{
	Registry& r = registry();
	BAutolock locker(r.lock);

	int64 id = r.nextId++;
	r.contexts.push_back(std::make_pair(id, context));
	while (r.contexts.size() > kMaxContexts)
		r.contexts.pop_front();
	return id;
}


RelationContextRef
RelationContexts::Find(int64 id)
{
	Registry& r = registry();
	BAutolock locker(r.lock);

	for (auto it = r.contexts.rbegin(); it != r.contexts.rend(); ++it) {
		if (it->first == id)
			return it->second;
	}
	return RelationContextRef();
}


RelationContextRef
RelationContexts::Find(const BMessage* message)
{
	int64 id;
	if (message == NULL || message->FindInt64(sen::key::kRelationContext, &id) != B_OK)
		return RelationContextRef();
	return Find(id);
}


void
RelationContexts::Tag(BMessage* message, int64 id)
{
	message->RemoveName(sen::key::kRelationContext);
	message->AddInt64(sen::key::kRelationContext, id);
}
