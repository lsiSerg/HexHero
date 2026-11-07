"""Rebuilds M_HexTerrain, M_HexMountain, M_HexWater, M_HexRiver and M_HexCliff, and the debug wireframes M_HexWireframe and M_HexMountainWireframe,
which it also sets in DA_HexMap. The terrain and mountains read the biome of each hex from the data texture of AHexWorldMap and find one surface from the world position,
so where a mountain meets the terrain they give the same color.

Each surface material also gets a masked instance, <name>_Cut, which AHexWorldMap uses while only the triangles of the hexes around the cursor are drawn,
so the rest of the map keeps its normal render. The opaque materials ignore the mask that it reads.

Every value of the look is a property of UHexMapSurfaceConfig or UHexMapWaterConfig, named as it in the code of the materials: add_config_params
gives each material a parameter for every one its code names, with the default of the class, and AHexWorldMap sets them all from DA_HexMapSurface and
DA_HexMapWater, which this script creates only when missing. So a new value needs only the property and its name in the code.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi
The hex math repeats FHexLayout and the channels repeat HexMapLoader::BuildHexData, so change them together.
"""

import os
import re

import unreal

TERRAIN = '/Game/HexMap/Materials/M_HexTerrain'
MOUNTAIN = '/Game/HexMap/Materials/M_HexMountain'
WATER = '/Game/HexMap/Materials/M_HexWater'
RIVER = '/Game/HexMap/Materials/M_HexRiver'
CLIFF = '/Game/HexMap/Materials/M_HexCliff'
WIRE = '/Game/HexMap/Materials/M_HexWireframe'
JOINED_WIRE = '/Game/HexMap/Materials/M_HexMountainWireframe'
MAP_CONFIG = '/Game/HexMap/DA_HexMap'
COLORS = '/Game/HexMap/Textures/Biomes/TA_Biome_Color'
NORMALS = '/Game/HexMap/Textures/Biomes/TA_Biome_Normal'
# the noise of water and ice, once the alpha of the snow layer, so an artist can change the snow (build_biome_textures.py)
NOISE = '/Game/HexMap/Textures/T_HexNoise'
MASKS = '/Game/HexMap/Textures/T_Mountain_Masks'
ATLAS_NORMAL = '/Game/HexMap/Textures/T_Mountain_Normal'
DEFAULT_DATA = '/Engine/EngineResources/DefaultTexture'

# finds the three hexes around the position and blends their surfaces, with the height in alpha deciding what shows through
SURFACE = '''
float2 p = float2(WorldPos.y, -WorldPos.x) / HexRadius;
float fq = 0.57735027 * p.x - p.y / 3.0;
float fr = p.y * 2.0 / 3.0;
float2 fl = floor(float2(fq, fr));
float u = fq - fl.x;
float v = fr - fl.y;
int2 hexes[3];
float weights[3];
if (u + v < 1.0)
{
    hexes[0] = int2(fl) + int2(0, 0); hexes[1] = int2(fl) + int2(1, 0); hexes[2] = int2(fl) + int2(0, 1);
    weights[0] = 1.0 - u - v; weights[1] = u; weights[2] = v;
}
else
{
    hexes[0] = int2(fl) + int2(1, 1); hexes[1] = int2(fl) + int2(0, 1); hexes[2] = int2(fl) + int2(1, 0);
    weights[0] = u + v - 1.0; weights[1] = 1.0 - u; weights[2] = 1.0 - v;
}
uint mapWidth, mapHeight;
HexData.GetDimensions(mapWidth, mapHeight);
float2 uv = WorldPos.xy / TileSize;
float2 macroUV = WorldPos.xy / (TileSize * MacroToneScale);
float2 uvDx = PosDdx.xy / TileSize;
float2 uvDy = PosDdy.xy / TileSize;
float3 surface = 0;
float3 landSurface = 0;
float landTotal = 0.00001;
float2 surfaceSlope = 0;
float total = 0;
float rockLayers[3];
float surfaceLayers[3];
float landWeights[3];
float snowLines[3];
for (int i = 0; i < 3; ++i)
{
    int row = clamp(hexes[i].y, 0, int(mapHeight) - 1);
    int column = clamp(hexes[i].x + ((hexes[i].y - (hexes[i].y & 1)) / 2), 0, int(mapWidth) - 1);
    float4 hexData = HexData.Load(int3(column, row, 0));
    rockLayers[i] = round(hexData.b * 255.0);
    // a cliff can reach so far out that only water is near, which then still has a rock of its own
    landWeights[i] = (pow(weights[i], SurfaceBlendPower) + 0.00001) * (hexData.g < 0.5 ? 0.001 : 1.0);
    // the alpha of water is its opacity, and no mountain takes its snow from there
    snowLines[i] = hexData.g < 0.5 ? 400.0 : hexData.a * 510.0 - 200.0;
    float layer = round(hexData.r * 255.0);
    surfaceLayers[i] = layer;
    float4 layerColor = Colors.SampleGrad(ColorsSampler, float3(uv, layer), uvDx, uvDy);
    float macroTone = Colors.SampleGrad(ColorsSampler, float3(macroUV, layer), uvDx / MacroToneScale, uvDy / MacroToneScale).a;
    float weight = pow(weights[i], SurfaceBlendPower) * exp(layerColor.a * SurfaceHeightBlend) + 0.00001;
    surface += layerColor.rgb * (1.0 - MacroTone + macroTone * 2.0 * MacroTone) * weight;
    landSurface += layerColor.rgb * (1.0 - MacroTone + macroTone * 2.0 * MacroTone) * weight * (hexData.g < 0.5 ? 0.0 : 1.0);
    landTotal += weight * (hexData.g < 0.5 ? 0.0 : 1.0);
    surfaceSlope += (Normals.SampleGrad(NormalsSampler, float3(uv, layer), uvDx, uvDy).xy * 2.0 - 1.0) * weight;
    total += weight;
}
surface /= total;
// the sand under water only reaches beaches, and not land high above the water such as the top of a cliff
surface = lerp(surface, landSurface / landTotal, landTotal > 0.001 ? smoothstep(SeabedSandStart, SeabedSandEnd, WorldPos.z) : 0.0);
surfaceSlope /= total;
'''

GRID = '''
float q = 0.57735027 * p.x - p.y / 3.0;
float r = p.y * 2.0 / 3.0;
float s = -q - r;
float rq = round(q);
float rr = round(r);
float rs = round(s);
float dq = abs(rq - q);
float dr = abs(rr - r);
float ds = abs(rs - s);
if (dq > dr && dq > ds) { rq = -rr - rs; } else if (dr > ds) { rr = -rq - rs; }
float2 l = p - float2(1.7320508 * (rq + rr * 0.5), 1.5 * rr);
float h = max(abs(l.x), max(abs(l.x * 0.5 + l.y * 0.8660254), abs(l.x * 0.5 - l.y * 0.8660254)));
float edgeDist = (0.8660254 - h) * HexRadius;
float aa = max((abs(PosDdx.x) + abs(PosDdx.y) + abs(PosDdy.x) + abs(PosDdy.y)) * 0.5, 0.001);
float gridLine = 1.0 - smoothstep(GridWidth, GridWidth + aa, edgeDist);
'''

# finds the height of the terrain under a place of a joined mesh, from the heights at its vertices that HexMeshBuilder::BuildTerrainHeights wrote.
# It blends them in the triangle of the terrain around the place, so a mesh lies on that terrain exactly. See FHexTerrainHeights
RIM_CODE = '''
float rimFy = RimPos.y / (0.8660254 * RimStep);
float rimFx = RimPos.x / RimStep - rimFy * 0.5;
int2 rimFloor = int2(floor(rimFx), floor(rimFy));
float rimU = rimFx - rimFloor.x;
float rimV = rimFy - rimFloor.y;
int2 rimPoints[3];
float rimWeights[3];
if (rimU + rimV < 1.0)
{
    rimPoints[0] = rimFloor; rimPoints[1] = rimFloor + int2(1, 0); rimPoints[2] = rimFloor + int2(0, 1);
    rimWeights[0] = 1.0 - rimU - rimV; rimWeights[1] = rimU; rimWeights[2] = rimV;
}
else
{
    rimPoints[0] = rimFloor + int2(1, 1); rimPoints[1] = rimFloor + int2(0, 1); rimPoints[2] = rimFloor + int2(1, 0);
    rimWeights[0] = rimU + rimV - 1.0; rimWeights[1] = 1.0 - rimU; rimWeights[2] = 1.0 - rimV;
}
uint rimWidth, rimHeight;
MeshRims.GetDimensions(rimWidth, rimHeight);
float rimTerrain = 0.0;
for (int rimIndex = 0; rimIndex < 3; ++rimIndex)
{
    int2 rimPoint = rimPoints[rimIndex];
    int rimColumn = clamp(rimPoint.x + (rimPoint.y - (rimPoint.y & 1)) / 2 - int(RimOriginX), 0, int(rimWidth) - 1);
    int rimRow = clamp(rimPoint.y - int(RimOriginY), 0, int(rimHeight) - 1);
    rimTerrain += MeshRims.Load(int3(rimColumn, rimRow, 0)).r * rimWeights[rimIndex];
}
'''

# how far inside its group of mountains a place lies, in hexes, by MassifData of HexMeshBuilder::BuildMassifDepths blended between the three nearest hexes.
# It is the same at the same place for every mesh, so neighbors still meet
MASSIF_CODE = '''
// a group of mountains grows taller the further inside it a place lies, by MassifData of HexMeshBuilder::BuildMassifDepths blended between
// the three nearest hexes. It is the same at the same place for every mesh, so neighbors still meet, and the foot does not move
float2 massifP = float2(RimPos.y, -RimPos.x) / HexRadius;
float massifQ = 0.57735027 * massifP.x - massifP.y / 3.0;
float massifR = massifP.y * 2.0 / 3.0;
float2 massifFloor = floor(float2(massifQ, massifR));
float massifU = massifQ - massifFloor.x;
float massifV = massifR - massifFloor.y;
int2 massifHexes[3];
float massifWeights[3];
if (massifU + massifV < 1.0)
{
    massifHexes[0] = int2(massifFloor); massifHexes[1] = int2(massifFloor) + int2(1, 0); massifHexes[2] = int2(massifFloor) + int2(0, 1);
    massifWeights[0] = 1.0 - massifU - massifV; massifWeights[1] = massifU; massifWeights[2] = massifV;
}
else
{
    massifHexes[0] = int2(massifFloor) + int2(1, 1); massifHexes[1] = int2(massifFloor) + int2(0, 1); massifHexes[2] = int2(massifFloor) + int2(1, 0);
    massifWeights[0] = massifU + massifV - 1.0; massifWeights[1] = 1.0 - massifU; massifWeights[2] = 1.0 - massifV;
}
uint massifWidth, massifHeight;
MassifData.GetDimensions(massifWidth, massifHeight);
float massifDepth = 0.0;
float massifTotal = 0.0;
for (int massifIndex = 0; massifIndex < 3; ++massifIndex)
{
    int2 massifHex = massifHexes[massifIndex];
    int massifRow = clamp(massifHex.y, 0, int(massifHeight) - 1);
    int massifColumn = clamp(massifHex.x + (massifHex.y - (massifHex.y & 1)) / 2, 0, int(massifWidth) - 1);
    // smooth weights, as straight ones would crease the slopes along the lines between the centers of hexes
    float massifWeight = massifWeights[massifIndex] * massifWeights[massifIndex] * (3.0 - 2.0 * massifWeights[massifIndex]);
    massifDepth += min(MassifData.Load(int3(massifColumn, massifRow, 0)).r * 255.0, MassifMaxSteps) * massifWeight;
    massifTotal += massifWeight;
}
massifDepth /= massifTotal;
'''

# moves the low part of a joined mesh so that its edge lies on the terrain. The move fades with height, so the top stays as it was modelled
RIM_OFFSET_CODE = '''
float3 rimOffset = 0.0;
float rimFade = 1.0 - smoothstep(FootHeight, FootHeight + RimFadeHeight, RimPos.z);
if (rimFade > 0.0)
{
''' + RIM_CODE + '''
    rimOffset.z = (rimTerrain - FootHeight) * rimFade;
}

''' + MASSIF_CODE + '''
// it rises as a whole and its slopes stretch, which deepens the gullies, while the foot stays on the terrain
rimOffset.z += max(RimPos.z - FootHeight, 0.0) * massifDepth * MassifStep + massifDepth * MassifRise * smoothstep(0.0, 30.0, RimPos.z - FootHeight);
'''

MOUNTAIN_OFFSET_CODE = RIM_OFFSET_CODE + '''
return rimOffset;
'''

