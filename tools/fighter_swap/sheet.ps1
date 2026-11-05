param([string]$pattern, [string]$out, [int]$cols = 4, [int]$w = 420, [int]$h = 315)
Add-Type -AssemblyName System.Drawing
$files = Get-ChildItem $pattern | Sort-Object Name
$rows = [math]::Ceiling($files.Count / $cols)
$bm = New-Object System.Drawing.Bitmap ($cols * $w), ($rows * $h)
$g = [System.Drawing.Graphics]::FromImage($bm); $g.Clear([System.Drawing.Color]::White)
$font = New-Object System.Drawing.Font 'Arial', 10
$i = 0
foreach ($f in $files) {
  $im = [System.Drawing.Image]::FromFile($f.FullName)
  $s = [math]::Min($w / $im.Width, $h / $im.Height)
  $g.DrawImage($im, ($i % $cols) * $w, [math]::Floor($i / $cols) * $h, [int]($im.Width * $s), [int]($im.Height * $s))
  $g.DrawString($f.BaseName, $font, [System.Drawing.Brushes]::Black, ($i % $cols) * $w + 3, [math]::Floor($i / $cols) * $h + 3)
  $im.Dispose(); $i++
}
$bm.Save($out, [System.Drawing.Imaging.ImageFormat]::Jpeg); $g.Dispose(); $bm.Dispose()
