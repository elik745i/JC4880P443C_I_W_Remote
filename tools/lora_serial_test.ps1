param(
    [string]$Port = 'COM3',
    [int]$Baud = 115200,
    [string[]]$Commands = @(),
    [int]$AfterMs = 1500
)
$sp = New-Object System.IO.Ports.SerialPort $Port,$Baud,'None',8,'One'
$sp.NewLine = "`r`n"
$sp.ReadTimeout = 500
$sp.WriteTimeout = 2000
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.Open()
Start-Sleep -Milliseconds 500
$sp.DiscardInBuffer()
foreach ($cmd in $Commands) {
    Write-Host ""
    Write-Host ">>> $cmd"
    $sp.WriteLine($cmd)
    $deadline = (Get-Date).AddMilliseconds($AfterMs)
    while ((Get-Date) -lt $deadline) {
        if ($sp.BytesToRead -gt 0) {
            $chunk = $sp.ReadExisting()
            Write-Host -NoNewline $chunk
        } else {
            Start-Sleep -Milliseconds 40
        }
    }
}
$sp.Close()