# colors the triangles of the debug wireframe by the hex they lie in: the one under the cursor, its neighbors and the rest.
# WireScope 0 draws them everywhere, 1 only in that hex and 2 also in its neighbors. The rounding repeats GRID
WIRE_CODE = '''
float2 p = float2(WorldPos.y, -WorldPos.x) / HexRadius;
float q = 0.57735027 * p.x - p.y / 3.0;
float r = p.y * 2.0 / 3.0;
float s = -q - r;
float rq = round(q);
float rr = round(r);
float rs = round(s);
float dq = abs(rq - q);
float dr = abs(rr - r);
float ds = abs(rs - s);
if (dq > dr && dq > ds) { rq = -rr - rs; } else if (dr > ds) { rr = -rq - rs; }
float hq = rq - WireHexQ;
float hr = rr - WireHexR;
float hexDistance = (abs(hq) + abs(hr) + abs(hq + hr)) * 0.5;
OutOpacity = (WireScope < 0.5 || hexDistance < WireScope - 0.5) ? 1.0 : WireOutsideOpacity;
return hexDistance < 0.5 ? float3(1.0, 0.75, 0.05) : hexDistance < 1.5 ? float3(0.05, 0.7, 1.0) : float3(0.8, 0.8, 0.8);
'''

# the mask of the _Cut instances: 0 in the hexes whose triangles alone are drawn, which WireScope chooses as WIRE_CODE does, and 1 elsewhere
CUT_CODE = '''
if (WireScope < 0.5) { return 1.0; }
float2 p = float2(WorldPos.y, -WorldPos.x) / HexRadius;
float q = 0.57735027 * p.x - p.y / 3.0;
float r = p.y * 2.0 / 3.0;
float s = -q - r;
float rq = round(q);
float rr = round(r);
float rs = round(s);
float dq = abs(rq - q);
float dr = abs(rr - r);
float ds = abs(rs - s);
if (dq > dr && dq > ds) { rq = -rr - rs; } else if (dr > ds) { rr = -rq - rs; }
float hq = rq - WireHexQ;
float hr = rr - WireHexR;
float hexDistance = (abs(hq) + abs(hr) + abs(hq + hr)) * 0.5;
return hexDistance < WireScope - 0.5 ? 0.0 : 1.0;
'''

# the triangles are drawn a little towards the camera, so they do not fight with the surface they lie on
WIRE_OFFSET_CODE = '''
return normalize(CameraPos - RimPos) * WireBias;
'''

JOINED_WIRE_OFFSET_CODE = RIM_OFFSET_CODE + '''
return rimOffset + normalize(CameraPos - RimPos - rimOffset) * WireBias;
'''

# steep terrain, such as a shore, shows the rock of its hex
TERRAIN_CODE = SURFACE + GRID + '''
float steep = 1.0 - smoothstep(SteepRockStart, SteepRockEnd, Normal.z);
if (steep > 0.0)
{
    int rockIndex = weights[0] > weights[1] ? (weights[0] > weights[2] ? 0 : 2) : (weights[1] > weights[2] ? 1 : 2);
    float3 side = Colors.SampleGrad(ColorsSampler, float3(float2(WorldPos.x + WorldPos.y, WorldPos.z) / TileSize, rockLayers[rockIndex]), float2(PosDdx.x + PosDdx.y, PosDdx.z) / TileSize, float2(PosDdy.x + PosDdy.y, PosDdy.z) / TileSize).rgb;
    surface = lerp(surface, side, steep);
}

// each wave runs up the beach and back: wet sand under its reach and a line of foam at its edge
float aboveWater = WorldPos.z - WaterHeight;
// the bottom seen through water is only soft color: fine detail looks sharp there, and blending between hexes would show them
float underwater = 1.0 - smoothstep(-UnderwaterBlend, UnderwaterBlend, aboveWater);
if (underwater > 0.0)
{
    float3 seabed = 0;
    float seabedTotal = 0.00001;
    for (int i = 0; i < 3; ++i)
    {
        seabed += Colors.SampleLevel(ColorsSampler, float3(uv * SeabedBlurScale, surfaceLayers[i]), SeabedBlurMip).rgb * weights[i];
        seabedTotal += weights[i];
    }
    surface = lerp(surface, seabed / seabedTotal, underwater);
    surfaceSlope *= 1.0 - underwater;
}
if (aboveWater > -1.0 && aboveWater < 12.0)
{
    float beachNoise = HexNoise.SampleGrad(HexNoiseSampler, uv * 0.41, uvDx * 0.41, uvDy * 0.41).r;
    float beachLace = HexNoise.SampleGrad(HexNoiseSampler, uv * 3.1 + Time * float2(0.007, -0.005), uvDx * 3.1, uvDy * 3.1).r;
    float waveReach = BeachWaveReach + BeachWaveSwing * sin(Time * BeachWaveSpeed + beachNoise * 14.0) + beachNoise * 1.5;
    float wetReach = BeachWetReach + beachNoise * 2.5;
    float flat = smoothstep(SteepRockStart, SteepRockEnd, Normal.z);
    surface *= lerp(1.0, BeachWetDarkness, (1.0 - smoothstep(wetReach - 1.5, wetReach + 1.5, aboveWater)) * flat);
    float edge = smoothstep(waveReach - 1.3, waveReach - 0.2, aboveWater) * (1.0 - smoothstep(waveReach, waveReach + 0.5, aboveWater));
    float behind = (1.0 - smoothstep(waveReach - 1.2, waveReach, aboveWater)) * smoothstep(0.58, 0.72, beachLace);
    // foam comes up the beach only here and there, in new places each time
    float beachTime = Time / BeachFoamPeriod + beachNoise * 0.4;
    float beachPatch = smoothstep(0.5, 0.62, HexNoise.SampleGrad(HexNoiseSampler, uv * 0.2 + frac(floor(beachTime) * 0.43) * 6.0, uvDx * 0.2, uvDy * 0.2).r) * sin(frac(beachTime) * 3.14159);
    surface = lerp(surface, BeachFoamColor.rgb, saturate(max(edge * smoothstep(0.3, 0.5, beachLace), behind * 0.8)) * flat * beachPatch * BeachFoamAmount);
}
// a river bed and its bank, from the alpha of the vertex colors (FTerrainWriter::ApplyRivers), are light earthy sand up to the top of the bank,
// and a little darker and wetter at the floor under the water of M_HexRiver
float river = saturate(RiverWater);
if (river > 0.001)
{
    // from three sides like rock, as the face of the bank is steep and a texture laid from above would stretch into streaks on it
    float3 sandSides = pow(abs(Normal), 4.0);
    sandSides /= sandSides.x + sandSides.y + sandSides.z;
    float sandScale = RiverSandScale / TileSize;
    float3 riverSand = (Colors.SampleGrad(ColorsSampler, float3(WorldPos.yz * sandScale, 1.0), PosDdx.yz * sandScale, PosDdy.yz * sandScale).rgb * sandSides.x
        + Colors.SampleGrad(ColorsSampler, float3(WorldPos.xz * sandScale, 1.0), PosDdx.xz * sandScale, PosDdy.xz * sandScale).rgb * sandSides.y
        + Colors.SampleGrad(ColorsSampler, float3(uv * RiverSandScale, 1.0), uvDx * RiverSandScale, uvDy * RiverSandScale).rgb * sandSides.z) * RiverSandTint.rgb;
    riverSand *= lerp(1.0, RiverFloorDarkness, smoothstep(0.85, 1.0, river));
    // past the top of the bank the alpha falls evenly with the distance, from RiverBankTopColor over RiverBankColorReach of the Tuning of DA_HexMap,
    // which AHexWorldMap gives this material, so the sand on the land ends at a line RiverBankRim wide on average that winds by noise
    // and stays sharp on any triangle
    float pastTop = saturate((RiverBankTopColor - river) / RiverBankTopColor) * RiverBankColorReach;
    float rimNoise = HexNoise.SampleGrad(HexNoiseSampler, WorldPos.xy / RiverBankRimScale, PosDdx.xy / RiverBankRimScale, PosDdy.xy / RiverBankRimScale).r;
    float rimWidth = RiverBankRim * (0.35 + rimNoise * 1.3);
    float rimEdge = max((abs(PosDdx.x) + abs(PosDdx.y) + abs(PosDdy.x) + abs(PosDdy.y)) * 0.5, 0.15);
    float sand = 1.0 - smoothstep(rimWidth - rimEdge, rimWidth + rimEdge, pastTop);
    surface = lerp(surface, riverSand * lerp(1.0, 1.0 + RiverBankRimLight, saturate(pastTop / max(rimWidth, 0.01))), sand);
    surfaceSlope *= 1.0 - sand * 0.5;
}
OutNormal = normalize(float3(Normal.xy * (1.0 - underwater * 0.7) + surfaceSlope * DetailStrength * (1.0 - steep), Normal.z));
// under water the grid is drawn on the water, as a second one on the bottom would look doubled
return lerp(surface, GridColor.rgb, gridLine * GridOpacity * GridStrength * smoothstep(-1.0, 0.0, aboveWater));
'''

# rock from three sides, as slopes would stretch a texture laid from above. The shade comes from the atlas of the mesh
MOUNTAIN_CODE = SURFACE + GRID + '''
float3 sides = pow(abs(Normal), 4.0);
sides /= sides.x + sides.y + sides.z;
float weightTotal = 0;
float3 rock = 0;
float snowLine = 0;
float waterNear = 0;
for (int j = 0; j < 3; ++j)
{
    float rockWeight = pow(weights[j], SurfaceBlendPower) + 0.00001;
    waterNear += snowLines[j] > 399.0 ? rockWeight : 0.0;
    float layer = rockLayers[j];
    float3 layered = Colors.SampleGrad(ColorsSampler, float3(WorldPos.yz / TileSize, layer), PosDdx.yz / TileSize, PosDdy.yz / TileSize).rgb * sides.x
        + Colors.SampleGrad(ColorsSampler, float3(WorldPos.xz / TileSize, layer), PosDdx.xz / TileSize, PosDdy.xz / TileSize).rgb * sides.y
        + Colors.SampleGrad(ColorsSampler, float3(WorldPos.xy / TileSize, layer), uvDx, uvDy).rgb * sides.z;
    rock += layered * rockWeight;
    snowLine += snowLines[j] * rockWeight;
    weightTotal += rockWeight;
}
rock /= weightTotal;
snowLine /= weightTotal;
waterNear /= weightTotal;
rock *= MountainShadeMin + Mask.r * MountainShadeRange;
float up = Normal.z;
float wander = (Mask.g - 0.5) * SnowLineWander;
float3 RimPos = WorldPos;
''' + MASSIF_CODE + '''
// snow takes the height the mesh had before its massif raised it and only a part of that rise, so it keeps to the highest tops
// of a massif, and its stretched slopes stay bare rock. The meshes inside a group are taller of their own, so they lose some more
float unraised = (WorldPos.z - massifDepth * MassifRise + FootHeight * massifDepth * MassifStep) / (1.0 + massifDepth * MassifStep);
float snowHeight = unraised + (WorldPos.z - unraised) * SnowMassifShare - massifDepth * SnowMassifDrop;
float snow = smoothstep(snowLine - SnowLineSoftness, snowLine + SnowLineSoftness, snowHeight + wander + (up - 0.6) * SnowSlopeLift) * smoothstep(SnowFlatStart, SnowFlatEnd, up);
float3 snowColor = Colors.SampleGrad(ColorsSampler, float3(uv, 3.0), uvDx, uvDy).rgb;
// snow has drifts and ridges of its own, a large and a small size of them, and the hollows are a little blue
float2 snowSlope = (Normals.SampleGrad(NormalsSampler, float3(uv * 0.8, 3.0), uvDx * 0.8, uvDy * 0.8).xy - 0.5) * 2.0 + (Normals.SampleGrad(NormalsSampler, float3(uv * 2.3 + 0.37, 3.0), uvDx * 2.3, uvDy * 2.3).xy - 0.5);
snowColor *= SnowTint.rgb + dot(snowSlope, normalize(SunDirection).xy) * SnowContrast * 0.1;
float3 color = lerp(rock, snowColor, snow);
''' + RIM_CODE + '''
// the land runs only a little way up the mountain, with a broken edge, and above it moss clings to the gentler rock of the foothill,
// so the mountain rises out of the terrain instead of standing on a ring of grass
float above = WorldPos.z - rimTerrain;
float footNoise = HexNoise.SampleGrad(HexNoiseSampler, uv * 0.9, uvDx * 0.9, uvDy * 0.9).r * 0.6 + HexNoise.SampleGrad(HexNoiseSampler, uv * 3.1 + 0.29, uvDx * 3.1, uvDy * 3.1).r * 0.4;
float foot = 1.0 - smoothstep(1.0 + footNoise * 4.0, 3.0 + footNoise * 7.0, above);
float moss = (1.0 - smoothstep(MossHeight / 3.0 + footNoise * 12.0, MossHeight + footNoise * 16.0, above)) * smoothstep(MossFlatStart, MossFlatEnd, up) * smoothstep(0.35, 0.6, footNoise + (up - 0.7));
// towards water the mountain falls into its cliff as bare rock
float bare = smoothstep(0.02, 0.2, waterNear);
moss *= 1.0 - bare;
foot *= 1.0 - bare;
color = lerp(color, lerp(rock, surface * 0.75, 0.65), moss * MossAmount);
color = lerp(color, surface, foot);
// the grid is clear on the foot of a mountain and only a hint on the rock, where it would cut the mountain into pieces
gridLine *= lerp(MountainGridHint, 1.0, foot);
// under the water the rock fades into the same soft color of the seabed as the terrain next to it, so their meeting does not show
float underwater = 1.0 - smoothstep(-MountainUnderwaterBlend, 0.0, WorldPos.z - WaterHeight);
if (underwater > 0.0)
{
    float3 seabed = 0;
    float seabedTotal = 0.00001;
    for (int k = 0; k < 3; ++k)
    {
        seabed += Colors.SampleLevel(ColorsSampler, float3(uv * SeabedBlurScale, surfaceLayers[k]), SeabedBlurMip).rgb * weights[k];
        seabedTotal += weights[k];
    }
    color = lerp(color, seabed / seabedTotal, underwater);
}
// where it stands in water, spray on the rock just above it rises and falls as on a cliff (CLIFF_CODE), and a surge throws it up the rock
SURGE_HERE
float sprayNoise = HexNoise.SampleGrad(HexNoiseSampler, uv * 0.5, uvDx * 0.5, uvDy * 0.5).r;
float sprayLace = HexNoise.SampleGrad(HexNoiseSampler, float2(WorldPos.x + WorldPos.y, WorldPos.z) / TileSize * 3.0 + float2(0.0, Time * 0.03), float2(PosDdx.x + PosDdx.y, PosDdx.z) / TileSize * 3.0, float2(PosDdy.x + PosDdy.y, PosDdy.z) / TileSize * 3.0).r;
float sprayTop = WaterHeight + SprayHeight + sprayNoise * SprayNoiseHeight + max(surgeSlow * 0.7, surgeQuick) * SurgeSprayHeight;
float spray = (1.0 - smoothstep(sprayTop - 2.0, sprayTop + 1.0, WorldPos.z)) * smoothstep(WaterHeight - 4.0, WaterHeight - 1.0, WorldPos.z) * smoothstep(0.42, 0.62, sprayLace) * bare;
color = lerp(color, SprayColor.rgb, spray);
OutNormal = normalize(float3(Baked.xy * (1.0 - snow * 0.45) + surfaceSlope * DetailStrength * 0.6 * (1.0 - snow) + snowSlope * SnowContrast * snow, Baked.z));
return lerp(color, GridColor.rgb, gridLine * GridOpacity * GridStrength);
'''


