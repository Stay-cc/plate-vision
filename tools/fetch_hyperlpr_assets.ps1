param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"

$assetRoot = Join-Path $Root "third_party\hyperlpr"
$modelRoot = Join-Path $assetRoot "models"
$imageRoot = Join-Path $assetRoot "images"
$modelZip = Join-Path $assetRoot "20230229.zip"

New-Item -ItemType Directory -Force -Path $modelRoot | Out-Null
New-Item -ItemType Directory -Force -Path $imageRoot | Out-Null

if (-not (Test-Path (Join-Path $modelRoot "20230229\onnx\y5fu_640x_sim.onnx"))) {
    curl.exe -L --fail `
        --output $modelZip `
        "http://hyperlpr.tunm.top/raw/20230229.zip"
    tar.exe -xf $modelZip -C $modelRoot
    Remove-Item -LiteralPath $modelZip -Force
}

$sampleImage = Join-Path $imageRoot "test_img.jpg"
if (-not (Test-Path $sampleImage)) {
    curl.exe -L --fail `
        --output $sampleImage `
        "https://github.com/szad670401/HyperLPR/raw/master/resource/images/test_img.jpg"
}

$cropRoot = Join-Path $imageRoot "rec_crop"
New-Item -ItemType Directory -Force -Path $cropRoot | Out-Null
$crops = @(
    @{
        File = "_0_津B6H920.jpg"
        Api = "https://api.github.com/repos/szad670401/HyperLPR/contents/resource/images/rec_crop/_0_%E6%B4%A5B6H920.jpg?ref=master"
    },
    @{
        File = "_1_皖KD01833.jpg"
        Api = "https://api.github.com/repos/szad670401/HyperLPR/contents/resource/images/rec_crop/_1_%E7%9A%96KD01833.jpg?ref=master"
    },
    @{
        File = "_6_蒙B023H6.jpg"
        Api = "https://api.github.com/repos/szad670401/HyperLPR/contents/resource/images/rec_crop/_6_%E8%92%99B023H6.jpg?ref=master"
    },
    @{
        File = "_8_冀D5L690.jpg"
        Api = "https://api.github.com/repos/szad670401/HyperLPR/contents/resource/images/rec_crop/_8_%E5%86%80D5L690.jpg?ref=master"
    }
)

foreach ($crop in $crops) {
    $target = Join-Path $cropRoot $crop.File
    if (-not (Test-Path $target)) {
        $response = Invoke-RestMethod `
            -Headers @{ "User-Agent" = "PlateVision" } `
            -Uri $crop.Api
        $bytes = [Convert]::FromBase64String(
            ($response.content -replace "\s", ""))
        [IO.File]::WriteAllBytes($target, $bytes)
    }
}

$yellowSource = Join-Path $imageRoot "1.jpg"
$yellowCrop = Join-Path $imageRoot "yellow_crop.png"
if (-not (Test-Path $yellowCrop)) {
    if (-not (Test-Path $yellowSource)) {
        $response = Invoke-RestMethod `
            -Headers @{ "User-Agent" = "PlateVision" } `
            -Uri "https://api.github.com/repos/szad670401/HyperLPR/contents/resource/images/1.jpg?ref=master"
        [IO.File]::WriteAllBytes(
            $yellowSource,
            [Convert]::FromBase64String(
                ($response.content -replace "\s", "")))
    }
    Add-Type -AssemblyName System.Drawing
    $image = [Drawing.Image]::FromFile($yellowSource)
    $bitmap = New-Object Drawing.Bitmap 36, 24
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.DrawImage(
        $image,
        [Drawing.Rectangle]::new(0, 0, 36, 24),
        [Drawing.Rectangle]::new(147, 210, 36, 24),
        [Drawing.GraphicsUnit]::Pixel)
    $graphics.Dispose()
    $image.Dispose()
    $bitmap.Save($yellowCrop, [Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}

Write-Host "HyperLPR assets are ready at $assetRoot"

