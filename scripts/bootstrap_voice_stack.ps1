param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Ensure-Dir([string]$Path) {
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Download-File([string]$Url, [string]$Destination) {
    Write-Host "Downloading $Url"
    Ensure-Dir ([System.IO.Path]::GetDirectoryName($Destination))
    Invoke-WebRequest -Headers @{ "User-Agent" = "vaxil-bootstrap" } -Uri $Url -OutFile $Destination
}

function Expand-Zip([string]$ArchivePath, [string]$Destination) {
    if (Test-Path $Destination) {
        Remove-Item -Recurse -Force $Destination
    }
    Ensure-Dir $Destination
    Expand-Archive -Path $ArchivePath -DestinationPath $Destination -Force
}

function Expand-TarGz([string]$ArchivePath, [string]$Destination) {
    if (Test-Path $Destination) {
        Remove-Item -Recurse -Force $Destination
    }
    Ensure-Dir $Destination
    tar -xzf $ArchivePath -C $Destination
}

function Expand-TarBz2([string]$ArchivePath, [string]$Destination) {
    if (Test-Path $Destination) {
        Remove-Item -Recurse -Force $Destination
    }
    Ensure-Dir $Destination
    function Convert-ToPosixTarPath([string]$Path) {
        $resolved = (Resolve-Path $Path).Path
        $resolved = $resolved -replace '\\', '/'
        if ($resolved -match '^([A-Za-z]):/(.*)$') {
            return '/' + $matches[1].ToLower() + '/' + $matches[2]
        }
        return $resolved
    }
    $gitBzip2 = "C:\Program Files\Git\usr\bin\bzip2.exe"
    $gitTar = "C:\Program Files\Git\usr\bin\tar.exe"
    if ((Test-Path $gitBzip2) -and (Test-Path $gitTar)) {
        $tempTar = Join-Path ([System.IO.Path]::GetDirectoryName($ArchivePath)) ([System.IO.Path]::GetFileNameWithoutExtension($ArchivePath))
        if (Test-Path $tempTar) {
            Remove-Item -Force $tempTar
        }
        $quotedBzip2 = '"' + $gitBzip2 + '"'
        $quotedArchive = '"' + $ArchivePath + '"'
        $quotedTempTar = '"' + $tempTar + '"'
        cmd /c "$quotedBzip2 -d -c $quotedArchive > $quotedTempTar" | Out-Null
        & $gitTar -xf (Convert-ToPosixTarPath $tempTar) -C (Convert-ToPosixTarPath $Destination)
        Remove-Item -Force $tempTar
    } else {
        throw "Cannot extract .tar.bz2 archives because Git tar/bzip2 was not found. Install Git for Windows or extract sherpa-onnx manually."
    }
}

$thirdPartyRoot = Join-Path $Root "third_party"
$downloadsRoot = Join-Path $thirdPartyRoot "downloads"
$modelsRoot = Join-Path $thirdPartyRoot "models"

Ensure-Dir $thirdPartyRoot
Ensure-Dir $downloadsRoot
Ensure-Dir $modelsRoot

$onnxVersion = "1.23.2"
$onnxArchive = Join-Path $downloadsRoot "onnxruntime-win-x64-$onnxVersion.zip"
$onnxExtractRoot = Join-Path $thirdPartyRoot "onnxruntime"
$onnxUrl = "https://github.com/microsoft/onnxruntime/releases/download/v$onnxVersion/onnxruntime-win-x64-$onnxVersion.zip"

$sileroModelPath = Join-Path $modelsRoot "silero_vad.onnx"
$sileroUrl = "https://github.com/snakers4/silero-vad/raw/master/src/silero_vad/data/silero_vad.onnx"

$sherpaVersion = "1.12.33"
$sherpaAsset = "sherpa-onnx-v$sherpaVersion-win-x64-shared-MD-Release-no-tts.tar.bz2"
$sherpaArchive = Join-Path $downloadsRoot $sherpaAsset
$sherpaExtractRoot = Join-Path $thirdPartyRoot "sherpa-onnx"
$sherpaUrl = "https://github.com/k2-fsa/sherpa-onnx/releases/download/v$sherpaVersion/$sherpaAsset"

$sherpaKwsAsset = "sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01.tar.bz2"
$sherpaKwsArchive = Join-Path $downloadsRoot $sherpaKwsAsset
$sherpaKwsExtractRoot = Join-Path $thirdPartyRoot "sherpa-kws-model"
$sherpaKwsUrl = "https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/$sherpaKwsAsset"

$sentencepieceArchive = Join-Path $downloadsRoot "sentencepiece-v0.2.1.zip"
$sentencepieceExtractRoot = Join-Path $thirdPartyRoot "sentencepiece"
$sentencepieceStageRoot = Join-Path $downloadsRoot "sentencepiece-stage"
$sentencepieceUrl = "https://github.com/google/sentencepiece/archive/refs/tags/v0.2.1.zip"

$rnnoiseArchive = Join-Path $downloadsRoot "rnnoise-main.zip"
$rnnoiseExtractRoot = Join-Path $thirdPartyRoot "rnnoise"
$rnnoiseUrl = "https://github.com/xiph/rnnoise/archive/refs/heads/main.zip"
$rnnoiseModelHash = "0a8755f8e2d834eff6a54714ecc7d75f9932e845df35f8b59bc52a7cfe6e8b37"
$rnnoiseModelArchive = Join-Path $downloadsRoot "rnnoise_data-$rnnoiseModelHash.tar.gz"
$rnnoiseModelUrl = "https://media.xiph.org/rnnoise/models/rnnoise_data-$rnnoiseModelHash.tar.gz"
$rnnoiseModelStageRoot = Join-Path $downloadsRoot "rnnoise-model-stage"

$speexArchive = Join-Path $downloadsRoot "speexdsp-master.zip"
$speexExtractRoot = Join-Path $thirdPartyRoot "speexdsp"
$speexStageRoot = Join-Path $downloadsRoot "speexdsp-stage"
$speexUrl = "https://github.com/xiph/speexdsp/archive/refs/heads/master.zip"

if (-not (Test-Path $onnxArchive)) {
    Download-File $onnxUrl $onnxArchive
}
Expand-Zip $onnxArchive $onnxExtractRoot

if (-not (Test-Path $sileroModelPath)) {
    Download-File $sileroUrl $sileroModelPath
}

if (-not (Test-Path $sherpaArchive)) {
    Download-File $sherpaUrl $sherpaArchive
}
Expand-TarBz2 $sherpaArchive $sherpaExtractRoot

if (-not (Test-Path $sherpaKwsArchive)) {
    Download-File $sherpaKwsUrl $sherpaKwsArchive
}
Expand-TarBz2 $sherpaKwsArchive $sherpaKwsExtractRoot

if (-not (Test-Path $sentencepieceArchive)) {
    Download-File $sentencepieceUrl $sentencepieceArchive
}
Expand-Zip $sentencepieceArchive $sentencepieceStageRoot
if (Test-Path (Join-Path $thirdPartyRoot "sentencepiece")) {
    Remove-Item -Recurse -Force (Join-Path $thirdPartyRoot "sentencepiece")
}
Move-Item (Join-Path $sentencepieceStageRoot "sentencepiece-0.2.1") $sentencepieceExtractRoot
Remove-Item -Recurse -Force $sentencepieceStageRoot

if (-not (Test-Path $rnnoiseArchive)) {
    Download-File $rnnoiseUrl $rnnoiseArchive
}
Expand-Zip $rnnoiseArchive $rnnoiseExtractRoot
if (-not (Test-Path $rnnoiseModelArchive)) {
    Download-File $rnnoiseModelUrl $rnnoiseModelArchive
}
Expand-TarGz $rnnoiseModelArchive $rnnoiseModelStageRoot
Copy-Item (Join-Path $rnnoiseModelStageRoot "src/rnnoise_data.c") (Join-Path $rnnoiseExtractRoot "rnnoise-main/src/rnnoise_data.c") -Force
Copy-Item (Join-Path $rnnoiseModelStageRoot "src/rnnoise_data.h") (Join-Path $rnnoiseExtractRoot "rnnoise-main/src/rnnoise_data.h") -Force
Remove-Item -Recurse -Force $rnnoiseModelStageRoot

if (-not (Test-Path $speexArchive)) {
    Download-File $speexUrl $speexArchive
}
Expand-Zip $speexArchive $speexStageRoot
if (Test-Path $speexExtractRoot) {
    Remove-Item -Recurse -Force $speexExtractRoot
}
Move-Item (Join-Path $speexStageRoot "speexdsp-master") $speexExtractRoot
Remove-Item -Recurse -Force $speexStageRoot

Write-Host ""
Write-Host "Voice stack bootstrap complete."
Write-Host "ONNX Runtime: $onnxExtractRoot"
Write-Host "Silero model: $sileroModelPath"
Write-Host "sherpa-onnx: $sherpaExtractRoot"
Write-Host "sherpa kws model: $sherpaKwsExtractRoot"
Write-Host "SentencePiece source: $sentencepieceExtractRoot"
Write-Host "RNNoise source: $rnnoiseExtractRoot"
Write-Host "SpeexDSP source: $speexExtractRoot"
