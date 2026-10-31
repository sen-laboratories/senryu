/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "RelationFolders.h"

#include <Alert.h>
#include <Autolock.h>
#include <Debug.h>
#include <Directory.h>
#include <File.h>
#include <Messenger.h>
#include <NodeMonitor.h>
#include <NodeInfo.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Volume.h>

#include <string.h>

#include <vector>

#include <sen/Sen.h>

#include "TrackerSenLog.h"
#include "TrackerSenRelations.h"

namespace {

/** the server answers within this time, a Tracker window must not hang for longer */
const bigtime_t kServerTimeout = 5000000;
/** the attributes that our own writing of a new file causes are ignored for this time */
const bigtime_t kSettleTime = 1500000;

bool
IsIdentityAttribute(const char* name)
{
	return strcmp(name, sen::attr::kRelationSource) == 0
		|| strcmp(name, sen::attr::kRelationTarget) == 0
		|| strcmp(name, sen::attr::kRelationSourceRef) == 0
		|| strcmp(name, sen::attr::kRelationTargetRef) == 0
		|| strcmp(name, sen::key::kRelationId) == 0
		|| strcmp(name, sen::key::kTargetMissing) == 0
		|| strcmp(name, sen::attr::kTo) == 0;
}

/**
 * Whether the SEN server offers the relation between two files: the types of the relation name the types of the files that it starts and ends at. Without an
 * answer (no server) it is not refused here: nothing is created then anyway.
 */
bool
Offers(const entry_ref& source, const entry_ref& target, const BString& relationType)
{
	BMessenger server(sen::kServerSignature);
	BMessage ask(sen::cmd::kRelationsGetCompatible), reply;
	ask.AddRef(sen::key::kSourceRef, &source);
	ask.AddRef(sen::key::kTargetRef, &target);
	ask.AddBool(sen::key::kWithConfigs, false);
	if (!server.IsValid() || server.SendMessage(&ask, &reply, 5000000, 5000000) != B_OK)
		return true;
	const char* type;
	for (int32 index = 0; reply.FindString(sen::key::kRelations, index, &type) == B_OK; index++) {
		if (relationType == type)
			return true;
	}
	return false;
}

}	// namespace


/** The node monitor messages of the registered folders, turned into the events of RelationFolders. */
class RelationFolders::Watcher : public BLooper {
public:
	Watcher(RelationFolders* folders)
		:
		BLooper("relation folder watcher"),
		fFolders(folders)
	{
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != B_NODE_MONITOR) {
			BLooper::MessageReceived(message);
			return;
		}

		node_ref node;
		message->FindInt32("device", &node.device);
		message->FindInt64("node", (int64*) &node.node);

		switch (message->GetInt32("opcode", 0)) {
			case B_ENTRY_REMOVED:
				fFolders->EntryRemoved(node);
				break;

			case B_ENTRY_MOVED:
			{
				int64 from = 0, to = 0;
				message->FindInt64("from directory", &from);
				message->FindInt64("to directory", &to);
				if (from == to) {
					fFolders->EntryRenamed(node, message->GetString("name", ""));
				} else {
					node_ref directory(node.device, (ino_t) to);
					fFolders->EntryMoved(node, directory);
				}
				break;
			}

			case B_ATTR_CHANGED:
				fFolders->AttributesChanged(node, message->GetString("attr", NULL));
				break;
		}
	}

private:
	RelationFolders*	fFolders;
};


RelationFolders::RelationFolders()
	:
	fWatcher(NULL),
	fLock("relation folders")
{
}


RelationFolders&
RelationFolders::Instance()
{
	static RelationFolders instance;
	return instance;
}


void
RelationFolders::RegisterFolder(const FolderInfo& folder)
{
	{
		BAutolock lock(fLock);
		fFolders[folder.node] = folder;
	}
	StartWatching(folder.node);
}


