/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once
#include <String.h>

#include <atomic>
#include <memory>

#include <sen/Sen.h>
#include "SlowMenu.h"

class OpenRelationTargetsMenu : public BSlowMenu {
public:
	OpenRelationTargetsMenu(const char* label, const BMessage* entriesToOpen,
							BWindow* parentWindow, const BMessenger &target);
	~OpenRelationTargetsMenu();

private:
	virtual bool StartBuildingItemList();
	virtual bool AddNextItem();
	virtual void DoneBuildingItemList();
	virtual void ClearMenuBuildingState();

	/**
	 * The question to the SEN server, asked in a thread of its own: a navigator can take half a minute (a web service), and the menu must open, say that it is fetching, and not wait
	 * in silence. The thread keeps its own copies, so that a menu that is closed meanwhile is no problem for it.
	 */
	struct Fetch {
		BMessenger			server;
		BMessage			request;
		BMessage			reply;
		status_t			result = B_ERROR;
		std::atomic<bool>	done{false};
	};
	/** the answer of the server is in: the part of the work that belongs to the context of the view (the tree of a document) */
	void		 FinishFetch();

	status_t	 AddRelationTargetItems(uint32* targetCount);
	status_t	 AddCompatibleRelationTargetItems(uint32* targetCount);
	status_t	 AddSelfRelationTargetItems(uint32* targetCount);

	BMessage	fEntriesToOpen;
	BMessenger	fMessenger;
	BMessenger* fSenMessenger;
	BWindow*	fParentWindow;

	BMessage	fOwnReply;
	// the answer of the server, or, for a sub menu of contained relations, its own message (the node of the tree)
	BMessage*	fRelationTargetsReply;
	BString 	fDefaultType;

	std::shared_ptr<Fetch>	fFetch;			///< the question that is being asked, if the answer is not in yet
	BMenuItem*	fFetchingItem;				///< "Fetching relations..." with a spinner, while it is
	int32		fSpinner;
	bool		fFetchFailed;

	typedef BSlowMenu _inherited;
};
