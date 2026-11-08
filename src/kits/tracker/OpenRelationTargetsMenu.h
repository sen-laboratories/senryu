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

class BMessageRunner;
class SpinnerMenuItem;

class OpenRelationTargetsMenu : public BSlowMenu {
public:
	OpenRelationTargetsMenu(const char* label, const BMessage* entriesToOpen,
							BWindow* parentWindow, const BMessenger &target);
	~OpenRelationTargetsMenu();

	/** the answer of the server is in (from the thread that asked): the menu has the items now */
	static const uint32		kFetched = 'sfdn';
	/** one step of the spinner (from a BMessageRunner) */
	static const uint32		kSpinnerTick = 'sptk';

	virtual void MessageReceived(BMessage* message);

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
	/** takes the placeholder, its spinner and the question that is waited for away */
	void		 StopPending();
	/** the answer came while the menu is open: the items take the place of the placeholder */
	void		 PopulateAfterFetch();

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
	SpinnerMenuItem*	fFetchingItem;		///< "Fetching relations..." with a spinner, while it is
	bool		fFetchFailed;
	bool		fPending;				///< the menu is open with the placeholder, and the items come when the answer is in
	BMessageRunner*	fSpinnerRunner;		///< turns the spinner while it is

	typedef BSlowMenu _inherited;
};
