/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/

/**
 PoseView SEN integration.
*/

#include "Commands.h"
#define DEBUG 1

#include "FSUtils.h"
#include <Query.h>
#include <VolumeRoster.h>

#include <stdlib.h>
#include <string.h>

#include "PoseView.h"
#include "Sensei.h"


bool
BPoseView::HandleSenMessage(BMessage* message)
{
	// filter for SEN messages
	switch(message->what) {
		case kOpenRelations:
		case kOpenSelfRelations:
		case SENSEI_CMD_EXTRACT:
		case SENSEI_CMD_ENRICH:
		case SENSEI_CMD_IDENTIFY:
		case SENSEI_CMD_NAVIGATE: { // fallthrough
			PRINT(("SEN msg detected.\n"));
			break;	// ok, go on below
		}
		default:
			// not a SEN command message, handle as normal
			return false;
	}

	// dispatch SENSEI messages
	BMessage reply(SENSEI_MESSAGE_RESULT);
	status_t result = B_OK;

	switch (message->what) {
		case kOpenRelations: {
			PRINT(("PoseView:: got kOpenRelations message from menu:\n"));
			message->PrintToStream();

			BMessage relationRefs(kOpenRelations);
			result = ExtractSenParams(message, &relationRefs);

			if (result == B_OK) {
				// forward to TrackerSen for central relation folder handling
				PRINT(("PoseView::SEN Open relations menu called, forwarding message:\n"));
				relationRefs.PrintToStream();

				be_app->PostMessage(&relationRefs);
			} else {
				PRINT(("failed to extract refs from selection: %s\n", strerror(result) ));
				break;
			}
			break;
		}
		case kOpenSelfRelations: {
			PRINT(("PoseView:: got kOpenSelfRelations message from menu:\n"));
			message->PrintToStream();

			BMessage relationRefs(kOpenRelations);
			result = ExtractSenParams(message, &relationRefs);

			if (result == B_OK) {
				// forward to TrackerSen for central relation folder handling
				PRINT(("PoseView::SEN Open self relations menu called, forwarding message:\n"));
				relationRefs.PrintToStream();

				be_app->PostMessage(&relationRefs);
			} else {
				PRINT(("failed to extract refs from selection: %s\n", strerror(result) ));
				break;
			}
			break;
		}
		case SENSEI_CMD_EXTRACT:
			PRINT(("PoseView::SENSEI extract called.\n"));
			break;

		case SENSEI_CMD_ENRICH: {
			PRINT(("PoseView::SENSEI enrich called.\n"));
			message->PrintToStream();
			result = EnrichRefsFromSelection(message->GetBool("wipe", true));
			break;
		}
		case SENSEI_CMD_IDENTIFY:
			PRINT(("PoseView::SENSEI identify called.\n"));
			break;

		case SENSEI_CMD_NAVIGATE:
			PRINT(("PoseView::SENSEI navigate called.\n"));
			break;

		default:
			PRINT(("PoseView::SENSEI unknown message received.\n"));
			result = B_NOT_SUPPORTED;
			break;
	}

	if (result != B_OK) {
		PRINT(("ERROR handling SENSEI msg: %s\n", strerror(result) ));
	}

	// done handling message, send a reply and finish
	reply.AddInt32("status", result);
	message->SendReply(&reply);

	return true;
}


status_t
BPoseView::ExtractRefsFromSelection(BMessage* refs) {
	status_t    result = B_OK;

	for (int32 index = 0; index < CountSelected(); index++) {
		BPose* pose = fSelectionList->ItemAt(index);
		const entry_ref* ref = pose->TargetModel()->ResolveIfLink()->EntryRef();
		if (ref != NULL) {
			result = refs->AddRef(SEN_RELATION_SOURCE_REF, ref);
			if (result != B_OK)
				break;
		}
	}
	return result;
}


