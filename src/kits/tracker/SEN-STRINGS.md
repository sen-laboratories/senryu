# New strings of the Tracker of SEN (Akita)

Localisation has low priority for now. These strings were added or changed for SEN, so that they are not lost when the catalogs are updated
(see `src/preferences/filetypes/SEN-STRINGS.md` for why the `.catkeys` files are not edited by hand).

| String | Context | Comment |
|--------|---------|---------|
| Open contained... | ContainerWindow | menu item for the relations that a file contains (was "Open self related..."), e.g. the bookmarks of a PDF or the attributes of a type |
| Open related... | ContainerWindow | menu item for the relations of a file |
| New related... | ContainerWindow | menu item to add a relation to a file |
| The SEN server is not running, files cannot be related. | PoseView | alert when files are dropped with Option on other files and the server is not there |
| No relation fits these files | PoseView | disabled item of the menu of an Option+drop when no relation can be created between the files |
| %count% of the relations could not be created. | PoseView | alert after an Option+drop; %count% is replaced by the number of relations that failed |
