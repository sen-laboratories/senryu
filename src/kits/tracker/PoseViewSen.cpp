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

#include "Attributes.h"
#include "FSUtils.h"
#include "Utilities.h"
#include <Catalog.h>
#include <NodeInfo.h>
#include <Query.h>
#include <VolumeRoster.h>

#include <stdlib.h>
#include <string.h>

#include "PoseView.h"
#include "TrackerSenRelations.h"
#include <vector>

#include <sen/Sen.h>
#include <sen/Sensei.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PoseView"


bool
BPoseView::SetupRelationColumns()
{
	if (TargetModel() == NULL)
		return false;

	// the folder of a relation type, or a folder in it (a nested relation, e.g. a chapter): the type of the folder is the
	// relation type
	BNode node(TargetModel()->EntryRef());
	char relationType[B_MIME_TYPE_LENGTH];
	BMessage attrInfo;
	if (node.InitCheck() != B_OK || BNodeInfo(&node).GetType(relationType) != B_OK
			|| strncmp(relationType, sen::mime::kRelationPrefix, strlen(sen::mime::kRelationPrefix)) != 0
			|| TrackerSenRelations::GetRelationAttributeInfo(relationType, &attrInfo) != B_OK)
		return false;

	AddColumn(new BColumn(B_TRANSLATE("Name"), 145, B_ALIGN_LEFT, kAttrStatName, B_STRING_TYPE, true, true));

	// all attributes of the relation type (and of the relation supertype) that are meant to be displayed
	const char* publicName;
	for (int32 index = 0; attrInfo.FindString("attr:public_name", index, &publicName) == B_OK; index++) {
		int32 type, align, width;
		bool editable;
		const char* attrName;
		if (!attrInfo.FindBool("attr:viewable", index)
				|| attrInfo.FindString("attr:name", index, &attrName) != B_OK
				|| attrInfo.FindInt32("attr:type", index, &type) != B_OK
				|| attrInfo.FindBool("attr:editable", index, &editable) != B_OK
				|| attrInfo.FindInt32("attr:width", index, &width) != B_OK
				|| attrInfo.FindInt32("attr:alignment", index, &align) != B_OK
				|| ColumnFor(AttrHashString(attrName, (uint32)type)) != NULL)
			continue;

		const char* displayAs = NULL;
		attrInfo.FindString("attr:display_as", index, &displayAs);
		AddColumn(new BColumn(publicName, width, (alignment)align, attrName, type, displayAs, false, editable));
	}
	return true;
}


bool
BPoseView::HandleSenMessage(BMessage* message)
{
	// filter for SEN messages
	switch(message->what) {
		case kOpenRelations:
		case kOpenSelfRelations:
		case sensei::cmd::kExtract:
		case sensei::cmd::kEnrich:
		case sensei::cmd::kIdentify:
		case sensei::cmd::kNavigate: { // fallthrough
			PRINT(("SEN msg detected.\n"));
			break;	// ok, go on below
		}
		default:
			// not a SEN command message, handle as normal
			return false;
	}

	// dispatch SENSEI messages
	BMessage reply(sensei::cmd::kResult);
	status_t result = B_OK;

	switch (message->what) {
		case kOpenRelations: {
			PRINT(("PoseView:: got kOpenRelations message from menu:\n"));
			message->PrintToStream();

			BMessage relationRefs(kOpenRelations);
			result = TrackerSenRelations::ExtractSenParams(message, &relationRefs);

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
			result = TrackerSenRelations::ExtractSenParams(message, &relationRefs);

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
		case sensei::cmd::kExtract:
			PRINT(("PoseView::SENSEI extract called.\n"));
			break;

		case sensei::cmd::kEnrich: {
			PRINT(("PoseView::SENSEI enrich called.\n"));
			message->PrintToStream();
			result = EnrichRefsFromSelection(message->GetBool("wipe", true));
			break;
		}
		case sensei::cmd::kIdentify:
			PRINT(("PoseView::SENSEI identify called.\n"));
			break;

		case sensei::cmd::kNavigate:
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
			result = refs->AddRef(sen::key::kSourceRef, ref);
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
		refs.FindRef(sen::key::kSourceRef, index, &ref);

		// call plugin
		result = EnrichRefWithPlugin(&ref, wipe);
		if (result != B_OK) {
			PRINT(("problem enriching ref %s, skipping: %s\n", ref.name, strerror(result)));
		}
	}
	return B_OK;	// in any case, concerning the caller, we've done our best.
}


status_t
BPoseView::EnrichRefWithPlugin(const entry_ref* ref, bool wipe) {
	// find suitable/default enrichment plugin on any mounted volume. The plugin type (META:TYPE) is indexed and has to be
	// the first attribute of the query; the feature flag is compared on those files.
	BString predicate;
	predicate << sen::attr::kType << "==" << sen::mime::kPlugin << " && " << sensei::kFeatureAttrPrefix << ":"
		<< sensei::feature::kEnrich << "==1";

	std::vector<entry_ref> plugins;
	status_t result = B_ENTRY_NOT_FOUND;
	BVolumeRoster volumeRoster;
	BVolume volume;
	while (volumeRoster.GetNextVolume(&volume) == B_OK) {
		if (!volume.KnowsQuery())
			continue;

		BQuery query;
		query.SetVolume(&volume);
		query.SetPredicate(predicate.String());
		if (query.Fetch() != B_OK)
			continue;

		entry_ref pluginRef;
		while (query.GetNextRef(&pluginRef) == B_OK)
			plugins.push_back(pluginRef);
	}

	if (plugins.empty()) {
		PRINT(("no matching plugin found for enrichment.\n"));
		return B_NOT_SUPPORTED;
	}

	BMessage refsMsg(B_REFS_RECEIVED);
	refsMsg.AddRef("refs", ref);
	refsMsg.AddBool("wipe", wipe);

	for (const entry_ref& pluginRef : plugins) {
		PRINT(("handling ref %s with plugin %s\n", ref->name, pluginRef.name ));

		result = TrackerLaunch(&pluginRef, &refsMsg, false);
		if (result == B_OK) {
			// done
			break;
		} else {
			PRINT(("error launching plugin, trying next if available: %s\n", strerror(result) ));
		}
	}
	return result;
}
