# Downloads the CC0 models of rider_model_sources.json to Saved/ModelSources/hero (not committed), about 3.1 MB, for blender_build_rider.py
$Root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$Target = Join-Path $Root 'Saved\ModelSources\hero'
New-Item -ItemType Directory -Force $Target | Out-Null
$Sources = Get-Content (Join-Path $PSScriptRoot 'rider_model_sources.json') -Raw | ConvertFrom-Json
foreach ($Model in $Sources.models) {
    $File = Join-Path $Target $Model.file
    if (-not (Test-Path $File)) {
        Invoke-WebRequest -Uri $Model.url -OutFile $File -UserAgent 'model-fetch'
    }
    "{0}: {1} bytes" -f $Model.file, (Get-Item $File).Length
}
