/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <String.h>

/**
 * Asks the user for the label of a relation that is about to be created (a modal window with a text field; Enter confirms, Esc cancels).
 * Blocks like BAlert::Go() does, so it can be called from the thread of a window.
 *
 * @param relationName the name of the relation, for the title of the window
 * @param label in: the text to start with; out: what the user entered (can be empty: no label)
 * @return false if the user cancelled
 */
bool PromptForRelationLabel(const char* relationName, BString* label);