# rock of the land above, as the water below has none that shows. The top takes the color of the land, from the vertex colors of the mesh
CLIFF_CODE = SURFACE + GRID + '''
float3 sides = pow(abs(Normal), 4.0);
sides /= sides.x + sides.y + sides.z;
float weightTotal = 0.00001;
float3 rock = 0;
for (int j = 0; j < 3; ++j)
{
    float layer = rockLayers[j];
    float3 layered = Colors.SampleGrad(ColorsSampler, float3(WorldPos.yz / TileSize, layer), PosDdx.yz / TileSize, PosDdy.yz / TileSize).rgb * sides.x
        + Colors.SampleGrad(ColorsSampler, float3(WorldPos.xz / TileSize, layer), PosDdx.xz / TileSize, PosDdy.xz / TileSize).rgb * sides.y
        + Colors.SampleGrad(ColorsSampler, float3(WorldPos.xy / TileSize, layer), uvDx, uvDy).rgb * sides.z;
    rock += layered * landWeights[j];
    weightTotal += landWeights[j];
}
rock /= weightTotal;

// bands of rock that follow the height, broken up a little, and a dark wet line just above the water
float strata = sin((WorldPos.z + (Colors.SampleGrad(ColorsSampler, float3(uv * 0.5, 2.0), uvDx * 0.5, uvDy * 0.5).a - 0.5) * CliffStrataWobble) * CliffStrataFrequency);
rock *= 0.82 + CliffStrataContrast * strata;
rock *= lerp(CliffWetDarkness, 1.0, smoothstep(WaterHeight, WaterHeight + CliffWetLine, WorldPos.z));
float3 color = lerp(rock, landSurface / landTotal, Top.r);
// spray on the rock just above the water, rising and falling
float sprayNoise = HexNoise.SampleGrad(HexNoiseSampler, uv * 0.5, uvDx * 0.5, uvDy * 0.5).r;
float sprayLace = HexNoise.SampleGrad(HexNoiseSampler, float2(WorldPos.x + WorldPos.y, WorldPos.z) / TileSize * 3.0 + float2(0.0, Time * 0.03), float2(PosDdx.x + PosDdx.y, PosDdx.z) / TileSize * 3.0, float2(PosDdy.x + PosDdy.y, PosDdy.z) / TileSize * 3.0).r;
// a surge throws it up the rock, with the same noise as the foam of the water
SURGE_HERE
float sprayTop = WaterHeight + SprayHeight + sprayNoise * SprayNoiseHeight + max(surgeSlow * 0.7, surgeQuick) * SurgeSprayHeight;
color = lerp(color, SprayColor.rgb, (1.0 - smoothstep(sprayTop - 2.0, sprayTop + 1.0, WorldPos.z)) * smoothstep(0.42, 0.62, sprayLace) * (1.0 - Top.r));
OutNormal = normalize(float3(Normal.xy + surfaceSlope * DetailStrength * Top.r, Normal.z));
return lerp(color, GridColor.rgb, gridLine * GridOpacity * GridStrength);
'''

# the water of a river on the ribbon of HexMeshBuilder::BuildRiverWater: UV.x goes across from -1 to 1 and UV.y along in world units,
# Flow.r is how fast the water falls, which brings rapids, Flow.g how near the sea it is, and FlowFade, the alpha, fades the ends. Unlit like M_HexWater, with its colors
RIVER_CODE = '''
float across = abs(UV.x);
float rapids = saturate(Flow.r);
float mouth = saturate(Flow.g);
// it flows faster and rougher down a step
float speed = RiverFlowSpeed + rapids * RiverRapidsSpeed;
float scale = 1.0 / TileSize;
float2 flowA = float2(UV.x * 0.18, (UV.y - Time * speed) * scale * 1.4);
float2 flowB = float2(UV.x * 0.45 + 0.37, (UV.y - Time * speed * 1.35) * scale * 3.3);
float2 gradAx = float2(UvDdx.x * 0.18, UvDdx.y * scale * 1.4);
float2 gradAy = float2(UvDdy.x * 0.18, UvDdy.y * scale * 1.4);
float noiseA = HexNoise.SampleGrad(HexNoiseSampler, flowA, gradAx, gradAy).r;
float noiseB = HexNoise.SampleGrad(HexNoiseSampler, flowB, gradAx * 2.4, gradAy * 2.4).r;
// still noise in the world for the patches of foam, so they do not all slide at once
float still = HexNoise.SampleGrad(HexNoiseSampler, WorldPos.xy * scale * 0.9, PosDdx.xy * scale * 0.9, PosDdy.xy * scale * 0.9).r;

// deeper and darker in the middle, clear and green at the banks, as the sea from open water to lagoon
float3 color = lerp(RiverDeepColor.rgb, RiverBankWaterColor.rgb, smoothstep(0.15, 0.95, across));
color *= 0.92 + (noiseA - 0.5) * 0.25 + (noiseB - 0.5) * 0.15;
// streaks along the flow catch the sky
color += smoothstep(0.62, 0.8, noiseB) * float3(0.04, 0.07, 0.08) * (1.0 - across);

// white water down a step, foam where the river meets the sea, and a thin line along the banks
float foamNoise = noiseA * 0.55 + noiseB * 0.45;
float foam = saturate(rapids * 1.5 - 0.15) * smoothstep(0.52 - rapids * 0.25, 0.66 - rapids * 0.2, foamNoise);
foam = max(foam, mouth * smoothstep(0.45, 0.62, foamNoise * 0.7 + still * 0.3) * RiverMouthFoam);
foam = max(foam, smoothstep(0.78, 0.95, across) * smoothstep(0.45, 0.6, noiseB) * RiverBankFoam);
color = lerp(color, FoamColor.rgb, foam);

// the ribbon is exactly as wide as the river, so its edge is the edge of the water, soft over about a pixel
float edgeSoftness = max((abs(UvDdx.x) + abs(UvDdy.x)) * 1.5, 0.03);
OutOpacity = saturate((lerp(RiverOpacity, RiverEdgeOpacity, smoothstep(0.5, 1.0, across)) + foam * 0.3) * (1.0 - smoothstep(1.0 - edgeSoftness, 1.0, across)) * saturate(FlowFade));

// for debugging it hides with the sea in the hex under the cursor (WaterHideScope 1) or also in its neighbors (2), finding its hex as GRID does
float2 hideP = float2(WorldPos.y, -WorldPos.x) / HexRadius;
float hq = 0.57735027 * hideP.x - hideP.y / 3.0;
float hr = hideP.y * 2.0 / 3.0;
float hs = -hq - hr;
float rq = round(hq);
float rr = round(hr);
float rs = round(hs);
float dq = abs(rq - hq);
float dr = abs(rr - hr);
float ds = abs(rs - hs);
if (dq > dr && dq > ds) { rq = -rr - rs; } else if (dr > ds) { rr = -rq - rs; }
float hideQ = rq - WaterHideHexQ;
float hideR = rr - WaterHideHexR;
OutOpacity *= (abs(hideQ) + abs(hideR) + abs(hideQ + hideR)) * 0.5 < WaterHideScope - 0.5 ? 0.0 : 1.0;
return color;
'''

