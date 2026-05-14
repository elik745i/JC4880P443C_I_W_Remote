param(
  [string]$Port = 'COM3'
)

$ErrorActionPreference = 'Continue'

function Open-Serial([string]$portName) {
  $sp = New-Object System.IO.Ports.SerialPort $portName, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
  $sp.ReadTimeout = 500
  $sp.WriteTimeout = 500
  $sp.NewLine = "`r`n"
  $sp.DtrEnable = $false
  $sp.RtsEnable = $false
  $sp.Open()
  Start-Sleep -Milliseconds 200
  # Pulse RTS only for clean boot (avoids download mode)
  $sp.RtsEnable = $true
  Start-Sleep -Milliseconds 150
  $sp.RtsEnable = $false
  Start-Sleep -Milliseconds 6000
  return $sp
}

function Drain([System.IO.Ports.SerialPort]$sp, [int]$ms = 500) {
  $deadline = [DateTime]::UtcNow.AddMilliseconds($ms)
  while ([DateTime]::UtcNow -lt $deadline) {
    try {
      if ($sp.BytesToRead -gt 0) {
        $chunk = $sp.ReadExisting()
        if ($chunk) { Write-Host -NoNewline $chunk }
      } else { Start-Sleep -Milliseconds 25 }
    } catch { }
  }
}

function Send-Cmd([System.IO.Ports.SerialPort]$sp, [string]$cmd, [int]$drainMs = 1500) {
  Write-Host ""
  Write-Host "===> $cmd"
  $sp.WriteLine($cmd)
  Drain $sp $drainMs
}

$sp = Open-Serial $Port
try {
  Drain $sp 1500
  Send-Cmd $sp 'lora.close' 2500
  Send-Cmd $sp 'lora.pinmap.show' 1200
  Send-Cmd $sp 'lora.module.set m22s' 1500
  Send-Cmd $sp 'lora.pinmap.show' 1200
  Send-Cmd $sp 'lora.apply' 4500
  Send-Cmd $sp 'lora.open' 6000
  Send-Cmd $sp 'lora.status' 1500
  Send-Cmd $sp 'lora.selftest.start' 5000
  Send-Cmd $sp 'lora.status' 1500
  Send-Cmd $sp 'lora.send.common SPI-radio-hello' 4000
  Send-Cmd $sp 'lora.status' 1500
}
finally {
  $sp.Close()
}
