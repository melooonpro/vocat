param(
    [Parameter(Position = 0)]
    [ValidatePattern('^(?i:COM\d+)$')]
    [string] $Port,

    [Parameter(Position = 1)]
    [ValidateSet('Auto', 'N32R16', 'N16R8')]
    [string] $Model = 'Auto'
)

$ErrorActionPreference = 'Stop'
$toolsDirectory = $PSScriptRoot
$projectDirectory = Split-Path -Parent $toolsDirectory
$idfLauncher = Join-Path $toolsDirectory 'idf.cmd'
$idfProfile = 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'

if (-not (Test-Path -LiteralPath $idfLauncher)) {
    throw "ESP-IDF launcher was not found: $idfLauncher"
}
if (-not (Test-Path -LiteralPath $idfProfile)) {
    throw "ESP-IDF environment profile was not found: $idfProfile"
}
. $idfProfile
$idfPython = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'

if (-not $Port) {
    $ports = @(Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.Name -match '(?i)(USB|JTAG|UART|Espressif).*\(COM\d+\)' } |
        ForEach-Object {
            if ($_.Name -match '\((COM\d+)\)') { $Matches[1].ToUpperInvariant() }
        } |
        Sort-Object -Unique)
    if ($ports.Count -eq 0) {
        throw 'No ESP USB/JTAG/UART serial port was found. Pass a port explicitly, for example: fmu COM5'
    }
    if ($ports.Count -gt 1) {
        throw "Multiple candidate ports were found ($($ports -join ', ')). Pass the intended port explicitly."
    }
    $Port = $ports[0]
}

if ($Model -eq 'Auto') {
    Write-Host "Detecting flash on $Port..." -ForegroundColor Cyan
    $flashIdLines = @(& $idfPython -m esptool --chip esp32s3 --port $Port flash-id 2>&1)
    $flashIdText = $flashIdLines -join "`n"
    if ($LASTEXITCODE -ne 0) {
        $flashIdLines | ForEach-Object { Write-Host $_ }
        throw "Unable to read the flash ID from $Port."
    }
    if ($flashIdText -notmatch '(?i)Detected flash size:\s*(16|32)\s*MB') {
        $flashIdLines | ForEach-Object { Write-Host $_ }
        throw 'Only the supported 16 MB and 32 MB VoCat variants can be selected automatically.'
    }
    $Model = if ($Matches[1] -eq '16') { 'N16R8' } else { 'N32R16' }
}

$profileName = $Model.ToLowerInvariant()
$buildDirectory = Join-Path $projectDirectory "build-$profileName"
$sdkconfigPath = Join-Path $projectDirectory "sdkconfig.$profileName"
$defaultsPath = Join-Path $projectDirectory "sdkconfig.defaults.$profileName"

if (-not (Test-Path -LiteralPath $defaultsPath)) {
    throw "Model defaults were not found: $defaultsPath"
}

Write-Host "Selected VoCat model: $Model" -ForegroundColor Green
Write-Host "Build directory: $buildDirectory" -ForegroundColor DarkGray

$idfArguments = @(
    '-B', $buildDirectory,
    "-DSDKCONFIG=$sdkconfigPath",
    "-DSDKCONFIG_DEFAULTS=$defaultsPath",
    '-p', $Port,
    'flash'
)

Push-Location $projectDirectory
try {
    & $idfLauncher @idfArguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & $idfPython (Join-Path $toolsDirectory 'vocat-monitor.py') $Port
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
