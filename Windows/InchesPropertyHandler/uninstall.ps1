#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Reverses install.ps1 using the backups it recorded.
.PARAMETER RestartExplorer
  Restart Explorer so the DLL is released and the install folder can be deleted.
#>
param([switch]$RestartExplorer)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

if (-not (Test-Path $StateKey)) { Write-Host 'Not installed.'; return }
$installDir = Get-RegValue $StateKey 'InstallDir'

# --- Restore property handlers and remove wrapper CLSIDs --------------------
if (Test-Path "$StateKey\Extensions") {
    foreach ($k in Get-ChildItem "$StateKey\Extensions") {
        $ext      = $k.PSChildName
        $previous = $k.GetValue('PreviousHandler')
        $clsid    = $k.GetValue('Clsid')
        $phKey    = "$PhRoot\$ext"

        $current = Get-DefaultValue $phKey
        if ($current -eq $clsid) {
            try { Set-Item -Path $phKey -Value $previous; Write-Host "$ext restored to $previous" }
            catch { Write-Warning "$ext : could not restore $phKey to $previous ($($_.Exception.Message))" }
        } else {
            Write-Warning "$ext : handler is now '$current' (changed since install); leaving it alone."
        }
        $clsidKey = "$ClassesRoot\CLSID\$clsid"
        if (Test-Path $clsidKey) { Remove-Item -Path $clsidKey -Recurse -Force }
    }
}

# --- Restore FullDetails ----------------------------------------------------
if (Test-Path "$StateKey\FullDetails") {
    foreach ($k in Get-ChildItem "$StateKey\FullDetails") {
        $path     = $k.GetValue('Path')
        $original = $k.GetValue('Original')
        if (-not (Test-Path $path)) { continue }
        $current = Get-RegValue $path 'FullDetails'
        if ($current -and -not $current.Contains('InchesHandler.Image.')) {
            Write-Warning "FullDetails at $path changed since install; leaving it alone."
            continue
        }
        if ($null -ne $original) {
            Set-ItemProperty -Path $path -Name FullDetails -Value $original
        } else {
            Remove-ItemProperty -Path $path -Name FullDetails -ErrorAction SilentlyContinue
        }
    }
}

# --- Unregister schema ------------------------------------------------------
if ($installDir) {
    $schema = Join-Path $installDir $SchemaName
    if (Test-Path $schema) {
        $hr = [InchesNative.Api]::PSUnregisterPropertySchema($schema)
        if ($hr -lt 0) { Write-Warning ('PSUnregisterPropertySchema: 0x{0:X8}' -f $hr) }
    }
}

Remove-Item -Path $StateKey -Recurse -Force
Send-AssocChanged

if ($RestartExplorer) { Restart-Explorer }

if ($installDir -and (Test-Path $installDir)) {
    try {
        Remove-Item -Path $installDir -Recurse -Force
        Write-Host "Removed $installDir"
    } catch {
        Write-Warning "Could not delete $installDir (DLL still loaded). Re-run with -RestartExplorer, or delete it after a reboot."
    }
}
Write-Host 'Uninstalled.'
