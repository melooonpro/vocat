param(
    [string] $Output = (Join-Path $PSScriptRoot 'main\clock_digits.bin')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$cellWidth = 50
$cellHeight = 80
$bytesPerRow = [int]($cellWidth / 2)
$font = New-Object System.Drawing.Font(
    'Arial', 150, [System.Drawing.FontStyle]::Bold,
    [System.Drawing.GraphicsUnit]::Pixel
)
$outputBytes = New-Object byte[] (10 * $cellHeight * $bytesPerRow)
$sources = New-Object 'System.Drawing.Bitmap[]' 10
$bounds = New-Object 'object[]' 10
$maxInkWidth = 0
$maxInkHeight = 0

for ($digit = 0; $digit -lt 10; $digit++) {
    $source = New-Object System.Drawing.Bitmap(180, 200)
    $graphics = [System.Drawing.Graphics]::FromImage($source)
    $graphics.Clear([System.Drawing.Color]::Black)
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    # Keep generous top padding. Cropping begins only after the complete glyph
    # has been rasterized, so caps and curved tops cannot be clipped.
    $graphics.DrawString([string]$digit, $font, [System.Drawing.Brushes]::White, 0, 4)

    $minX = $source.Width
    $minY = $source.Height
    $maxX = -1
    $maxY = -1
    for ($y = 0; $y -lt $source.Height; $y++) {
        for ($x = 0; $x -lt $source.Width; $x++) {
            if ($source.GetPixel($x, $y).R -gt 2) {
                $minX = [Math]::Min($minX, $x)
                $minY = [Math]::Min($minY, $y)
                $maxX = [Math]::Max($maxX, $x)
                $maxY = [Math]::Max($maxY, $y)
            }
        }
    }

    $graphics.Dispose()
    $inkWidth = [int]($maxX - $minX + 1)
    $inkHeight = [int]($maxY - $minY + 1)
    $sources[$digit] = $source
    $bounds[$digit] = [PSCustomObject]@{
        X = $minX; Y = $minY; Width = $inkWidth; Height = $inkHeight
    }
    $maxInkWidth = [Math]::Max($maxInkWidth, $inkWidth)
    $maxInkHeight = [Math]::Max($maxInkHeight, $inkHeight)
}

# All glyphs share the same X and Y scales. The independent horizontal scale
# creates the tall condensed look of a mechanical flip clock while preserving
# identical height and baseline across 0-9.
$scaleX = 46.0 / $maxInkWidth
$scaleY = 76.0 / $maxInkHeight

for ($digit = 0; $digit -lt 10; $digit++) {
    $source = $sources[$digit]
    $box = $bounds[$digit]
    $inkWidth = $box.Width
    $inkHeight = $box.Height
    $targetWidth = [Math]::Max(1, [int][Math]::Round($inkWidth * $scaleX))
    $targetHeight = [Math]::Max(1, [int][Math]::Round($inkHeight * $scaleY))
    $targetX = [int][Math]::Floor(($cellWidth - $targetWidth) / 2)
    $targetY = [int][Math]::Floor(($cellHeight - $targetHeight) / 2)

    $cell = New-Object System.Drawing.Bitmap($cellWidth, $cellHeight)
    $cellGraphics = [System.Drawing.Graphics]::FromImage($cell)
    $cellGraphics.Clear([System.Drawing.Color]::Black)
    $cellGraphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $cellGraphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $cellGraphics.DrawImage(
        $source,
        [System.Drawing.Rectangle]::new($targetX, $targetY, $targetWidth, $targetHeight),
        $box.X, $box.Y, $inkWidth, $inkHeight,
        [System.Drawing.GraphicsUnit]::Pixel
    )

    for ($y = 0; $y -lt $cellHeight; $y++) {
        for ($pair = 0; $pair -lt $bytesPerRow; $pair++) {
            $left = [int][Math]::Round($cell.GetPixel($pair * 2, $y).R / 17.0)
            $right = [int][Math]::Round($cell.GetPixel($pair * 2 + 1, $y).R / 17.0)
            $offset = $digit * $cellHeight * $bytesPerRow + $y * $bytesPerRow + $pair
            $outputBytes[$offset] = [byte](($left -shl 4) -bor $right)
        }
    }

    $cellGraphics.Dispose()
    $cell.Dispose()
    $source.Dispose()
}

$font.Dispose()
[System.IO.File]::WriteAllBytes($Output, $outputBytes)
Write-Host "Generated $Output ($($outputBytes.Length) bytes)"