# deep water is fully opaque all over its hex, which hides where the terrain ends, and shallow water reaches that at the side between them.
# Distance to the hexes of deep water gives a smooth edge, where blending by the centers of hexes would show their triangles.
# Unlit, as translucent surfaces get little light and water should keep its color
WATER_CODE = '''
float2 p = float2(WorldPos.y, -WorldPos.x) / HexRadius;
float fq = 0.57735027 * p.x - p.y / 3.0;
float fr = p.y * 2.0 / 3.0;
float2 fl = floor(float2(fq, fr));
float u = fq - fl.x;
float v = fr - fl.y;
int2 hexes[3];
float weights[3];
if (u + v < 1.0)
{
    hexes[0] = int2(fl) + int2(0, 0); hexes[1] = int2(fl) + int2(1, 0); hexes[2] = int2(fl) + int2(0, 1);
    weights[0] = 1.0 - u - v; weights[1] = u; weights[2] = v;
}
else
{
    hexes[0] = int2(fl) + int2(1, 1); hexes[1] = int2(fl) + int2(0, 1); hexes[2] = int2(fl) + int2(1, 0);
    weights[0] = u + v - 1.0; weights[1] = 1.0 - u; weights[2] = 1.0 - v;
}
''' + GRID + '''
uint mapWidth, mapHeight;
HexData.GetDimensions(mapWidth, mapHeight);
float clearness = 0;
for (int i = 0; i < 3; ++i)
{
    int row = clamp(hexes[i].y, 0, int(mapHeight) - 1);
    int column = clamp(hexes[i].x + ((hexes[i].y - (hexes[i].y & 1)) / 2), 0, int(mapWidth) - 1);
    float4 hexData = HexData.Load(int3(column, row, 0));
    float level = round(hexData.g * 255.0) - 128.0;
    float isFrozen = (level < 0.0 && hexData.a > 0.999) ? 1.0 : 0.0;
    clearness += weights[i] * (level < 0.0 ? (isFrozen > 0.0 ? IceOpacity : hexData.a * 255.0 / 254.0) : ShoreOpacity);
}
float2 uv = WorldPos.xy / TileSize;
float2 uvDx = PosDdx.xy / TileSize;
float2 uvDy = PosDdy.xy / TileSize;
float4 snow = Colors.SampleGrad(ColorsSampler, float3(uv, 3.0), uvDx, uvDy);
float snowNoise = HexNoise.SampleGrad(HexNoiseSampler, uv, uvDx, uvDy).r;

// the hexes are looked up at a wandering position and with round corners, so the edges of deep water and ice do not show their outline
float2 wander = float2(HexNoise.SampleGrad(HexNoiseSampler, uv * 0.19, uvDx * 0.19, uvDy * 0.19).r, HexNoise.SampleGrad(HexNoiseSampler, uv * 0.19 + 0.37, uvDx * 0.19, uvDy * 0.19).r) - 0.5;
float2 wandered = p + wander * 1.1;
float frozenDistance = 100000.0;
float openDistance = 100000.0;
float realOpenDistance = 100000.0;
int2 around[7] = { int2(0, 0), int2(1, -1), int2(1, 0), int2(0, 1), int2(-1, 1), int2(-1, 0), int2(0, -1) };
for (int n = 0; n < 7; ++n)
{
    int2 other = int2(rq, rr) + around[n];
    int otherRow = clamp(other.y, 0, int(mapHeight) - 1);
    int otherColumn = clamp(other.x + ((other.y - (other.y & 1)) / 2), 0, int(mapWidth) - 1);
    float4 otherData = HexData.Load(int3(otherColumn, otherRow, 0));
    float otherLevel = round(otherData.g * 255.0) - 128.0;
    float2 toHex = wandered - float2(1.7320508 * (other.x + other.y * 0.5), 1.5 * other.y);
    float size = max(abs(toHex.x), max(abs(toHex.x * 0.5 + toHex.y * 0.8660254), abs(toHex.x * 0.5 - toHex.y * 0.8660254)));
    float round_size = lerp(size, length(toHex) * 0.9, 0.5);
    if (otherLevel < 0.0 && otherData.a > 0.999)
    {
        frozenDistance = min(frozenDistance, (round_size - 0.8660254) * HexRadius);
    }
    if (!(otherLevel < 0.0 && otherData.a > 0.999))
    {
        realOpenDistance = min(realOpenDistance, (round_size - 0.8660254) * HexRadius);
    }
    // some hexes of ice crumble whole, which breaks up the line of a long straight edge
    if (!(otherLevel < 0.0 && otherData.a > 0.999) || frac(sin(dot(float2(other), float2(12.9898, 78.233))) * 43758.547) < IceCrumble)
    {
        openDistance = min(openDistance, (round_size - 0.8660254) * HexRadius);
    }
}
// round corners leave gaps where three hexes meet, which this closes
frozenDistance -= 14.0;

// the distance to the real coast, see FHexShoreField. Waves need no map of currents, as they run along the lines of equal distance towards the land
float4 shore = ShoreField.SampleLevel(ShoreFieldSampler, (WorldPos.xy - float2(ShoreMinX, ShoreMinY)) / float2(ShoreSizeX, ShoreSizeY), 0);
float shoreDistance = shore.r * 200.0;
float softDistance = shore.g * 400.0;
float cliffNear = shore.b;
float2 shoreStep = 30.0 / float2(ShoreSizeX, ShoreSizeY);
float2 shoreUV = (WorldPos.xy - float2(ShoreMinX, ShoreMinY)) / float2(ShoreSizeX, ShoreSizeY);
float2 awayFromLand = float2(ShoreField.SampleLevel(ShoreFieldSampler, shoreUV + float2(shoreStep.x, 0.0), 0).g - shore.g, ShoreField.SampleLevel(ShoreFieldSampler, shoreUV + float2(0.0, shoreStep.y), 0).g - shore.g);
awayFromLand /= max(length(awayFromLand), 0.0001);
// over shallows the water is calmer than in the open sea, with smaller waves and less foam, while the swash at the edge stays.
// WATER_OFFSET_CODE lowers its waves by the same amount
float shallowCalm = 1.0 - smoothstep(CalmDepthStart, CalmDepthEnd, shore.a * 100.0);

float noiseWide = HexNoise.SampleGrad(HexNoiseSampler, uv * WideNoiseScale + Time * float2(0.0011, 0.0007), uvDx * WideNoiseScale, uvDy * WideNoiseScale).r;
float noiseMiddle = HexNoise.SampleGrad(HexNoiseSampler, uv * MiddleNoiseScale - Time * float2(0.0016, 0.0009), uvDx * MiddleNoiseScale, uvDy * MiddleNoiseScale).r;
// long swells from one side, bent by noise so that they never look like straight lines
float2 swellPos = WorldPos.xy + (float2(noiseWide, noiseMiddle) - 0.5) * SwellBend;
float2 swellA = normalize(float2(0.8, 0.6));
float2 swellB = normalize(float2(0.45, 0.9));
float2 swellC = normalize(float2(0.95, -0.3));
float2 slope = swellA * cos(dot(swellPos, swellA) * SwellWaveA.x - Time * SwellWaveA.y) * SwellWaveA.z
    + swellB * cos(dot(swellPos, swellB) * SwellWaveB.x - Time * SwellWaveB.y) * SwellWaveB.z
    + swellC * cos(dot(swellPos, swellC) * SwellWaveC.x - Time * SwellWaveC.y) * SwellWaveC.z;

// from close by shorter waves show too, and the water moves more
float closeUp = 1.0 - smoothstep(CloseUpStart, max(CloseUpEnd, CloseUpStart + 0.01), aa);
float2 chopA = normalize(float2(-0.5, 0.85));
float2 chopB = normalize(float2(0.9, 0.4));
slope += (chopA * cos(dot(swellPos, chopA) * ChopWaveA.x - Time * ChopWaveA.y) * ChopWaveA.z + chopB * cos(dot(swellPos, chopB) * ChopWaveB.x - Time * ChopWaveB.y) * ChopWaveB.z) * closeUp;
slope *= lerp(1.0, ShallowCalm.x, shallowCalm);
// everything seen on the water sways with the swell, which is what makes it look afloat from close by
float2 swayUV = uv + slope * WaterSway;
float lace = HexNoise.SampleGrad(HexNoiseSampler, swayUV * LaceScale + Time * float2(0.006, -0.004), uvDx * LaceScale, uvDy * LaceScale).r;
float laceFine = HexNoise.SampleGrad(HexNoiseSampler, swayUV * LaceScale * 2.04 - Time * float2(0.009, 0.005), uvDx * LaceScale * 2.04, uvDy * LaceScale * 2.04).r;
lace = lace * 0.65 + laceFine * 0.35;

// each wave of the shore comes at its own time along the coast, and breaks in its own places
// waves live within a hex or two of the shore, and grow as they come in
float waveReach = 1.0 - smoothstep(WaveReachStart, WaveReachEnd, softDistance);
waveReach *= waveReach;
// the blurred distance never reaches zero, so a wave uses the sharp one as it comes to the shore
float waveDistance = lerp(shoreDistance, softDistance, smoothstep(0.0, 60.0, shoreDistance));
float wavePhase = waveDistance / WaveLength + Time / WavePeriod + noiseWide * 3.0 + noiseMiddle * 0.8;
float waveIndex = floor(wavePhase);
float wave = wavePhase - waveIndex;
slope += awayFromLand * waveReach * (wave < 0.3 ? -1.0 : 0.35) * smoothstep(0.0, 0.05, wave) * (1.0 - smoothstep(0.3, 1.0, wave)) * WaveSlope * lerp(1.0, 0.35, shallowCalm);

float2 rippleA = Normals.SampleGrad(NormalsSampler, float3(swayUV * RippleScaleA + float2(Time * 0.012, Time * 0.007), 1.0), uvDx * RippleScaleA, uvDy * RippleScaleA).xy - 0.5;
float2 rippleB = Normals.SampleGrad(NormalsSampler, float3(swayUV * RippleScaleB - float2(Time * 0.008, Time * 0.01), 1.0), uvDx * RippleScaleB, uvDy * RippleScaleB).xy - 0.5;
float2 ripple = (rippleA + rippleB) * lerp(1.0, ShallowCalm.y, shallowCalm);
float3 waterNormal = normalize(float3(slope * lerp(NormalStrength.x, NormalStrength.y, closeUp) + ripple * lerp(NormalStrength.z, NormalStrength.w, closeUp), 1.0));

// from the shore: clear lagoon, then turquoise, then the blue of the open sea, whose tone drifts in wide patches
// the water grows opaque with the depth of the seabed under it, so land is seen going on down under it, and a little unevenly by place.
// The depth is blurred, so nothing shows its hexes
float fadeNoise = HexNoise.SampleGrad(HexNoiseSampler, uv * FadeNoiseScale, uvDx * FadeNoiseScale, uvDy * FadeNoiseScale).r;
float seabedDepth = shore.a * 100.0 + (fadeNoise - 0.5) * SeabedDepthNoise;
float depthAmount = smoothstep(WaterClearDepth, WaterOpaqueDepth, seabedDepth);
float opacity = lerp(clearness, 1.0, depthAmount);
float3 shallow = lerp(LagoonColor.rgb, ShallowColor.rgb, smoothstep(LagoonDistanceStart, LagoonDistanceEnd, shoreDistance));
float patches = saturate((noiseWide - 0.5) * PatchContrast + 0.5);
float patchesFine = saturate((noiseMiddle - 0.5) * PatchFineContrast + 0.5);
float3 deepColor = lerp(DeepColorDark.rgb, DeepColorLight.rgb, lerp(patches, patchesFine, PatchFineWeight));
deepColor = lerp(OffshoreColor.rgb, deepColor, smoothstep(OffshoreDistanceStart, OffshoreDistanceEnd, softDistance));
float3 color = lerp(shallow, deepColor, depthAmount);

// the sky shows on what faces away from the camera, and the side of a wave towards the sun is lighter
float3 toCamera = normalize(CameraDir);
// the sun of the level, which AHexWorldMap gives through SunDirection
float3 sunDirection = normalize(SunDirection);
float fresnel = pow(1.0 - saturate(dot(waterNormal, toCamera)), FresnelPower);
color *= 1.0 + dot(waterNormal.xy, sunDirection.xy) * WaveLight;
color = lerp(color, SkyColor.rgb, fresnel * SkyReflection);
// glints of the sun: sparkles that live on the water, drift with it and last for seconds, so they do not flicker when the camera moves.
// The camera only decides where the path of the sun lies, which slides smoothly over them and over the swell
// only the long swell, as finer waves would make the path flicker with the camera
float3 reflected = reflect(-toCamera, normalize(float3(slope * NormalStrength.x, 1.0)));
float sunPath = lerp(SunPathMin, 1.0, smoothstep(SunPathStart, SunPathEnd, dot(reflected, sunDirection)));
float sparkleA = HexNoise.SampleGrad(HexNoiseSampler, swayUV * GlintScaleA + Time * float2(0.02, 0.011), uvDx * GlintScaleA * 2.0, uvDy * GlintScaleA * 2.0).r;
float sparkleB = HexNoise.SampleGrad(HexNoiseSampler, swayUV * GlintScaleB - Time * float2(0.014, 0.022), uvDx * GlintScaleB * 2.0, uvDy * GlintScaleB * 2.0).r;
// from far away each would be one pixel, which only looks like noise
color += smoothstep(GlintThresholdStart, GlintThresholdEnd, sparkleA * sparkleB) * sunPath * (1.0 - smoothstep(GlintFarStart, GlintFarEnd, aa)) * GlintColor.rgb * GlintAmount * lerp(1.0, ShallowCalm.z, shallowCalm);

// a wave is born somewhere half a hex to a hex out, its own place for each wave, and its foam rolls in from there to the shore
float2 waveCell = uv * 0.6 + frac(waveIndex * 0.37) * 3.0;
float breaking = HexNoise.SampleGrad(HexNoiseSampler, waveCell, uvDx * 0.6, uvDy * 0.6).r;
float birthNoise = HexNoise.SampleGrad(HexNoiseSampler, uv * 0.27 + frac(waveIndex * 0.61) * 5.0, uvDx * 0.27, uvDy * 0.27).r;
float birthSpread = saturate((birthNoise - 0.3) * 2.2);
// most waves only show close in, and few break as far as a hex out
float birth = lerp(WaveBirthNear, WaveBirthFar, birthSpread * birthSpread * birthSpread);
float born = 1.0 - smoothstep(birth - 45.0, birth, waveDistance);
float nearShore = 1.0 - smoothstep(10.0, 120.0, waveDistance);
float crestWidth = lerp(CrestWidthFar, CrestWidthNear, nearShore);
float crest = smoothstep(0.0, 0.05, wave) * (1.0 - smoothstep(crestWidth, crestWidth + 0.1, wave));
// each wave has foam only in patches one to three hexes wide, new ones for every wave, and nothing between them
float foamPatch = smoothstep(0.5, 0.62, HexNoise.SampleGrad(HexNoiseSampler, uv * 0.2 + frac(waveIndex * 0.29) * 7.0, uvDx * 0.2, uvDy * 0.2).r);
float foamMask = smoothstep(0.62 - nearShore * 0.35, 0.42 - nearShore * 0.35, 1.0 - breaking) * foamPatch;
float waveFoam = crest * born * foamMask * smoothstep(0.3, 0.62, lace) * WaveFoamAmount;
// what a wave leaves behind stays for a while as thin lace
waveFoam = max(waveFoam, (1.0 - smoothstep(0.2, 0.7, wave)) * born * nearShore * foamMask * smoothstep(0.76, 0.9, lace) * WaveLaceAmount);
waveFoam *= lerp(1.0, ShallowFoamCalm, shallowCalm);

// the line along the water's edge comes with each wave that reaches it
float surf = (1.0 - smoothstep(1.0, SurfWidth, shoreDistance)) * (1.0 - smoothstep(0.0, 0.55, wave));
float surfFoam = surf * smoothstep(0.38, 0.62, lace + surf * 0.2) * foamPatch * SurfAmount * lerp(1.0, ShallowSurfCalm, shallowCalm);

// water breaks against a cliff in layers that move against each other.
// A surge rises fast and falls slowly in a patch a few hexes wide, and some layers run off sooner than others. M_HexCliff and WATER_OFFSET_CODE use the same one
SURGE_HERE
float surge = surgeSlow;
float cliffFoam = 0.0;
float foamRelief = 1.0;
float cliffAmount = smoothstep(0.1, 0.6, cliffNear);
// how far below the surface lies what the water touches. It comes from the depth of the scene behind the water, turned into a depth below the surface,
// so it does not change much with the camera angle. Where the seabed is missing nothing lies close behind
float contactDepth = max(BehindDepth - SurfaceDepth, 0.0) * saturate(toCamera.z);
// the foot of a mountain stands out in the water further than the shore field knows, and rock falls steeply, so how deep it lies tells how far it is
float rockDistance = min(shoreDistance, contactDepth * 1.5);
if (cliffAmount > 0.0)
{
    // foam carried towards the rock and foam thrown back from it. Two copies out of step hide the moment when each jumps back
    float flowA = frac(Time * CliffFoamFlowSpeed);
    float flowB = frac(Time * CliffFoamFlowSpeed + 0.5);
    float flowMix = abs(1.0 - 2.0 * flowA);
    float churnA = lerp(HexNoise.SampleGrad(HexNoiseSampler, uv * 3.1 + awayFromLand * flowA * 0.4, uvDx * 3.1, uvDy * 3.1).r,
        HexNoise.SampleGrad(HexNoiseSampler, uv * 3.1 + 0.37 + awayFromLand * flowB * 0.4, uvDx * 3.1, uvDy * 3.1).r, flowMix);
    float thrown = lerp(HexNoise.SampleGrad(HexNoiseSampler, uv * 2.3 + 0.61 - awayFromLand * flowA * 0.3, uvDx * 2.3, uvDy * 2.3).r,
        HexNoise.SampleGrad(HexNoiseSampler, uv * 2.3 + 0.13 - awayFromLand * flowB * 0.3, uvDx * 2.3, uvDy * 2.3).r, flowMix);
    // fronts that run in along the distance to the rock
    // noise changing fast along the coast would tilt them and they would seem to run sideways
    float frontWave = frac(rockDistance / CliffFoamFrontSpacing + Time * CliffFoamFlowSpeed + fadeNoise * 1.2);
    float front = smoothstep(0.0, 0.1, frontWave) * (1.0 - smoothstep(0.2, 0.55, frontWave));
    float reach = CliffFoamReach + (CliffFoamReachNoise + CliffFoamSurgeReach * surge) * (0.5 + 0.5 * noiseMiddle);
    float zone = 1.0 - smoothstep(reach * 0.35, reach, rockDistance);
    float inner = 1.0 - smoothstep(3.0, 9.0 + 10.0 * surge, rockDistance);
    cliffFoam = cliffAmount * max(max(inner * smoothstep(0.42, 0.62, churnA), zone * front * (0.4 + 0.6 * surgeQuick) * smoothstep(0.34, 0.56, churnA)), zone * smoothstep(0.5, 0.68, thrown) * (0.35 + 0.65 * surge));
    // where the layers pile up the foam stands higher and catches more light
    foamRelief = 0.76 + 0.55 * saturate(churnA * 0.6 + thrown * 0.6 - 0.25);
}

// now and then a single wave rolls in from the middle of a hex to the shore, one or two hexes long, somewhere new each time
float rollTime = Time / RollerPeriod + HexNoise.SampleGrad(HexNoiseSampler, uv * 0.03, uvDx * 0.03, uvDy * 0.03).r;
float rollAge = frac(rollTime);
float rollFront = RollerDistance * (1.0 - saturate(rollAge / 0.6));
float rollBehind = waveDistance - rollFront;
float rollBand = rollBehind > 0.0 ? 1.0 - smoothstep(0.0, RollerWidth, rollBehind) : smoothstep(-3.0, 0.0, rollBehind);
float rollPatch = smoothstep(RollerRarity, RollerRarity + 0.08, HexNoise.SampleGrad(HexNoiseSampler, uv * 0.26 + frac(floor(rollTime) * 0.47) * 9.0, uvDx * 0.26, uvDy * 0.26).r);
float rollFade = smoothstep(0.0, 0.06, rollAge) * (1.0 - smoothstep(0.55, 0.72, rollAge));
float rollFoam = rollBand * rollPatch * rollFade * smoothstep(0.28, 0.55, lace) * (1.0 - cliffAmount) * RollerAmount * lerp(1.0, ShallowFoamCalm, shallowCalm);

// what always lies where the water touches anything, the ground, a cliff or a mountain, under what the waves bring: a soft film of foam that washes
// in quickly and back slowly, each stretch of coast at its own time, with a brighter rim at its front. It is never gone, only clearer in some stretches
// and fainter in others, which slowly change
float edgeMood = HexNoise.SampleGrad(HexNoiseSampler, uv * 0.13 + Time * float2(0.004, -0.0025), uvDx * 0.13, uvDy * 0.13).r;
float edgeStrength = lerp(EdgeFoamMinStrength, 1.0, smoothstep(0.35, 0.62, edgeMood));
// how far this is from the edge across the water, from how deep the ground lies here and how fast it falls, which comes from ShoreHeight
// and so is the same from any side and on any coast. The filtering of that unblurred height finds the edge between its pixels.
// It repeats HeightRange of FHexShoreField
uint heightWidth, heightHeight;
ShoreHeight.GetDimensions(heightWidth, heightHeight);
float2 heightTexel = 1.0 / float2(heightWidth, heightHeight);
float2 heightStep = float2(ShoreSizeX, ShoreSizeY) * heightTexel;
float groundHere = (ShoreHeight.SampleLevel(ShoreHeightSampler, shoreUV, 0).r - 0.5) * 48.0;
float2 groundSlope = float2(ShoreHeight.SampleLevel(ShoreHeightSampler, shoreUV + float2(heightTexel.x, 0.0), 0).r - ShoreHeight.SampleLevel(ShoreHeightSampler, shoreUV - float2(heightTexel.x, 0.0), 0).r,
    ShoreHeight.SampleLevel(ShoreHeightSampler, shoreUV + float2(0.0, heightTexel.y), 0).r - ShoreHeight.SampleLevel(ShoreHeightSampler, shoreUV - float2(0.0, heightTexel.y), 0).r) * 48.0 / (2.0 * heightStep);
// flat shallows count as far from the edge, however shallow they are
float edgeDistance = max(-groundHere, 0.0) / max(length(groundSlope), 0.05);
// a film thinner than a pixel would flicker, so from far away it grows
float edgeWidth = WaterEdgeFoamWidth * lerp(EdgeFoamMinStrength, 1.0, edgeStrength) + aa;
// each swash runs in to the edge as a bright rim with broken foam trailing behind it, then goes back as a thin sheet that spreads out from the edge
// and thins into streaks. Three of them a third of a cycle apart, so one always goes back while the next comes in, and the edge is never bare.
// Each reaches further or less and has lace of its own
float swashFoam = 0.0;
for (int s = 0; s < 3; ++s)
{
    // slow enough that the edge breathes instead of churning
    float swashTime = Time / SwashPeriod + noiseWide * 2.0 + edgeMood * 1.5 + s / 3.0;
    float swashAge = frac(swashTime);
    float swashCycle = floor(swashTime) * 3.0 + s;
    float swashReach = edgeWidth * lerp(SwashMinReach, 1.0, frac(swashCycle * 0.618));
    float runFront = swashReach * (1.0 - smoothstep(0.0, 0.35, swashAge));
    float backFront = swashReach * 0.85 * smoothstep(0.3, 1.0, swashAge);
    // the lace goes with the water, in with the rim and back out with the sheet
    float carried = swashAge < 0.35 ? runFront - swashReach : backFront - swashReach;
    // wide soft patches, sampled blurred, which drift only part of the way the water goes, so the film is a smooth sheet rather than boiling veins
    float2 swashUV = (uv + awayFromLand * carried * 0.5 / TileSize) * 1.6 + Time * float2(0.002, -0.0013) + frac(swashCycle * float2(0.37, 0.71)) * 4.0;
    float swashLace = smoothstep(0.35, 0.65, HexNoise.SampleGrad(HexNoiseSampler, swashUV, uvDx * 1.6 * 8.0, uvDy * 1.6 * 8.0).r);
    float running = smoothstep(0.0, 0.06, swashAge) * (1.0 - smoothstep(0.3, 0.42, swashAge));
    // a soft rim, as a thin one would trace every small wobble of the ground
    float runRim = (1.0 - smoothstep(0.0, edgeWidth * 0.18, abs(edgeDistance - runFront))) * lerp(0.5, 0.7, swashLace);
    float runTrail = smoothstep(runFront - 1.0, runFront, edgeDistance) * (1.0 - smoothstep(runFront, runFront + swashReach * 0.4, edgeDistance)) * lerp(0.3, 0.4, swashLace);
    float backSheet = (1.0 - smoothstep(backFront * 0.55, backFront + 2.0, edgeDistance)) * lerp(0.35, 0.5, swashLace)
        * smoothstep(0.3, 0.4, swashAge) * (1.0 - smoothstep(0.72, 1.0, swashAge));
    swashFoam = max(swashFoam, max(max(runRim, runTrail) * running, backSheet));
}
// right where it touches, the edge stays a little brighter
float edgeTouch = (1.0 - smoothstep(edgeWidth * 0.04, edgeWidth * 0.12, edgeDistance)) * EdgeTouchAmount;
float edgeFoam = max(swashFoam, edgeTouch) * edgeStrength * WaterEdgeFoam;

float foam = saturate(max(max(max(max(waveFoam, surfFoam), cliffFoam), rollFoam), edgeFoam));
// the film at the edge is smooth, and only the foam of waves shows the veins of the lace
float foamVeins = lerp(0.86 + lace * 0.28, 1.0, saturate(edgeFoam / max(foam, 0.001)));
color = lerp(color, FoamColor.rgb * foamVeins * foamRelief, foam);
// the water fades out right at its edge, where its depth comes to nothing
opacity *= smoothstep(0.0, 4.0, edgeDistance);
opacity = max(opacity, foam);

// small waves run in to the ice and leave a thin line of foam on it
if (frozenDistance > -6.0 && frozenDistance < IceWaveReach)
{
    // long enough that one or at most two fit on the way in, and each comes only in places of its own
    float icePhase = frozenDistance / IceWaveSpacing + Time / IceWavePeriod + fadeNoise * 1.5;
    float iceWave = frac(icePhase);
    float iceGate = smoothstep(0.5, 0.64, HexNoise.SampleGrad(HexNoiseSampler, uv * 0.43 + frac(floor(icePhase) * 0.41) * 6.0, uvDx * 0.43, uvDy * 0.43).r);
    float iceCrest = smoothstep(0.0, 0.06, iceWave) * (1.0 - smoothstep(0.08, 0.22, iceWave)) * iceGate;
    float iceReach = 1.0 - smoothstep(6.0, IceWaveReach, frozenDistance);
    color += iceCrest * iceReach * float3(0.05, 0.09, 0.1);
    float iceFoam = iceCrest * iceReach * iceReach * smoothstep(0.42, 0.66, lace) * IceFoamAmount;
    color = lerp(color, FoamColor.rgb, iceFoam);
    opacity = max(opacity, iceFoam);
}

float iceAmount = 1.0 - smoothstep(-3.0, 3.0, frozenDistance + (snowNoise - 0.5) * 16.0);
float3 iceColor = lerp(IceColor.rgb, snow.rgb * 0.62, smoothstep(0.25, 0.5, snowNoise));
float3 iceBaseTone = iceColor;
// near its edge ice is broken into floes that jostle, closed up further in and scattered further out
// how far into the ice this is from open water, as the sides between two hexes of ice are not an edge. Noise makes the width of the broken part differ by place
float plateMix = 0.0;
float iceWobble = (fadeNoise - 0.5) * 110.0 + (HexNoise.SampleGrad(HexNoiseSampler, uv * 0.31 + 0.17, uvDx * 0.31, uvDy * 0.31).r - 0.5) * 70.0;
float iceDepth = (frozenDistance < 0.0 ? -(openDistance + 14.0) : frozenDistance) + iceWobble;
if (iceDepth > -130.0 && iceDepth < 260.0)
{
    // noise bends the straight sides that such cells would have. Each floe is its own cell moved as a whole, so it keeps its shape as it floats on the swell
    float2 floePos = (WorldPos.xy + (snowNoise - 0.5) * 16.0 + wander * 30.0) / FloeSize;
    float2 floeCell = floor(floePos);
    // only near real water do floes float apart. Further in a broken hex is hard plates of ice, so it never looks open
    float realDepth = (frozenDistance < 0.0 ? -(realOpenDistance + 14.0) : frozenDistance) + iceWobble * 0.5;
    float nearWater = smoothstep(-75.0, -35.0, realDepth);
    float floeFreedom = smoothstep(-95.0, -35.0, iceDepth) * nearWater;
    float2 restPlaces[25];
    float2 restHashes[25];
    for (int cy = 0; cy < 5; ++cy)
    {
        for (int cx = 0; cx < 5; ++cx)
        {
            float2 cell = floeCell + float2(cx - 2, cy - 2);
            float2 hash = frac(sin(float2(dot(cell, float2(127.1, 311.7)), dot(cell, float2(269.5, 183.3)))) * 43758.547);
            restHashes[cy * 5 + cx] = hash;
            restPlaces[cy * 5 + cx] = cell + 0.15 + hash * 0.7;
        }
    }
    float floeMargin = -10.0;
    float2 floeCenter = 0;
    float2 floeHash = 0;
    for (int fy = 1; fy <= 3; ++fy)
    {
        for (int fx = 1; fx <= 3; ++fx)
        {
            int own = fy * 5 + fx;
            float2 rest = restPlaces[own];
            float swing = dot(rest * FloeSize, swellA) * SwellWaveA.x - Time * SwellWaveA.y + restHashes[own].x * 1.5;
            float2 drift = swellA * sin(swing) * 0.1 + float2(-swellA.y, swellA.x) * cos(Time * 0.37 + restHashes[own].y * 6.28) * 0.05;
            // where this place would be in the floe if it had not moved, and how far from its side that is
            float2 restPos = floePos - drift * floeFreedom;
            float ownDistance = length(restPos - rest);
            float nearestOther = 10.0;
            for (int ny = -1; ny <= 1; ++ny)
            {
                for (int nx = -1; nx <= 1; ++nx)
                {
                    if (nx != 0 || ny != 0)
                    {
                        nearestOther = min(nearestOther, length(restPos - restPlaces[(fy + ny) * 5 + fx + nx]));
                    }
                }
            }
            if (nearestOther - ownDistance > floeMargin)
            {
                floeMargin = nearestOther - ownDistance;
                floeCenter = rest;
                floeHash = restHashes[own];
            }
        }
    }
    // a floe is whole or gone for good, so what counts is the distance to the ice at the place where it rests
    float2 floeP = float2(floeCenter.y, -floeCenter.x) * FloeSize / HexRadius;
    float floeIce = 100000.0;
    float floeOpen = 100000.0;
    for (int n = 0; n < 7; ++n)
    {
        int2 other = int2(rq, rr) + around[n];
        int otherRow = clamp(other.y, 0, int(mapHeight) - 1);
        int otherColumn = clamp(other.x + ((other.y - (other.y & 1)) / 2), 0, int(mapWidth) - 1);
        float4 otherData = HexData.Load(int3(otherColumn, otherRow, 0));
        float2 toHex = floeP - float2(1.7320508 * (other.x + other.y * 0.5), 1.5 * other.y);
        float size = max(abs(toHex.x), max(abs(toHex.x * 0.5 + toHex.y * 0.8660254), abs(toHex.x * 0.5 - toHex.y * 0.8660254)));
        float toSide = (lerp(size, length(toHex) * 0.9, 0.5) - 0.8660254) * HexRadius;
        if (otherData.g < 0.5 && otherData.a > 0.999)
        {
            floeIce = min(floeIce, toSide - 14.0);
        }
        if (!(otherData.g < 0.5 && otherData.a > 0.999) || frac(sin(dot(float2(other), float2(12.9898, 78.233))) * 43758.547) < IceCrumble)
        {
            floeOpen = min(floeOpen, toSide);
        }
    }
    floeIce = (floeIce < 0.0 ? -(floeOpen + 14.0) : floeIce) + iceWobble;
    float loose = smoothstep(-95.0, -35.0, floeIce);
    float floeKeeps = floeIce < lerp(-10.0, 90.0, floeHash.x * floeHash.x * floeHash.y) ? 1.0 : 0.0;
    // the gaps leave room for the floes to move in
float gap = 1.0 - smoothstep(0.02 + 0.16 * floeFreedom, 0.06 + 0.16 * floeFreedom, floeMargin);
    float floeShows = floeKeeps * (1.0 - gap);
    iceAmount = lerp(1.0, max(floeShows, 1.0 - nearWater), loose);
    plateMix = smoothstep(-95.0, -35.0, iceDepth) * (1.0 - nearWater);
    // each floe has its own tone and a lighter rim
    // a floe lifts and tips as the swell goes under it, all of it at once
    iceColor *= 1.0 + loose * ((floeHash.x - 0.5) * FloeToneVariation + sin(dot(floeCenter * FloeSize, swellA) * SwellWaveA.x - Time * SwellWaveA.y + floeHash.x * 1.5) * FloeTilt);
    iceColor = lerp(iceColor, iceColor * (1.0 + FloeRimLight), loose * (1.0 - smoothstep(0.1 + 0.16 * floeFreedom, 0.2 + 0.16 * floeFreedom, floeMargin)));
}
// the surface of the ice: blue where bare and white under heaps of snow, with cracks that ignore the hexes. Relief comes from the slope of the heaps and the normals of snow
float iceBare = saturate((HexNoise.SampleGrad(HexNoiseSampler, uv * 0.17 + 0.23, uvDx * 0.17, uvDy * 0.17).r - 0.3) * 2.5);
float heapA = HexNoise.SampleGrad(HexNoiseSampler, uv * 0.43 + 0.11, uvDx * 0.43, uvDy * 0.43).r;
float heapB = HexNoise.SampleGrad(HexNoiseSampler, uv * 1.4 + 0.47, uvDx * 1.4, uvDy * 1.4).r;
float2 heapSlope = float2(HexNoise.SampleGrad(HexNoiseSampler, uv * 0.43 + 0.11 + float2(0.012, 0.0), uvDx * 0.43, uvDy * 0.43).r, HexNoise.SampleGrad(HexNoiseSampler, uv * 0.43 + 0.11 + float2(0.0, 0.012), uvDx * 0.43, uvDy * 0.43).r) - heapA;
float heap = smoothstep(0.48, 0.7, heapA * 0.7 + heapB * 0.3);
float3 iceBase = lerp(IceBareColor.rgb, IceClearColor.rgb, iceBare);
float3 iceSurface = lerp(iceBase, IceSnowColor.rgb, heap) * (0.97 + snowNoise * 0.06);
float2 iceDetail = Normals.SampleGrad(NormalsSampler, float3(uv * 1.3, 3.0), uvDx * 1.3, uvDy * 1.3).xy - 0.5;
iceSurface *= 1.0 + clamp(dot(heapSlope * 2.0 + iceDetail * 0.2, sunDirection.xy), -IceRelief, IceRelief);

float2 crackPos = (WorldPos.xy + wander * 50.0 + (snowNoise - 0.5) * 10.0) / CrackSize;
float2 crackCell = floor(crackPos);
float crackNear = 10.0;
float crackSecond = 10.0;
for (int cy = -1; cy <= 1; ++cy)
{
    for (int cx = -1; cx <= 1; ++cx)
    {
        float2 cell = crackCell + float2(cx, cy);
        float2 hash = frac(sin(float2(dot(cell, float2(41.3, 289.1)), dot(cell, float2(157.7, 73.9)))) * 43758.547);
        float crackDistance = length(crackPos - (cell + 0.1 + hash * 0.8));
        if (crackDistance < crackNear)
        {
            crackSecond = crackNear;
            crackNear = crackDistance;
        }
        else if (crackDistance < crackSecond)
        {
            crackSecond = crackDistance;
        }
    }
}
// only some sides of the cells are cracks, under snow they do not show, and the broken edge has its own gaps
float crackGate = smoothstep(0.46, 0.6, HexNoise.SampleGrad(HexNoiseSampler, uv * 0.29 + 0.71, uvDx * 0.29, uvDy * 0.29).r) * (1.0 - heap * 0.85) * (1.0 - smoothstep(-70.0, -30.0, iceDepth));
float crackLine = crackSecond - crackNear;
iceSurface *= 1.0 + (1.0 - smoothstep(0.05, 0.16, crackLine)) * crackGate * 0.14;
// some cracks are dark and some let the turquoise of the water through
float3 crackColor = lerp(CrackDarkColor.rgb, CrackOpenColor.rgb, smoothstep(0.4, 0.6, iceBare));
iceSurface = lerp(iceSurface, crackColor, (1.0 - smoothstep(0.012, 0.04, crackLine)) * crackGate * CrackAmount);
iceColor = iceSurface * (dot(iceColor, float3(0.33, 0.33, 0.34)) / max(dot(iceBaseTone, float3(0.33, 0.33, 0.34)), 0.001) * 0.3 + 0.7);
if (plateMix > 0.0)
{
    // large hard plates, each with its own tone and its own direction of scratches, which do not move
    float2 platePos = (WorldPos.xy + wander * 40.0 + (snowNoise - 0.5) * 8.0) / PlateSize;
    float2 plateCell = floor(platePos);
    float plateNear = 10.0;
    float plateSecond = 10.0;
    float2 plateHash = 0;
    for (int py = -1; py <= 1; ++py)
    {
        for (int px = -1; px <= 1; ++px)
        {
            float2 cell = plateCell + float2(px, py);
            float2 hash = frac(sin(float2(dot(cell, float2(83.7, 217.3)), dot(cell, float2(191.9, 53.1)))) * 43758.547);
            float plateDistance = length(platePos - (cell + 0.1 + hash * 0.8));
            if (plateDistance < plateNear)
            {
                plateSecond = plateNear;
                plateNear = plateDistance;
                plateHash = hash;
            }
            else if (plateDistance < plateSecond)
            {
                plateSecond = plateDistance;
            }
        }
    }
    float2 scratchWay = float2(cos(plateHash.x * 6.28), sin(plateHash.x * 6.28));
    float2 scratchUV = float2(dot(uv, scratchWay) * 2.6, dot(uv, float2(-scratchWay.y, scratchWay.x)) * 0.5) + plateHash * 5.0;
    float2 scratch = Normals.SampleGrad(NormalsSampler, float3(scratchUV, 3.0), uvDx * 2.6, uvDy * 2.6).xy - 0.5;
    float3 plateColor = lerp(PlateColorDark.rgb, PlateColorLight.rgb, plateHash.y);
    plateColor *= 1.0 + clamp(dot(scratch, sunDirection.xy) * 1.2 + (plateHash.x - 0.5) * 0.1, -0.2, 0.2);
    float plateSeam = plateSecond - plateNear;
    plateColor *= 1.0 + (1.0 - smoothstep(0.05, 0.15, plateSeam)) * 0.1;
    plateColor = lerp(plateColor, PlateSeamColor.rgb, (1.0 - smoothstep(0.015, 0.045, plateSeam)) * 0.85);
    iceColor = lerp(iceColor, plateColor, plateMix);
}
color = lerp(color, iceColor, iceAmount);
OutOpacity = lerp(opacity, 1.0, iceAmount);
float waterGrid = gridLine * GridOpacity * GridStrength;
OutOpacity = lerp(OutOpacity, 1.0, waterGrid);
// the grid of the water follows the camera past the edge of the map
OutOpacity *= (all(shoreUV >= 0.0) && all(shoreUV <= 1.0)) ? 1.0 : 0.0;
// for debugging the water can be hidden in the hex under the cursor (WaterHideScope 1) or also in its neighbors (2)
float hideQ = rq - WaterHideHexQ;
float hideR = rr - WaterHideHexR;
float hideDistance = (abs(hideQ) + abs(hideR) + abs(hideQ + hideR)) * 0.5;
OutOpacity *= hideDistance < WaterHideScope - 0.5 ? 0.0 : 1.0;
return lerp(color, GridColor.rgb, waterGrid);
'''


