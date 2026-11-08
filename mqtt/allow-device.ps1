param([string]$BrokerIP = '192.168.1.110')
$ErrorActionPreference = 'Stop'
$mqttRuntime = Join-Path $PSScriptRoot 'runtime/mosquitto.exe'
$mqttRule = 'ESP32 MQTT Lab'
$mqttIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$mqttPrincipal = New-Object Security.Principal.WindowsPrincipal($mqttIdentity)
if (-not $mqttPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script as administrator to allow the board to connect.'
}
$mqttBlocks = @(Get-NetFirewallApplicationFilter -Program $mqttRuntime -ErrorAction SilentlyContinue | Get-NetFirewallRule | Where-Object { $_.Direction -eq 'Inbound' -and $_.Action -eq 'Block' -and $_.Enabled -eq 'True' })
if ($mqttBlocks.Count) {
    $mqttBlocks | Select-Object Name,DisplayName,Enabled,Action,Direction,Profile | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $PSScriptRoot 'firewall-disabled-blocks.json')
    $mqttBlocks | Disable-NetFirewallRule | Out-Null
}
if (-not (Get-NetFirewallRule -DisplayName $mqttRule -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -DisplayName $mqttRule -Direction Inbound -Action Allow -Protocol TCP `
        -LocalPort 1884 -LocalAddress $BrokerIP -RemoteAddress LocalSubnet -Profile Any `
        -Program $mqttRuntime | Out-Null
}
'MQTT firewall rule ready.' | Set-Content (Join-Path $PSScriptRoot 'firewall-ready.txt')
