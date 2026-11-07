# Downloads the texture sets of texture_sources.json to Saved/TextureSources/<source>/<id>/ as <id>_color.jpg, <id>_normal.jpg (DirectX) and
# <id>_height.jpg, skipping sets already there. Poly Haven gives the three files; ambientCG gives a zip, unpacked into a folder of its own.
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$sources = Get-Content (Join-Path $PSScriptRoot 'texture_sources.json') -Raw | ConvertFrom-Json
$target = Join-Path $root 'Saved\TextureSources'
$kinds = 'color', 'normal', 'height'
foreach ($set in $sources.sets) {
    $source = if ($set.source) { $set.source } else { 'polyhaven' }
    $folder = Join-Path $target "$source\$($set.id)"
    if (@($kinds | Where-Object { -not (Test-Path (Join-Path $folder "$($set.id)_$_.jpg")) }).Count -eq 0) { continue }
    New-Item -ItemType Directory -Force $folder | Out-Null
    if ($source -eq 'polyhaven') {
        $maps = @{ color = 'Diffuse'; normal = 'nor_dx'; height = 'Displacement' }
        $files = Invoke-RestMethod -Uri "https://api.polyhaven.com/files/$($set.id)" -UserAgent 'HexHero-tools'
        foreach ($kind in $kinds) {
            $entry = $files.($maps[$kind]).($sources.resolution).jpg
            if (-not $entry) { throw "$($set.id) has no $($maps[$kind]) jpg at $($sources.resolution)" }
            Invoke-WebRequest -Uri $entry.url -OutFile (Join-Path $folder "$($set.id)_$kind.jpg") -UserAgent 'HexHero-tools' -UseBasicParsing
        }
    }
    elseif ($source -eq 'ambientcg') {
        $attribute = $sources.resolution.ToUpper() + '-JPG'
        $zip = Join-Path $folder "$($set.id)_$attribute.zip"
        Invoke-WebRequest -Uri "https://ambientcg.com/get?file=$($set.id)_$attribute.zip" -OutFile $zip -UserAgent 'HexHero-tools' -UseBasicParsing
        $unpacked = Join-Path $folder 'unpacked'
        Expand-Archive -Path $zip -DestinationPath $unpacked -Force
        $maps = @{ color = 'Color'; normal = 'NormalDX'; height = 'Displacement' }
        foreach ($kind in $kinds) {
            $file = Get-ChildItem $unpacked -Filter "*_$($maps[$kind]).jpg" | Select-Object -First 1
            if (-not $file) { throw "$($set.id) has no $($maps[$kind]) in its zip" }
            Copy-Item $file.FullName (Join-Path $folder "$($set.id)_$kind.jpg")
        }
    }
    else { throw "unknown source $source of $($set.id)" }
    Write-Output ("{0} {1} {2:N1} MB" -f $source, $set.id, ((Get-ChildItem $folder -Recurse -File | Measure-Object Length -Sum).Sum / 1MB))
}
Write-Output 'DONE'