void
RelationFolders::StartWatching(const node_ref& folder)
{
	BAutolock lock(fLock);
	if (fWatched.find(folder) != fWatched.end())
		return;

	if (fWatcher == NULL) {
		fWatcher = new Watcher(this);
		fWatcher->Run();
	}

	// one watch for the folder reports what happens to its entries and the attributes of all of them, also the ones that
	// are created later (B_WATCH_CHILDREN); the entries of folders below it are not reported
	status_t status = watch_node(&folder, B_WATCH_DIRECTORY | B_WATCH_CHILDREN | B_WATCH_ATTR, BMessenger(fWatcher));
	if (status != B_OK) {
		ERROR("could not watch the relation folder: %s\n", strerror(status));
		return;
	}
	fWatched[folder] = true;
}


void
RelationFolders::RegisterFile(const node_ref& file, const FileInfo& info)
{
	BAutolock lock(fLock);
	fFiles[file] = info;
	fFiles[file].registeredAt = system_time();
}


void
RelationFolders::Clear()
{
	BAutolock lock(fLock);
	if (fWatcher != NULL) {
		for (auto& watched : fWatched)
			watch_node(&watched.first, B_STOP_WATCHING, BMessenger(fWatcher));
	}
	fWatched.clear();
	fFolders.clear();
	fFiles.clear();
}


bool
RelationFolders::IsRelationFolder(const node_ref& node) const
{
	BAutolock lock(fLock);
	return fFolders.find(node) != fFolders.end();
}


bool
RelationFolders::IsTopLevelFolder(const node_ref& node) const
{
	BAutolock lock(fLock);
	auto found = fFolders.find(node);
	return found != fFolders.end() && found->second.relationType.IsEmpty() && !found->second.all;
}


bool
RelationFolders::IsAllRelationsFolder(const node_ref& node) const
{
	BAutolock lock(fLock);
	auto found = fFolders.find(node);
	return found != fFolders.end() && found->second.all;
}


bool
RelationFolders::IsRelationFile(const node_ref& node) const
{
	BAutolock lock(fLock);
	return fFiles.find(node) != fFiles.end();
}


status_t
RelationFolders::Send(BMessage* message, BMessage* reply) const
{
	BMessenger server(sen::kServerSignature);
	if (!server.IsValid()) {
		ERROR("SEN server is not running, the relation is not changed.\n");
		return B_ERROR;
	}

	status_t status = server.SendMessage(message, reply, kServerTimeout, kServerTimeout);
	if (status != B_OK) {
		ERROR("could not reach the SEN server: %s\n", strerror(status));
		return status;
	}

	status = reply->GetInt32(sen::key::kResult, B_ERROR);
	if (status != B_OK) {
		ERROR("SEN server: %s (status %d)\n", reply->GetString(sen::key::kDetail, strerror(status)),
			(int) reply->GetInt32(sen::key::kStatus, 0));
	}
	return status;
}


bool
RelationFolders::EntryRemoved(const node_ref& file)
{
	FileInfo info;
	{
		BAutolock lock(fLock);
		auto found = fFiles.find(file);
		if (found == fFiles.end())
			return false;
		info = found->second;
		fFiles.erase(found);
	}

	BMessage remove(sen::cmd::kRelationRemove);
	remove.AddRef(sen::key::kSourceRef, &info.sourceRef);
	remove.AddString(sen::key::kRelationType, info.relationType);
	remove.AddString(sen::key::kTargetId, info.targetId);
	if (!info.relationId.IsEmpty())
		remove.AddString(sen::key::kRelationId, info.relationId);

	BMessage reply;
	Send(&remove, &reply);
	return true;
}


/** whether the folder is the Trash of its volume or in it */
static bool
IsInTrash(const node_ref& directory)
{
	BDirectory folder(&directory);
	BEntry entry;
	BPath path, trash;
	BVolume volume(directory.device);
	if (folder.InitCheck() != B_OK || folder.GetEntry(&entry) != B_OK || entry.GetPath(&path) != B_OK
			|| find_directory(B_TRASH_DIRECTORY, &trash, false, &volume) != B_OK)
		return false;
	size_t length = strlen(trash.Path());
	return strncmp(path.Path(), trash.Path(), length) == 0 && (path.Path()[length] == '\0' || path.Path()[length] == '/');
}


