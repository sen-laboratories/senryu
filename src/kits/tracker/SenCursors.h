/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

class BCursor;

/** The cursor for the SEN mode of a drag (Option pressed): the knot of SEN Labs, hotspot in its middle. Made when first needed and kept;
 *  NULL if it could not be made (then the cursor stays as it is). */
const BCursor* SenKnotCursor();
