/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
*/

#pragma once

#include <Message.h>
#include <StringList.h>
#include <SupportDefs.h>

class TemplateUtils {
public:
    static int32    GetInstalledTemplates(const char* path = NULL,
										  const BStringList* mimeIncludes = NULL,
										  const BStringList* mimeExcludes = NULL,
                                          BMessage *templatesMsg = new BMessage());
    static status_t GetTemplateForType(const char* mimeType, entry_ref* ref);

	static int32    FindPartialMatch(const char* nameToFind, const BStringList* names);

	static status_t GetUserTemplatesPath(BPath* path);

    // only static access allowed
    TemplateUtils() = delete;

private:
	inline static const char* templatesPath = NULL;
};
