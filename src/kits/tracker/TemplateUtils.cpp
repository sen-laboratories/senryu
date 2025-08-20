/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
*/

#include <Directory.h>
#include <NodeInfo.h>
#include <Path.h>
#define DEBUG 1
#include <Debug.h>
#include <FindDirectory.h>

#include "Sen.h"
#include "TemplateUtils.h"
// NOTE: if used from TemplatesMenu later, remove this and move kTemplatesDirectory to this header!
#include "TemplatesMenu.h"

// LATER: cache and watch templates dir for changes using WatchNode()
int32 TemplateUtils::GetInstalledTemplates(
    const char* path,
	const BStringList* mimeIncludes,
    const BStringList* mimeExcludes,
	BMessage* templatesMsg)
{
	BEntry entry;
	BPath templatesPath;
	int32 templatesCount = 0;

	if (path == NULL) {
		status_t status = GetUserTemplatesPath(&templatesPath);
		if (status != B_OK) {
			return -1;
		}
		path = templatesPath.Path();
	}
	PRINT(("  >> entering templates path %s...\n", path ));

	BDirectory templatesDir(path);

	while (templatesDir.GetNextEntry(&entry) == B_OK) {
		BNode node(&entry);
		BNodeInfo nodeInfo(&node);
		char fileName[B_FILE_NAME_LENGTH];

		entry.GetName(fileName);
		if (nodeInfo.InitCheck() == B_OK) {
			char mimeType[B_MIME_TYPE_LENGTH];
			nodeInfo.GetType(mimeType);

			BMimeType mime(mimeType);
			if (mime.IsValid()) {
				entry_ref ref;
				entry.GetRef(&ref);

				// Check if the template is a directory
				BDirectory dir(&entry);
				if (dir.InitCheck() == B_OK) {
					BPath subdirPath;
					if (entry.GetPath(&subdirPath) == B_OK) {
						int32 subDirResult = GetInstalledTemplates(
							subdirPath.Path(), mimeIncludes, mimeExcludes, templatesMsg);
						if (subDirResult < 0)
							return subDirResult;	// return error
						else
							templatesCount += subDirResult;
					}
					continue;
				}

				// filter for included/excluded MIME types
				// filters are optional, but IF incldue filter is given, the element MUST be included,
				// so use a designated negative index (different from `-1` for "not found") as default for that.
				int32 indexInclude = -2, indexExclude = -1;

				if (mimeIncludes != NULL && ! mimeIncludes->IsEmpty()) {
					// type MUST be included in filter
					// we need to check every element to support partial names as patterns, e.g. just a supertype
					indexInclude = FindPartialMatch(mimeType, mimeIncludes);
				}

				// type could still be excluded below
				// Note: (partial) type may be included in both lists, e.g. include "entity" but exclude "entity/blah"
				//       in that case, the item will be excluded
				if (mimeExcludes != NULL && ! mimeExcludes->IsEmpty()) {
					// type MAY be included in filter
					// we need to check every element to support partial names as patterns, e.g. just a supertype
					indexExclude = FindPartialMatch(mimeType, mimeExcludes);
				}

				if (indexInclude != -1 && indexExclude == -1) {
					// add type + ref to result
					PRINT(("  > ADD MIME Type %s.\n", mimeType ));
					templatesMsg->AddRef(mimeType, &ref);
					templatesCount++;
				} else {
					PRINT(("  > SKIP MIME Type %s.\n", mimeType ));
					continue;
				}
			}
		}
	}
	PRINT(("%d matching templates found.\n", templatesCount));

	if (templatesCount >= 0)
		return templatesCount;
	else
		return B_ERROR;
}

status_t TemplateUtils::GetUserTemplatesPath(BPath* path) {
	status_t result = B_OK;

	// to be correct, we should block on a Benaphore or similar here, but it doesn't really matter here

	if ((result = find_directory(B_USER_SETTINGS_DIRECTORY, path)) != B_OK)
	{
		PRINT(("could not find user settings directory (using default): %s\n", strerror(result)));
		path->SetTo("/boot/home/config/settings");
	}

	path->Append(kTemplatesDirectory);
	PRINT(("templates path is %s\n", path->Path()));

	return result;
}

int32 TemplateUtils::FindPartialMatch(const char* nameToFind, const BStringList* names)
{
	if (names == NULL || names->IsEmpty())
		return -1;

	BString nameStr(nameToFind);

	for (int32 i = 0; i < names->CountStrings(); i++) {
		if (nameStr.StartsWith(names->StringAt(i))) {
			return i;
		}
	}
	return -1;
}

status_t TemplateUtils::GetTemplateForType(const char* mimeType, entry_ref* ref)
{
	status_t result;

	// build simple MIME path
	BMimeType mime(mimeType);
	if ((result = mime.InitCheck()) != B_OK) {
		PRINT(("invalid MIME type %s: %s\n", mimeType, strerror(result)));
		return result;
	}

	// create new tmp file of given type to act as template for now
	BPath templatePath;

	// use existing path?
	if (templatePath.SetTo(ref) != B_OK) {
		if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &templatePath) != B_OK) {
			PRINT(("could not find user settings directory, falling back to /tmp.\n"));
			templatePath.SetTo("/tmp");
		}
		templatePath.Append("sen");
		templatePath.Append(SEN_ENTITY_SUPERTYPE);
	}

	BDirectory outputDir;
	result = create_directory(templatePath.Path(), B_CREATE_FILE);
	if (result != B_OK && result != B_FILE_EXISTS) {
		PRINT(("failed to set up template directory: %s\n", strerror(result) ));
		return result;
	}

	// create template with MIME path, possibly reuse existing one
	BFile templateFile;
	char shortDesc[B_MIME_TYPE_LENGTH];
	mime.GetShortDescription(shortDesc);
	templatePath.Append(shortDesc);

	outputDir.CreateFile(templatePath.Path(), &templateFile);

	result = templateFile.InitCheck();
	if (result != B_OK && result != B_FILE_EXISTS) {
		PRINT(("failed to create template at path %s: %s\n", mimeType, strerror(result) ));
		return result;
	}

	// set type
	BNodeInfo templateInfo(&templateFile);
	if ((result = templateInfo.InitCheck()) == B_OK) {
		result = templateInfo.SetType(mimeType);
		if (result == B_OK) {
			// get ref of our temp template
			templateFile.Sync();
			BEntry templateEntry(templatePath.Path());
			if ((result = templateEntry.InitCheck()) == B_OK) {
				templateEntry.GetRef(ref);
				PRINT(("got ref %s in %s for Tracker New.\n", ref->name, templatePath.Path() ));
			}
		}
	}
	return result;
}
