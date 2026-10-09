/*
 * Copyright 2006-2010, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "AttributeWindow.h"
#include "FileTypes.h"
#include "FileTypesWindow.h"

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Mime.h>
#include <PopUpMenu.h>
#include <SpaceLayoutItem.h>
#include <String.h>
#include <TextControl.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <fs_index.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Attribute Window"


// SEN: an attribute that is searchable has an index (on every volume that has queries), without it a query for the attribute
// finds nothing. BFS indexes numbers, strings and times; not booleans, messages, refs...
static uint32
index_type_for(type_code type)
{
	switch (type) {
		case B_INT32_TYPE:
		case B_UINT32_TYPE:
		case B_INT64_TYPE:
		case B_UINT64_TYPE:
		case B_FLOAT_TYPE:
		case B_DOUBLE_TYPE:
		case B_STRING_TYPE:
			return type;
		case B_MIME_STRING_TYPE:
			return B_STRING_TYPE;
		case B_TIME_TYPE:
			return B_INT64_TYPE;
		default:
			return 0;
	}
}


/** Create or remove the index of an attribute on all volumes that can have one. The first error is returned (existing and
 *  missing indices are not errors). */
static status_t
change_index_on_all_volumes(const char* name, type_code type, bool create)
{
	status_t result = B_OK;
	BVolumeRoster roster;
	BVolume volume;
	while (roster.GetNextVolume(&volume) == B_OK) {
		if (!volume.KnowsQuery() || !volume.KnowsAttr() || volume.IsReadOnly())
			continue;

		int status = create ? fs_create_index(volume.Device(), name, index_type_for(type), 0)
			: fs_remove_index(volume.Device(), name);
		if (status != 0 && errno != B_FILE_EXISTS && errno != B_ENTRY_NOT_FOUND && result == B_OK)
			result = errno;
	}
	return result;
}


const uint32 kMsgAttributeUpdated = 'atup';
const uint32 kMsgTypeChosen = 'typc';
const uint32 kMsgDisplayAsChosen = 'dach';
const uint32 kMsgVisibilityChanged = 'vsch';
const uint32 kMsgAlignmentChosen = 'alnc';
const uint32 kMsgAccept = 'acpt';


static bool
compare_display_as(const char* a, const char* b)
{
	bool emptyA = a == NULL || !a[0];
	bool emptyB = b == NULL || !b[0];

	if (emptyA && emptyB)
		return true;
	if (emptyA || emptyB)
		return false;

	const char* end = strchr(a, ':');
	int32 lengthA = end ? end - a : strlen(a);
	end = strchr(b, ':');
	int32 lengthB = end ? end - b : strlen(b);

	if (lengthA != lengthB)
		return false;

	return !strncasecmp(a, b, lengthA);
}


static const char*
display_as_parameter(const char* special)
{
	const char* parameter = strchr(special, ':');
	if (parameter != NULL)
		return parameter + 1;

	return NULL;
}


//	#pragma mark -


