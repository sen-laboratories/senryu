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
#include <Alert.h>
#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <MimeType.h>
#include <NodeInfo.h>
#include <PopUpMenu.h>
#include <Query.h>
#include <Roster.h>
#include <VolumeRoster.h>

#include <stdlib.h>
#include <string.h>

#include "IconMenuItem.h"
#include "PoseView.h"
#include "TrackerSenRelations.h"
#include <set>
#include <string>
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

	// the columns for the attributes of an attribute info that are meant to be displayed (and not there yet)
	auto addColumnsOf = [this](const BMessage& info) {
		const char* publicName;
		for (int32 index = 0; info.FindString("attr:public_name", index, &publicName) == B_OK; index++) {
			int32 type, align, width;
			bool editable;
			const char* attrName;
			if (!info.FindBool("attr:viewable", index)
					|| info.FindString("attr:name", index, &attrName) != B_OK
					|| info.FindInt32("attr:type", index, &type) != B_OK
					|| info.FindBool("attr:editable", index, &editable) != B_OK
					|| info.FindInt32("attr:width", index, &width) != B_OK
					|| info.FindInt32("attr:alignment", index, &align) != B_OK
					|| ColumnFor(AttrHashString(attrName, (uint32)type)) != NULL)
				continue;

			const char* displayAs = NULL;
			info.FindString("attr:display_as", index, &displayAs);
			AddColumn(new BColumn(publicName, width, (alignment)align, attrName, type, displayAs, false, editable));
		}
	};

	// what the folder contains can be of another type than the relation, e.g. what a type contains are its attributes: the
	// properties of those come first, then the ones of the relation type (and of the relation supertype)
	std::set<std::string> itemTypes;
	BDirectory folder(TargetModel()->EntryRef());
	BEntry item;
	while (folder.GetNextEntry(&item) == B_OK) {
		BNode itemNode(&item);
		char itemType[B_MIME_TYPE_LENGTH];
		if (itemNode.InitCheck() == B_OK && BNodeInfo(&itemNode).GetType(itemType) == B_OK
				&& strcmp(itemType, relationType) != 0)
			itemTypes.insert(itemType);
	}
	for (const std::string& itemType : itemTypes) {
		BMessage itemInfo;
		if (BMimeType(itemType.c_str()).GetAttrInfo(&itemInfo) == B_OK)
			addColumnsOf(itemInfo);
	}
	addColumnsOf(attrInfo);

	// no size (the files of a relation are empty: the properties are attributes, which are not counted), and the time of the
	// last change after the properties
	AddColumn(new BColumn(B_TRANSLATE("Modified"), 150, B_ALIGN_LEFT, kAttrStatModified, B_TIME_TYPE, true, false));
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


