/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file AkitaUpdateWatcher.h
 * @brief Akita notices that its package was updated and offers a restart (header only, so that a test program can use it).
 *
 * A running program keeps the old files, so after an update of the package the user works with the old Akita until something starts it again. The folders of the packages are watched for a
 * package of Akita ("akita-...hpkg") that appears, goes or is renamed. The package is ACTIVATED some seconds after its file appears (and the program is not told), so then the program
 * file is looked at every few seconds for a while: the file that this program was started from (in the package file system) is another one now (inode, size, time). The user is asked
 * once, with a BAlert; "Restart" starts the program again from a helper (after a pause, when this one has gone) and quits it, which writes its open windows as it always does.
 */

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <Handler.h>
#include <Invoker.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Node.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include <stdlib.h>
#include <sys/stat.h>

#include <vector>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TrackerSen"

class AkitaUpdateWatcher : public BHandler {
public:
	/** The program file and the folders to watch are those of the running program unless a test gives others. */
	AkitaUpdateWatcher(const char* programPath = NULL, const std::vector<BString>& folders = std::vector<BString>(), bigtime_t interval = 4000000)
		:
		BHandler("akita update watcher"),
		fPath(programPath != NULL ? programPath : ""),
		fFolders(folders),
		fInterval(interval),
		fRunner(NULL),
		fChecks(0),
		fAsked(false)
	{
	}

	virtual ~AkitaUpdateWatcher()
	{
		stop_watching(this);
		delete fRunner;
	}

	/** Starts watching: only when the program runs from a file (and the folders can be watched). The application is the looper of the handler. */
	void Start()
	{
		if (fPath.Length() == 0) {
			app_info info;
			if (be_app->GetAppInfo(&info) != B_OK)
				return;
			BPath path(&info.ref);
			if (path.InitCheck() != B_OK)
				return;
			fPath = path.Path();
		}
		if (!_Stat(fPath.String(), &fStarted))
			return;
		if (fFolders.empty()) {
			const directory_which folders[] = { B_SYSTEM_PACKAGES_DIRECTORY, B_USER_PACKAGES_DIRECTORY };
			for (directory_which folder : folders) {
				BPath packages;
				if (find_directory(folder, &packages) == B_OK)
					fFolders.push_back(BString(packages.Path()));
			}
		}
		be_app->Lock();
		be_app->AddHandler(this);
		for (const BString& folder : fFolders) {
			node_ref ref;
			if (BDirectory(folder.String()).GetNodeRef(&ref) == B_OK)
				watch_node(&ref, B_WATCH_DIRECTORY, this);
		}
		be_app->Unlock();
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case B_NODE_MONITOR:
			{
				int32 opcode = 0;
				const char* name = NULL;
				if (message->FindInt32("opcode", &opcode) != B_OK || message->FindString("name", &name) != B_OK || name == NULL)
					break;
				if (opcode != B_ENTRY_CREATED && opcode != B_ENTRY_REMOVED && opcode != B_ENTRY_MOVED)
					break;
				const BString file(name);
				if (!file.IStartsWith("akita-") || !file.IEndsWith(".hpkg"))
					break;
				// (the package is activated a little later: look for a while)
				fChecks = 0;
				if (fRunner == NULL)
					fRunner = new BMessageRunner(this, new BMessage(kCheckProgramFile), fInterval);
				break;
			}
			case kCheckProgramFile:
			{
				Stamp now;
				if (!fAsked && _Stat(fPath.String(), &now) && !_Same(now, fStarted)) {
					fAsked = true;
					_StopChecking();
					_Ask();
				} else if (++fChecks >= 15)
					_StopChecking();
				break;
			}
			case kRestartAnswer:
				if (message->GetInt32("which", 0) == 1)
					_Restart();
				break;
			default:
				BHandler::MessageReceived(message);
		}
	}

private:
	enum { kCheckProgramFile = 'Apck', kRestartAnswer = 'Apra' };

	struct Stamp {
		ino_t	inode = 0;
		dev_t	device = 0;
		off_t	size = 0;
		time_t	modified = 0;
	};

	static bool _Stat(const char* path, Stamp* stamp)
	{
		struct stat st;
		if (stat(path, &st) != 0)
			return false;
		stamp->inode = st.st_ino;
		stamp->device = st.st_dev;
		stamp->size = st.st_size;
		stamp->modified = st.st_mtime;
		return true;
	}

	static bool _Same(const Stamp& a, const Stamp& b)
	{
		return a.inode == b.inode && a.device == b.device && a.size == b.size && a.modified == b.modified;
	}

	void _StopChecking()
	{
		delete fRunner;
		fRunner = NULL;
		fChecks = 0;
	}

	void _Ask()
	{
		BAlert* alert = new BAlert(B_TRANSLATE("Akita updated"),
			B_TRANSLATE("Akita has been updated. Restart it now to use the new version? Your windows are opened again."),
			B_TRANSLATE("Later"), B_TRANSLATE("Restart"), NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		alert->Go(new BInvoker(new BMessage(kRestartAnswer), this));
	}

	/** A helper starts the program again when this one has gone (a pause, so that the launch is not taken for a second start of this one), then this one quits. */
	void _Restart()
	{
		BString command;
		command << "(sleep 4; '" << fPath << "') >/dev/null 2>&1 </dev/null &";
		system(command.String());
		be_app_messenger.SendMessage(B_QUIT_REQUESTED);
	}

	BString					fPath;
	Stamp					fStarted;
	std::vector<BString>	fFolders;
	bigtime_t				fInterval;
	BMessageRunner*			fRunner;
	int32					fChecks;
	bool					fAsked;
};
