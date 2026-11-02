# Rasterize the small, editable SVG subset used by app-icon.svg; no external tools.
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$repo=Split-Path $PSScriptRoot -Parent
$assets=Join-Path $repo 'native\assets'
[xml]$svg=Get-Content -LiteralPath (Join-Path $assets 'app-icon.svg') -Raw
$frames=@()
foreach($size in @(16,20,24,32,40,48,64,128,256)){
    # Supersample every size so narrow tray outlines remain clean.
    $scale=4
    $large=[Drawing.Bitmap]::new($size*$scale,$size*$scale,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics=[Drawing.Graphics]::FromImage($large)
    $graphics.SmoothingMode=[Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.ScaleTransform(($size*$scale/256.0),($size*$scale/256.0))
    try{
        foreach($polygon in $svg.SelectNodes('//*[local-name()="polygon"]')){
            [Drawing.PointF[]]$points=@($polygon.points -split ' ' | Where-Object {$_} | ForEach-Object {
                $xy=$_ -split ','
                [Drawing.PointF]::new([single]::Parse($xy[0],[Globalization.CultureInfo]::InvariantCulture),[single]::Parse($xy[1],[Globalization.CultureInfo]::InvariantCulture))
            })
            $opacity=if($polygon.HasAttribute('opacity')){[double]::Parse($polygon.opacity,[Globalization.CultureInfo]::InvariantCulture)}else{1.0}
            if($polygon.fill -eq 'url(#purple)'){
                $brush=[Drawing.Drawing2D.LinearGradientBrush]::new([Drawing.Point]::new(0,30),[Drawing.Point]::new(0,226),[Drawing.ColorTranslator]::FromHtml('#D69CFF'),[Drawing.ColorTranslator]::FromHtml('#7628BC'))
            }else{
                $color=[Drawing.ColorTranslator]::FromHtml($polygon.fill)
                $brush=[Drawing.SolidBrush]::new([Drawing.Color]::FromArgb([int](255*$opacity),$color))
            }
            try{
                if($polygon.HasAttribute('stroke')){
                    $pen=[Drawing.Pen]::new([Drawing.ColorTranslator]::FromHtml($polygon.stroke),[single]::Parse($polygon.GetAttribute('stroke-width'),[Globalization.CultureInfo]::InvariantCulture))
                    $pen.LineJoin=[Drawing.Drawing2D.LineJoin]::Round
                    try{$graphics.DrawPolygon($pen,$points)}finally{$pen.Dispose()}
                }
                $graphics.FillPolygon($brush,$points)
            }finally{$brush.Dispose()}
        }
        $bitmap=[Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $down=[Drawing.Graphics]::FromImage($bitmap)
        try{
            $down.CompositingMode=[Drawing.Drawing2D.CompositingMode]::SourceCopy
            $down.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $down.PixelOffsetMode=[Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $down.DrawImage($large,[Drawing.Rectangle]::new(0,0,$size,$size))
            $stream=[IO.MemoryStream]::new()
            try{
                $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
                $frames+=,@{Size=$size;Bytes=$stream.ToArray()}
                if($size -eq 256){[IO.File]::WriteAllBytes((Join-Path $assets 'app-icon.png'),$stream.ToArray())}
            }finally{$stream.Dispose()}
        }finally{$down.Dispose();$bitmap.Dispose()}
    }finally{$graphics.Dispose();$large.Dispose()}
}
$file=[IO.File]::Create((Join-Path $assets 'app.ico'))
$writer=[IO.BinaryWriter]::new($file)
try{
    $writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$frames.Count)
    $offset=6+16*$frames.Count
    foreach($frame in $frames){
        $dimension=if($frame.Size -eq 256){0}else{$frame.Size}
        $writer.Write([byte]$dimension);$writer.Write([byte]$dimension)
        $writer.Write([byte]0);$writer.Write([byte]0)
        $writer.Write([uint16]1);$writer.Write([uint16]32)
        $writer.Write([uint32]$frame.Bytes.Length);$writer.Write([uint32]$offset)
        $offset+=$frame.Bytes.Length
    }
    foreach($frame in $frames){$writer.Write([byte[]]$frame.Bytes)}
}finally{$writer.Dispose()}
Get-Item -LiteralPath (Join-Path $assets 'app.ico') | Select-Object FullName,Length
