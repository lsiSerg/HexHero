# Rebuilds the mountain meshes from mountain_params.json and puts them into the project. Needs the editor closed.
# Run with: powershell -ExecutionPolicy Bypass -File Tools\HexMountains\rebuild_hex_mountains.ps1

$ErrorActionPreference = 'Stop'
$Root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$Project = Join-Path $Root 'HexHero.uproject'
$Log = Join-Path $Root 'Saved\Logs\HexHero.log'
$Editor = 'D:\Work\lsiSerg\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$Blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'

# a running editor holds the assets and would keep its own copies of them
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) {
    throw 'Close the editor first'
}

function Invoke-Script($Script, $Pattern) {
    & $Editor $Project -run=pythonscript "-script=$Script" -unattended -nullrhi | Out-Null
    $Errors = Select-String -Path $Log -Pattern 'Error: Traceback|RuntimeError|Exception:'
    if ($Errors) {
        $Errors | Select-Object -First 5 | ForEach-Object { $_.Line }
        throw "$Script failed"
    }
    Select-String -Path $Log -Pattern $Pattern | Select-Object -Last 1 | ForEach-Object { $_.Line -replace '^.*LogPython: ', '' }
}

Write-Host 'Building the meshes in Blender'
# Blender writes warnings to stderr, which must not stop the script
$ErrorActionPreference = 'Continue'
$Output = & $Blender --background --factory-startup --python (Join-Path $PSScriptRoot 'blender_build_mountains.py') 2>&1 | ForEach-Object { "$_" }
$ErrorActionPreference = 'Stop'
$Output | Select-String -Pattern 'SEAMS' | ForEach-Object { $_.Line }
if (-not ($Output | Select-String -Pattern '^DONE')) {
    $Output | Select-Object -Last 15
    throw 'The Blender build failed'
}

Write-Host 'Importing them'
Invoke-Script (Join-Path $PSScriptRoot 'import_hex_mountains.py') 'MOUNTAIN SM_'
$Wrong = (Select-String -Path $Log -Pattern 'wrong_sides=[1-9]').Count
if ($Wrong) {
    throw "$Wrong meshes have wrong sides"
}

Write-Host 'Building the materials'
Invoke-Script (Join-Path $Root 'Tools\build_hex_materials.py') 'HEXMAT M_HexMountain '

Write-Host 'Assigning them to the level'
Invoke-Script (Join-Path $PSScriptRoot 'assign_hex_mountains.py') 'MOUNTAIN assigned'
Write-Host 'Done'