# real height of the swell in WATER_CODE, with the same waves. It is lower at the shore, where it would move the edge of the water too far
WATER_OFFSET_CODE = '''
float2 uv = WorldPos.xy / TileSize;
float noiseWide = HexNoise.SampleLevel(HexNoiseSampler, uv * WideNoiseScale + Time * float2(0.0011, 0.0007), 2.0).r;
float noiseMiddle = HexNoise.SampleLevel(HexNoiseSampler, uv * MiddleNoiseScale - Time * float2(0.0016, 0.0009), 2.0).r;
float2 swellPos = WorldPos.xy + (float2(noiseWide, noiseMiddle) - 0.5) * SwellBend;
float2 swellA = normalize(float2(0.8, 0.6));
float2 swellB = normalize(float2(0.45, 0.9));
float2 swellC = normalize(float2(0.95, -0.3));
float2 chopA = normalize(float2(-0.5, 0.85));
float2 chopB = normalize(float2(0.9, 0.4));
float height = sin(dot(swellPos, swellA) * SwellWaveA.x - Time * SwellWaveA.y) * SwellWaveA.w
    + sin(dot(swellPos, swellB) * SwellWaveB.x - Time * SwellWaveB.y) * SwellWaveB.w
    + sin(dot(swellPos, swellC) * SwellWaveC.x - Time * SwellWaveC.y) * SwellWaveC.w;
float closeUp = 1.0 - smoothstep(ChopLiftStart, max(ChopLiftEnd, ChopLiftStart + 1.0), length(CameraPos - WorldPos));
height += (sin(dot(swellPos, chopA) * ChopWaveA.x - Time * ChopWaveA.y) * ChopWaveA.w + sin(dot(swellPos, chopB) * ChopWaveB.x - Time * ChopWaveB.y) * ChopWaveB.w) * closeUp;
// ice is rigid and does not move, and the water goes on close up to it
float2 p = float2(WorldPos.y, -WorldPos.x) / HexRadius;
float fq = 0.57735027 * p.x - p.y / 3.0;
float fr = p.y * 2.0 / 3.0;
float2 fl = floor(float2(fq, fr));
float u = fq - fl.x;
float v = fr - fl.y;
int2 hexes[3];
float weights[3];
if (u + v < 1.0)
{
    hexes[0] = int2(fl) + int2(0, 0); hexes[1] = int2(fl) + int2(1, 0); hexes[2] = int2(fl) + int2(0, 1);
    weights[0] = 1.0 - u - v; weights[1] = u; weights[2] = v;
}
else
{
    hexes[0] = int2(fl) + int2(1, 1); hexes[1] = int2(fl) + int2(0, 1); hexes[2] = int2(fl) + int2(1, 0);
    weights[0] = u + v - 1.0; weights[1] = 1.0 - u; weights[2] = 1.0 - v;
}
uint mapWidth, mapHeight;
HexData.GetDimensions(mapWidth, mapHeight);
float frozen = 0;
for (int i = 0; i < 3; ++i)
{
    int row = clamp(hexes[i].y, 0, int(mapHeight) - 1);
    int column = clamp(hexes[i].x + ((hexes[i].y - (hexes[i].y & 1)) / 2), 0, int(mapWidth) - 1);
    float4 hexData = HexData.Load(int3(column, row, 0));
    frozen += (hexData.g < 0.5 && hexData.a > 0.999) ? weights[i] : 0.0;
}
height *= 1.0 - smoothstep(0.22, 0.42, frozen);
float4 shore = ShoreField.SampleLevel(ShoreFieldSampler, (WorldPos.xy - float2(ShoreMinX, ShoreMinY)) / float2(ShoreSizeX, ShoreSizeY), 0);
// the same surge as the foam of WATER_CODE heaves the water up against a cliff
SURGE_HERE
float heave = surgeSlow * smoothstep(0.1, 0.6, shore.b) * SurgeHeave;
// the same calm over shallows as in WATER_CODE
height *= lerp(1.0, ShallowCalm.w, 1.0 - smoothstep(CalmDepthStart, CalmDepthEnd, shore.a * 100.0));
return float3(0.0, 0.0, (height * lerp(lerp(NearLandHeight, 1.0, smoothstep(0.0, NearLandDistance, shore.g * 400.0)), CliffWaveHeight, smoothstep(0.1, 0.6, shore.b)) + heave) * WaveHeight);
'''


