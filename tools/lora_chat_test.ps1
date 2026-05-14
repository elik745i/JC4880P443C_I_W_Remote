param([string]$Port = 'COM3')

$ErrorActionPreference = 'Continue'

function Open-Serial([string]$portName) {
  $sp = New-Object System.IO.Ports.SerialPort $portName, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
  $sp.ReadTimeout = 500; $sp.WriteTimeout = 500; $sp.NewLine = "`r`n"
  $sp.DtrEnable = $false; $sp.RtsEnable = $false
  $sp.Open(); Start-Sleep -Milliseconds 200
  $sp.RtsEnable = $true; Start-Sleep -Milliseconds 150; $sp.RtsEnable = $false
  Start-Sleep -Milliseconds 6000
  return $sp
}

function Drain([System.IO.Ports.SerialPort]$sp, [int]$ms = 500) {
  $deadline = [DateTime]::UtcNow.AddMilliseconds($ms)
  while ([DateTime]::UtcNow -lt $deadline) {
    try { if ($sp.BytesToRead -gt 0) { $chunk = $sp.ReadExisting(); if ($chunk) { Write-Host -NoNewline $chunk } } else { Start-Sleep -Milliseconds 25 } } catch { }
  }
}

function Send-Cmd([System.IO.Ports.SerialPort]$sp, [string]$cmd, [int]$drainMs = 1500) {
  Write-Host ""; Write-Host "===> $cmd"; $sp.WriteLine($cmd); Drain $sp $drainMs
}

function Run-Phase {
  param([System.IO.Ports.SerialPort]$sp, [string]$label, [int]$tx, [int]$rx)
  Write-Host "`n############# PHASE $label tx=$tx rx=$rx #############"
  Send-Cmd $sp 'lora.close' 2000
  Send-Cmd $sp 'lora.module.set t22s' 1500
  Send-Cmd $sp ("lora.pinmap.set tx={0} rx={1} m0=51 m1=29 aux=33" -f $tx, $rx) 1500
  Send-Cmd $sp 'lora.pinmap.show' 1200
  Send-Cmd $sp 'lora.apply' 4500
  Send-Cmd $sp 'lora.open' 5500
  Send-Cmd $sp 'lora.status' 1500
  Send-Cmd $sp 'lora.selftest.start' 5500
  Send-Cmd $sp 'lora.status' 1500
  Send-Cmd $sp ("lora.send.common HELLO-{0}-{1}{2}" -f $label, $tx, $rx) 5000
  Send-Cmd $sp 'lora.status' 1500
}

$sp = Open-Serial $Port
try {
  Drain $sp 1500
  Run-Phase $sp 'A' 31 30
  Run-Phase $sp 'B' 30 31
}
finally { $sp.Close() }
