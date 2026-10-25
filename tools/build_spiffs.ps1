<#
.SYNOPSIS
    建立並選擇性燒錄 LILYGO T-Display-S3 的 SPIFFS 映像（spiffs.bin）。

.DESCRIPTION
    從 partitions.csv 讀取 spiffs 分割區的 offset 與 size，
    使用 Arduino ESP32 工具鏈內建的 mkspiffs 打包 data 目錄；
    加上 -Flash 時再以 esptool 寫入開發板。

.PARAMETER DataDir
    要打包的資料目錄，預設為 TDisplayS3_Menu\data。

.PARAMETER Out
    輸出檔名，預設為專案根目錄的 spiffs.bin。

.PARAMETER Flash
    打包後直接燒錄到裝置。

.PARAMETER Port
    序列埠，預設 COM5。

.PARAMETER Baud
    燒錄鮑率，預設 921600。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\build_spiffs.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\build_spiffs.ps1 -Flash -Port COM5
#>
[CmdletBinding()]
param(
    [string]$DataDir,
    [string]$Out = "spiffs.bin",
    [switch]$Flash,
    [string]$Port = "COM5",
    [int]$Baud = 921600
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$partitionFile = Join-Path $root "TDisplayS3_Menu\partitions.csv"

if (-not $DataDir) { $DataDir = Join-Path $root "TDisplayS3_Menu\data" }

if (-not (Test-Path $DataDir)) { throw "找不到資料目錄：$DataDir" }
if (-not (Test-Path $partitionFile)) { throw "找不到分割表：$partitionFile" }

# ---------------------------------------------------------------- 讀取分割區
$line = Get-Content $partitionFile |
    Where-Object { $_ -match '^\s*spiffs\s*,' } |
    Select-Object -First 1
if (-not $line) { throw "partitions.csv 找不到 spiffs 分割區" }

$fields = $line -split ',' | ForEach-Object { $_.Trim() }
$offset = $fields[3]
$size = $fields[4]
if (-not $offset -or -not $size) { throw "無法解析 spiffs 分割區：$line" }

# ---------------------------------------------------------------- 尋找工具
$espTools = Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32\tools"
if (-not (Test-Path $espTools)) {
    throw "找不到 ESP32 工具鏈：$espTools（請先安裝 ESP32 開發板支援）"
}

function Get-VersionOrZero([string]$text) {
    $v = [version]"0.0.0"
    if ([version]::TryParse($text, [ref]$v)) { return $v }
    return [version]"0.0.0"
}

function Find-Tool([string]$folder, [string]$exe) {
    $base = Join-Path $espTools $folder
    if (-not (Test-Path $base)) { return $null }
    $hits = @(Get-ChildItem -Path $base -Recurse -File -Filter $exe -ErrorAction SilentlyContinue)
    if ($hits.Count -eq 0) { return $null }
    return ($hits |
        Sort-Object `
            @{ Expression = { Get-VersionOrZero $_.Directory.Name } },
            @{ Expression = { $_.LastWriteTime } } -Descending |
        Select-Object -First 1).FullName
}

$mkspiffs = Find-Tool "mkspiffs" "mkspiffs.exe"
$esptool = Find-Tool "esptool_py" "esptool.exe"

if (-not $mkspiffs) { throw "找不到 mkspiffs.exe，請確認 ESP32 開發板支援已安裝" }
if ($Flash -and -not $esptool) { throw "找不到 esptool.exe，無法燒錄" }

# ---------------------------------------------------------------- 建立映像
$image = $Out
if (-not [System.IO.Path]::IsPathRooted($image)) { $image = Join-Path $root $image }

Write-Host "資料目錄 : $DataDir"
Write-Host "分割區   : offset=$offset size=$size"
Write-Host "mkspiffs : $mkspiffs"
Write-Host "輸出映像 : $image"
Write-Host ""

if (Test-Path $image) { Remove-Item $image -Force }

& $mkspiffs -c $DataDir -b 4096 -p 256 -s $size $image
if ($LASTEXITCODE -ne 0) { throw "mkspiffs 失敗（exit $LASTEXITCODE）" }

$imageInfo = Get-Item $image
Write-Host ("映像建立完成：{0}（{1:N0} bytes）" -f $imageInfo.Name, $imageInfo.Length)

if ($size.StartsWith("0x") -or $size.StartsWith("0X")) {
    $expected = [Convert]::ToInt64($size.Substring(2), 16)
} else {
    $expected = [Convert]::ToInt64($size)
}
if ($imageInfo.Length -ne $expected) {
    Write-Warning ("映像大小 {0:N0} 與分割區 {1:N0} 不符，裝置會拒絕此映像。" -f $imageInfo.Length, $expected)
}

# ---------------------------------------------------------------- 燒錄
if ($Flash) {
    Write-Host ""
    Write-Host "燒錄至 $Port（$Baud baud）..."
    & $esptool --chip esp32s3 --port $Port --baud $Baud write-flash $offset $image
    if ($LASTEXITCODE -ne 0) { throw "esptool 燒錄失敗（exit $LASTEXITCODE）" }
    Write-Host "SPIFFS 映像已寫入，請重新啟動裝置。"
} else {
    Write-Host ""
    Write-Host "直接燒錄："
    Write-Host ("  powershell -ExecutionPolicy Bypass -File tools\build_spiffs.ps1 -Flash -Port {0}" -f $Port)
    Write-Host "或於 Web 控制台「OTA 更新 → SPIFFS 映像更新」上傳此檔案。"
}
