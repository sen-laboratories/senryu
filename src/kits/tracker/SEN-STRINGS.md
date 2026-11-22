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
| Label of the relation | RelationLabelWindow | title of the window that asks for the label of a relation (Shift+choose in the menu of a Command+drop); the name of the relation is appended |
| Label: | RelationLabelWindow | text field of that window |
| Cancel | RelationLabelWindow | button |
| OK | RelationLabelWindow | button |
| Akita updated | TrackerSen | title of the alert that offers a restart when the package of Akita was updated |
| Akita has been updated. Restart it now to use the new version? Your windows are opened again. | TrackerSen | text of that alert |
| Later | TrackerSen | button |
| Restart | TrackerSen | button |
| Sorry, that is not a time. Type a day and time such as 2026-11-12 09:30, or a time that is not exact such as 1994-05~. | WidgetAttributeText | alert when the text typed into the start or end column of the list is no time (the example is not translated) |
| Sorry, that is not a time. Type a day and time such as 2026-11-12 09:30. | WidgetAttributeText | the same for another time attribute |
| All day | TextWidget | box in the calendar of a time that is edited in the list: the time of day is left out (the same text is not shown in the Find window, whose calendar is for days) |
| Custom… | TextWidget | last item of the list of Repeats: opens the window for a repeat cycle |
| Repeat | PoseView | title of that window (the other texts of it: Every, days, weeks, months, years, Ends, Never, After, times, On (comment: the last day of a repeat), Doesn't repeat, OK, Cancel, and the note that a repeat says more than can be set there) |
| n/a | WidgetAttributeText | text of a cell (greyed, not editable) whose attribute is none of the type of the file (the Event Type of a calendar); comment: not applicable |
