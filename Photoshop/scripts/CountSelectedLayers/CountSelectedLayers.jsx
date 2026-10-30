// CountSelectedLayers.jsx
// Shows how many layers are currently selected in the active Photoshop document.
// Groups count as one layer each (if the group itself is selected).

#target photoshop

(function () {
    if (app.documents.length === 0) {
        alert("No document open.");
        return;
    }

    var ref = new ActionReference();
    ref.putProperty(charIDToTypeID("Prpr"), stringIDToTypeID("targetLayers"));
    ref.putEnumerated(charIDToTypeID("Dcmn"), charIDToTypeID("Ordn"), charIDToTypeID("Trgt"));
    var desc = executeActionGet(ref);

    var count = 0;
    if (desc.hasKey(stringIDToTypeID("targetLayers"))) {
        count = desc.getList(stringIDToTypeID("targetLayers")).count;
    }

    alert(count + " layer" + (count === 1 ? "" : "s") + " selected.");
})();
