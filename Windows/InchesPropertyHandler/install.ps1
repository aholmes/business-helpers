#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Installs the inches property handler for the given image extensions.

.DESCRIPTION
  For each extension:
    1. Reads the currently registered property handler CLSID (e.g. Microsoft's).
    2. Registers a wrapper CLSID that points at our DLL and records the original
       as its WrappedHandler.
    3. Points the extension's PropertyHandlers entry at the wrapper.
    4. Adds our property to the FullDetails list right after "Dimensions"
       so it appears in Properties > Details.
  Everything it changes is backed up under HKLM\SOFTWARE\InchesPropertyHandler
  so uninstall.ps1 can restore it exactly.

.EXAMPLE
  .\install.ps1
  .\install.ps1 -Extensions .jpg,.png -RestartExplorer
#>
param(
    [string[]]$Extensions = @('.jpg', '.jpeg', '.png', '.tif', '.tiff', '.bmp', '.gif'),
    [string]$InstallDir   = (Join-Path $env:ProgramFiles 'InchesPropertyHandler'),
    [string]$DllPath      = (Join-Path $PSScriptRoot 'build\InchesPropertyHandler.dll'),
    [switch]$RestartExplorer
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

if (Test-Path $StateKey) {
    throw "Already installed (found $StateKey). Run uninstall.ps1 first."
}
if (-not (Test-Path $DllPath)) { throw "DLL not found: $DllPath (run build.cmd first)" }
$schemaSrc = Join-Path $PSScriptRoot "schema\$SchemaName"
if (-not (Test-Path $schemaSrc)) { throw "Schema not found: $schemaSrc" }

# --- 1. Copy files ----------------------------------------------------------
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
$dllDest    = Join-Path $InstallDir $DllName
$schemaDest = Join-Path $InstallDir $SchemaName
Copy-Item $DllPath   $dllDest    -Force
Copy-Item $schemaSrc $schemaDest -Force
Unblock-File $dllDest, $schemaDest   # clear Mark-of-the-Web from the downloaded zip

New-Item -Path $StateKey -Force | Out-Null
Set-ItemProperty -Path $StateKey -Name InstallDir -Value $InstallDir
New-Item -Path "$StateKey\Extensions"  -Force | Out-Null
New-Item -Path "$StateKey\FullDetails" -Force | Out-Null

# --- 2. Register the property schema ----------------------------------------
$hr = [InchesNative.Api]::PSRegisterPropertySchema($schemaDest)
if ($hr -lt 0) { throw ('PSRegisterPropertySchema failed: 0x{0:X8}' -f $hr) }
Write-Host "Registered property schema: $schemaDest"

# --- FullDetails helpers ----------------------------------------------------
$fdBackupIndex = 0
function Backup-FullDetails([string]$keyPath, $original) {
    $b = "$StateKey\FullDetails\$($script:fdBackupIndex)"
    $script:fdBackupIndex++
    New-Item -Path $b -Force | Out-Null
    Set-ItemProperty -Path $b -Name Path -Value $keyPath
    if ($null -ne $original) {
        Set-ItemProperty -Path $b -Name Original -Value $original
    }   # no Original value => the FullDetails value did not exist; uninstall deletes it
}

function Add-OurProps([string]$list) {
    if ($list.Contains('InchesHandler.Image.')) { return $list }
    # Insert directly after the first anchor found (normally Dimensions).
    $anchors = 'System.Image.Dimensions', 'System.Image.VerticalSize',
               'System.Image.VerticalResolution'
    foreach ($a in $anchors) {
        $i = $list.IndexOf($a)
        if ($i -ge 0) {
            $end = $i + $a.Length
            return $list.Substring(0, $end) + ';' + $OurProps + $list.Substring($end)
        }
    }
    return $list.TrimEnd(';') + ';System.PropGroup.Image;' + $OurProps
}

$fallbackFullDetails = 'prop:System.PropGroup.Image;System.Image.Dimensions;System.Image.HorizontalSize;' +
                       'System.Image.VerticalSize;System.Image.HorizontalResolution;' +
                       'System.Image.VerticalResolution;System.Image.BitDepth;' +
                       'System.PropGroup.FileSystem;System.ItemNameDisplay;System.ItemTypeText;' +
                       'System.ItemFolderPathDisplay;System.DateCreated;System.DateModified;System.Size'

# --- 3. Per-extension registration ------------------------------------------
$slot = 0
$installed = @()
foreach ($ext in $Extensions) {
    $ext = $ext.ToLower()
    if (-not $ext.StartsWith('.')) { $ext = ".$ext" }
    $slot++
    if ($slot -gt 255) { Write-Warning 'More than 255 extensions; stopping.'; break }

    $phKey = "$PhRoot\$ext"
    $previous = Get-DefaultValue $phKey
    if (-not $previous) {
        Write-Warning "$ext has no existing property handler to wrap; skipping."
        continue
    }
    if (Test-OurClsid $previous) {
        Write-Warning "$ext already points at an InchesHandler CLSID; skipping."
        continue
    }

    $clsid = '{' + $ClsidPrefix + ('{0:X2}' -f $slot) + '}'
    $clsidKey = "$ClassesRoot\CLSID\$clsid"

    # 3a. Wrapper CLSID
    New-Item -Path $clsidKey -Force | Out-Null
    Set-Item -Path $clsidKey -Value "Inches Property Handler ($ext)"
    Set-ItemProperty -Path $clsidKey -Name WrappedHandler -Value $previous
    New-Item -Path "$clsidKey\InprocServer32" -Force | Out-Null
    Set-Item -Path "$clsidKey\InprocServer32" -Value $dllDest
    Set-ItemProperty -Path "$clsidKey\InprocServer32" -Name ThreadingModel -Value 'Both'

    # 3b. Point the extension at the wrapper (back up first)
    $bk = "$StateKey\Extensions\$ext"
    New-Item -Path $bk -Force | Out-Null
    Set-ItemProperty -Path $bk -Name PreviousHandler -Value $previous
    Set-ItemProperty -Path $bk -Name Clsid -Value $clsid
    try {
        Set-Item -Path $phKey -Value $clsid
    } catch {
        Write-Warning ("$ext : could not write $phKey ($($_.Exception.Message)). If this is " +
                       "access denied, the key is protected on your Windows build; see README " +
                       "'Protected keys'. Skipping $ext.")
        Remove-Item -Path $bk -Recurse -Force
        Remove-Item -Path $clsidKey -Recurse -Force
        continue
    }

    # 3c. FullDetails (which properties the Details tab lists, and in what order)
    $progId = Get-DefaultValue "$ClassesRoot\$ext"
    $candidates = @()
    if ($progId) { $candidates += "$ClassesRoot\$progId" }
    $candidates += "$ClassesRoot\SystemFileAssociations\$ext"

    $patchedAny = $false
    foreach ($c in $candidates) {
        $fd = Get-RegValue $c 'FullDetails'
        if ($fd) {
            Backup-FullDetails $c $fd
            Set-ItemProperty -Path $c -Name FullDetails -Value (Add-OurProps $fd)
            $patchedAny = $true
        }
    }
    if (-not $patchedAny) {
        $perceived = Get-RegValue "$ClassesRoot\$ext" 'PerceivedType'
        $base = $null
        if ($perceived) { $base = Get-RegValue "$ClassesRoot\SystemFileAssociations\$perceived" 'FullDetails' }
        if (-not $base) { $base = $fallbackFullDetails }
        $target = "$ClassesRoot\SystemFileAssociations\$ext"
        New-Item -Path $target -Force | Out-Null
        Backup-FullDetails $target $null
        Set-ItemProperty -Path $target -Name FullDetails -Value (Add-OurProps $base)
    }

    $installed += $ext
    Write-Host "$ext -> $clsid (wrapping $previous)"
}

Send-AssocChanged

if ($installed.Count -eq 0) {
    Write-Warning 'No extensions were installed. Run uninstall.ps1 to clean up.'
} else {
    Write-Host "Installed for: $($installed -join ', ')"
}

if ($RestartExplorer) {
    Restart-Explorer
} else {
    Write-Host 'Restart Explorer (or sign out/in) for the Details tab to pick up the change.'
}
