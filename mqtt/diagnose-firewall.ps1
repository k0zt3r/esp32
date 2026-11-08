$ErrorActionPreference = 'Stop'
$mqttRuntime = (Resolve-Path (Join-Path $PSScriptRoot 'runtime/mosquitto.exe')).Path
$mqttRules = Get-NetFirewallApplicationFilter -Program $mqttRuntime -ErrorAction SilentlyContinue | Get-NetFirewallRule
$mqttReport = @()
foreach ($mqttRule in $mqttRules) {
    $mqttReport += [PSCustomObject]@{Name=$mqttRule.Name;DisplayName=$mqttRule.DisplayName;Enabled=[string]$mqttRule.Enabled;Action=[string]$mqttRule.Action;Direction=[string]$mqttRule.Direction;Profile=[string]$mqttRule.Profile;Program=$mqttRuntime}
}
$mqttReport | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $PSScriptRoot 'firewall-diagnostic.json')
Get-NetTCPConnection -LocalPort 1884 -State Listen | Select-Object LocalAddress,LocalPort,OwningProcess | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $PSScriptRoot 'listener-diagnostic.json')
