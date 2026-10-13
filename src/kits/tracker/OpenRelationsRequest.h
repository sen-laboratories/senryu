/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <Message.h>
#include <String.h>

/**
 * The request to open the folder of the relations of a file ("Open related..." and "Open contained..." in the menus): what the item of the
 * menu says, read from its message into fields and checked, and made into a message again for the application, with these fields only.
 * (This replaces copying all the fields of the message that the menu sent, whatever they were.)
 *
 *   refs                      the file (required)
 *   SEN:sourceId              the SEN:ID of the file, if it has one
 *   SEN:relationContext       the context of the menu that made the item (see RelationContext), required to find the relations
 */
struct OpenRelationsRequest {
	entry_ref	sourceRef;
	BString		sourceId;
	int64		contextId;

	OpenRelationsRequest() : contextId(0) {}

	/** Read from a message; B_BAD_VALUE (with the reason in @p error, if given) if something required is missing. */
	static status_t	Parse(const BMessage& message, OpenRelationsRequest* request, BString* error = NULL);

	/** The message for the application: @p what is kOpenRelations or kOpenSelfRelations. */
	void			ToMessage(uint32 what, BMessage* message) const;
};

/** Asks the SEN server for the relations of a file and keeps what the menus of the file would (the request for the application, see above);
 *  @p what is kOpenRelations or kOpenSelfRelations. B_NAME_NOT_FOUND if the file has no relations. */
status_t RequestOpenRelationsOf(uint32 what, const entry_ref& source, OpenRelationsRequest* request);

/** Opens the folder of all relations of a file in the file manager (the same as "Open related..."): used by the question before a file is deleted. */
void ShowRelationsInTracker(const entry_ref& source);