AttributeWindow::AttributeWindow(FileTypesWindow* target, BMimeType& mimeType,
		AttributeItem* attributeItem)
	:
	BWindow(BRect(100, 100, 350, 200), B_TRANSLATE("Attribute"),
		B_MODAL_WINDOW_LOOK, B_MODAL_SUBSET_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS),
	fTarget(target),
	fMimeType(mimeType.Type())
{
	float padding = be_control_look->DefaultItemSpacing();

	if (attributeItem != NULL)
		fAttribute = *attributeItem;

	fPublicNameControl = new BTextControl(B_TRANSLATE("Attribute name:"),
		fAttribute.PublicName(), NULL);
	fPublicNameControl->SetModificationMessage(
		new BMessage(kMsgAttributeUpdated));
	fPublicNameControl->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);

	fAttributeControl = new BTextControl(B_TRANSLATE("Internal name:"),
		fAttribute.Name(), NULL);
	fAttributeControl->SetModificationMessage(
		new BMessage(kMsgAttributeUpdated));
	fAttributeControl->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);

	// filter out invalid characters that can't be part of an attribute
	BTextView* textView = fAttributeControl->TextView();
	const char* disallowedCharacters = "/";
	for (int32 i = 0; disallowedCharacters[i]; i++) {
		textView->DisallowChar(disallowedCharacters[i]);
	}

	fTypeMenu = new BPopUpMenu("type");
	BMenuItem* item = NULL;
	for (int32 i = 0; kTypeMap[i].name != NULL; i++) {
		BMessage* message = new BMessage(kMsgTypeChosen);
		message->AddInt32("type", kTypeMap[i].type);

		item = new BMenuItem(kTypeMap[i].name, message);
		fTypeMenu->AddItem(item);

		if (kTypeMap[i].type == fAttribute.Type())
			item->SetMarked(true);
	}

	BMenuField* typeMenuField = new BMenuField("types" , B_TRANSLATE("Type:"),
		fTypeMenu);
	typeMenuField->SetAlignment(B_ALIGN_RIGHT);
	// we must set the color manually when adding a menuField directly
	// into a window.
	typeMenuField->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	typeMenuField->SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fVisibleCheckBox = new BCheckBox("visible", B_TRANSLATE("Visible"),
		new BMessage(kMsgVisibilityChanged));
	fVisibleCheckBox->SetValue(fAttribute.Visible());

	BMenu* menu = new BPopUpMenu("display as");
	for (int32 i = 0; kDisplayAsMap[i].name != NULL; i++) {
		BMessage* message = new BMessage(kMsgDisplayAsChosen);
		if (kDisplayAsMap[i].identifier != NULL) {
			message->AddString("identifier", kDisplayAsMap[i].identifier);
			for (int32 j = 0; kDisplayAsMap[i].supported[j]; j++) {
				message->AddInt32("supports", kDisplayAsMap[i].supported[j]);
			}
		}

		item = new BMenuItem(kDisplayAsMap[i].name, message);
		menu->AddItem(item);

		if (compare_display_as(kDisplayAsMap[i].identifier,
				fAttribute.DisplayAs()))
			item->SetMarked(true);
	}

	fDisplayAsMenuField = new BMenuField("display as",
		B_TRANSLATE_COMMENT("Display as:",
			"Tracker offers different display modes for attributes."), menu);
	fDisplayAsMenuField->SetAlignment(B_ALIGN_RIGHT);

	fEditableCheckBox = new BCheckBox("editable",
		B_TRANSLATE_COMMENT("Editable",
			"If Tracker allows to edit this attribute."),
		new BMessage(kMsgAttributeUpdated));
	fEditableCheckBox->SetValue(fAttribute.Editable());

	// SEN: independent of whether the attribute is displayed
	fSearchableCheckBox = new BCheckBox("searchable",
		B_TRANSLATE_COMMENT("Searchable",
			"The attribute is indexed on the volumes so that it can be queried."),
		new BMessage(kMsgAttributeUpdated));
	fSearchableCheckBox->SetValue(fAttribute.Searchable());

	fSpecialControl = new BTextControl(B_TRANSLATE("Special:"),
		display_as_parameter(fAttribute.DisplayAs()), NULL);
	fSpecialControl->SetModificationMessage(
		new BMessage(kMsgAttributeUpdated));
	fSpecialControl->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);
	fSpecialControl->SetEnabled(false);

	char text[64];
	snprintf(text, sizeof(text), "%" B_PRId32, fAttribute.Width());
	fWidthControl = new BTextControl(B_TRANSLATE_COMMENT("Width:",
		"Default column width in Tracker for this attribute."),
		text, NULL);
	fWidthControl->SetModificationMessage(
		new BMessage(kMsgAttributeUpdated));
	fWidthControl->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);

	// filter out invalid characters that can't be part of a width
	textView = fWidthControl->TextView();
	for (int32 i = 0; i < 256; i++) {
		if (!isdigit(i))
			textView->DisallowChar(i);
	}
	textView->SetMaxBytes(4);

	const struct alignment_map {
		int32		alignment;
		const char*	name;
	} kAlignmentMap[] = {
		{B_ALIGN_LEFT, B_TRANSLATE_COMMENT("Left",
			"Attribute column alignment in Tracker")},
		{B_ALIGN_RIGHT, B_TRANSLATE_COMMENT("Right",
			"Attribute column alignment in Tracker")},
		{B_ALIGN_CENTER, B_TRANSLATE_COMMENT("Center",
			"Attribute column alignment in Tracker")},
		{0, NULL}
	};

	menu = new BPopUpMenu("alignment");
	for (int32 i = 0; kAlignmentMap[i].name != NULL; i++) {
		BMessage* message = new BMessage(kMsgAlignmentChosen);
		message->AddInt32("alignment", kAlignmentMap[i].alignment);

		item = new BMenuItem(kAlignmentMap[i].name, message);
		menu->AddItem(item);

		if (kAlignmentMap[i].alignment == fAttribute.Alignment())
			item->SetMarked(true);
	}

	fAlignmentMenuField = new BMenuField("alignment",
		B_TRANSLATE("Alignment:"), menu);
	fAlignmentMenuField->SetAlignment(B_ALIGN_RIGHT);

	fAcceptButton = new BButton("add",
		item ? B_TRANSLATE("Done") : B_TRANSLATE("Add"),
		new BMessage(kMsgAccept));
	fAcceptButton->SetEnabled(false);

	BButton* cancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));

	BBox* visibleBox;
	BLayoutBuilder::Group<>(this, B_VERTICAL, padding)
		.SetInsets(padding, padding, padding, padding)
		.AddGrid(padding, padding / 2)
			.Add(fPublicNameControl->CreateLabelLayoutItem(), 0, 0)
			.Add(fPublicNameControl->CreateTextViewLayoutItem(), 1, 0)
			.Add(fAttributeControl->CreateLabelLayoutItem(), 0, 1)
			.Add(fAttributeControl->CreateTextViewLayoutItem(), 1, 1)
			.Add(typeMenuField->CreateLabelLayoutItem(), 0, 2)
			.Add(typeMenuField->CreateMenuBarLayoutItem(), 1, 2)
			.End()
		.Add(visibleBox = new BBox(B_FANCY_BORDER,
			BLayoutBuilder::Grid<>(padding, padding / 2)
				.Add(fDisplayAsMenuField->CreateLabelLayoutItem(), 0, 0)
				.Add(fDisplayAsMenuField->CreateMenuBarLayoutItem(), 1, 0)
				.Add(fEditableCheckBox, 2, 0)
				.Add(fSpecialControl->CreateLabelLayoutItem(), 0, 1)
				.Add(fSpecialControl->CreateTextViewLayoutItem(), 1, 1, 2)
				.Add(fWidthControl->CreateLabelLayoutItem(), 0, 2)
				.Add(fWidthControl->CreateTextViewLayoutItem(), 1, 2, 2)
				.Add(fAlignmentMenuField->CreateLabelLayoutItem(), 0, 3)
				.Add(fAlignmentMenuField->CreateMenuBarLayoutItem(), 1, 3, 2)
				.SetInsets(padding, padding, padding, padding)
				.View())
			)
		.Add(fSearchableCheckBox)
		.AddGroup(B_HORIZONTAL, padding)
			.AddGlue()
			.Add(cancelButton)
			.Add(fAcceptButton);

	visibleBox->SetLabel(fVisibleCheckBox);

	fAcceptButton->MakeDefault(true);
	fPublicNameControl->MakeFocus(true);

	target->PlaceSubWindow(this);
	AddToSubset(target);

	_CheckDisplayAs();
	_CheckSearchable();
	_CheckAcceptable();
}


