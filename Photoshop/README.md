# CountSelectedLayers

A small Photoshop script that shows how many layers you have selected in the active document.

## Usage

**Run it once:** choose **File → Scripts → Browse…** and select `CountSelectedLayers.jsx`.

**Install it:** copy `CountSelectedLayers.jsx` into Photoshop's `Presets/Scripts` folder and restart Photoshop. The script then appears under **File → Scripts → CountSelectedLayers**.

| OS      | Folder                                                          |
|---------|-----------------------------------------------------------------|
| Windows | `C:\Program Files\Adobe\Adobe Photoshop <version>\Presets\Scripts` |
| macOS   | `/Applications/Adobe Photoshop <version>/Presets/Scripts`       |

Replace `<version>` with your installed version (e.g. `2026`). Copying into this folder requires admin rights.

> Note: On both Windows 11 and macOS, Adobe gives the `Adobe Photoshop <version>` folder Photoshop's icon, so it looks like the app rather than a folder. It's still a folder; open it normally.

**Keyboard shortcut (optional):** after installing, assign a shortcut in **Edit → Keyboard Shortcuts…** under *Application Menus → File → Scripts*.

## Behavior

- Shows an alert such as `3 layers selected.`
- A selected group counts as **one** layer; the layers inside it are not counted.
- If no document is open, shows `No document open.`

## Compatibility

Tested only by inspection. It should work in Photoshop CC 2015 and later, because it reads the `targetLayers` property through the Action Manager.

