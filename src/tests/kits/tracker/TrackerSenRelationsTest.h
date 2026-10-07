/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
*/

#ifndef __tracker_sen_relations_test_h__
#define __tracker_sen_relations_test_h__

#include <Path.h>
#include <TestCase.h>


class TrackerSenRelationsTest : public BTestCase {
public:
	static CppUnit::Test* Suite();

	void setUp();
	void tearDown();

	void ResolveRelationFound();
	void ResolveRelationMissingAttributes();
	void GetInodeForRefExistingEntry();
	void GetInodeForRefMissingEntryFallsBack();
	void ConvertAttributesToMessageFiltersSenPrefix();

private:
	BPath fTestDirPath;
};

#endif	// __tracker_sen_relations_test_h__
