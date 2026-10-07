/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "RelationFolders.h"

#include <Autolock.h>
#include <Debug.h>
#include <Directory.h>
#include <File.h>
#include <Messenger.h>
#include <NodeInfo.h>
#include <Path.h>

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

/** the name of the folder of a relation type below the top level: the part after the supertype */
BString
FolderName(const char* relationType)
{
	BString name(relationType);
	if (name.StartsWith(sen::mime::kRelationPrefix))
		name.Remove(0, strlen(sen::mime::kRelationPrefix));
	return name;
}

}	// namespace


RelationFolders::RelationFolders()
	:
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
	BAutolock lock(fLock);
	fFolders[folder.node] = folder;
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

	// out of the relation folders (Trash, any other folder): the relation is gone
	return EntryRemoved(file);
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
RelationFolders::AddRelation(const FolderInfo& folder, const entry_ref& target)
{
	BMessage add(sen::cmd::kRelationAdd);
	add.AddRef(sen::key::kSourceRef, &folder.sourceRef);
	add.AddString(sen::key::kRelationType, folder.relationType);
	add.AddRef(sen::key::kTargetRef, &target);

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

			// the folder of the type, made when it is not there yet
			BPath path(&folder.ref);
			path.Append(FolderName(target.relationType));
			BEntry typeFolder(path.Path());
			if (!typeFolder.Exists()) {
				entry_ref created;
				if (TrackerSenRelations::CreateRelationDirectory(folder.viewId, folder.sourceId, target.relationType,
						&target.relationConfig, &created) != B_OK) {
					continue;
				}
			}
			typeFolder.SetTo(path.Path());
			typeFolder.GetRef(&target.ref);
			typeFolder.GetNodeRef(&target.node);
			RegisterFolder(target);
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

		AddRelation(target, dropped);
	}
	return true;
}
