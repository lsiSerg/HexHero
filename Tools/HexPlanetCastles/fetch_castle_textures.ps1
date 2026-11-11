# Downloads the masonry sets of castle_texture_sources.json to Saved/TextureSources/castle/<id>/ as <id>_color.jpg and <id>_normal.jpg (DirectX),
# skipping sets already there. Poly Haven gives both files; the request names no project.
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$sources = Get-Content (Join-Path $PSScriptRoot 'castle_texture_sources.json') -Raw | ConvertFrom-Json
$target = Join-Path $root 'Saved\TextureSources\castle'
$maps = @{ color = 'Diffuse'; normal = 'nor_dx' }
foreach ($set in $sources.sets) {
    $folder = Join-Path $target $set.id
    if (@($maps.Keys | Where-Object { -not (Test-Path (Join-Path $folder "$($set.id)_$_.jpg")) }).Count -eq 0) { continue }
    New-Item -ItemType Directory -Force $folder | Out-Null
    $files = Invoke-RestMethod -Uri "https://api.polyhaven.com/files/$($set.id)" -UserAgent 'texture-fetch'
    foreach ($kind in $maps.Keys) {
        $entry = $files.($maps[$kind]).($sources.resolution).jpg
        if (-not $entry) { throw "$($set.id) has no $($maps[$kind]) jpg at $($sources.resolution)" }
        Invoke-WebRequest -Uri $entry.url -OutFile (Join-Path $folder "$($set.id)_$kind.jpg") -UserAgent 'texture-fetch' -UseBasicParsing
    }
    Write-Output ("{0} {1:N1} MB" -f $set.id, ((Get-ChildItem $folder -File | Measure-Object Length -Sum).Sum / 1MB))
}
Write-Output 'DONE'
