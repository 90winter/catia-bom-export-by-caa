Add-Type -AssemblyName System.Drawing

function Convert-PngToIco {
    param(
        [string]$PngPath,
        [string]$IcoPath,
        [int[]]$Sizes = @(256, 128, 64, 48, 32, 16)
    )

    $src = [System.Drawing.Bitmap]::FromFile($PngPath)
    $images = @()

    foreach ($s in $Sizes) {
        $bmp = New-Object System.Drawing.Bitmap $s, $s
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.Clear([System.Drawing.Color]::Transparent)
        $g.DrawImage($src, 0, 0, $s, $s)
        $g.Dispose()

        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $bytes = $ms.ToArray()
        $ms.Dispose()

        $images += @{
            Size = $s
            Bytes = $bytes
        }
    }
    $src.Dispose()

    $fs = [System.IO.File]::Create($IcoPath)
    $bw = New-Object System.IO.BinaryWriter $fs

    # ICONDIR
    $bw.Write([uint16]0) # Reserved
    $bw.Write([uint16]1) # Type 1 = Icon
    $bw.Write([uint16]$images.Count)

    $offset = 6 + (16 * $images.Count)
    foreach ($img in $images) {
        $w = if ($img.Size -ge 256) { 0 } else { [byte]$img.Size }
        $h = if ($img.Size -ge 256) { 0 } else { [byte]$img.Size }
        $bw.Write([byte]$w)
        $bw.Write([byte]$h)
        $bw.Write([byte]0) # Color count
        $bw.Write([byte]0) # Reserved
        $bw.Write([uint16]1) # Planes
        $bw.Write([uint16]32) # Bit count
        $bw.Write([uint32]$img.Bytes.Length)
        $bw.Write([uint32]$offset)
        $offset += $img.Bytes.Length
    }

    foreach ($img in $images) {
        $bw.Write($img.Bytes)
    }

    $bw.Flush()
    $fs.Close()
    Write-Output "Successfully wrote $IcoPath with $($images.Count) resolutions."
}