status_t
BPoseView::EnrichRefsFromSelection(bool wipe) {
	status_t    result;

	BMessage refs;
	result = ExtractRefsFromSelection(&refs);

	if (result != B_OK)
		return result;

	entry_ref ref;
	for (int32 index = 0; index < refs.CountNames(B_REF_TYPE); index++) {
		refs.FindRef(SEN_RELATION_SOURCE_REF, index, &ref);

		// call plugin
		result = EnrichRefWithPlugin(&ref, wipe);
		if (result != B_OK) {
			PRINT(("problem enriching ref %s, skipping: %s\n", ref.name, strerror(result)));
		}
	}
	return B_OK;	// in any case, concerning the caller, we've done our best.
}


// TODO: unify and just handle refs extra, rest has always SEN: prefix, then move to utility class
status_t
BPoseView::ExtractSenParams(const BMessage* message, BMessage* enrichedMessage)
{
	entry_ref srcRef;
	status_t result = message->FindRef(SEN_RELATION_SOURCE_REF, &srcRef);
	if (result == B_OK)
		enrichedMessage->AddRef(SEN_RELATION_SOURCE_REF, &srcRef);

	BString srcId;
	result = message->FindString(SEN_RELATION_SOURCE_ID, &srcId);
	if (result == B_OK)
		enrichedMessage->AddString(SEN_RELATION_SOURCE_ID, srcId);

	BString relationType;
	result = message->FindString(SEN_RELATION_TYPE, &relationType);
	if (result == B_OK)
		enrichedMessage->AddString(SEN_RELATION_TYPE, relationType);

	BStringList relations;
	result = message->FindStrings(SEN_RELATIONS, &relations);
	if (result == B_OK)
		enrichedMessage->AddStrings(SEN_RELATIONS, relations);

	bool self = message->GetBool(SEN_RELATION_IS_SELF, false);
	enrichedMessage->AddBool(SEN_RELATION_IS_SELF, self);

	bool bidir = message->GetBool(SEN_RELATION_IS_BIDIR, false);
	enrichedMessage->AddBool(SEN_RELATION_IS_BIDIR, bidir);

	bool dynamic = message->GetBool(SEN_RELATION_IS_DYNAMIC, false);
	enrichedMessage->AddBool(SEN_RELATION_IS_DYNAMIC, dynamic);

	return B_OK;	// all params are optional for now
}


status_t
BPoseView::EnrichRefWithPlugin(const entry_ref* ref, bool wipe) {
	// find suitable/default enrichment plugin
	// todo: migrate to common method from SEN SelfRelations.cpp !
	BString predicate("SEN:TYPE==meta/x-vnd.sen-meta.plugin && SENSEI:TYPE==enricher");
	BVolumeRoster volRoster;
	BVolume bootVolume;
	volRoster.GetBootVolume(&bootVolume);

	BQuery query;
	query.SetVolume(&bootVolume);
	query.SetPredicate(predicate.String());

    status_t result;
	if ((result = query.Fetch()) != B_OK) {
        if (result == B_ENTRY_NOT_FOUND) {
            PRINT(("no matching enricher found for ref %s\n", ref->name));
            return B_NOT_SUPPORTED;
        }
        // something else went wrong
        PRINT(("could not execute query for suitable SENSEI extractors: %s\n", strerror(result) ));
        return result;
    }

	BMessage refsMsg(B_REFS_RECEIVED);
	refsMsg.AddRef("refs", ref);
	refsMsg.AddBool("wipe", wipe);

    entry_ref pluginRef;
    while ((result = query.GetNextRef(&pluginRef)) == B_OK) {
        PRINT(("handling ref %s with plugin %s\n", ref->name, pluginRef.name ));

		result = TrackerLaunch(reinterpret_cast<const entry_ref*>(&pluginRef), &refsMsg, false);
		if (result == B_OK) {
			// done
			break;
		} else {
			PRINT(("error launching plugin, trying next if available: %s\n", strerror(result) ));
		}
	}
	return result;
}