# one surge for the foam, the water and the rock: two tracks of events at different periods, each event in new patches two or three hexes wide, starting at nearly one time across a patch
SURGE_CODE = '''
float surgeSpot = HexNoise.SampleLevel(HexNoiseSampler, uv * 0.025, 2.0).r;
float surgeTimeA = Time / SurgePeriodA + surgeSpot;
float surgeTimeB = Time / SurgePeriodB + surgeSpot * 1.3 + 0.37;
float surgeA = frac(surgeTimeA);
float surgeB = frac(surgeTimeB);
float surgePatchA = smoothstep(0.54, 0.66, HexNoise.SampleLevel(HexNoiseSampler, uv * 0.21 + frac(floor(surgeTimeA) * 0.37) * 7.0, 1.0).r);
float surgePatchB = smoothstep(0.54, 0.66, HexNoise.SampleLevel(HexNoiseSampler, uv * 0.27 + 0.3 + frac(floor(surgeTimeB) * 0.53) * 5.0, 1.0).r);
float surgeRiseA = smoothstep(0.0, 0.07, surgeA) * surgePatchA;
float surgeRiseB = smoothstep(0.0, 0.05, surgeB) * surgePatchB;
// most are ordinary, and now and then one comes higher
float surgeSizeA = 0.8 + 0.8 * pow(frac(floor(surgeTimeA) * 0.73), 3.0);
float surgeSizeB = 0.8 + 0.8 * pow(frac(floor(surgeTimeB) * 0.61), 3.0);
float surgeSlow = max(surgeRiseA * surgeSizeA * pow(1.0 - surgeA, 3.2), surgeRiseB * surgeSizeB * pow(1.0 - surgeB, 3.2));
float surgeQuick = max(surgeRiseA * surgeSizeA * pow(1.0 - surgeA, 10.0), surgeRiseB * surgeSizeB * pow(1.0 - surgeB, 14.0));
'''
WATER_CODE = WATER_CODE.replace('SURGE_HERE', SURGE_CODE)
WATER_OFFSET_CODE = WATER_OFFSET_CODE.replace('SURGE_HERE', SURGE_CODE)
CLIFF_CODE = CLIFF_CODE.replace('SURGE_HERE', SURGE_CODE)
MOUNTAIN_CODE = MOUNTAIN_CODE.replace('SURGE_HERE', SURGE_CODE)


