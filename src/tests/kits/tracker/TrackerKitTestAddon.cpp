/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
*/

#include <TestSuite.h>
#include <TestSuiteAddon.h>

#include "TemplateUtilsTest.h"
#include "TrackerSenRelationsTest.h"

BTestSuite* getTestSuite() {
	BTestSuite* suite = new BTestSuite("Tracker");

	suite->addTest("TrackerSenRelations", TrackerSenRelationsTest::Suite());
	suite->addTest("TemplateUtils", TemplateUtilsTest::Suite());

	return suite;
}
