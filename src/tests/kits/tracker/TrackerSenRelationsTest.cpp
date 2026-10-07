/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
*/

#include "TrackerSenRelationsTest.h"

#include <cppunit/Test.h>
#include <cppunit/TestCaller.h>
#include <cppunit/TestSuite.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <NodeInfo.h>
#include <Path.h>

#include <filesystem>

#include <sen/Sen.h>

#include "TrackerSenRelations.h"


// Suite
CppUnit::Test*
TrackerSenRelationsTest::Suite()
{
	CppUnit::TestSuite* suite = new CppUnit::TestSuite();
	typedef CppUnit::TestCaller<TrackerSenRelationsTest> TC;

	suite->addTest(new TC("ResolveRelation: found",
		&TrackerSenRelationsTest::ResolveRelationFound));
	suite->addTest(new TC("ResolveRelation: missing attributes",
		&TrackerSenRelationsTest::ResolveRelationMissingAttributes));
	suite->addTest(new TC("GetInodeForRef: existing entry",
		&TrackerSenRelationsTest::GetInodeForRefExistingEntry));
	suite->addTest(new TC("GetInodeForRef: missing entry falls back",
		&TrackerSenRelationsTest::GetInodeForRefMissingEntryFallsBack));
	suite->addTest(new TC("ConvertAttributesToMessage: filters SEN prefix",
		&TrackerSenRelationsTest::ConvertAttributesToMessageFiltersSenPrefix));

	return suite;
}


void
TrackerSenRelationsTest::setUp()
{
	BTestCase::setUp();

	find_directory(B_SYSTEM_TEMP_DIRECTORY, &fTestDirPath);
	fTestDirPath.Append("TrackerSenRelationsTest");

	std::filesystem::remove_all(fTestDirPath.Path());
	create_directory(fTestDirPath.Path(), B_READ_WRITE);
}


void
TrackerSenRelationsTest::tearDown()
{
	std::filesystem::remove_all(fTestDirPath.Path());

	BTestCase::tearDown();
}


void
TrackerSenRelationsTest::ResolveRelationFound()
{
	BPath filePath(fTestDirPath.Path());
	filePath.Append("resolve-found");

	BFile file(filePath.Path(), B_CREATE_FILE | B_READ_WRITE);
	CPPUNIT_ASSERT(file.InitCheck() == B_OK);

	BString expectedSrcId("src-id-123");
	BString expectedTargetId("target-id-456");
	file.WriteAttrString(SEN_RELATION_SOURCE_ATTR, &expectedSrcId);
	file.WriteAttrString(SEN_RELATION_TARGET_ATTR, &expectedTargetId);
	file.Sync();

	BEntry entry(filePath.Path());
	entry_ref ref;
	entry.GetRef(&ref);

	BString srcId, targetId;
	bool found = TrackerSenRelations::ResolveRelation(&ref, &srcId, &targetId);

	CPPUNIT_ASSERT(found);
	CPPUNIT_ASSERT(srcId == expectedSrcId);
	CPPUNIT_ASSERT(targetId == expectedTargetId);
}


void
TrackerSenRelationsTest::ResolveRelationMissingAttributes()
{
	BPath filePath(fTestDirPath.Path());
	filePath.Append("resolve-missing");

	BFile file(filePath.Path(), B_CREATE_FILE | B_READ_WRITE);
	CPPUNIT_ASSERT(file.InitCheck() == B_OK);
	file.Sync();

	BEntry entry(filePath.Path());
	entry_ref ref;
	entry.GetRef(&ref);

	BString srcId, targetId;
	bool found = TrackerSenRelations::ResolveRelation(&ref, &srcId, &targetId);

	CPPUNIT_ASSERT(!found);
}


void
TrackerSenRelationsTest::GetInodeForRefExistingEntry()
{
	BPath filePath(fTestDirPath.Path());
	filePath.Append("inode-existing");

	BFile file(filePath.Path(), B_CREATE_FILE | B_READ_WRITE);
	CPPUNIT_ASSERT(file.InitCheck() == B_OK);
	file.Sync();

	BEntry entry(filePath.Path());
	entry_ref ref;
	entry.GetRef(&ref);

	BString inode;
	status_t result = TrackerSenRelations::GetInodeForRef(&ref, &inode);

	CPPUNIT_ASSERT(result == B_OK);
	CPPUNIT_ASSERT(!inode.IsEmpty());
}


void
TrackerSenRelationsTest::GetInodeForRefMissingEntryFallsBack()
{
	entry_ref missingRef;
	// device/directory left at 0, name points at a file that doesn't exist
	missingRef.set_name("does-not-exist");

	BString inode;
	status_t result = TrackerSenRelations::GetInodeForRef(&missingRef, &inode);

	// entry doesn't exist, so InitCheck() fails - but the fallback path
	// still populates inode from device/directory/name.
	CPPUNIT_ASSERT(result != B_OK);
	CPPUNIT_ASSERT(!inode.IsEmpty());
}


void
TrackerSenRelationsTest::ConvertAttributesToMessageFiltersSenPrefix()
{
	BPath filePath(fTestDirPath.Path());
	filePath.Append("convert-attrs");

	BFile file(filePath.Path(), B_CREATE_FILE | B_READ_WRITE);
	CPPUNIT_ASSERT(file.InitCheck() == B_OK);

	BString senValue("sen-value");
	BString otherValue("not-managed-by-sen");
	BString senAttrName(SEN_ATTR_PREFIX);
	senAttrName << "TestAttr";

	file.WriteAttrString(senAttrName.String(), &senValue);
	file.WriteAttrString("NotSen:TestAttr", &otherValue);
	file.Sync();

	BEntry entry(filePath.Path());
	entry_ref ref;
	entry.GetRef(&ref);

	BMessage params;
	status_t result = TrackerSenRelations::ConvertAttributesToMessage(&ref, &params);

	CPPUNIT_ASSERT(result == B_OK);

	BString readBack;
	CPPUNIT_ASSERT(params.FindString(senAttrName.String(), &readBack) == B_OK);
	CPPUNIT_ASSERT(readBack == senValue);
	CPPUNIT_ASSERT(!params.HasString("NotSen:TestAttr"));
}
