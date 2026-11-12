# Downloads the CC0 models of unit_model_sources.json to Saved/ModelSources/units (not committed), about 3.4 MB, for blender_build_units.py;
# the knight comes with Tools/HexPlanetHeroes/fetch_rider_models.ps1
$Root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$Target = Join-Path $Root 'Saved\ModelSources\units'
New-Item -ItemType Directory -Force $Target | Out-Null
$Sources = Get-Content (Join-Path $PSScriptRoot 'unit_model_sources.json') -Raw | ConvertFrom-Json
foreach ($Model in $Sources.models) {
    $File = Join-Path $Target $Model.file
    if (-not (Test-Path $File)) {
        Invoke-WebRequest -Uri $Model.url -OutFile $File -UserAgent 'model-fetch'
    }
    "{0}: {1} bytes" -f $Model.file, (Get-Item $File).Length
}
& (Join-Path $Root 'Tools\HexPlanetHeroes\fetch_rider_models.ps1')
