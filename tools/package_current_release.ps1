param(
    [string]$WorkspaceRoot = (Get-Location).Path
)

$ErrorActionPreference = 'Stop'

function Get-ProjectVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CMakeListsPath
    )

    $content = Get-Content $CMakeListsPath -Raw
    $match = [regex]::Match(
        $content,
        'project\([^\r\n)]*VERSION\s+([0-9]+(?:\.[0-9]+)+)\)',
        [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
    )
    if (-not $match.Success) {
        throw 'Unable to resolve project version from CMakeLists.txt.'
    }

    return $match.Groups[1].Value
}

function Copy-RequiredItem {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,

        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    if (-not (Test-Path $Source)) {
        throw "Required file not found: $Source"
    }

    Copy-Item -Force $Source $Destination
}

$workspacePath = (Resolve-Path $WorkspaceRoot).Path
$version = Get-ProjectVersion -CMakeListsPath (Join-Path $workspacePath 'CMakeLists.txt')
$tag = "v$version"
$releaseDir = Join-Path $workspacePath (Join-Path 'release' $tag)
$p4Dir = Join-Path $releaseDir 'p4_full_flash'
$c6Dir = Join-Path $releaseDir 'c6_full_flash'

$python = 'C:\Users\Elik\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe'
$esptool = 'C:\Espressif\frameworks\esp-idf-v5.5.4\components\esptool_py\esptool\esptool.py'

$p4BuildDir = Join-Path $workspacePath 'build'
$c6BuildDir = Join-Path $workspacePath 'coprocessor_c6\build'
$c6MergedPath = Join-Path $c6BuildDir 'merged-binary.bin'

New-Item -ItemType Directory -Force -Path $releaseDir, $p4Dir, $c6Dir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $p4Dir 'bootloader'), (Join-Path $p4Dir 'partition_table'), (Join-Path $c6Dir 'bootloader'), (Join-Path $c6Dir 'partition_table') | Out-Null

& $python $esptool --chip esp32c6 merge_bin -o $c6MergedPath --flash_mode dio --flash_freq 80m --flash_size 4MB 0x10000 (Join-Path $c6BuildDir 'network_adapter.bin') 0x8000 (Join-Path $c6BuildDir 'partition_table\partition-table.bin') 0xd000 (Join-Path $c6BuildDir 'ota_data_initial.bin') 0x0 (Join-Path $c6BuildDir 'bootloader\bootloader.bin')
if ($LASTEXITCODE -ne 0) {
    throw "esptool merge_bin failed with exit code $LASTEXITCODE"
}

Copy-RequiredItem -Source (Join-Path $p4BuildDir 'ESP32P4_Remote.bin') -Destination (Join-Path $releaseDir "JC4880P443C_I_W_Remote_${tag}_ota.bin")
Copy-RequiredItem -Source $c6MergedPath -Destination (Join-Path $releaseDir "JC4880P443C_I_W_Remote_C6_${tag}_merged.bin")

Copy-RequiredItem -Source (Join-Path $p4BuildDir 'ESP32P4_Remote.bin') -Destination (Join-Path $p4Dir 'ESP32P4_Remote.bin')
Copy-RequiredItem -Source (Join-Path $p4BuildDir 'flash_args') -Destination (Join-Path $p4Dir 'flash_args')
Copy-RequiredItem -Source (Join-Path $p4BuildDir 'ota_data_initial.bin') -Destination (Join-Path $p4Dir 'ota_data_initial.bin')
Copy-RequiredItem -Source (Join-Path $p4BuildDir 'storage.bin') -Destination (Join-Path $p4Dir 'storage.bin')
Copy-RequiredItem -Source (Join-Path $p4BuildDir 'bootloader\bootloader.bin') -Destination (Join-Path $p4Dir 'bootloader\bootloader.bin')
Copy-RequiredItem -Source (Join-Path $p4BuildDir 'partition_table\partition-table.bin') -Destination (Join-Path $p4Dir 'partition_table\partition-table.bin')

Copy-RequiredItem -Source (Join-Path $c6BuildDir 'network_adapter.bin') -Destination (Join-Path $c6Dir 'network_adapter.bin')
Copy-RequiredItem -Source (Join-Path $c6BuildDir 'flash_args') -Destination (Join-Path $c6Dir 'flash_args')
Copy-RequiredItem -Source (Join-Path $c6BuildDir 'ota_data_initial.bin') -Destination (Join-Path $c6Dir 'ota_data_initial.bin')
Copy-RequiredItem -Source (Join-Path $c6BuildDir 'bootloader\bootloader.bin') -Destination (Join-Path $c6Dir 'bootloader\bootloader.bin')
Copy-RequiredItem -Source (Join-Path $c6BuildDir 'partition_table\partition-table.bin') -Destination (Join-Path $c6Dir 'partition_table\partition-table.bin')

$p4Zip = Join-Path $releaseDir "JC4880P443C_I_W_Remote_${tag}_full_flash.zip"
$c6Zip = Join-Path $releaseDir "JC4880P443C_I_W_Remote_C6_${tag}_full_flash.zip"

Remove-Item -Force -ErrorAction SilentlyContinue $p4Zip, $c6Zip
Compress-Archive -Path (Join-Path $p4Dir '*') -DestinationPath $p4Zip -Force
Compress-Archive -Path (Join-Path $c6Dir '*') -DestinationPath $c6Zip -Force

Get-ChildItem $releaseDir | Select-Object Name, Length | Format-Table -AutoSize
