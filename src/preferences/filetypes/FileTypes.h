/*
 * Copyright 2006, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FILE_TYPES_H
#define FILE_TYPES_H


#include <Alert.h>

class BFile;


extern const char* kSignature;

static const uint32 kMsgOpenFilePanel = 'opFp';

static const uint32 kMsgOpenTypesWindow = 'opTw';
static const uint32 kMsgTypesWindowClosed = 'clTw';

static const uint32 kMsgOpenApplicationTypesWindow = 'opAw';
static const uint32 kMsgApplicationTypesWindowClosed = 'clAw';

static const uint32 kMsgTypeWindowClosed = 'cltw';
static const uint32 kMsgWindowClosed = 'WiCl';

static const uint32 kMsgSettingsChanged = 'SeCh';

// SEN: open an attribute of a type (sen::cmd::kOpenMimeAttribute, with SEN:mimeType and SEN:attr:name), asked by the application of
// another program (the navigator of references). The window of the types opens the attribute when its type is the current one.
static const uint32 kMsgSenOpenMimeAttribute = 'SOma';
static const uint32 kMsgOpenAttributeByName = 'oaBn';
#define kSenMimeTypeKey "SEN:mimeType"
#define kSenAttributeNameKey "SEN:attr:name"


// exported functions

extern bool is_application(BFile& file);
extern bool is_resource(BFile& file);
extern void error_alert(const char* message, status_t status = B_OK,
	alert_type type = B_WARNING_ALERT);

#endif	// FILE_TYPES_H
