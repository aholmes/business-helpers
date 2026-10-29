# Inches Property Handler

Adds one row to **Properties → Details → Image** for image files, directly under
*Dimensions*:

| Row                 | Example        |
|---------------------|----------------|
| Dimensions (inches) | `6.67 × 10 in` |

Computed as `pixels ÷ DPI` using the width/height/resolution values Windows
already reports for the file.

If anything goes wrong, the row reads **Error**. That covers: the original
handler can't be found, created or initialized; width, height or horizontal DPI is
missing; or a value is zero/negative. (A missing *vertical* DPI is tolerated by
assuming it equals the horizontal one.) The reason is logged with
`OutputDebugString`; run Sysinternals DebugView (Capture Win32) and look for lines
starting with `InchesHandler:`.

When the original handler fails, the wrapper still initializes, so the Details tab
shows the file-system properties plus the Error row instead of nothing. In that
state the file's metadata is read-only through Explorer.

## How it works

Windows allows **one** property handler per extension
(`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\.jpg`).
Replacing Microsoft's would lose all native metadata, so this DLL is a **wrapper**:

1. `install.ps1` reads the existing handler CLSID for each extension and registers a
   wrapper CLSID (`{E35046DE-611F-44CA-BC68-2E042D1CAE01}`, `…02`, … one per
   extension) whose `WrappedHandler` value records the original.
2. The wrapper creates the original handler, forwards every `IPropertyStore` call
   to it (including writes, so editing tags/rating still works), and appends one
   read-only string property.
3. The property is described by `schema\InchesProps.propdesc`, registered with
   `PSRegisterPropertySchema`.
4. The extension's `FullDetails` list is patched to show it under Dimensions in the Details tab.

Every registry change is backed up under `HKLM\SOFTWARE\InchesPropertyHandler`;
`uninstall.ps1` restores from there.

## Build

A prebuilt x64 DLL is included at `build\InchesPropertyHandler.dll`
(cross-compiled with clang/mingw-w64, depends only on DLLs built into Windows 10/11).
SHA-256: `8252226f40cefd547d4c891d2140b0ec8180e21ed82d6aa2e397647634c2a51f`

To rebuild it yourself instead, from an **x64 Native Tools Command Prompt for VS**
(Visual Studio or the free Build Tools):

```
build.cmd
```

Output: `build\InchesPropertyHandler.dll` (64-bit, static CRT).

## Install / uninstall

From an **elevated** PowerShell in this folder:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\install.ps1 -RestartExplorer
# optional: .\install.ps1 -Extensions .jpg,.jpeg,.png,.tif,.tiff -RestartExplorer

.\uninstall.ps1 -RestartExplorer
```

Default extensions: `.jpg .jpeg .png .tif .tiff .bmp .gif`. Extensions with no
existing handler are skipped (there is nothing to wrap).

To verify without the GUI:

```powershell
$sh = New-Object -ComObject Shell.Application
$f  = $sh.Namespace('C:\path\to\folder').ParseName('photo.jpg')
$f.ExtendedProperty('InchesHandler.Image.Dimensions')
```

## Caveats (read these)

- **Inches are only as real as the stored DPI.** Many PNGs and screenshots have no
  DPI; Windows typically reports 96 dpi for those, so "inches" will be pixels ÷ 96.
  The values are useful for print-ready art that was saved with an intended DPI.
- **Protected keys.** On some Windows builds the per-extension `PropertyHandlers`
  keys are owned by TrustedInstaller and read-only to Administrators. The installer
  detects this and skips the extension with a warning rather than changing
  permissions. If you want it anyway, you'd need to take ownership of that one key
  in regedit (Permissions → Advanced → Owner) and re-run. *Caution: that alters
  system ACLs; uninstall restores the value, not the ACL.*
- **Precedence of FullDetails.** The installer patches the ProgID key and
  `SystemFileAssociations\<ext>`. If another app (Photoshop, etc.) owns the
  association with its own `FullDetails`, the row may not appear until that key is
  patched too.
- **Windows feature updates** sometimes reset handler registrations. Re-run
  uninstall + install if the row disappears.
- **Indexer.** Windows Search will load the wrapper too (it reads via the same
  registration). The new property is not indexed, so impact is a few extra
  `GetValue` calls per file.
- **32-bit apps** (e.g. file dialogs in 32-bit programs) use the WOW64 registry
  view and will not see the row. Explorer itself is 64-bit and is covered.
- I could not run this against a live Windows shell from where it was written:
  it compiles cleanly with the mingw-w64 toolchain, but the install path is
  untested. Try it on one extension first (`-Extensions .jpg`).

## Files

```
src\InchesHandler.cpp     wrapper handler + class factory + DLL exports
src\InchesHandler.def     exports
schema\InchesProps.propdesc
build.cmd
common.ps1 install.ps1 uninstall.ps1
```