bool
RelationFolders::EntryMoved(const node_ref& file, const node_ref& newDirectory)
{
	FileInfo info;
	FolderInfo target;
	bool toRelationFolder = false;
	{
		BAutolock lock(fLock);
		auto found = fFiles.find(file);
		if (found == fFiles.end())
			return false;
		if (found->second.folder == newDirectory)
			return true;	// our own move, or no move at all
		info = found->second;

		auto folder = fFolders.find(newDirectory);
		if (folder != fFolders.end()) {
			target = folder->second;
			toRelationFolder = true;
		}
	}

	if (toRelationFolder && !target.relationType.IsEmpty() && target.sourceId == info.sourceId
			&& target.relationType != info.relationType) {
		// moved by hand to the folder of another type of the same source
		return MoveRelation(info, target, file) == B_OK;
	}

	// into the Trash: the relation is gone
	if (IsInTrash(newDirectory))
		return EntryRemoved(file);

	// into any other folder: the file is a link to the target of the relation, which stays (it is not a file of the folder any more)
	BAutolock lock(fLock);
	fFiles.erase(file);
	return true;
}


bool
RelationFolders::EntryRenamed(const node_ref& file, const char* name)
{
	BAutolock lock(fLock);
	auto found = fFiles.find(file);
	if (found == fFiles.end())
		return false;
	found->second.ref.set_name(name);
	return true;
}


status_t
RelationFolders::ReadProperties(const entry_ref& file, BMessage* properties)
{
	BMessage all;
	status_t status = TrackerSenRelations::ConvertAttributesToMessage(&file, &all);
	if (status != B_OK)
		return status;

	char* name;
	type_code type;
	int32 count;
	for (int32 i = 0; all.GetInfo(B_ANY_TYPE, i, &name, &type, &count) == B_OK; i++) {
		if (IsIdentityAttribute(name))
			continue;
		const void* data;
		ssize_t size;
		if (all.FindData(name, type, &data, &size) == B_OK)
			properties->AddData(name, type, data, size);
	}
	return B_OK;
}


bool
RelationFolders::AttributesChanged(const node_ref& file, const char* attribute)
{
	FileInfo info;
	{
		BAutolock lock(fLock);
		auto found = fFiles.find(file);
		if (found == fFiles.end())
			return false;
		info = found->second;
	}

	// the identity of the relation is not a property; our own writing of the file is not an edit
	if ((attribute != NULL && IsIdentityAttribute(attribute)) || system_time() - info.registeredAt < kSettleTime)
		return true;

	BMessage properties;
	if (ReadProperties(info.ref, &properties) != B_OK)
		return true;

	BMessage update(sen::cmd::kRelationUpdate);
	update.AddRef(sen::key::kSourceRef, &info.sourceRef);
	update.AddString(sen::key::kRelationType, info.relationType);
	update.AddString(sen::key::kTargetId, info.targetId);
	if (!info.relationId.IsEmpty())
		update.AddString(sen::key::kRelationId, info.relationId);
	update.AddMessage(sen::key::kRelationProperties, &properties);

	BMessage reply;
	Send(&update, &reply);
	return true;
}