def clear_expressions(material):
    """Deletes every expression of a material. One call of delete_all_material_expressions deletes only about half of them in this engine,
    and the rest would stay in the asset unconnected, growing it with every run"""
    library = unreal.MaterialEditingLibrary
    for _ in range(64):
        if library.get_num_material_expressions(material) == 0:
            return
        library.delete_all_material_expressions(material)
    unreal.log_warning('%s still has %d expressions' % (material.get_name(), library.get_num_material_expressions(material)))


def add_param(library, material, name, value, y):
    node = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, y)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', value)
    return node


# every value of these configs is a parameter of the same name, which AHexWorldMap sets from DA_HexMapSurface and DA_HexMapWater
CONFIGS = [('/Game/HexMap/DA_HexMapSurface', 'HexMapSurfaceConfig.h', 'HexMapSurfaceConfig'), ('/Game/HexMap/DA_HexMapWater', 'HexMapWaterConfig.h', 'HexMapWaterConfig')]


def read_config_names():
    """The values of the configs and their classes, read from the headers, as Python cannot list the properties of a class."""
    folder = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), 'Source', 'HexHero', 'Map')
    names = {}
    for _, header, class_name in CONFIGS:
        with open(os.path.join(folder, header), encoding='utf-8') as source:
            for name in re.findall(r'UPROPERTY\([^\n]*\)\s*\n\s*(?:float|FLinearColor|FVector4|FVector) (\w+)', source.read()):
                names[name] = getattr(unreal, class_name)
    return names


CONFIG_NAMES = read_config_names()


def add_config_params(library, material, code, existing, y, pins=True):
    """Adds a parameter for every value of the configs that the code names and the custom node does not have yet, with the default of its class.
    A vector parameter gives a pin only its RGB, so an FVector4 has its alpha appended."""
    connections = []
    for name, config_class in sorted(CONFIG_NAMES.items()):
        if name in existing or not re.search(r'\b%s\b' % name, code):
            continue
        value = unreal.get_default_object(config_class).get_editor_property(re.sub(r'(?<!^)(?=[A-Z])', '_', name).lower())
        node_y = y + len(connections) * 40
        if isinstance(value, (unreal.LinearColor, unreal.Vector4, unreal.Vector)):
            color = value if isinstance(value, unreal.LinearColor) else unreal.LinearColor(value.x, value.y, value.z, getattr(value, 'w', 0.0))
            node = library.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, node_y)
            node.set_editor_property('parameter_name', name)
            node.set_editor_property('default_value', color)
            if isinstance(value, unreal.Vector4):
                append = library.create_material_expression(material, unreal.MaterialExpressionAppendVector, -450, node_y)
                library.connect_material_expressions(node, '', append, 'A')
                library.connect_material_expressions(node, 'A', append, 'B')
                node = append
        else:
            node = add_param(library, material, name, value, node_y)
        connections.append((name, node, '') if pins else (name, node))
    return connections


def build_configs():
    # created only when missing, as their values are tuned by hand; new values start at the defaults of the class
    for path, _, class_name in CONFIGS:
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.AssetToolsHelpers.get_asset_tools().create_asset(path.split('/')[-1], path.rsplit('/', 1)[0], getattr(unreal, class_name), unreal.DataAssetFactory())
            unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)


def add_texture_object(library, material, path, name, sampler_type, y):
    """A named texture object reaches the code of the custom node as a texture with a sampler."""
    if name in ('HexData', 'ShoreField', 'ShoreHeight', 'MeshRims', 'MassifData'):
        node = library.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -700, y)
        node.set_editor_property('parameter_name', name)
    else:
        node = library.create_material_expression(material, unreal.MaterialExpressionTextureObject, -700, y)
    node.set_editor_property('texture', unreal.load_asset(path))
    node.set_editor_property('sampler_type', sampler_type)
    return node


def build(path, code, extra_inputs):
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(path) if assets.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(path.split('/')[-1], path.rsplit('/', 1)[0], unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)

    custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -200, 0)
    custom.set_editor_property('code', code)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)

    output = unreal.CustomOutput()
    output.set_editor_property('output_name', 'OutNormal')
    output.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property('additional_outputs', [output])

    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -700, -300)
    normal = library.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -700, -200)

    # ray tracing allows no automatic mip choice and Nanite has no derivatives in custom code, so they come from nodes
    position_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -500, -400)
    position_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -500, -300)
    library.connect_material_expressions(position, '', position_ddx, '')
    library.connect_material_expressions(position, '', position_ddy, '')

    connections = [('WorldPos', position, ''), ('Normal', normal, ''), ('PosDdx', position_ddx, ''), ('PosDdy', position_ddy, ''),
        ('HexRadius', add_param(library, material, 'HexRadius', 100.0, -100), ''),
        ('GridOpacity', add_param(library, material, 'GridOpacity', 1.0, 100), ''),
        ('HexData', add_texture_object(library, material, DEFAULT_DATA, 'HexData', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 400), ''),
        ('Colors', add_texture_object(library, material, COLORS, 'Colors', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 500), ''),
        ('HexNoise', add_texture_object(library, material, NOISE, 'HexNoise', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, 510), ''),
        ('Normals', add_texture_object(library, material, NORMALS, 'Normals', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 600), '')]
    connections += extra_inputs(library, material)
    connections += add_config_params(library, material, code, [item[0] for item in connections], 3000)

    inputs = []
    for name, _, _ in connections:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)

    for name, node, pin in connections:
        library.connect_material_expressions(node, pin, custom, name)

    roughness = add_config_params(library, material, 'SurfaceRoughness', [], 300)[0][1]
    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_BASE_COLOR)
    library.connect_material_property(custom, 'OutNormal', unreal.MaterialProperty.MP_NORMAL)
    library.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)

    cut = library.create_material_expression(material, unreal.MaterialExpressionCustom, -200, 2400)
    cut.set_editor_property('code', CUT_CODE)
    cut.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    cut_connections = [('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -700, 2400)),
        ('HexRadius', add_param(library, material, 'HexRadius', 100.0, 2450)),
        ('WireHexQ', add_param(library, material, 'WireHexQ', 1000000.0, 2500)),
        ('WireHexR', add_param(library, material, 'WireHexR', 1000000.0, 2550)),
        ('WireScope', add_param(library, material, 'WireScope', 0.0, 2600))]
    cut_inputs = []
    for name, _ in cut_connections:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        cut_inputs.append(item)
    cut.set_editor_property('inputs', cut_inputs)
    for name, node in cut_connections:
        library.connect_material_expressions(node, '', cut, name)
    library.connect_material_property(cut, '', unreal.MaterialProperty.MP_OPACITY_MASK)
    return material


def build_cut(path):
    """The masked instance of a surface material, <name>_Cut, whose mask hides the hexes that only the triangles are drawn in."""
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    cut_path = path + '_Cut'
    instance = unreal.load_asset(cut_path) if assets.does_asset_exist(cut_path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        cut_path.split('/')[-1], cut_path.rsplit('/', 1)[0], unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    library.set_material_instance_parent(instance, unreal.load_asset(path))
    overrides = instance.get_editor_property('base_property_overrides')
    overrides.set_editor_property('override_blend_mode', True)
    overrides.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    instance.set_editor_property('base_property_overrides', overrides)
    library.update_material_instance(instance)
    assets.save_asset(cut_path, only_if_is_dirty=False)


def build_water():
    library = unreal.MaterialEditingLibrary
    material = unreal.load_asset(WATER)
    clear_expressions(material)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)

    custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -200, 0)
    custom.set_editor_property('code', WATER_CODE)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    output = unreal.CustomOutput()
    output.set_editor_property('output_name', 'OutOpacity')
    output.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    custom.set_editor_property('additional_outputs', [output])

    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -700, -300)
    position_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -500, -400)
    position_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -500, -300)
    library.connect_material_expressions(position, '', position_ddx, '')
    library.connect_material_expressions(position, '', position_ddy, '')

    connections = [('WorldPos', position), ('PosDdx', position_ddx), ('PosDdy', position_ddy),
        ('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -700, -200)),
        ('HexRadius', add_param(library, material, 'HexRadius', 100.0, -100)),
        ('GridOpacity', add_param(library, material, 'GridOpacity', 1.0, 100)),
        ('CameraDir', library.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, -700, -250)),
        ('WaterClearDepth', add_param(library, material, 'WaterClearDepth', 6.0, 348)),
        ('WaterOpaqueDepth', add_param(library, material, 'WaterOpaqueDepth', 45.0, 349)),
        ('WaterEdgeFoam', add_param(library, material, 'WaterEdgeFoam', 0.8, 350)),
        ('WaterEdgeFoamWidth', add_param(library, material, 'WaterEdgeFoamWidth', 50.0, 355)),
        ('BehindDepth', library.create_material_expression(material, unreal.MaterialExpressionSceneDepth, -700, -150)),
        ('SurfaceDepth', library.create_material_expression(material, unreal.MaterialExpressionPixelDepth, -700, -125)),
        ('ShoreMinX', add_param(library, material, 'ShoreMinX', 0.0, 360)),
        ('ShoreMinY', add_param(library, material, 'ShoreMinY', 0.0, 370)),
        ('ShoreSizeX', add_param(library, material, 'ShoreSizeX', 1.0, 380)),
        ('ShoreSizeY', add_param(library, material, 'ShoreSizeY', 1.0, 390)),
        ('ShoreField', add_texture_object(library, material, DEFAULT_DATA, 'ShoreField', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 395)),
        ('ShoreHeight', add_texture_object(library, material, DEFAULT_DATA, 'ShoreHeight', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 394)),
        ('WaterHideHexQ', add_param(library, material, 'WaterHideHexQ', 1000000.0, 396)),
        ('WaterHideHexR', add_param(library, material, 'WaterHideHexR', 1000000.0, 397)),
        ('WaterHideScope', add_param(library, material, 'WaterHideScope', 0.0, 398)),
        ('HexData', add_texture_object(library, material, DEFAULT_DATA, 'HexData', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 400)),
        ('Colors', add_texture_object(library, material, COLORS, 'Colors', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 500)),
        ('HexNoise', add_texture_object(library, material, NOISE, 'HexNoise', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, 510)),
        ('Normals', add_texture_object(library, material, NORMALS, 'Normals', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 600))]
    connections += add_config_params(library, material, WATER_CODE, [item[0] for item in connections], 3000, pins=False)

    inputs = []
    for name, _ in connections:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)
    for name, node in connections:
        library.connect_material_expressions(node, '', custom, name)

    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.connect_material_property(custom, 'OutOpacity', unreal.MaterialProperty.MP_OPACITY)

    # the same swell as WATER_CODE moves the vertices of the grid
    offset = library.create_material_expression(material, unreal.MaterialExpressionCustom, -200, 900)
    offset.set_editor_property('code', WATER_OFFSET_CODE)
    offset.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    offset_connections = [('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -700, 900)),
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -700, 950)),
        ('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -700, 1000)),
        ('HexRadius', add_param(library, material, 'HexRadius', 100.0, 1085)),
        ('HexData', add_texture_object(library, material, DEFAULT_DATA, 'HexData', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 1090)),
        ('ShoreMinX', add_param(library, material, 'ShoreMinX', 0.0, 1100)),
        ('ShoreMinY', add_param(library, material, 'ShoreMinY', 0.0, 1120)),
        ('ShoreSizeX', add_param(library, material, 'ShoreSizeX', 1.0, 1140)),
        ('ShoreSizeY', add_param(library, material, 'ShoreSizeY', 1.0, 1160)),
        ('ShoreField', add_texture_object(library, material, DEFAULT_DATA, 'ShoreField', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 1180)),
        ('Colors', add_texture_object(library, material, COLORS, 'Colors', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 1200)),
        ('HexNoise', add_texture_object(library, material, NOISE, 'HexNoise', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, 1210))]
    offset_connections += add_config_params(library, material, WATER_OFFSET_CODE, [item[0] for item in offset_connections], 6000, pins=False)
    offset_inputs = []
    for name, _ in offset_connections:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        offset_inputs.append(item)
    offset.set_editor_property('inputs', offset_inputs)
    for name, node in offset_connections:
        library.connect_material_expressions(node, '', offset, name)
    library.connect_material_property(offset, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    return material


def build_river():
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(RIVER) if assets.does_asset_exist(RIVER) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(RIVER.split('/')[-1], RIVER.rsplit('/', 1)[0], unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)

    custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -200, 0)
    custom.set_editor_property('code', RIVER_CODE)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    output = unreal.CustomOutput()
    output.set_editor_property('output_name', 'OutOpacity')
    output.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    custom.set_editor_property('additional_outputs', [output])

    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -700, -300)
    coordinates = library.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -700, -100)
    derivatives = []
    for source, y in ((position, -400), (coordinates, -200)):
        for kind in (unreal.MaterialExpressionDDX, unreal.MaterialExpressionDDY):
            node = library.create_material_expression(material, kind, -500, y)
            library.connect_material_expressions(source, '', node, '')
            derivatives.append(node)
            y += 50

    flow = library.create_material_expression(material, unreal.MaterialExpressionVertexColor, -700, 0)
    connections = [('WorldPos', position, ''), ('PosDdx', derivatives[0], ''), ('PosDdy', derivatives[1], ''),
        ('UV', coordinates, ''), ('UvDdx', derivatives[2], ''), ('UvDdy', derivatives[3], ''),
        ('Flow', flow, ''), ('FlowFade', flow, 'A'),
        ('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -700, 100), ''),
        ('HexRadius', add_param(library, material, 'HexRadius', 100.0, 250), ''),
        ('WaterHideHexQ', add_param(library, material, 'WaterHideHexQ', 1000000.0, 300), ''),
        ('WaterHideHexR', add_param(library, material, 'WaterHideHexR', 1000000.0, 350), ''),
        ('WaterHideScope', add_param(library, material, 'WaterHideScope', 0.0, 400), ''),
        ('Colors', add_texture_object(library, material, COLORS, 'Colors', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 500), ''),
        ('HexNoise', add_texture_object(library, material, NOISE, 'HexNoise', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, 510), '')]
    connections += add_config_params(library, material, RIVER_CODE, [item[0] for item in connections], 3000)

    inputs = []
    for name, _, _ in connections:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)
    for name, node, pin in connections:
        library.connect_material_expressions(node, pin, custom, name)

    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.connect_material_property(custom, 'OutOpacity', unreal.MaterialProperty.MP_OPACITY)
    return material


