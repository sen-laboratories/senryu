/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * Test of the request to open the relations of a file (OpenRelationsRequest.h), without Tracker. Build and run it on Haiku:
 *
 *   g++ -std=c++20 -I/boot/home/config/non-packaged/develop/headers -I<tracker sources> -o open-relations-request-test \
 *       OpenRelationsRequestTest.cpp <tracker sources>/OpenRelationsRequest.cpp -lbe && ./open-relations-request-test
 */

#include "OpenRelationsRequest.h"

#include <Messenger.h>

#include <sen/Sen.h>

#include <stdio.h>
#include <string.h>

static int sFailures = 0;

#define CHECK(condition) \
	do { if (!(condition)) { printf("  FAIL line %d: %s\n", __LINE__, #condition); sFailures++; } } while (0)


int
main()
{
	entry_ref ref;
	get_ref_for_path("/boot/home", &ref);

	printf("a message of a menu item is read, whatever else it carries\n");
	{
		BMessage item('Tore');
		item.AddRef(sen::key::kSourceRef, &ref);
		item.AddString(sen::key::kSourceId, "");
		item.AddInt64(sen::key::kRelationContext, 2);
		item.AddInt32("index", 6);
		item.AddInt64("when", 46671728670LL);
		item.AddPointer("source", (void*)0x1234);
		item.AddMessenger("be:sender", BMessenger());

		OpenRelationsRequest request;
		CHECK(OpenRelationsRequest::Parse(item, &request) == B_OK);
		CHECK(request.sourceRef == ref);
		CHECK(request.sourceId == "");
		CHECK(request.contextId == 2);

		BMessage out;
		request.ToMessage(0x6f525221, &out);
		CHECK(out.what == 0x6f525221);
		CHECK(out.CountNames(B_ANY_TYPE) == 2);	// the file and the context, nothing of the rest
		entry_ref got;
		CHECK(out.FindRef(sen::key::kSourceRef, &got) == B_OK && got == ref);
		CHECK(!out.HasString(sen::key::kSourceId));
		int64 context = 0;
		CHECK(out.FindInt64(sen::key::kRelationContext, &context) == B_OK && context == 2);
	}

	printf("the source id of a file that has one is handed on\n");
	{
		BMessage item('Tore');
		item.AddRef(sen::key::kSourceRef, &ref);
		item.AddString(sen::key::kSourceId, "02V738G23PNWC");
		item.AddInt64(sen::key::kRelationContext, 7);
		OpenRelationsRequest request;
		CHECK(OpenRelationsRequest::Parse(item, &request) == B_OK);
		BMessage out;
		request.ToMessage(0x6f525221, &out);
		CHECK(strcmp(out.GetString(sen::key::kSourceId, ""), "02V738G23PNWC") == 0);
	}

	printf("a message without the file or the context is refused, with the reason\n");
	{
		OpenRelationsRequest request;
		BString error;

		BMessage noFile('Tore');
		noFile.AddInt64(sen::key::kRelationContext, 2);
		CHECK(OpenRelationsRequest::Parse(noFile, &request, &error) == B_BAD_VALUE);
		CHECK(!error.IsEmpty());

		BMessage noContext('Tore');
		noContext.AddRef(sen::key::kSourceRef, &ref);
		error.SetTo("");
		CHECK(OpenRelationsRequest::Parse(noContext, &request, &error) == B_BAD_VALUE);
		CHECK(!error.IsEmpty());

		BMessage badContext('Tore');
		badContext.AddRef(sen::key::kSourceRef, &ref);
		badContext.AddInt64(sen::key::kRelationContext, 0);
		CHECK(OpenRelationsRequest::Parse(badContext, &request) == B_BAD_VALUE);

		BMessage empty('Tore');
		CHECK(OpenRelationsRequest::Parse(empty, &request) == B_BAD_VALUE);
	}

	printf("%s\n", sFailures == 0 ? "all ok" : "FAILURES");
	return sFailures == 0 ? 0 : 1;
}
