/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
*/

#include "TemplateUtilsTest.h"

#include <cppunit/Test.h>
#include <cppunit/TestCaller.h>
#include <cppunit/TestSuite.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <NodeInfo.h>
#include <Path.h>
#include <StringList.h>

#include <filesystem>

#include "TemplateUtils.h"


// Suite
CppUnit::Test*
TemplateUtilsTest::Suite()
{
	CppUnit::TestSuite* suite = new CppUnit::TestSuite();
	typedef CppUnit::TestCaller<TemplateUtilsTest> TC;

	suite->addTest(new TC("FindPartialMatch: finds prefix",
		&TemplateUtilsTest::FindPartialMatchFindsPrefix));
	suite->addTest(new TC("FindPartialMatch: no match",
		&TemplateUtilsTest::FindPartialMatchNoMatch));
	suite->addTest(new TC("GetInstalledTemplates: filters by MIME type",
		&TemplateUtilsTest::GetInstalledTemplatesFiltersByMimeType));
	suite->addTest(new TC("GetTemplateForType: creates template",
		&TemplateUtilsTest::GetTemplateForTypeCreatesTemplate));

	return suite;
}


void
TemplateUtilsTest::setUp()
{
	BTestCase::setUp();

	find_directory(B_SYSTEM_TEMP_DIRECTORY, &fTestDirPath);
	fTestDirPath.Append("TemplateUtilsTest");

	std::filesystem::remove_all(fTestDirPath.Path());
	create_directory(fTestDirPath.Path(), B_READ_WRITE);
}


void
TemplateUtilsTest::tearDown()
{
	std::filesystem::remove_all(fTestDirPath.Path());

	BTestCase::tearDown();
}


void
TemplateUtilsTest::FindPartialMatchFindsPrefix()
{
	BStringList names;
	names.Add("sen/entity");
	names.Add("text/plain");

	int32 index = TemplateUtils::FindPartialMatch("sen/entity/note", &names);

	CPPUNIT_ASSERT(index == 0);
}


void
TemplateUtilsTest::FindPartialMatchNoMatch()
{
	BStringList names;
	names.Add("sen/entity");
	names.Add("text/plain");

	int32 index = TemplateUtils::FindPartialMatch("image/png", &names);

	CPPUNIT_ASSERT(index == -1);
}


void
TemplateUtilsTest::GetInstalledTemplatesFiltersByMimeType()
{
	BPath includedPath(fTestDirPath.Path());
	includedPath.Append("included.txt");
	BFile includedFile(includedPath.Path(), B_CREATE_FILE | B_READ_WRITE);
	CPPUNIT_ASSERT(includedFile.InitCheck() == B_OK);
	BNodeInfo(&includedFile).SetType("text/plain");

	BPath excludedPath(fTestDirPath.Path());
	excludedPath.Append("excluded.bin");
	BFile excludedFile(excludedPath.Path(), B_CREATE_FILE | B_READ_WRITE);
	CPPUNIT_ASSERT(excludedFile.InitCheck() == B_OK);
	BNodeInfo(&excludedFile).SetType("application/octet-stream");

	BStringList mimeIncludes;
	mimeIncludes.Add("text/plain");

	BMessage templatesMsg;
	int32 count = TemplateUtils::GetInstalledTemplates(fTestDirPath.Path(),
		&mimeIncludes, NULL, &templatesMsg);

	CPPUNIT_ASSERT(count == 1);

	entry_ref ref;
	CPPUNIT_ASSERT(templatesMsg.FindRef("text/plain", &ref) == B_OK);
	CPPUNIT_ASSERT(!templatesMsg.HasRef("application/octet-stream"));
}


void
TemplateUtilsTest::GetTemplateForTypeCreatesTemplate()
{
	entry_ref ref;
	status_t result = TemplateUtils::GetTemplateForType("text/plain", &ref);

	CPPUNIT_ASSERT(result == B_OK);

	BNode node(&ref);
	CPPUNIT_ASSERT(node.InitCheck() == B_OK);

	BNodeInfo nodeInfo(&node);
	char mimeType[B_MIME_TYPE_LENGTH];
	nodeInfo.GetType(mimeType);
	CPPUNIT_ASSERT(BString(mimeType) == "text/plain");
}
