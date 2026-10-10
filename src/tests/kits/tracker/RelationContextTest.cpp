/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * Test of the contexts of the relation menus (RelationContext.h), without Tracker. Build and run it on Haiku:
 *
 *   g++ -std=c++20 -I/boot/home/config/non-packaged/develop/headers -I<tracker sources> -o relation-context-test \
 *       RelationContextTest.cpp <tracker sources>/RelationContext.cpp -lbe && ./relation-context-test
 */

#include "RelationContext.h"

#include <sen/Sen.h>

#include <stdio.h>
#include <thread>
#include <vector>

static int sFailures = 0;

#define CHECK(condition) \
	do { if (!(condition)) { printf("  FAIL line %d: %s\n", __LINE__, #condition); sFailures++; } } while (0)


static RelationContextRef
MakeContext(const char* name)
{
	entry_ref ref;
	ref.set_name(name);
	RelationContextRef context = std::make_shared<RelationContext>(ref, BString("ID") << name);
	BMessage config;
	config.AddBool(sen::conf::kSelf, true);
	context->relationConfigs.AddMessage("relation/test", &config);
	context->relations.Add("relation/test");
	return context;
}


int
main()
{
	printf("a context is found again by its id, and by the message of an item\n");
	{
		RelationContextRef context = MakeContext("a");
		int64 id = RelationContexts::Add(context);
		CHECK(id > 0);
		CHECK(RelationContexts::Find(id) == context);

		BMessage item(B_REFS_RECEIVED);
		RelationContexts::Tag(&item, id);
		CHECK(RelationContexts::Find(&item) == context);

		// tagging again replaces the id, it does not add a second one
		int64 other = RelationContexts::Add(MakeContext("b"));
		RelationContexts::Tag(&item, other);
		CHECK(item.CountNames(B_INT64_TYPE) == 1);
		CHECK(RelationContexts::Find(&item) != context);

		BMessage none;
		CHECK(RelationContexts::Find(&none) == NULL);
		CHECK(RelationContexts::Find((const BMessage*) NULL) == NULL);
		CHECK(RelationContexts::Find((int64) 123456789) == NULL);
	}

	printf("the config of a relation type\n");
	{
		RelationContextRef context = MakeContext("c");
		BMessage config;
		CHECK(context->FindConfig("relation/test", &config));
		CHECK(config.GetBool(sen::conf::kSelf, false));
		CHECK(!context->FindConfig("relation/other", &config));
	}

	printf("the result tree is set once the answer is there\n");
	{
		RelationContextRef context = MakeContext("d");
		BMessage root;
		CHECK(!context->GetRoot(&root));
		BMessage tree(B_REFS_RECEIVED);
		tree.AddString("x", "y");
		context->SetRoot(tree);
		CHECK(context->GetRoot(&root));
		CHECK(strcmp(root.GetString("x", ""), "y") == 0);
	}

	printf("the context of a node (a bookmark) shows only its children, not the tree of the whole document\n");
	{
		BMessage whole(B_REFS_RECEIVED);
		for (const char* name : {"1 Introduction", "2 Installing", "3 Reading"}) {
			BMessage item;
			item.AddString("name", name);
			whole.AddMessage(sen::key::kRelations, &item);
		}

		// a context of the document restricts nothing
		BMessage reply(whole);
		MakeContext("doc")->RestrictToNode(&reply);
		CHECK(reply.CountNames(B_MESSAGE_TYPE) == 1);
		int32 count = 0;
		reply.GetInfo(sen::key::kRelations, NULL, &count);
		CHECK(count == 3);

		// a node with two children
		BMessage children;
		for (const char* name : {"2.1 Updating", "2.2 What it needs"}) {
			BMessage item;
			item.AddString("name", name);
			children.AddMessage(sen::key::kRelations, &item);
		}
		RelationContextRef node = MakeContext("node");
		CHECK(!node->IsNode());
		node->SetNode(children);
		CHECK(node->IsNode());
		reply = whole;
		node->RestrictToNode(&reply);
		count = 0;
		reply.GetInfo(sen::key::kRelations, NULL, &count);
		CHECK(count == 2);
		BMessage first;
		CHECK(reply.FindMessage(sen::key::kRelations, 0, &first) == B_OK);
		CHECK(strcmp(first.GetString("name", ""), "2.1 Updating") == 0);

		// a leaf contains nothing
		RelationContextRef leaf = MakeContext("leaf");
		leaf->SetNode(BMessage());
		reply = whole;
		leaf->RestrictToNode(&reply);
		CHECK(!reply.HasMessage(sen::key::kRelations));
	}

	printf("only the latest contexts are kept, the oldest is dropped; one in use stays alive for who holds it\n");
	{
		RelationContextRef first = MakeContext("first");
		int64 firstId = RelationContexts::Add(first);
		for (int i = 0; i < 200; i++)
			RelationContexts::Add(MakeContext("n"));
		CHECK(RelationContexts::Find(firstId) == NULL);
		CHECK(first->sourceId == "IDfirst");	// still valid for the holder
		CHECK(RelationContexts::Find(RelationContexts::Add(MakeContext("last"))) != NULL);
	}

	printf("any thread can add and find (menus are built in threads of their own)\n");
	{
		std::vector<std::thread> threads;
		std::vector<int> found(8, 0);
		for (int t = 0; t < 8; t++) {
			threads.emplace_back([t, &found]() {
				for (int i = 0; i < 200; i++) {
					RelationContextRef context = MakeContext("t");
					int64 id = RelationContexts::Add(context);
					RelationContextRef again = RelationContexts::Find(id);
					if (again == NULL || again == context)
						found[t]++;	// dropped by the others meanwhile, or found: both are fine, no crash
					BMessage tree;
					context->SetRoot(tree);
					BMessage out;
					context->GetRoot(&out);
				}
			});
		}
		for (std::thread& thread : threads)
			thread.join();
		CHECK(found[0] == 200 && found[7] == 200);
	}

	printf("%s\n", sFailures == 0 ? "all ok" : "FAILURES");
	return sFailures == 0 ? 0 : 1;
}