AttributeWindow::~AttributeWindow()
{
}


type_code
AttributeWindow::_CurrentType() const
{
	type_code type = B_STRING_TYPE;
	BMenuItem* item = fTypeMenu->FindMarked();
	if (item != NULL && item->Message() != NULL) {
		int32 value;
		if (item->Message()->FindInt32("type", &value) == B_OK)
			type = value;
	}

	return type;
}


BMenuItem*
AttributeWindow::_DefaultDisplayAs() const
{
	return fDisplayAsMenuField->Menu()->ItemAt(0);
}


void
AttributeWindow::_CheckDisplayAs()
{
	// check display as suported types

	type_code currentType = _CurrentType();

	BMenu* menu = fDisplayAsMenuField->Menu();
	for (int32 i = menu->CountItems(); i-- > 0;) {
		BMenuItem* item = menu->ItemAt(i);
		bool supported = item == _DefaultDisplayAs();
			// the default type is always supported
		type_code type;
		for (int32 j = 0; item->Message()->FindInt32("supports",
				j, (int32*)&type) == B_OK; j++) {
			if (type == currentType) {
				supported = true;
				break;
			}
		}

		item->SetEnabled(supported);
		if (item->IsMarked() && !supported)
			menu->ItemAt(0)->SetMarked(true);
	}

	fSpecialControl->SetEnabled(!_DefaultDisplayAs()->IsMarked());
}


