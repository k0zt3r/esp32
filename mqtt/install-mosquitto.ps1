# Extract the official distribution into the project; no Windows service is installed.
$ErrorActionPreference = 'Stop'
$mqttVersion = '2.1.2'
$mqttArchive = Join-Path $PSScriptRoot "downloads/mosquitto-$mqttVersion.exe"
$mqttRuntime = Join-Path $PSScriptRoot 'runtime'
$mqtt7zip = Get-Command 7z.exe -ErrorAction SilentlyContinue
$mqtt7zipPath = if ($mqtt7zip) { $mqtt7zip.Source } else { Join-Path $env:ProgramFiles '7-Zip/7z.exe' }
if (-not (Test-Path -LiteralPath $mqtt7zipPath)) { throw 'Install 7-Zip first.' }
if (Test-Path -LiteralPath (Join-Path $mqttRuntime 'mosquitto.exe')) { throw 'Mosquitto is already present; existing installation was left untouched.' }
New-Item -ItemType Directory -Force (Split-Path $mqttArchive) | Out-Null
Invoke-WebRequest -Uri "https://mosquitto.org/files/binary/win64/mosquitto-$mqttVersion-install-windows-x64.exe" -OutFile $mqttArchive
& $mqtt7zipPath x $mqttArchive "-o$mqttRuntime" -y
if ($LASTEXITCODE -ne 0) { throw 'Extraction failed.' }
