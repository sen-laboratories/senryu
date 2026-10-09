# New strings of the FileTypes of SEN

Localisation has low priority for now. These are the strings that SEN added to FileTypes, with their context and comment, so that they
are not lost when the catalogs are updated (`jam -q catkeys`, then the translators). The `.catkeys` files are not edited by hand: each has
a fingerprint of its source strings, and Haiku skips a catalog whose fingerprint does not match.

The signature of this FileTypes is `application/x-vnd.sen-labs.FileTypes` (the one of Haiku is `application/x-vnd.Haiku-FileTypes`), and the
catalogs are found by the signature: until the catalogs exist for the new signature (the signature in the header of every `.catkeys` file and in the
`DoCatalogs` rule of the Jamfile), it shows English only.

## Context "Attribute Window" (AttributeWindow.cpp)

| String | Comment |
|--------|---------|
| Searchable | The attribute is indexed on the volumes so that it can be queried. |
| Only numbers, times and strings can be indexed, so only these can be searchable. A boolean, a 16 bit number or a message cannot. | (tooltip of the checkbox when it is not available) |
| No index possible | |
| The type of this attribute cannot be indexed, so it cannot be searchable. Choose a number, a string or a time. | |
| Index | (title of the question about the index) |
| The attribute is searchable now. Create its index on all volumes? Queries for an attribute without an index find nothing. | |
| The attribute is not searchable any more. Remove its index from all volumes? Queries for it will find nothing then. | |
| Leave the index | |
| Create index | |
| Remove index | |
| Could not change the index | |
| OK | (button of the message that the type cannot be indexed; new in this context, "Cancel" exists already) |