void
AttributeWindow::_CheckAcceptable()
{
	bool enabled = fAttributeControl->Text() != NULL
		&& fAttributeControl->Text()[0] != '\0'
		&& fPublicNameControl->Text() != NULL
		&& fPublicNameControl->Text()[0] != '\0';

	if (enabled) {
		// check for equality
		AttributeItem* item = _NewItemFromCurrent();
		enabled = fAttribute != *item;
		delete item;
	}

	// Update button

	if (fAcceptButton->IsEnabled() != enabled)
		fAcceptButton->SetEnabled(enabled);
}


AttributeItem*
AttributeWindow::_NewItemFromCurrent()
{
	const char* newAttribute = fAttributeControl->Text();

	type_code type = _CurrentType();

	int32 alignment = B_ALIGN_LEFT;
	BMenuItem* item = fAlignmentMenuField->Menu()->FindMarked();
	if (item != NULL && item->Message() != NULL) {
		int32 value;
		if (item->Message()->FindInt32("alignment", &value) == B_OK)
			alignment = value;
	}

	int32 width = atoi(fWidthControl->Text());
	if (width < 0)
		width = 0;

	BString displayAs;
	item = fDisplayAsMenuField->Menu()->FindMarked();
	if (item != NULL) {
		const char* identifier;
		if (item->Message()->FindString("identifier", &identifier) == B_OK) {
			displayAs = identifier;

			if (fSpecialControl->Text() && fSpecialControl->Text()[0]) {
				displayAs += ":";
				displayAs += fSpecialControl->Text();
			}
		}
	}

	return new AttributeItem(newAttribute,
		fPublicNameControl->Text(), type, displayAs.String(), alignment,
		width, fVisibleCheckBox->Value() == B_CONTROL_ON,
		fEditableCheckBox->Value() == B_CONTROL_ON,
		fSearchableCheckBox->Value() == B_CONTROL_ON);
}


// SEN: only what BFS can index can be searchable (not a boolean, not a 16 bit number, not a message,...)
void
AttributeWindow::_CheckSearchable()
{
	AttributeItem* current = _NewItemFromCurrent();
	bool indexable = index_type_for(current->Type()) != 0;
	delete current;

	if (!indexable)
		fSearchableCheckBox->SetValue(B_CONTROL_OFF);
	fSearchableCheckBox->SetEnabled(indexable);

	// say why it is not available
	fSearchableCheckBox->SetToolTip(indexable ? NULL
		: B_TRANSLATE("Only numbers, times and strings can be indexed, so only these can be searchable. "
			"A boolean, a 16 bit number or a message cannot."));
}