status_t
RelationFolders::AddRelation(const FolderInfo& folder, const entry_ref& target, bool reverse)
{
	BMessage add(sen::cmd::kRelationAdd);
	add.AddRef(sen::key::kSourceRef, reverse ? &target : &folder.sourceRef);
	add.AddString(sen::key::kRelationType, folder.relationType);
	add.AddRef(sen::key::kTargetRef, reverse ? &folder.sourceRef : &target);

	BMessage reply;
	status_t status = Send(&add, &reply);
	if (status != B_OK)
		return status;

	// a relation that exists already is not shown twice
	if (reply.GetInt32(sen::key::kStatus, 0) != sen::status::kCreated)
		return B_OK;

	BNode targetNode(&target);
	BString targetId;
	targetNode.ReadAttrString(sen::attr::kId, &targetId);

	BMessage properties;
	properties.AddString(sen::attr::kTo, targetId);
	properties.AddString(sensei::key::kName, target.name);
	BMessage relationConfig;
	folder.relationConfig.FindMessage(sen::conf::kRelation, &relationConfig);
	const char* label = relationConfig.GetString(sen::attr::kRelationLabel, "");
	if (label[0] != '\0')
		properties.AddString(sen::attr::kRelationLabel, label);
	if (reply.HasString(sen::key::kRelationId))
		properties.AddString(sen::key::kRelationId, reply.GetString(sen::key::kRelationId, ""));

	BMessage relations;
	relations.AddMessage(sen::key::kRelations, &properties);

	BMessage config(folder.relationConfig);
	config.RemoveName(sen::key::kSourceId);
	config.AddString(sen::key::kSourceId, folder.sourceId);
	config.RemoveName(sen::key::kSourceRef);
	config.AddRef(sen::key::kSourceRef, &folder.sourceRef);
	config.RemoveName(sen::key::kRelationType);
	config.AddString(sen::key::kRelationType, folder.relationType);

	entry_ref folderRef(folder.ref), openRef(folder.ref);
	return TrackerSenRelations::WriteTargetRelations(&relations, &config, &folderRef, &openRef);
}


status_t
RelationFolders::MoveRelation(const FileInfo& info, const FolderInfo& to, const node_ref& fileNode)
{
	BMessage update(sen::cmd::kRelationUpdate);
	update.AddRef(sen::key::kSourceRef, &info.sourceRef);
	update.AddString(sen::key::kRelationType, info.relationType);
	update.AddString(sen::key::kTargetId, info.targetId);
	if (!info.relationId.IsEmpty())
		update.AddString(sen::key::kRelationId, info.relationId);
	update.AddString(sen::key::kNewRelationType, to.relationType);

	BMessage reply;
	status_t status = Send(&update, &reply);
	if (status != B_OK)
		return status;

	// the relation has the type of its new folder now (and a new relation id, if it is not alone with its target any more)
	FileInfo moved(info);
	moved.relationType = to.relationType;
	moved.folder = to.node;
	moved.relationId.SetTo(reply.GetString(sen::key::kRelationId, ""));
	{
		BAutolock lock(fLock);
		fFiles[fileNode] = moved;
		fFiles[fileNode].registeredAt = system_time();
	}

	// the file follows: its type, its relation id and its folder (if the user did not move it there already)
	BEntry entry(&info.ref);
	BDirectory directory(&to.ref);
	if (entry.InitCheck() == B_OK && directory.InitCheck() == B_OK) {
		BNode node(&info.ref);
		BNodeInfo(&node).SetType(to.relationType);
		if (moved.relationId.IsEmpty())
			node.RemoveAttr(sen::key::kRelationId);
		else
			node.WriteAttrString(sen::key::kRelationId, &moved.relationId);

		BEntry parent;
		node_ref parentNode;
		if (entry.GetParent(&parent) == B_OK && parent.GetNodeRef(&parentNode) == B_OK && !(parentNode == to.node)) {
			entry.MoveTo(&directory);
			entry_ref currentRef;
			entry.GetRef(&currentRef);
			BAutolock lock(fLock);
			fFiles[fileNode].ref = currentRef;
		}
	}
	return B_OK;
}


