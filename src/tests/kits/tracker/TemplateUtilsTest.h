/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
*/

#ifndef __template_utils_test_h__
#define __template_utils_test_h__

#include <Path.h>
#include <TestCase.h>


class TemplateUtilsTest : public BTestCase {
public:
	static CppUnit::Test* Suite();

	void setUp();
	void tearDown();

	void FindPartialMatchFindsPrefix();
	void FindPartialMatchNoMatch();
	void GetInstalledTemplatesFiltersByMimeType();
	void GetTemplateForTypeCreatesTemplate();

private:
	BPath fTestDirPath;
};

#endif	// __template_utils_test_h__
