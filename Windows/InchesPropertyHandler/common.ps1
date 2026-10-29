# Shared definitions for install.ps1 / uninstall.ps1 (dot-sourced).

$Script:ClsidPrefix  = 'E35046DE-611F-44CA-BC68-2E042D1CAE'   # + 2 hex digits (slot)
$Script:StateKey     = 'HKLM:\SOFTWARE\InchesPropertyHandler'
$Script:PhRoot       = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers'
$Script:ClassesRoot  = 'HKLM:\SOFTWARE\Classes'
$Script:OurProps     = 'InchesHandler.Image.Dimensions'
$Script:DllName      = 'InchesPropertyHandler.dll'
$Script:SchemaName   = 'InchesProps.propdesc'

if (-not ('InchesNative.Api' -as [type])) {
    Add-Type -Namespace InchesNative -Name Api -MemberDefinition @'
[DllImport("propsys.dll", CharSet = CharSet.Unicode)]
public static extern int PSRegisterPropertySchema(string path);
[DllImport("propsys.dll", CharSet = CharSet.Unicode)]
public static extern int PSUnregisterPropertySchema(string path);
[DllImport("propsys.dll")]
public static extern int PSRefreshPropertySchema();
[DllImport("shell32.dll")]
public static extern void SHChangeNotify(int wEventId, uint uFlags, IntPtr dwItem1, IntPtr dwItem2);
'@
}

function Test-OurClsid([string]$value) {
    return $value -and ($value.Trim('{}').ToUpper().StartsWith($Script:ClsidPrefix))
}

function Get-DefaultValue([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    return (Get-Item -LiteralPath $path).GetValue('')
}

function Get-RegValue([string]$path, [string]$name) {
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    return (Get-Item -LiteralPath $path).GetValue($name)
}

function Send-AssocChanged {
    # SHCNE_ASSOCCHANGED, SHCNF_IDLIST
    [InchesNative.Api]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
}

function Restart-Explorer {
    Write-Host 'Restarting Explorer...'
    Get-Process explorer -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 3   # Explorer normally restarts itself (AutoRestartShell)
    if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }
}
