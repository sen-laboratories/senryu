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
 We use separate message codes for Tracker SEN commands and SEN commands because internally
 they
*/

#include "Commands.h"
#include "FSUtils.h"
#include <Query.h>
#include <VolumeRoster.h>
#define DEBUG 1

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "PoseView.h"
#include "Sensei.h"


bool
BPoseView::HandleSenMessage(BMessage* message)
{
	PRINT(("BPoseView::HandleSenMessage:\n"));

	// filter for SEN messages, in this case SENSEI commands
	switch(message->what) {
		case SENSEI_CMD_EXTRACT:
		case SENSEI_CMD_ENRICH:
		case SENSEI_CMD_IDENTIFY:
		case SENSEI_CMD_NAVIGATE:	// fallthrough
			break;	// ok, go on below
		default:
			// not a SEN command message, handle as normal
			return false;
	}

	// dispatch SENSEI messages
	BMessage reply(SENSEI_MESSAGE_RESULT);
	status_t result = B_OK;

	switch (message->what) {
		case SENSEI_CMD_EXTRACT:
			PRINT(("PoseView::SENSEI extract called.\n"));
			break;

		case SENSEI_CMD_ENRICH: {
			PRINT(("PoseView::SENSEI enrich called.\n"));
			result = EnrichRefsFromMsg(message);
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
BPoseView::EnrichRefsFromMsg(BMessage* message) {
	type_code 	type;
	int32  		count;
	status_t    result;

	message->GetInfo("refs", &type, &count);

	for (int32 index = 0; index < count; index++) {
		entry_ref ref;
		message->FindRef("refs", index, &ref);

		BEntry entry(&ref, true);
		if (entry.InitCheck() != B_OK || !entry.Exists()) {
			PRINT(("skipping ref %s.\n", ref.name));
			continue;
		}
		// call plugin
		result = EnrichRefWithPlugin(&ref);
		if (result != B_OK) {
			PRINT(("problem enriching ref %s, skipping.\n", ref->name));
		}
	}
	return B_OK;	// in any case, concerning the caller, we've done our best.
}

status_t
BPoseView::EnrichRefWithPlugin(entry_ref* ref) {
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

    BEntry entry;
    while ((result = query.GetNextEntry(&entry)) == B_OK) {
        BPath path;
        entry.GetPath(&path);
        PRINT(("handling ref %s with plugin at path %s\n", entry.Name(), path.Path() ));

		result = TrackerLaunch(ref, true);
		if (result == B_OK) {
			// done
			break;
		} else {
			PRINT(("error launching plugin, trying next if available: %s\n", strerror(result) ));
		}
	}
	return result;
}
