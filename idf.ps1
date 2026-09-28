param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $IdfArguments
)

$ErrorActionPreference = 'Stop'

$idfProfile = 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
$idfPython = 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe'
$idfEntry = 'D:\esp-idf\v6.1\esp-idf\tools\idf.py'
$env:PYTHONUTF8 = '1'

if (-not (Test-Path -LiteralPath $idfProfile)) {
    throw "ESP-IDF 6.1 environment profile was not found: $idfProfile"
}

if (-not (Test-Path -LiteralPath $idfPython) -or -not (Test-Path -LiteralPath $idfEntry)) {
    throw 'ESP-IDF 6.1 Python environment or idf.py was not found.'
}

# This script is normally launched by idf.cmd with a one-process execution
# policy bypass. All remaining arguments are forwarded directly to idf.py.
. $idfProfile
& $idfPython $idfEntry @IdfArguments
exit $LASTEXITCODE
