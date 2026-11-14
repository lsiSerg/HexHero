# Downloads the models of battle_model_sources.json as glTF with their textures to Saved/ModelSources/battle/<id>/, and the normal of each in
# the DirectX convention as <id>_nor_dx.jpg for the engine, skipping models already there. Poly Haven gives all of them; the request names no
# project.
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$sources = Get-Content (Join-Path $PSScriptRoot 'battle_model_sources.json') -Raw | ConvertFrom-Json
$target = Join-Path $root 'Saved\ModelSources\battle'
foreach ($model in $sources.models) {
    $folder = Join-Path $target $model.id
    $gltf = Join-Path $folder "$($model.id).gltf"
    $normal = Join-Path $folder "$($model.id)_nor_dx.jpg"
    if ((Test-Path $gltf) -and (Test-Path $normal)) { continue }
    New-Item -ItemType Directory -Force $folder | Out-Null
    $files = Invoke-RestMethod -Uri "https://api.polyhaven.com/files/$($model.id)" -UserAgent 'asset-fetch'
    $entry = $files.gltf.($sources.resolution).gltf
    if (-not $entry) { throw "$($model.id) has no glTF at $($sources.resolution)" }
    Invoke-WebRequest -Uri $entry.url -OutFile $gltf -UserAgent 'asset-fetch' -UseBasicParsing
    foreach ($include in $entry.include.PSObject.Properties) {
        $path = Join-Path $folder ($include.Name -replace '/', '\')
        New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
        Invoke-WebRequest -Uri $include.Value.url -OutFile $path -UserAgent 'asset-fetch' -UseBasicParsing
    }
    $dx = $files.nor_dx.($sources.resolution).jpg
    if (-not $dx) { throw "$($model.id) has no DirectX normal at $($sources.resolution)" }
    Invoke-WebRequest -Uri $dx.url -OutFile $normal -UserAgent 'asset-fetch' -UseBasicParsing
    Write-Output ("{0} {1:N1} MB" -f $model.id, ((Get-ChildItem $folder -File -Recurse | Measure-Object Length -Sum).Sum / 1MB))
}
Write-Output 'DONE'