bool
RelationFolders::HandleDrop(const BMessage& drop, const node_ref& folderNode)
{
	FolderInfo folder;
	{
		BAutolock lock(fLock);
		auto found = fFolders.find(folderNode);
		if (found == fFolders.end())
			return false;
		folder = found->second;
	}

	// the files that do not fit the relation of the folder: they are said at the end, the others are related
	std::vector<BString> refused;
	BString relationName;

	entry_ref dropped;
	for (int32 i = 0; drop.FindRef("refs", i, &dropped) == B_OK; i++) {
		node_ref droppedNode;
		BEntry droppedEntry(&dropped, true);
		if (droppedEntry.InitCheck() != B_OK || droppedEntry.GetNodeRef(&droppedNode) != B_OK)
			continue;

		FolderInfo target(folder);

		// dropped on the top level: a generic relation to the file, in the folder of that type
		if (target.relationType.IsEmpty()) {
			target.relationType.SetTo(sen::mime::kReferenceRelation);

			BMessage ask(sen::cmd::kRelationsGet);
			ask.AddRef(sen::key::kSourceRef, &target.sourceRef);
			ask.AddString(sen::key::kRelationType, target.relationType);
			BMessage answer, configs;
			if (Send(&ask, &answer) != B_OK || answer.FindMessage(sen::key::kRelationConfigMap, &configs) != B_OK
					|| configs.FindMessage(target.relationType, &target.relationConfig) != B_OK) {
				continue;
			}

			if (folder.all) {
				// the folder of all relations has them all in it: the new one is shown there, not in a folder of its type (which nobody has open)
				target.all = false;
			} else {
				// the folder of the type (it is named after the relation), made when it is not there yet
				BPath path;
				if (TrackerSenRelations::RelationDirectoryPath(folder.viewId, target.relationType, &target.relationConfig, &path) != B_OK)
					continue;
				BEntry typeFolder(path.Path());
				if (!typeFolder.Exists()) {
					entry_ref created;
					if (TrackerSenRelations::CreateRelationDirectory(folder.viewId, folder.sourceId, TrackerSenRelations::DisplayNameOf(folder.sourceRef).String(), target.relationType,
							&target.relationConfig, &created) != B_OK) {
						continue;
					}
				}
				typeFolder.SetTo(path.Path());
				typeFolder.GetRef(&target.ref);
				typeFolder.GetNodeRef(&target.node);
				RegisterFolder(target);
			}
		}

		FileInfo fileInfo;
		bool isRelationFile = false;
		{
			BAutolock lock(fLock);
			auto file = fFiles.find(droppedNode);
			if (file != fFiles.end()) {
				fileInfo = file->second;
				isRelationFile = true;
			}
		}

		if (isRelationFile) {
			// dragged from another relation folder: it moves, if it is a relation of the same source
			if (fileInfo.sourceId == target.sourceId && fileInfo.relationType != target.relationType)
				MoveRelation(fileInfo, target, droppedNode);
			continue;
		}

		// the relation has to fit the types of the two files, as the types of the relation say (SEN:sourceTypes and SEN:targetTypes, what the SEN server
		// answers for files that are dragged onto others): a file that does not is not related. A relation that the file can be the source of, with the
		// source of the folder as its target (the authors dropped on the authorships of a book), is made the other way round.
		bool reverse = false;
		if (!Offers(target.sourceRef, dropped, target.relationType)) {
			if (Offers(dropped, target.sourceRef, target.relationType)) {
				reverse = true;
			} else {
				refused.push_back(BString(dropped.name));
				if (relationName.IsEmpty())
					relationName.SetTo(target.relationConfig.GetString(sen::key::kRelationName, target.relationType.String()));
				continue;
			}
		}

		AddRelation(target, dropped, reverse);
	}

	if (!refused.empty()) {
		BString text("The relation \"");
		text << relationName << "\" does not fit ";
		for (size_t index = 0; index < refused.size() && index < 5; index++)
			text << (index > 0 ? ", " : "") << refused[index];
		if (refused.size() > 5)
			text << " and " << (int) (refused.size() - 5) << " more";
		text << ", by the types of the files that it is for.";
		BAlert* alert = new BAlert("", text.String(), "OK", NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);	// (not waited for: the drop is done)
	}
	return true;
}
