/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "OpenRelationsRequest.h"

#include <sen/Sen.h>


status_t
OpenRelationsRequest::Parse(const BMessage& message, OpenRelationsRequest* request, BString* error)
{
	auto fail = [error](const char* reason) {
		if (error != NULL)
			error->SetTo(reason);
		return (status_t)B_BAD_VALUE;
	};

	if (message.FindRef(sen::key::kSourceRef, &request->sourceRef) != B_OK)
		return fail("no file to show the relations of");
	if (message.FindInt64(sen::key::kRelationContext, &request->contextId) != B_OK || request->contextId <= 0)
		return fail("the menu did not say which relations it showed");

	// (optional: a file may not have an ID yet)
	request->sourceId.SetTo(message.GetString(sen::key::kSourceId, ""));
	return B_OK;
}


void
OpenRelationsRequest::ToMessage(uint32 what, BMessage* message) const
{
	message->MakeEmpty();
	message->what = what;
	message->AddRef(sen::key::kSourceRef, &sourceRef);
	if (!sourceId.IsEmpty())
		message->AddString(sen::key::kSourceId, sourceId);
	message->AddInt64(sen::key::kRelationContext, contextId);
}