// SEN key (Command) + drop: relate files
//
// The files that are dragged and the ones that they are dropped on are the two groups of files that are related. A file in a view of
// relations stands for its target, the Trash and the Desktop folder have no relations. If the pose that the files are dropped on is
// selected, the whole selection is the target, else just the pose. Which relations are offered is up to the SEN server (what fits all
// the files on both sides); what is chosen is created between every source and every target. The files are not copied, moved or
// opened, and stay where they are.
bool
BPoseView::HandleRelationDrop(const BMessage* dragMessage, BPose* targetPose, BPoint dropPoint)
{
	// (the cursor and the highlight of the target are reset by the caller of the drop)
	if (fDropTarget != NULL)
		HiliteDropTarget(false);
	fDropTarget = NULL;
	fSenDropMode = false;

	auto add = [](std::vector<entry_ref>* list, const entry_ref& ref) {
		if (TrackerSenRelations::HasNoRelations(&ref))
			return;
		for (const entry_ref& existing : *list) {
			if (existing == ref)
				return;
		}
		list->push_back(ref);
	};

	std::vector<entry_ref> sources, targets;
	entry_ref ref;
	for (int32 index = 0; dragMessage->FindRef("refs", index, &ref) == B_OK; index++)
		add(&sources, TrackerSenRelations::RelationTargetOrSelf(&ref));

	if (DragSelectionContains(targetPose, dragMessage))
		return true;	// dropped on itself: nothing to relate
	if (targetPose->IsSelected()) {
		for (int32 index = 0; index < CountSelected(); index++) {
			BPose* pose = fSelectionList->ItemAt(index);
			if (DragSelectionContains(pose, dragMessage))
				continue;
			add(&targets, TrackerSenRelations::RelationTargetOrSelf(pose->TargetModel()->ResolveIfLink()->EntryRef()));
		}
	} else {
		add(&targets, TrackerSenRelations::RelationTargetOrSelf(targetPose->TargetModel()->ResolveIfLink()->EntryRef()));
	}

	// a file is not related to itself
	for (auto it = targets.begin(); it != targets.end();) {
		bool isSource = false;
		for (const entry_ref& source : sources)
			isSource = isSource || source == *it;
		it = isSource ? targets.erase(it) : it + 1;
	}
	if (sources.empty() || targets.empty())
		return true;

	BMessenger server(sen::kServerSignature);
	BMessage reply;
	if (server.IsValid()) {
		BMessage ask(sen::cmd::kRelationsGetCompatible);
		for (const entry_ref& source : sources)
			ask.AddRef(sen::key::kSourceRef, &source);
		for (const entry_ref& target : targets)
			ask.AddRef(sen::key::kTargetRef, &target);
		ask.AddBool(sen::key::kWithConfigs, true);
		if (server.SendMessage(&ask, &reply, 5000000, 5000000) != B_OK)
			reply.MakeEmpty();
	}
	if (!server.IsValid()) {
		BAlert* alert = new BAlert("", B_TRANSLATE("The SEN server is not running, files cannot be related."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
		return true;
	}

	// the menu of the relations that fit
	BMessage configs;
	reply.FindMessage(sen::key::kRelationConfigMap, &configs);
	BPopUpMenu menu("relations", false, false);
	const char* type;
	for (int32 index = 0; reply.FindString(sen::key::kRelations, index, &type) == B_OK; index++) {
		BMimeType mime(type);
		if (!mime.IsInstalled())
			continue;	// (Tracker could not show it)

		BMessage config;
		configs.FindMessage(type, &config);
		BString label = config.GetString(sen::key::kRelationName, "");
		if (label.IsEmpty()) {
			char shortName[B_MIME_TYPE_LENGTH];
			label = mime.GetShortDescription(shortName) == B_OK && shortName[0] != '\0' ? shortName : type;
		}

		BMessage* choice = new BMessage(sen::cmd::kRelationAdd);
		choice->AddString(sen::key::kRelationType, type);
		menu.AddItem(new IconMenuItem(label.String(), choice, type));
	}
	if (menu.CountItems() == 0) {
		BMenuItem* none = new BMenuItem(B_TRANSLATE("No relation fits these files"), NULL);
		none->SetEnabled(false);
		menu.AddItem(none);
	}

	CommitActivePose();
	BPoint where(dropPoint);
	ConvertToScreen(&where);
	// don't send the message, sticky menu, synchronous: Esc or a click outside end it
	BMenuItem* chosen = menu.Go(where, false, true, false);
	if (chosen == NULL || chosen->Message() == NULL)
		return true;

	// the relation is created between every source and every target (the server also writes the opposite direction)
	const char* relationType = chosen->Message()->GetString(sen::key::kRelationType, "");
	int32 failed = 0;
	BString firstError;
	for (const entry_ref& source : sources) {
		for (const entry_ref& target : targets) {
			BMessage add(sen::cmd::kRelationAdd), answer;
			add.AddString(sen::key::kRelationType, relationType);
			add.AddRef(sen::key::kSourceRef, &source);
			add.AddRef(sen::key::kTargetRef, &target);
			status_t result = server.SendMessage(&add, &answer, 10000000, 10000000);
			int32 status = answer.GetInt32(sen::key::kStatus, 0);
			if (result != B_OK || status >= 400) {
				if (failed++ == 0) {
					firstError = answer.GetString(sen::key::kDetail, "");
					if (firstError.IsEmpty())
						firstError = result != B_OK ? strerror(result) : "";
				}
			}
		}
	}

	if (failed > 0) {
		BString text(B_TRANSLATE("%count% of the relations could not be created."));
		BString count;
		count << failed;
		text.ReplaceFirst("%count%", count);
		if (!firstError.IsEmpty())
			text << "\n\n" << firstError;
		BAlert* alert = new BAlert("", text.String(), B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
	}
	return true;
}