def terrain_inputs(library, material):
    return [('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -700, 700), ''),
        ('WaterHeight', add_param(library, material, 'WaterHeight', -15.0, 800), ''),
        # the Tuning of DA_HexMap shapes the bank color of the terrain, and AHexWorldMap gives these two of it
        ('RiverBankTopColor', add_param(library, material, 'RiverBankTopColor', 0.6, 850), ''),
        ('RiverBankColorReach', add_param(library, material, 'RiverBankColorReach', 20.0, 860), ''),
        ('RiverWater', library.create_material_expression(material, unreal.MaterialExpressionVertexColor, -700, 900), 'A')]


def mountain_inputs(library, material):
    masks = library.create_material_expression(material, unreal.MaterialExpressionTextureSample, -700, 700)
    masks.set_editor_property('texture', unreal.load_asset(MASKS))
    masks.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    baked = library.create_material_expression(material, unreal.MaterialExpressionTextureSample, -700, 1000)
    baked.set_editor_property('texture', unreal.load_asset(ATLAS_NORMAL))
    baked.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    return [('Mask', masks, 'RGB'), ('Baked', baked, 'RGB'), ('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -700, 1280), ''),('FootHeight', add_param(library, material, 'FootHeight', 5.0, 1200), ''), ('WaterHeight', add_param(library, material, 'WaterHeight', -15.0, 1210), ''), 
        ('RimOriginX', add_param(library, material, 'RimOriginX', 0.0, 1310), ''),
        ('RimOriginY', add_param(library, material, 'RimOriginY', 0.0, 1320), ''),
        ('RimStep', add_param(library, material, 'RimStep', 16.67, 1330), ''),
        ('MeshRims', add_texture_object(library, material, DEFAULT_DATA, 'MeshRims', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 1300), ''),
        ('MassifStep', add_param(library, material, 'MassifStep', 0.0, 1340), ''),
        ('MassifMaxSteps', add_param(library, material, 'MassifMaxSteps', 0.0, 1350), ''),
        ('MassifRise', add_param(library, material, 'MassifRise', 0.0, 1360), ''),
        ('MassifData', add_texture_object(library, material, DEFAULT_DATA, 'MassifData', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 1370), '')]


def add_mountain_offset(material, code=MOUNTAIN_OFFSET_CODE, extra_connections=()):
    library = unreal.MaterialEditingLibrary
    offset = library.create_material_expression(material, unreal.MaterialExpressionCustom, -200, 900)
    offset.set_editor_property('code', code)
    offset.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    connections = [('RimPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -700, 1400)),
        ('RimOriginX', add_param(library, material, 'RimOriginX', 0.0, 1450)),
        ('RimOriginY', add_param(library, material, 'RimOriginY', 0.0, 1470)),
        ('RimStep', add_param(library, material, 'RimStep', 16.67, 1490)),
        ('FootHeight', add_param(library, material, 'FootHeight', 5.0, 1500)),
        ('RimFadeHeight', add_param(library, material, 'RimFadeHeight', 55.0, 1550)),
        ('MeshRims', add_texture_object(library, material, DEFAULT_DATA, 'MeshRims', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 1600)),
        ('HexRadius', add_param(library, material, 'HexRadius', 100.0, 1620)),
        ('MassifStep', add_param(library, material, 'MassifStep', 0.0, 1640)),
        ('MassifMaxSteps', add_param(library, material, 'MassifMaxSteps', 0.0, 1660)),
        ('MassifRise', add_param(library, material, 'MassifRise', 0.0, 1670)),
        ('MassifData', add_texture_object(library, material, DEFAULT_DATA, 'MassifData', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 1680))]
    connections += list(extra_connections)
    connections += add_config_params(library, material, code, [item[0] for item in connections], 7000, pins=False)
    inputs = []
    for name, _ in connections:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    offset.set_editor_property('inputs', inputs)
    for name, node in connections:
        library.connect_material_expressions(node, '', offset, name)
    library.connect_material_property(offset, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    # Nanite culls by this much beyond the bounds of a mesh, which must cover the rise of the deepest massif, MassifMaxSteps hexes in
    material.set_editor_property('max_world_position_offset_displacement', 1000.0)


def build_wire(path, offset_code):
    """An unlit translucent wireframe, which AHexWorldMap draws over the surface or instead of it."""
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(path) if assets.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(path.split('/')[-1], path.rsplit('/', 1)[0], unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('wireframe', True)
    material.set_editor_property('used_with_instanced_static_meshes', True)

    custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -200, 0)
    custom.set_editor_property('code', WIRE_CODE)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    output = unreal.CustomOutput()
    output.set_editor_property('output_name', 'OutOpacity')
    output.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    custom.set_editor_property('additional_outputs', [output])

    connections = [('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -700, -300)),
        ('HexRadius', add_param(library, material, 'HexRadius', 100.0, -100)),
        ('WireHexQ', add_param(library, material, 'WireHexQ', 1000000.0, 0)),
        ('WireHexR', add_param(library, material, 'WireHexR', 1000000.0, 100)),
        ('WireScope', add_param(library, material, 'WireScope', 0.0, 200)),
        ('WireOutsideOpacity', add_param(library, material, 'WireOutsideOpacity', 0.0, 300))]
    inputs = []
    for name, _ in connections:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)
    for name, node in connections:
        library.connect_material_expressions(node, '', custom, name)

    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.connect_material_property(custom, 'OutOpacity', unreal.MaterialProperty.MP_OPACITY)

    # the plain wireframe takes the inputs of the joined one and leaves the rims unused
    add_mountain_offset(material, offset_code, [
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -700, 1700)),
        ('WireBias', add_param(library, material, 'WireBias', 0.5, 1750))])
    return material


def cliff_inputs(library, material):
    return [('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -700, 800), ''), ('WaterHeight', add_param(library, material, 'WaterHeight', -15.0, 850), ''), ('Top', library.create_material_expression(material, unreal.MaterialExpressionVertexColor, -700, 700), '')]


def main():
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary

    build_configs()
    terrain = build(TERRAIN, TERRAIN_CODE, terrain_inputs)
    # the terrain has no tangents, so its normal is written in world space
    terrain.set_editor_property('tangent_space_normal', False)
    library.recompile_material(terrain)
    assets.save_asset(TERRAIN, only_if_is_dirty=False)

    mountain = build(MOUNTAIN, MOUNTAIN_CODE, mountain_inputs)
    mountain.set_editor_property('used_with_nanite', True)
    mountain.set_editor_property('used_with_instanced_static_meshes', True)
    add_mountain_offset(mountain)
    library.recompile_material(mountain)
    assets.save_asset(MOUNTAIN, only_if_is_dirty=False)

    cliff = build(CLIFF, CLIFF_CODE, cliff_inputs)
    cliff.set_editor_property('tangent_space_normal', False)
    cliff.set_editor_property('used_with_nanite', True)
    cliff.set_editor_property('used_with_instanced_static_meshes', True)
    library.recompile_material(cliff)
    assets.save_asset(CLIFF, only_if_is_dirty=False)

    for path in (TERRAIN, MOUNTAIN, CLIFF):
        build_cut(path)

    water = build_water()
    library.recompile_material(water)
    assets.save_asset(WATER, only_if_is_dirty=False)

    river = build_river()
    library.recompile_material(river)
    assets.save_asset(RIVER, only_if_is_dirty=False)

    wire = build_wire(WIRE, WIRE_OFFSET_CODE)
    library.recompile_material(wire)
    assets.save_asset(WIRE, only_if_is_dirty=False)

    joined_wire = build_wire(JOINED_WIRE, JOINED_WIRE_OFFSET_CODE)
    library.recompile_material(joined_wire)
    assets.save_asset(JOINED_WIRE, only_if_is_dirty=False)

    map_config = unreal.load_asset(MAP_CONFIG)
    map_config.set_editor_property('wireframe_material', wire)
    map_config.set_editor_property('joined_wireframe_material', joined_wire)
    map_config.set_editor_property('river_material', river)
    # saved in the asset, so a cook finds the configs through it
    map_config.set_editor_property('surface_config', unreal.load_asset(CONFIGS[0][0]))
    map_config.set_editor_property('water_look_config', unreal.load_asset(CONFIGS[1][0]))
    assets.save_asset(MAP_CONFIG, only_if_is_dirty=False)

    for material in (terrain, mountain, cliff, water, river, wire, joined_wire):
        unreal.log('HEXMAT %s expressions=%d' % (material.get_name(), library.get_num_material_expressions(material)))


main()