void
AttributeWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAttributeUpdated:
		case kMsgAlignmentChosen:
		case kMsgTypeChosen:
			_CheckDisplayAs();
			_CheckSearchable();
			_CheckAcceptable();
			break;

		case kMsgDisplayAsChosen:
			fSpecialControl->SetEnabled(!_DefaultDisplayAs()->IsMarked());
			_CheckAcceptable();
			break;

		case kMsgVisibilityChanged:
		{
			bool enabled = fVisibleCheckBox->Value() != B_CONTROL_OFF;

			fDisplayAsMenuField->SetEnabled(enabled);
			fWidthControl->SetEnabled(enabled);
			fAlignmentMenuField->SetEnabled(enabled);
			fEditableCheckBox->SetEnabled(enabled);

			_CheckDisplayAs();
			_CheckAcceptable();
			break;
		}

		case kMsgAccept:
		{
			// SEN: changing "searchable" changes the index of the attribute: the user decides
			AttributeItem* current = _NewItemFromCurrent();
			bool wasSearchable = fAttribute.Searchable();
			BString oldName(fAttribute.Name());
			BString newName(current->Name());
			type_code newType = current->Type();
			bool searchable = current->Searchable();
			delete current;

			int32 indexChange = 0;		// 1: create the index of newName, -1: remove the index of oldName
			if (searchable && (!wasSearchable || oldName != newName))
				indexChange = 1;
			else if (!searchable && wasSearchable)
				indexChange = -1;

			if (indexChange == 1 && index_type_for(newType) == 0) {
				BAlert* alert = new BAlert(B_TRANSLATE("No index possible"),
					B_TRANSLATE("The type of this attribute cannot be indexed, so it cannot be searchable. "
						"Choose a number, a string or a time."), B_TRANSLATE("OK"), NULL, NULL,
					B_WIDTH_AS_USUAL, B_WARNING_ALERT);
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
				break;
			}

			if (indexChange != 0) {
				BString text(indexChange > 0
					? B_TRANSLATE("The attribute is searchable now. Create its index on all volumes? Queries for an "
						"attribute without an index find nothing.")
					: B_TRANSLATE("The attribute is not searchable any more. Remove its index from all volumes? "
						"Queries for it will find nothing then."));
				BAlert* alert = new BAlert(B_TRANSLATE("Index"), text.String(), B_TRANSLATE("Cancel"),
					B_TRANSLATE("Leave the index"),
					indexChange > 0 ? B_TRANSLATE("Create index") : B_TRANSLATE("Remove index"),
					B_WIDTH_AS_USUAL, B_WARNING_ALERT);
				alert->SetShortcut(0, B_ESCAPE);
				int32 choice = alert->Go();
				if (choice == 0)
					break;			// back to the window, nothing is changed
				if (choice == 1)
					indexChange = 0;
			}

			BMessage attributes;
			status_t status = fMimeType.GetAttrInfo(&attributes);
			if (status == B_OK) {
				// replace the entry, and remove any equivalent entries
				BList list;

				const char* newAttribute = fAttributeControl->Text();

				const char* attribute;
				for (int32 i = 0; attributes.FindString("attr:name", i,
						&attribute) == B_OK; i++) {
					if (!strcmp(fAttribute.Name(), attribute)
						|| !strcmp(newAttribute, attribute)) {
						// remove this item
						continue;
					}

					AttributeItem* item = create_attribute_item(attributes, i);
					if (item != NULL)
						list.AddItem(item);
				}

				list.AddItem(_NewItemFromCurrent());

				// Copy them to a new message (their memory is still part of the
				// original BMessage)
				BMessage newAttributes;
				for (int32 i = 0; i < list.CountItems(); i++) {
					AttributeItem* item = (AttributeItem*)list.ItemAt(i);

					newAttributes.AddString("attr:name", item->Name());
					newAttributes.AddString("attr:public_name", item->PublicName());
					newAttributes.AddInt32("attr:type", (int32)item->Type());
					newAttributes.AddString("attr:display_as", item->DisplayAs());
					newAttributes.AddInt32("attr:alignment", item->Alignment());
					newAttributes.AddInt32("attr:width", item->Width());
					newAttributes.AddBool("attr:viewable", item->Visible());
					newAttributes.AddBool("attr:editable", item->Editable());
					newAttributes.AddBool(kSenSearchable, item->Searchable());

					delete item;
				}

				status = fMimeType.SetAttrInfo(&newAttributes);
			}

			if (status != B_OK) {
				error_alert(B_TRANSLATE("Could not change attributes"),
					status);
			} else if (indexChange != 0) {
				status = indexChange > 0
					? change_index_on_all_volumes(newName.String(), newType, true)
					: change_index_on_all_volumes(oldName.String(), newType, false);
				if (status != B_OK)
					error_alert(B_TRANSLATE("Could not change the index"), status);
			}

			PostMessage(B_QUIT_REQUESTED);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
AttributeWindow::QuitRequested()
{
	return true;
}
