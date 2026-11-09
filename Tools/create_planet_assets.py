"""Creates the planet prototype: M_HexPlanet with its masked instance M_HexPlanet_Cut, M_HexPlanetWater, M_HexPlanetClouds, M_HexPlanetAurora and M_HexPlanetAuroraSpark with MPC_HexPlanet, M_HexPlanetWireframe and Lvl_HexPlanet,
and DA_HexPlanet, DA_HexPlanetWater, DA_HexPlanetClouds and DA_HexPlanetCamera when they are missing, and DA_HexUnderground as a copy of
DA_HexPlanet with the features of the surface off (UNDERGROUND_VALUES), with M_HexUnderground and its masked M_HexUnderground_Cut, the fog
M_HexUndergroundFog and the materials of the markers of entrances, M_HexPlanetMarker and M_HexPlanetShaft.
Every value a material takes from a config is a parameter named as it, whose default here comes from the class, and AHexPlanet sets it from the asset
with the planet, a movable sun and the planet game mode.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi
Running it again rebuilds the material and the level; -script="<this file> configs" only makes the data assets that are missing, and
-script="<this file> underground" only those and the materials of the underground and of the markers.
"""

import re
import sys

import unreal

FOLDER = '/Game/HexPlanet'
MATERIAL = FOLDER + '/M_HexPlanet'
LEVEL = FOLDER + '/Lvl_HexPlanet'

# U of the first UV is 0 in the center of a cell and 1 at its sides, so the grid line is where it nears 1.
# Its width in pixels comes from the derivative nodes, as ray tracing allows none in custom code, and the line fades when cells get too small.
# It is never thinner than about a pixel, which keeps it from flickering far away. V is the height above the sea as a share of the mean peak
# of mountains, and the line fades out from GridFadeStart to GridFadeEnd of that height.
# Vertex colors are drawn only on the tiles of painting (PreviewTile), sRGB encoded, so they are decoded first; the surface draws the layers of the biomes (BIOME_CODE).
# Cloud shadows (CLOUD_SHADOW_CODE, shared with the water) darken the base color, so only the lit side.

# cloudCover is how much cloud lies in the direction cloudDir from the center, from four octaves of 3D value noise, computed here and
# not sampled, so they never repeat or show a seam. The noise turns around the axis of the planet at CloudSpeed radians per second
# and the finer octaves also slide along other axes, so the clouds change shape as they drift.
# Every Cloud* parameter is a value of UHexPlanetCloudConfig of the same name, which AHexPlanet gives every material that draws the clouds
# or their shadows from DA_HexPlanetClouds, so they always match. The defaults here come from the class and only show before it does
CLOUD_PARAMETERS = ['CloudScale', 'CloudCoverage', 'CloudSoftness', 'CloudContrast', 'CloudCurl', 'CloudDetail', 'CloudSpeed', 'CloudMorph', 'CloudMorphSpeed', 'CloudHeight']
SHADOW_PARAMETERS = ['CloudShadowStrength', 'CloudShadowReach']
SHELL_PARAMETERS = ['CloudOpacity', 'CloudBrightness', 'CloudDuskColor', 'CloudDuskWidth', 'CloudNightBrightness', 'CloudEdgeOnFade', 'CloudEdgeOnFadeNear', 'CloudDetailFade', 'CloudSkyHeight', 'CloudSkySwitch', 'CloudSkySwitchFade', 'CloudThickness', 'CloudVolumeSteps',
    'CloudVolumeStep', 'CloudVolumeDensity', 'CloudVolumeRound', 'CloudVolumeBaseShade',
    'CloudFadeStart', 'CloudFadeEnd', 'CloudNearOpacity', 'CloudFarHeight', 'CloudRelief', 'CloudLightStep', 'CloudShadeBias', 'CloudShadeColor', 'CloudSunlitBoost']
# the haze of the air, values of UHexPlanetCloudConfig too, as AHexPlanet gives that config to every material that needs them
HAZE_PARAMETERS = ['HazeHeight', 'HazeFalloff', 'HazeDensity', 'HazeStart', 'HazeStartFade', 'HazeMax', 'HazeColor', 'HazeNightColor', 'HazeSunWrap']

# The air of the planet is a shell HazeHeight thick, densest at the sea and thinner by exp(-height / HazeFalloff), so a view along the ground
# towards the horizon goes through much of it and one from orbit straight down through little, while at the rim of the planet the views that
# miss it pass through the most and glow. Its haze takes the length of the view through it, from the camera at hazeCam along hazeDir up to
# hazeEnd, in eight steps, each of thicker air the lower it lies, nothing within HazeStart of the camera and all of it HazeStartFade further,
# so what is close stays clear; it is lit by the sun where each step lies, wrapped over the terminator by HazeSunWrap. M_HexPlanetHaze, a
# post process before the translucency, lays it over the opaque scene and the space behind the planet, and the water and the clouds, which
# are drawn after it, lay it over themselves
HAZE_CODE = r'''
float hazeAmount = 0.0;
float3 hazeColor = HazeColor.rgb;
{
    float hazeTop = PlanetRadius + HazeHeight;
    float hazeB = dot(hazeCam, hazeDir);
    float hazeDisc = hazeB * hazeB - (dot(hazeCam, hazeCam) - hazeTop * hazeTop);
    if (hazeDisc > 0.0 && PlanetRadius > 0.0)
    {
        float hazeRoot = sqrt(hazeDisc);
        float hazeIn = max(-hazeB - hazeRoot, 0.0);
        float hazeOut = min(-hazeB + hazeRoot, hazeEnd);
        if (hazeOut > hazeIn)
        {
            float3 hazeSun = normalize(SunDirection.xyz);
            float hazeStep = (hazeOut - hazeIn) / 8.0;
            float hazeDepth = 0.0;
            float hazeLit = 0.0;
            for (int hazeIndex = 0; hazeIndex < 8; ++hazeIndex)
            {
                float hazeT = hazeIn + (hazeIndex + 0.5) * hazeStep;
                float3 hazeP = hazeCam + hazeDir * hazeT;
                float hazeR = length(hazeP);
                float hazeAir = exp(-max(hazeR - PlanetRadius, 0.0) / max(HazeFalloff, 1.0)) * saturate((hazeT - HazeStart) / max(HazeStartFade, 1.0));
                hazeDepth += hazeAir;
                hazeLit += hazeAir * saturate(dot(hazeP / hazeR, hazeSun) * HazeSunWrap + 0.5);
            }
            hazeColor = lerp(HazeNightColor.rgb, HazeColor.rgb, hazeLit / max(hazeDepth, 1e-6));
            hazeAmount = HazeMax * (1.0 - exp(-hazeDepth * hazeStep * HazeDensity));
        }
    }
}
'''

CLOUD_CODE = r'''
struct FCloudNoise
{
    float Hash(float3 p)
    {
        p = frac(p * 0.1031);
        p += dot(p, p.zyx + 31.32);
        return frac((p.x + p.y) * p.z);
    }
    float Value(float3 p)
    {
        float3 i = floor(p);
        float3 f = p - i;
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(lerp(Hash(i), Hash(i + float3(1, 0, 0)), f.x), lerp(Hash(i + float3(0, 1, 0)), Hash(i + float3(1, 1, 0)), f.x), f.y),
            lerp(lerp(Hash(i + float3(0, 0, 1)), Hash(i + float3(1, 0, 1)), f.x), lerp(Hash(i + float3(0, 1, 1)), Hash(i + float3(1, 1, 1)), f.x), f.y), f.z);
    }
    // the large octave shifts the finer ones, so the edges of clouds curl into puffs instead of smooth blobs
    float Clouds(float3 dir, float angle, float morphTime, float morphAmount, float scale, float contrast, float curlAmount, float detail)
    {
        float3 turnedDir = float3(dir.x * cos(angle) - dir.y * sin(angle), dir.x * sin(angle) + dir.y * cos(angle), dir.z);
        float3 turned = turnedDir * scale;
        float3 slide = float3(0.3, -0.5, 0.8) * angle * scale;
        // each octave breathes along the radius at its own pace, which takes another slice of the 3D noise, so clouds grow, part and
        // dissolve in place instead of only drifting; it stays within morphAmount, so their size never creeps
        float4 morph = sin(morphTime * float4(1.0, 1.73, 2.91, 4.37) + float4(0.0, 1.9, 4.1, 5.3)) * morphAmount;
        float base = Value(turned + turnedDir * morph.x);
        float3 curl = float3(base, -base, base * 0.5) * curlAmount;
        float sum = base * 0.5 + Value(turned * 2.03 + slide + curl + turnedDir * morph.y + 17.1) * 0.27;
        // far away the finest octaves are smaller than a pixel and only shimmer, so they are skipped where detail is 0
        if (detail > 0.001)
        {
            sum += (Value(turned * 4.07 - slide * 1.7 + curl * 2.0 + turnedDir * morph.z + 41.7) * 0.15
                + Value(turned * 8.13 + slide * 2.3 + curl * 4.0 + turnedDir * morph.w + 73.3) * 0.08) * detail;
        }
        // the finest octaves are taken out around their middle, so CloudDetail does not move the coverage
        return (sum + 0.115 * (1.0 - detail) - 0.5) * contrast + 0.5;
    }
};
FCloudNoise cloudNoise;
float cloudAngle = Time * CloudSpeed;
float cloudMorphTime = Time * CloudMorphSpeed;
float clouds = cloudNoise.Clouds(cloudDir, cloudAngle, cloudMorphTime, CloudMorph, CloudScale, CloudContrast, CloudCurl, cloudDetail);
float cloudThreshold = 1.0 - CloudCoverage;
float cloudCover = smoothstep(cloudThreshold - CloudSoftness * 0.5, cloudThreshold + CloudSoftness * 0.5, clouds);
'''

# the shadow of a point is the cloud where the ray from it to the sun meets the shell of clouds, so it lies under its cloud as seen
# along the sun. Where the sun is low that ray is long, and it stops at CloudShadowReach heights of the clouds, as the night side is unlit anyway
CLOUD_SHADOW_CODE = r'''
float3 shadowSun = normalize(SunDirection);
float shadowAlong = dot(WorldPos, shadowSun);
float shadowShell = PlanetRadius + CloudHeight;
float shadowReach = sqrt(max(shadowAlong * shadowAlong - dot(WorldPos, WorldPos) + shadowShell * shadowShell, 0.0)) - shadowAlong;
float3 cloudDir = normalize(WorldPos + shadowSun * min(shadowReach, CloudHeight * CloudShadowReach));
float cloudDetail = CloudDetail;
''' + CLOUD_CODE + r'''
float cloudShadow = 1.0 - CloudShadowStrength * cloudCover;
'''

# the shell of clouds at CloudHeight, unlit and translucent: white where the sun is high, CloudDuskColor where it is low, dim past the terminator
# and gone at night. It is two sided, as past the rim of the planet the far clouds are seen from below, and so is the sky while the camera is
# close; the planet hides the rest of the far half
CLOUDS_CODE = r'''
float3 cloudDir = normalize(WorldPos);
// the finest octave fades out where a pixel covers more than CloudDetailFade of its cells, as from far away it only draws streaks
float cloudDetailCells = length(fwidth(cloudDir)) * CloudScale * 8.13;
float cloudDetail = CloudDetail * (1.0 - smoothstep(CloudDetailFade * 0.5, max(CloudDetailFade, 1e-3), cloudDetailCells));
''' + CLOUD_CODE + r'''
float3 sun = normalize(SunDirection);
float sunUp = dot(cloudDir, sun);
float3 tint = lerp(CloudDuskColor.rgb, float3(1.0, 1.0, 1.0), smoothstep(0.0, CloudDuskWidth, sunUp));
float light = lerp(CloudNightBrightness, 1.0, smoothstep(-0.15, 0.25, sunUp));

// volume: the density is a height, and a second look a little towards the sun (CloudLightStep of a cloud) finds whether it rises there,
// which puts this side in its shade, as on a heap of cotton, by CloudRelief. Thin edges let light through and stay bright, and the shaded
// side takes CloudShadeColor. Only pixels with cloud pay for the second look
float facing = 1.0;
if (cloudCover > 0.001)
{
    float3 sunAlong = sun - cloudDir * sunUp;
    float3 litDir = normalize(cloudDir + sunAlong * (CloudLightStep / max(CloudScale, 0.1)));
    facing = saturate(CloudShadeBias + (clouds - cloudNoise.Clouds(litDir, cloudAngle, cloudMorphTime, CloudMorph, CloudScale, CloudContrast, CloudCurl, cloudDetail)) * CloudRelief);
}
facing = lerp(1.0, facing, smoothstep(0.0, 0.6, cloudCover));
float3 shade = lerp(CloudShadeColor.rgb, float3(1.0, 1.0, 1.0), facing);
float cameraHeight = length(CameraPos) - PlanetRadius;
// AHexPlanet draws the shell twice: below the camera, and with CloudSkyLayer 1 lifted by SHELL_LIFT_CODE to CloudSkyHeight as a sky whose
// clouds show over the horizon. As the camera comes down through CloudSkySwitch the one below hands over to the sky over CloudSkySwitchFade
// on both sides, so there are always clouds, and the camera never looks along a shell from its own height
float sky = CloudSkyLayer;
float skyShare = 1.0 - smoothstep(CloudSkySwitch - CloudSkySwitchFade, CloudSkySwitch + max(CloudSkySwitchFade, 1.0), cameraHeight);
float switchFade = lerp(1.0 - skyShare, skyShare, sky);
// at night they thin out, as dark clouds over the dark side only look like holes, and so they do where the camera sees the shell edge on,
// as at the rim of the planet they would stand out against the sky in stripes; the sky towards the horizon is seen nearly edge on, so it
// fades less (CloudEdgeOnFadeNear). Both sides show: past the rim of the planet a line that runs under the shell meets the far clouds from below
// close above them they thin to CloudNearOpacity, so the land shows through, and they are whole only from CloudFarHeight; both go by the
// distance from the camera to the cloud, plus CloudHeight below the camera, which is the height of the camera straight above it, so the
// clouds next to the camera are gone and those far off stay
float viewDistance = length(CameraPos - WorldPos) + CloudHeight * (1.0 - sky);
float heightOpacity = smoothstep(CloudFadeStart, max(CloudFadeEnd, CloudFadeStart + 1.0), viewDistance)
    * lerp(CloudNearOpacity, 1.0, smoothstep(CloudFadeEnd, max(CloudFarHeight, CloudFadeEnd + 1.0), viewDistance));
float edgeOnFade = lerp(CloudEdgeOnFade, CloudEdgeOnFadeNear, sky);
// a cloud behind the curve of the sea is hidden, as the water does not write depth and drawn after it the cloud would show through it near
// the horizon: the line from the camera dips below the sea on its way there
float3 viewRay = WorldPos - CameraPos;
float viewLength = length(viewRay);
viewRay /= max(viewLength, 1e-3);
float lowestAlong = clamp(-dot(CameraPos, viewRay), 0.0, viewLength);
float aboveSea = smoothstep(-2.0, 2.0, length(CameraPos + viewRay * lowestAlong) - PlanetRadius);

// volume: the clouds fill CloudThickness around their height, and SHELL_LIFT_CODE puts the shell on the side of it towards the camera, its top
// below the camera and its bottom for the sky, so every view into it meets the shell. From there the view is marched through it to where it
// leaves, taking the noise at a few points, forwards where it goes in and backwards from where the far side of the shell is seen, which past
// the rim of the planet is where the view comes out of its second pass. A point needs more cloud at the top and the bottom than in the middle
// (CloudVolumeRound), so big clouds fill the thickness and small ones stay thin, the light it takes grows with the length (CloudVolumeDensity),
// and its bottom is darker (CloudVolumeBaseShade). From above the view through it is short and takes few looks, along it at the horizon
// up to CloudVolumeSteps, offset by frame so the temporal filter blends them
float shellMiddle = PlanetRadius + lerp(CloudHeight, CloudSkyHeight, sky);
float shellInner = shellMiddle - CloudThickness * 0.5;
float shellOuter = shellMiddle + CloudThickness * 0.5;
float3 marchStart = cloudDir * lerp(shellOuter, shellInner, sky);
float goingOut = dot(viewRay, cloudDir);
float3 marchDir = viewRay * ((goingOut < 0.0) == (sky < 0.5) ? 1.0 : -1.0);
float marchLength = 1e9;
float marchAlong = dot(marchStart, marchDir);
float marchFrom = dot(marchStart, marchStart);
for (int sphere = 0; sphere < 2; sphere++)
{
    float radius = sphere == 0 ? shellInner : shellOuter;
    float disc = marchAlong * marchAlong - marchFrom + radius * radius;
    if (disc >= 0.0)
    {
        float root = sqrt(disc);
        float rootNear = -marchAlong - root;
        float rootFar = -marchAlong + root;
        marchLength = min(marchLength, rootNear > 0.01 ? rootNear : (rootFar > 0.01 ? rootFar : 1e9));
    }
}
marchLength = min(marchLength, CloudThickness * 40.0);
float marchCount = clamp(ceil(marchLength / max(CloudVolumeStep, 1.0)), 1.0, max(CloudVolumeSteps, 1.0));
float marchStep = marchLength / marchCount;
float marchJitter = frac(cloudNoise.Hash(floor(WorldPos * 0.5)) + View.StateFrameIndexMod8 * 0.618);
float transmit = 1.0;
float volumeLight = 0.0;
[loop] for (int look = 0; look < 16; look++)
{
    if (look >= marchCount)
    {
        break;
    }
    float3 at = marchStart + marchDir * (marchStep * (look + marchJitter));
    float atLength = length(at);
    float up = saturate((atLength - shellInner) / max(CloudThickness, 1.0));
    float atCloud = cloudNoise.Clouds(at / atLength, cloudAngle, cloudMorphTime, CloudMorph, CloudScale, CloudContrast, CloudCurl, cloudDetail);
    float atThreshold = cloudThreshold + CloudVolumeRound * (2.0 * up - 1.0) * (2.0 * up - 1.0);
    float density = smoothstep(atThreshold - CloudSoftness * 0.5, atThreshold + CloudSoftness * 0.5, atCloud);
    float taken = 1.0 - exp(-density * CloudVolumeDensity * marchStep);
    volumeLight += transmit * taken * lerp(CloudVolumeBaseShade, 1.0, up);
    transmit *= 1.0 - taken;
}
float volumeOpacity = 1.0 - transmit;
float volumeShade = volumeLight / max(volumeOpacity, 1e-4);
float3 bottomTint = lerp(CloudShadeColor.rgb, float3(1.0, 1.0, 1.0), volumeShade);
OutOpacity = volumeOpacity * CloudOpacity * heightOpacity * aboveSea * switchFade
    * smoothstep(-0.25, 0.1, sunUp) * smoothstep(edgeOnFade * 0.07, edgeOnFade, abs(dot(cloudDir, viewRay)));
float3 cloudColor = tint * light * shade * bottomTint * (1.0 + CloudSunlitBoost * facing) * CloudBrightness;
float3 hazeCam = CameraPos;
float hazeEnd = length(WorldPos - CameraPos);
float3 hazeDir = (WorldPos - CameraPos) / max(hazeEnd, 1e-3);
HAZE_CODE
return lerp(cloudColor, hazeColor, hazeAmount);
'''.replace('HAZE_CODE', HAZE_CODE)

# the shell is built at CloudHeight and moves to the side of the thickness of the clouds towards the camera: the top of it below the camera,
# and the bottom of it for the copy with CloudSkyLayer 1, which moves out to CloudSkyHeight, above the close camera
SHELL_LIFT_CODE = r'''
return normalize(WorldPos) * ((CloudSkyHeight - CloudHeight) * CloudSkyLayer + CloudThickness * 0.5 * (1.0 - 2.0 * CloudSkyLayer));
'''

# the grid line from U, which is 0 in the center of a cell and 1 at its sides: GridWidth wide, never under GridMinPixels nor over GridMaxPixels
# (each cell draws the half inside its side), so a slope facing the camera does not draw it wider than flat ground seen at a slant, and gone where
# a cell is only a few pixels. AHexPlanet::BuildWaterGrid makes its strips as wide as the widest line, so change both together
GRID_LINE_CODE = r'''
float pixel = max(abs(UDdx) + abs(UDdy), 1e-5);
float width = clamp(GridWidth, pixel * GridMinPixels, pixel * max(GridMaxPixels, GridMinPixels));
float gridLine = saturate((U - (1.0 - width)) / pixel) * saturate(1.5 - pixel * 8.0) * saturate(GridWidth / width);
'''

# the outlines of marked cells (AHexPlanet::SetHoveredCell), centered on the edge of a set of cells: the texture CellMarks has a pixel per cell,
# at the third UV of its vertices (HexPlanetSurface::GetCellUV), with bits of the sides to outline in red, drawn in MarkPrimaryColor, and in
# green for a second layer in NeighborOutlineColor. Each cell along the edge draws its half of the line on the grid line inside the side,
# MarkWidth or MarkMinPixels wide, which never fades with distance. Where the edge bends, the halves of two sides would stop at the bisector of
# their corner and leave a notch, so the half of a marked side also goes on past the corner into the side next to it, as far as the band of
# that side, which joins the line with a sharp miter as two strokes would. The fourth UV is where a point lies in a regular cell whose side K
# faces K turns of a side and whose corner K lies half a turn further, so the side and the corner nearest a pixel are found by its angle, in
# ints, as fmod and exp2 of floats may land just under a whole number and drop to the one before. The first 12 cells are the pentagons.
# It needs pixel from GRID_LINE_CODE
MARK_CODE = r'''
float2 markCell = round(CellUV);
float4 markPixel = CellMarks.Load(int3(markCell, 0));
uint2 markBits = (uint2)round(markPixel.rg * 255.0);
// blue is 1 in the cells whose water the debug view hex.Water hides (AHexPlanet::SetWaterHiddenCells), and alpha holds the bits of the rim
// of that hole, which only the water grid draws (MARK_HOLE_BITS), so the seabed keeps the outline of the cell alone
float waterHidden = markPixel.b;
uint holeBits = MARK_HOLE_BITS;
float markLine = 0.0;
float3 markColor = MarkPrimaryColor.rgb;
if (any(markBits > 0u) || holeBits > 0u)
{
    int sides = markCell.y < 0.5 && markCell.x < 11.5 ? 5 : 6;
    float sideTurn = 6.2831853 / sides;
    float angle = atan2(LocalUV.y, LocalUV.x);
    int side = ((int)round(angle / sideTurn) + sides) % sides;
    // corner K lies between sides K and K + 1, so the other side at the nearest corner
    int corner = ((int)floor(angle / sideTurn) + sides) % sides;
    int other = side == corner ? (corner + 1) % sides : corner;
    float markWidth = max(MarkWidth, pixel * MarkMinPixels);
    float band = saturate((U - (1.0 - markWidth)) / pixel);
    // how far the point lies towards the line of the other side, as U measures it towards the nearest side
    float towardsOther = dot(LocalUV, float2(cos(other * sideTurn), sin(other * sideTurn))) / cos(3.14159265 / sides);
    float miter = saturate((towardsOther - (1.0 - markWidth)) / pixel);
    float primary = max((float)((markBits.r >> side) & 1u), (float)((markBits.r >> other) & 1u) * miter);
    float secondary = max((float)((markBits.g >> side) & 1u), (float)((markBits.g >> other) & 1u) * miter);
    float hole = max((float)((holeBits >> side) & 1u), (float)((holeBits >> other) & 1u) * miter);
    markLine = band * max(max(primary, secondary), hole);
    markColor = primary > 0.0 ? MarkPrimaryColor.rgb : secondary > 0.0 ? NeighborOutlineColor.rgb : WaterHideOutlineColor.rgb;
}
'''

# the surface from the texture layers of the biomes (TA_Biome_Color with height in alpha, TA_Biome_Normal), projected from the three planes of
# the world, as the sphere has no seamless UVs, and blended by the normal of the vertex; planes with little weight are skipped with their samples.
# CellBiomes has two pixels per cell (AHexPlanet::WriteBiomePixels): the terrain, rock and beach layers of its biome, and the terrain layers of its
# six neighbors, two to a byte. A cell blends into the neighbors across the side nearest a pixel and the other side at the nearest corner, as
# MARK_CODE finds them from the fourth UV, half and half on the side and none BiomeBlendWidth inside it. A noise moves that line off the side,
# which both cells see with opposite signs, by the order of their layers, so they agree on the side. The height in alpha then decides which
# layer covers the other (BiomeHeightBlend, BiomeBlendDepth), which never shows a layer past the end of its blend. Over that: the beach layer of
# the biome near and below the sea, the rock layer on slopes and snow high up, each sampled only where it shows; the seabed darkens with depth.
# The normals of the layers bend the normal of the vertex, in world space. PreviewTile draws the vertex colors instead, for the tiles of painting
BIOME_CODE = r'''
struct FBiomeLayers
{
    float3 Planes;
    float3 Pos;
    float3 Ddx;
    float3 Ddy;
    float DefaultTile;
    // the shares of cliff, scree and gravel of a rock that has them (LayerParams), by the slope, and how their heights decide where they meet
    float3 RockWeights;
    float RockHeightBlend;
    float RockBlendDepth;

    // the color of a layer and the bend of its normal in world space, from each plane that has weight, at the tile size, tint, normal strength
    // and height of the layer in LayerParams (UHexTerrainLayersConfig::CreateParamsTexture), whose default black texture keeps the old values
    float4 SampleOne(Texture2DArray colors, SamplerState colorsSampler, Texture2DArray normals, SamplerState normalsSampler, Texture2D params, float layer, out float3 bend)
    {
        float4 look = params.Load(int3(layer, 0, 0));
        float4 shape = params.Load(int3(layer, 1, 0));
        bool known = look.w > 0.5;
        float tile = known ? look.w : DefaultTile;
        float3 pos = Pos / tile;
        float3 ddx = Ddx / tile;
        float3 ddy = Ddy / tile;
        float4 color = 0.0;
        bend = 0.0;
        [branch] if (Planes.x > 0.0)
        {
            color += colors.SampleGrad(colorsSampler, float3(pos.yz, layer), ddx.yz, ddy.yz) * Planes.x;
            float2 slope = normals.SampleGrad(normalsSampler, float3(pos.yz, layer), ddx.yz, ddy.yz).xy * 2.0 - 1.0;
            bend += float3(0.0, slope.x, slope.y) * Planes.x;
        }
        [branch] if (Planes.y > 0.0)
        {
            color += colors.SampleGrad(colorsSampler, float3(pos.xz, layer), ddx.xz, ddy.xz) * Planes.y;
            float2 slope = normals.SampleGrad(normalsSampler, float3(pos.xz, layer), ddx.xz, ddy.xz).xy * 2.0 - 1.0;
            bend += float3(slope.x, 0.0, slope.y) * Planes.y;
        }
        [branch] if (Planes.z > 0.0)
        {
            color += colors.SampleGrad(colorsSampler, float3(pos.xy, layer), ddx.xy, ddy.xy) * Planes.z;
            float2 slope = normals.SampleGrad(normalsSampler, float3(pos.xy, layer), ddx.xy, ddy.xy).xy * 2.0 - 1.0;
            bend += float3(slope.x, slope.y, 0.0) * Planes.z;
        }
        if (known)
        {
            color.rgb *= look.rgb;
            color.a = saturate((color.a - 0.5) * shape.y + 0.5 + shape.z);
            bend *= shape.x;
        }
        return color;
    }

    // a layer, or for a rock with a scree or gravel layer (third row of LayerParams, plus one) its cliff, scree and gravel by RockWeights,
    // the higher covering the lower where they meet
    float4 Sample(Texture2DArray colors, SamplerState colorsSampler, Texture2DArray normals, SamplerState normalsSampler, Texture2D params, float layer, out float3 bend)
    {
        float4 parts = params.Load(int3(layer, 2, 0));
        float scree = parts.x - 1.0;
        float gravel = parts.y - 1.0;
        [branch] if (scree < 0.0 && gravel < 0.0)
        {
            return SampleOne(colors, colorsSampler, normals, normalsSampler, params, layer, bend);
        }
        float3 weights = RockWeights;
        if (gravel < 0.0)
        {
            weights.y += weights.z;
            weights.z = 0.0;
            gravel = layer;
        }
        if (scree < 0.0)
        {
            weights.z += weights.y;
            weights.y = 0.0;
            scree = layer;
        }
        float3 ids = float3(layer, scree, gravel);
        float4 partColors[3];
        float3 partBends[3];
        float3 heights = 0.0;
        [unroll] for (int k = 0; k < 3; ++k)
        {
            partColors[k] = 0.0;
            partBends[k] = 0.0;
            [branch] if (weights[k] > 0.01)
            {
                partColors[k] = SampleOne(colors, colorsSampler, normals, normalsSampler, params, ids[k], partBends[k]);
                heights[k] = weights[k] + partColors[k].a * RockHeightBlend;
            }
        }
        float3 covers = max(heights - (max(max(heights.x, heights.y), heights.z) - RockBlendDepth), 0.0) * (weights > 0.01);
        covers /= max(covers.x + covers.y + covers.z, 1e-4);
        bend = partBends[0] * covers.x + partBends[1] * covers.y + partBends[2] * covers.z;
        return partColors[0] * covers.x + partColors[1] * covers.y + partColors[2] * covers.z;
    }

    // what a table of three float4 (12 layers) gives each of four layers, such as the beach or the rock of a layer
    float4 Map(float4 ids, float4 table0, float4 table1, float4 table2)
    {
        float4 mapped;
        [unroll] for (int k = 0; k < 4; ++k)
        {
            int index = (int)max(ids[k], 0.0);
            float4 table = index < 4 ? table0 : (index < 8 ? table1 : table2);
            mapped[k] = table[index & 3];
        }
        return mapped;
    }

    // the layers that the blended ones map to, blended by the same covers, so they blend between cells as the layers do; a layer already
    // sampled is taken again, so sand next to sand costs nothing
    float3 BlendMapped(Texture2DArray colors, SamplerState colorsSampler, Texture2DArray normals, SamplerState normalsSampler, float4 ids, float4 mapped,
        float4 covers, float4 shown, float4 known[4], float3 knownBends[4], Texture2D params, out float3 outBend, out float outAlpha)
    {
        float4 found[4];
        float3 foundBends[4];
        float3 result = 0.0;
        outBend = 0.0;
        outAlpha = 0.0;
        [unroll] for (int m = 0; m < 4; ++m)
        {
            found[m] = known[m];
            foundBends[m] = knownBends[m];
            if (covers[m] > 0.002 && mapped[m] != ids[m])
            {
                bool done = false;
                [unroll] for (int n = 0; n < 4; ++n)
                {
                    if (!done && shown[n] > 0.5 && ids[n] == mapped[m])
                    {
                        found[m] = known[n];
                        foundBends[m] = knownBends[n];
                        done = true;
                    }
                    if (!done && n < m && covers[n] > 0.002 && mapped[n] == mapped[m])
                    {
                        found[m] = found[n];
                        foundBends[m] = foundBends[n];
                        done = true;
                    }
                }
                [branch] if (!done)
                {
                    float3 sampledBend;
                    found[m] = Sample(colors, colorsSampler, normals, normalsSampler, params, mapped[m], sampledBend);
                    foundBends[m] = sampledBend;
                }
            }
            result += found[m].rgb * covers[m];
            outBend += foundBends[m] * covers[m];
            outAlpha += found[m].a * covers[m];
        }
        return result;
    }
};
float3 surfaceUp = normalize(WorldPos);
float3 vertexNormal = normalize(VertexNormal);
float3 planes = pow(abs(vertexNormal), TriplanarSharpness);
planes /= planes.x + planes.y + planes.z;
planes = saturate((planes - 0.04) / 0.96);
planes /= planes.x + planes.y + planes.z;
FBiomeLayers layers;
layers.Planes = planes;
layers.Pos = WorldPos;
layers.Ddx = PosDdx;
layers.Ddy = PosDdy;
layers.DefaultTile = max(BiomeTileSize, 1.0);
float height = length(WorldPos) - PlanetRadius;
float flatness = dot(vertexNormal, surfaceUp);
float crevice = saturate(CreviceShare);
// a rock with scree and gravel (UHexTerrainLayersConfig) is its cliff on steep ground, scree on middle slopes and in gullies, and gravel on flat
// ground, where the flatness meets them moved by a noise, so they do not run along the slope like contour lines
float rockFlat = flatness + (cloudNoise.Value(WorldPos / max(RockSetNoiseScale, 1.0) + 9.1) - 0.5) * 2.0 * RockSetNoise;
float rockCliff = 1.0 - smoothstep(RockCliffFlatStart, max(RockCliffFlatEnd, RockCliffFlatStart + 1e-3), rockFlat);
float rockGravel = smoothstep(RockGravelFlatStart, max(RockGravelFlatEnd, RockGravelFlatStart + 1e-3), rockFlat);
float rockGully = crevice * RockGullyScree;
layers.RockWeights = float3(rockCliff * (1.0 - rockGully), saturate(1.0 - rockCliff - rockGravel) + rockCliff * rockGully, rockGravel);
layers.RockHeightBlend = RockSetHeightBlend;
layers.RockBlendDepth = max(RockSetBlendDepth, 0.01);

float2 biomeCell = round(CellUV);
uint4 biomeOwn = (uint4)round(CellBiomes.Load(int3(biomeCell.x * 2.0, biomeCell.y, 0)) * 255.0);
uint4 biomeNear = (uint4)round(CellBiomes.Load(int3(biomeCell.x * 2.0 + 1.0, biomeCell.y, 0)) * 255.0);
int biomeSides = biomeCell.y < 0.5 && biomeCell.x < 11.5 ? 5 : 6;
float biomeTurn = 6.2831853 / biomeSides;
float apothem = cos(3.14159265 / biomeSides);
float ownLayer = (float)biomeOwn.r;

// the line between two layers lies on their side, moved by a noise of two sizes that both cells see with opposite signs by the order of their
// layers, so they agree, and never so far that the blend reaches past the apothem: of each pair of opposite sides one is at least that far, so
// every side whose blend reaches a pixel is taken, and a blend never stops on a straight line where another side would take over
float blendWidth = clamp(BiomeBlendWidth, 0.01, apothem - 0.05);
float blendNoise = ((cloudNoise.Value(WorldPos / max(BiomeBlendNoiseScale, 1.0)) - 0.5) * 2.0
    + (cloudNoise.Value(WorldPos / max(BiomeBlendNoiseScale * BiomeBlendDetailScale, 1.0) + 3.7) - 0.5) * 2.0 * BiomeBlendDetail) * BiomeBlendNoise;
float blendLimit = apothem - 0.03 - blendWidth;
// a mountain pushes the line into its neighbor that is not one (bit K of blue: neighbor K is a mountain, bit 6: this cell is), so its rock
// reaches down to the water and over the foot, and both cells see the same shift with opposite signs
uint mountainBits = biomeOwn.b;
float ownMountain = (float)((mountainBits >> 6) & 1u);
// the land pushes its layer into a neighbor under the sea (alpha, the same bits), over the part its winding coast lifts above the water
uint seaBits = biomeOwn.a;
float ownSea = (float)((seaBits >> 6) & 1u);
// the layer of this cell and up to three other layers whose blend reaches the pixel, with their weights
float4 blendIds = float4(ownLayer, -1.0, -1.0, -1.0);
float4 blendWeights = float4(1.0, 0.0, 0.0, 0.0);
[unroll] for (int side = 0; side < 6; ++side)
{
    if (side < biomeSides)
    {
        uint sideByte = side < 2 ? biomeNear.r : (side < 4 ? biomeNear.g : biomeNear.b);
        float sideLayer = (float)((((side & 1) != 0) ? (sideByte >> 4) : sideByte) & 15u);
        float toSide = apothem - dot(LocalUV, float2(cos(side * biomeTurn), sin(side * biomeTurn)));
        float lineShift = blendNoise * (ownLayer < sideLayer ? 1.0 : -1.0) + ((float)((mountainBits >> side) & 1u) - ownMountain) * MountainBlendShift
            + (ownSea - (float)((seaBits >> side) & 1u)) * CoastBlendShift;
        lineShift = clamp(lineShift, -blendLimit, blendLimit);
        float share = sideLayer == ownLayer ? 0.0 : 1.0 - saturate(0.5 + (toSide - lineShift) / (2.0 * blendWidth));
        if (share > 0.0)
        {
            float weight = share / max(1.0 - share, 1e-3);
            if (blendIds.y < 0.0 || blendIds.y == sideLayer) { blendIds.y = sideLayer; blendWeights.y += weight; }
            else if (blendIds.z < 0.0 || blendIds.z == sideLayer) { blendIds.z = sideLayer; blendWeights.z += weight; }
            else { blendIds.w = blendIds.w < 0.0 ? sideLayer : blendIds.w; blendWeights.w += weight; }
        }
    }
}
blendWeights /= blendWeights.x + blendWeights.y + blendWeights.z + blendWeights.w;
float4 blendShown = float4(1.0, blendWeights.y > 0.002, blendWeights.z > 0.002, blendWeights.w > 0.002);

float4 layerColors[4];
float3 layerBends[4];
[unroll] for (int i = 0; i < 4; ++i)
{
    layerColors[i] = 0.0;
    layerBends[i] = 0.0;
    [branch] if (blendShown[i] > 0.5)
    {
        layerColors[i] = layers.Sample(Colors, ColorsSampler, Normals, NormalsSampler, LayerParams, blendIds[i], layerBends[i]);
    }
}
// the alpha of a layer, its height, decides which covers the other where they blend, and a noise of each layer of its own, so the edge is
// ragged and in patches rather than along a line
float4 heights = blendWeights + float4(layerColors[0].a, layerColors[1].a, layerColors[2].a, layerColors[3].a) * BiomeHeightBlend * blendShown;
[branch] if (blendShown.y + blendShown.z + blendShown.w > 0.5)
{
    float3 edgePos = WorldPos / max(BiomeEdgeNoiseScale, 1.0);
    [unroll] for (int e = 0; e < 4; ++e)
    {
        heights[e] += blendShown[e] > 0.5 ? (cloudNoise.Value(edgePos + blendIds[e] * float3(7.13, 3.71, 5.37)) - 0.5) * 2.0 * BiomeEdgeNoise : 0.0;
    }
}
float4 covers = max(heights - (max(max(heights.x, heights.y), max(heights.z, heights.w)) - BiomeBlendDepth), 0.0) * blendShown;
covers /= max(covers.x + covers.y + covers.z + covers.w, 1e-4);
float3 albedo = 0.0;
float3 bend = 0.0;
[unroll] for (int j = 0; j < 4; ++j)
{
    albedo += layerColors[j].rgb * covers[j];
    bend += layerBends[j] * covers[j];
}

// grass dries a little towards a sandy tone: the higher a hill the more, in green and sandy streaks on its flat tops, and in rare patches anywhere,
// so a wide plain is not one tone. Its own texture still shows through, by the height in its alpha
float4 isGrass = float4(abs(blendIds.x - GrassTintLayer) < 0.5, abs(blendIds.y - GrassTintLayer) < 0.5, abs(blendIds.z - GrassTintLayer) < 0.5, abs(blendIds.w - GrassTintLayer) < 0.5);
float grassShare = dot(covers, isGrass);
[branch] if (grassShare > 0.01)
{
    float hillRise = smoothstep(GrassDryStart, max(GrassDryEnd, GrassDryStart + 1.0), height);
    float3 streakPos = WorldPos / max(GrassStreakScale, 1.0);
    float streakBend = cloudNoise.Value(streakPos * 0.37 + 5.1);
    float streakValue = cloudNoise.Value(streakPos + streakBend * GrassStreakWarp);
    float streak = smoothstep(1.0 - GrassStreakWidth, 1.0, 1.0 - abs(streakValue * 2.0 - 1.0)) * hillRise * smoothstep(0.9, 0.97, flatness);
    float patchValue = cloudNoise.Value(WorldPos / max(GrassPatchScale, 1.0) + streakBend * 0.8 + 11.7);
    float patch = smoothstep(GrassPatchCover, GrassPatchCover + 0.1, patchValue);
    float dry = saturate(hillRise * GrassHillDry + streak * GrassStreak + patch * GrassPatch);
    float grassAlpha = dot(float4(layerColors[0].a, layerColors[1].a, layerColors[2].a, layerColors[3].a), covers * isGrass) / grassShare;
    float3 dryColor = GrassDryColor.rgb * (0.75 + 0.5 * grassAlpha);
    albedo = lerp(albedo, dryColor, dry * grassShare);
}

// the beach of each layer (BeachLayers0..2: sand for grass, snow for snow, the rock itself for the rock of a mountain), blended as the layers
// are: below BeachHeight, and on the slopes of the coast up to ShoreSlopeTop, where it takes the place of the rock of slopes
float slopeRock = 1.0 - smoothstep(SlopeStart, max(SlopeEnd, SlopeStart + 1e-3), flatness);
float beachShift = (cloudNoise.Value(WorldPos / (BiomeBlendNoiseScale * 0.5) + 7.3) - 0.5) * 2.0 * BeachNoise;
float shoreSlope = 1.0 - smoothstep(ShoreSlopeTop, ShoreSlopeTop + max(ShoreSlopeFade, 0.01), height + beachShift);
float beach = max(1.0 - smoothstep(BeachHeight, BeachHeight + max(BeachFade, 0.01), height + beachShift), slopeRock * shoreSlope);
[branch] if (beach > 0.01)
{
    // under the water the beach turns into the seabed of its layer (SeabedLayers0..2: coastal gravel for snow, so snow never lies under the
    // sea), from SeabedHeight, a little above the water, down over SeabedFade, sampled only where a layer has a seabed of its own
    float4 beachIds = layers.Map(blendIds, BeachLayers0, BeachLayers1, BeachLayers2);
    float4 seabedIds = layers.Map(blendIds, SeabedLayers0, SeabedLayers1, SeabedLayers2);
    float seabed = any(seabedIds != beachIds) ? 1.0 - smoothstep(SeabedHeight - max(SeabedFade, 0.01), SeabedHeight, height + beachShift * 0.5) : 0.0;
    float3 beachBend = 0.0;
    float beachAlpha = 0.0;
    float3 beachAlbedo = 0.0;
    [branch] if (seabed < 0.99)
    {
        beachAlbedo = layers.BlendMapped(Colors, ColorsSampler, Normals, NormalsSampler, blendIds, beachIds,
            covers, blendShown, layerColors, layerBends, LayerParams, beachBend, beachAlpha);
    }
    [branch] if (seabed > 0.01)
    {
        float3 seabedBend;
        float seabedAlpha;
        float3 seabedAlbedo = layers.BlendMapped(Colors, ColorsSampler, Normals, NormalsSampler, blendIds, seabedIds,
            covers, blendShown, layerColors, layerBends, LayerParams, seabedBend, seabedAlpha);
        // the heights of both decide where each lies in the band, so snow stays in the hollows of the sand and the shore reads as snowy sand
        // the upper part of the band is sand dusted with the beach (snow), more the higher, so the shore reads as snowy sand and not a line
        seabedAlbedo = lerp(seabedAlbedo, beachAlbedo, (1.0 - seabed) * SeabedDusting * (seabed < 0.99 ? 1.0 : 0.0));
        float seabedCover = seabed > 0.99 ? 1.0 : saturate(((seabed + seabedAlpha * SeabedHeightBlend) - (1.0 - seabed + beachAlpha * SeabedHeightBlend))
            / (2.0 * max(SeabedBlendDepth, 0.01)) + 0.5);
        beachAlbedo = lerp(beachAlbedo, seabedAlbedo, seabedCover);
        beachBend = lerp(beachBend, seabedBend, seabedCover);
    }
    albedo = lerp(albedo, beachAlbedo, beach);
    bend = lerp(bend, beachBend, beach);
}
// rock on slopes above the coast, and on high ground whatever its slope, so a low mountain reads as rock and not grass. The rock of each layer
// (RockLayers0..2: that of its biome, a rock for itself) blends as the layers do, so a slope shared by two biomes has no seam
float rockAmount = max(slopeRock * (1.0 - shoreSlope), smoothstep(HighRockStart, max(HighRockEnd, HighRockStart + 1e-3), height));
[branch] if (rockAmount > 0.01)
{
    float3 rockBend;
    float rockAlpha;
    float3 rock = layers.BlendMapped(Colors, ColorsSampler, Normals, NormalsSampler, blendIds, layers.Map(blendIds, RockLayers0, RockLayers1, RockLayers2),
        covers, blendShown, layerColors, layerBends, LayerParams, rockBend, rockAlpha);
    float rockCover = saturate(rockAmount * (0.6 + rockAlpha * 0.8));
    albedo = lerp(albedo, rock, rockCover);
    bend = lerp(bend, rockBend, rockCover);
    // rock on snow ground (layer 3) is snowed over, only its high edges peeking out by its height, so a steep hill of snow stays snowy
    float4 isSnowGround = float4(abs(blendIds.x - 3.0) < 0.5, abs(blendIds.y - 3.0) < 0.5, abs(blendIds.z - 3.0) < 0.5, abs(blendIds.w - 3.0) < 0.5);
    float snowGround = dot(covers, isSnowGround);
    [branch] if (snowGround > 0.01)
    {
        float4 groundSnow = 0.0;
        float3 groundSnowBend = 0.0;
        [unroll] for (int s = 0; s < 4; ++s)
        {
            if (isSnowGround[s] > 0.5)
            {
                groundSnow = layerColors[s];
                groundSnowBend = layerBends[s];
            }
        }
        float rockSnow = rockCover * snowGround * saturate(SnowRockCover + (0.5 - rockAlpha) * 2.0 * SnowRockPeek);
        albedo = lerp(albedo, groundSnow.rgb, rockSnow);
        bend = lerp(bend, groundSnowBend, rockSnow);
    }
}
float snowShift = (cloudNoise.Value(WorldPos / (BiomeBlendNoiseScale * 3.0) + 3.1) - 0.5) * 2.0 * SnowNoise;
// a gully (Y of the second UV) is darker, and snow reaches down it and lies on steeper ground there
albedo *= lerp(1.0, CreviceDarkening, crevice);
float snowFlatStart = SnowFlatStart - GullySnowFlatness * crevice;
float snow = smoothstep(SnowHeight, SnowHeight + max(SnowFade, 0.01), height + snowShift + GullySnowReach * crevice) * smoothstep(snowFlatStart, max(SnowFlatEnd - GullySnowFlatness * crevice, snowFlatStart + 1e-3), flatness);
[branch] if (snow > 0.01)
{
    float3 snowBend;
    float4 snowColor = layers.Sample(Colors, ColorsSampler, Normals, NormalsSampler, LayerParams, 3.0, snowBend);
    albedo = lerp(albedo, snowColor.rgb, snow);
    bend = lerp(bend, snowBend, snow);
}
albedo *= lerp(1.0, SeabedDarkening, saturate(-height / max(DeepDepth, 1.0)));
if (PreviewTile > 0.5)
{
    albedo = pow(max(Color, 0.0), 2.2);
    bend = 0.0;
}
// high ground of big mountains (FacetShare, the second UV) is shaded flat by triangle, each face leaning at random by regions of FacetCell, so it
// reads as split stone. The faces come from the change of the position across a pixel, which ray tracing does not have, so it keeps the vertex normal
float3 shadedNormal = vertexNormal;
[branch] if (FacetShare > 0.01 && FacetStrength > 0.0)
{
    float3 faceNormal = cross(PosDdy, PosDdx);
    float faceLength = length(faceNormal);
    if (faceLength > 1e-12)
    {
        faceNormal = faceNormal / faceLength * (dot(faceNormal, vertexNormal) < 0.0 ? -1.0 : 1.0);
        float3 facetPos = WorldPos / max(FacetCell, 1.0);
        float3 facetBase = floor(facetPos);
        float facetBest = 1e9;
        float3 lean = 0.0;
        for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
        for (int z = -1; z <= 1; ++z)
        {
            float3 c = facetBase + float3(x, y, z);
            float3 h = frac(c * float3(0.1031, 0.1030, 0.0973));
            h += dot(h, h.yxz + 33.33);
            h = frac((h.xxy + h.yxx) * h.zyx);
            float3 toPoint = c + h - facetPos;
            float d = dot(toPoint, toPoint);
            if (d < facetBest)
            {
                facetBest = d;
                lean = h * 2.0 - 1.0;
            }
        }
        faceNormal = normalize(faceNormal + (lean - faceNormal * dot(lean, faceNormal)) * FacetTilt);
        shadedNormal = normalize(lerp(vertexNormal, faceNormal, saturate(FacetShare) * FacetStrength));
    }
}
OutNormal = normalize(shadedNormal + (bend - shadedNormal * dot(bend, shadedNormal)) * BiomeNormalStrength);
'''

# the beach of each layer of the biome textures, in the order of LAYERS in HexTextures/build_biome_textures.py: sand for grass, swamp, tundra and
# steppe, snow for snow, and each rock for itself, as on a mountain; AHexPlanet::SetBiomeLayers gives the material those of the biome assets
BEACH_LAYERS = [1, 1, 2, 3, 1, 5, 6, 7, 1, 1, 10, 11]
# the seabed of each layer under the water: its beach, but sand for snow, so the bottom is sand under every coast and ice
SEABED_LAYERS = [1, 1, 2, 1, 1, 5, 6, 7, 1, 1, 10, 11]
# the rock on the slopes of each layer: that of the first land biome with it, and each rock for itself, as a mountain draws its rock
ROCK_LAYERS = [2, 6, 2, 2, 7, 5, 6, 7, 2, 6, 10, 11]

# the values of UHexPlanetConfig that BIOME_CODE takes
BIOME_PARAMETERS = ['BiomeTileSize', 'BiomeBlendWidth', 'BiomeBlendNoise', 'BiomeBlendNoiseScale', 'BiomeHeightBlend', 'BiomeBlendDepth',
    'BiomeNormalStrength', 'TriplanarSharpness', 'SeabedDarkening', 'BeachNoise', 'SnowNoise', 'SlopeStart', 'SlopeEnd', 'BeachHeight', 'BeachFade',
    'SnowHeight', 'SnowFade', 'SnowFlatStart', 'SnowFlatEnd', 'DeepDepth', 'FacetStrength', 'FacetTilt', 'FacetCell', 'CreviceDarkening', 'GullySnowReach', 'GullySnowFlatness', 'HighRockStart', 'HighRockEnd',
    'GrassTintLayer', 'GrassDryColor', 'GrassDryStart', 'GrassDryEnd', 'GrassHillDry', 'GrassStreak', 'GrassStreakScale', 'GrassStreakWarp',
    'GrassStreakWidth', 'GrassPatch', 'GrassPatchScale', 'GrassPatchCover', 'BiomeBlendDetail', 'BiomeBlendDetailScale', 'ShoreSlopeTop', 'ShoreSlopeFade', 'BiomeEdgeNoise', 'BiomeEdgeNoiseScale', 'MountainBlendShift',
    'RockCliffFlatStart', 'RockCliffFlatEnd', 'RockGravelFlatStart', 'RockGravelFlatEnd', 'RockGullyScree', 'RockSetNoise', 'RockSetNoiseScale', 'RockSetHeightBlend', 'RockSetBlendDepth', 'SnowRockCover', 'SnowRockPeek', 'CoastBlendShift', 'SeabedHeight', 'SeabedFade', 'SeabedHeightBlend', 'SeabedBlendDepth', 'SeabedDusting']

# a hash of a wave and a stretch of the coast, which WATER_CODE and RUNUP_CODE share, so both know the same waves
SHORE_HASH_CODE = '''
#define SHORE_HASH(A, B) frac(sin(dot(float2(A, B), float2(12.9898, 78.233))) * 43758.5453)
'''

# the wave of the stretch of coast in front of a point (the shore field of HexPlanetSeaField, sampled by SHORE_DIR), which WATER_CODE and
# RUNUP_CODE share, so the run-up on the beach follows the breaker in the water: each stretch breaks at its own phase, so waves come one by one
# along the coast, now and then skips a wave (SurfChance) or sends a roller (RollerChance), and fades out towards its ends (SurfBorderStart,
# SurfBorderEnd, SurfLength), where the next stretch breaks at another time.
# Its crest rolls in at an even speed (stretchFast) from SurfReach over SurfRunShare of SurfPeriod, to SurfShoreDistance from the coast line of
# the field, then changes its speed to SurfShoreSpeed of that by the line, faster as the white water of a broken wave rushes in, and rolls on at
# that pace, so it reaches the real edge of the water, on
# either side of the line, at its own time: stretchCrest is the distance to the coast it has reached stretchTime seconds after it formed,
# and SHORE_CREST_TIME(C) when it reaches the distance C
SHORE_WAVE_CODE = r'''
float4 shoreStretch = ShoreField.SampleLevel(ShoreFieldSampler, SHORE_DIR, 0);
float stretchCycle = Time / max(SurfPeriod, 0.1) + shoreStretch.r;
float stretchWave = floor(stretchCycle);
float stretchProgress = frac(stretchCycle);
float stretchId = floor(shoreStretch.r * 255.0 + 0.5);
float stretchPresent = SHORE_HASH(stretchWave, stretchId) < SurfChance ? 1.0 : 0.0;
float stretchRoller = SHORE_HASH(stretchWave + 0.37, stretchId + 0.71) < RollerChance ? 1.0 : 0.0;
// the ends: the bisector towards the next stretch (green), and SurfLength along the coast from the middle of the stretch (blue)
float stretchEnds = smoothstep(SurfBorderStart, max(SurfBorderEnd, SurfBorderStart + 1.0), shoreStretch.g * 256.0)
    * (1.0 - smoothstep(max(SurfLength * 0.5 - (SurfBorderEnd - SurfBorderStart), 0.0), max(SurfLength * 0.5, 1.0), shoreStretch.b * 256.0));
float runShare = clamp(SurfRunShare, 0.1, 0.9);
float stretchTime = stretchProgress * max(SurfPeriod, 0.1);
float stretchFarTime = runShare * max(SurfPeriod, 0.1);
float shoreZone = clamp(SurfShoreDistance, 1.0, max(SurfReach - 1.0, 1.0));
float stretchFast = max(SurfReach - shoreZone, 1.0) / stretchFarTime;
// the speed by the coast line, as a share of the speed further out; never exactly the same, as the closed form below divides by the change
float stretchShore = stretchFast * (abs(SurfShoreSpeed - 1.0) < 0.01 ? 1.01 : clamp(SurfShoreSpeed, 0.05, 4.0));
// near the shore the speed changes evenly with the distance, from stretchFast to stretchShore, so the distance changes exponentially in time
float stretchShoreA = (stretchFast - stretchShore) / shoreZone;
float stretchShoreTime = log(stretchFast / stretchShore) / stretchShoreA;
float stretchCrest = stretchTime < stretchFarTime ? SurfReach - stretchFast * stretchTime
    : (stretchTime < stretchFarTime + stretchShoreTime
        ? ((stretchShore + stretchShoreA * shoreZone) * exp(-stretchShoreA * (stretchTime - stretchFarTime)) - stretchShore) / stretchShoreA
        : -stretchShore * (stretchTime - stretchFarTime - stretchShoreTime));
#define SHORE_CREST_TIME(C) ((C) >= shoreZone ? (SurfReach - (C)) / stretchFast : ((C) >= 0.0 \
    ? stretchFarTime + log((stretchShore + stretchShoreA * shoreZone) / (stretchShore + stretchShoreA * (C))) / stretchShoreA \
    : stretchFarTime + stretchShoreTime - (C) / stretchShore))
'''

# the run-up on land: when the crest of the breaker of a stretch (SHORE_WAVE_CODE) reaches the edge of the water below a point of the beach,
# found from the distance to the coast there and the slope of the ground (the normal of the vertex, smooth across triangles), down which
# the sea lies height / tan(slope) further out, its water runs up the beach there: fast, over RunupRiseTime, up to RunupHeight above the sea,
# then slowly back over RunupFallTime, a rim of foam at its edge and a laced sheet behind it, and the foam it left above slides down after it in
# thinning streaks while the sand it wet dries. So the foam is born just where and when the wave meets the real shore, wherever that lies
# against the coast line of the field. A roller runs RollerScale higher, and towards the ends of its stretch it runs lower. Faded from far away
# as the waves of the water. It only finds the foam and the wet sand (runupFoam, runupWet), which RUNUP_APPLY_CODE lays on after the rivers, as
# a river and its banks keep them off, so the foam of the sea does not roll up a river
RUNUP_CODE = SHORE_HASH_CODE + r'''
float runupWet = 0.0;
float runupFoam = 0.0;
{
    float3 shoreUp = normalize(WorldPos);
    float3 shorePlanes = pow(abs(shoreUp), 8.0);
    shorePlanes /= shorePlanes.x + shorePlanes.y + shorePlanes.z;
    float3 shoreP = WorldPos / TileSize;
    float3 shoreDdx = PosDdx / TileSize;
    float3 shoreDdy = PosDdy / TileSize;
#define SHORE_NOISE(S, O) (HexNoise.SampleGrad(HexNoiseSampler, shoreP.yz * (S) + (O), shoreDdx.yz * (S), shoreDdy.yz * (S)).r * shorePlanes.x \
    + HexNoise.SampleGrad(HexNoiseSampler, shoreP.xz * (S) + (O), shoreDdx.xz * (S), shoreDdy.xz * (S)).r * shorePlanes.y \
    + HexNoise.SampleGrad(HexNoiseSampler, shoreP.xy * (S) + (O), shoreDdx.xy * (S), shoreDdy.xy * (S)).r * shorePlanes.z)
    float shoreAa = max((length(PosDdx) + length(PosDdy)) * 0.5, 0.001);
    float runFar = smoothstep(ShoreFarStart, max(ShoreFarEnd, ShoreFarStart + 0.01), shoreAa);
''' + SHORE_WAVE_CODE.replace('SHORE_DIR', 'shoreUp') + r'''
    float runTop = RunupHeight * lerp(1.0, RollerScale, stretchRoller) * lerp(0.4, 1.0, stretchEnds);
    [branch] if (height > -0.5 && height < runTop && runFar < 0.999 && PreviewTile < 0.5 && stretchPresent * stretchEnds > 0.001)
    {
        // the edge of the water below this point, in the distance to the coast of the water (WATER_CODE, divided by its slope)
        float4 runSea = SeaField.SampleLevel(SeaFieldSampler, shoreUp, 0);
        float landDistance = (runSea.a * 450.0 - 50.0) / max(shoreStretch.a, 0.35)
            + (cloudNoise.Value(WorldPos / max(SurfReach * 1.2, 1.0) + 11.0) - 0.5) * SurfWobble;
        float slopeUp = max(dot(vertexNormal, shoreUp), 0.05);
        float slopeTan = length(vertexNormal - shoreUp * dot(vertexNormal, shoreUp)) / slopeUp;
        float edgeDistance = landDistance + max(height, 0.0) / max(slopeTan, 0.05);
        // seconds since the crest reached it
        float age = stretchTime - SHORE_CREST_TIME(edgeDistance);
        if (age > 0.0)
        {
            float laceNoise = SHORE_NOISE(ShoreLaceScale, 0.0);
            float rise = max(RunupRiseTime, 0.05);
            float fall = saturate((age - rise) / max(RunupFallTime, 0.1));
            float reach = age < rise ? runTop * sin(age / rise * 1.5707963) : runTop * pow(1.0 - fall, 1.5);
            float rim = (1.0 - smoothstep(0.0, RunupEdge, abs(height - reach))) * (reach > 0.01 ? 1.0 : 0.0);
            float sheet = height < reach ? lerp(0.35, 0.8, smoothstep(0.3, 0.7, laceNoise)) : 0.0;
            // what it left above the water as it slides back thins into streaks and fades
            float left = (age > rise && height >= reach) ? smoothstep(0.25 + fall * 0.45, 0.4 + fall * 0.45, laceNoise) * (1.0 - fall) : 0.0;
            // gone before the next wave of the stretch
            float cycleEnd = 1.0 - smoothstep(0.95, 1.0, stretchProgress);
            float runFoam = max(max(rim, sheet), left) * stretchEnds * lerp(1.0, 1.3, stretchRoller) * cycleEnd;
            // the sand it wet darkens only once the water has slid back off it, and dries; under the water it stayed light, as a darker band
            // under the sheet of foam looked like a shadow
            float wet = (age > rise && height >= reach) ? (1.0 - fall) * cycleEnd : 0.0;
            float onLand = smoothstep(-0.5, 0.0, height) * (1.0 - runFar);
            runupWet = wet * stretchEnds * onLand;
            runupFoam = saturate(runFoam * RunupFoam) * onLand;
        }
    }
#undef SHORE_NOISE
#undef SHORE_CREST_TIME
}
'''

RUNUP_APPLY_CODE = r'''
albedo *= lerp(1.0, WetDarkening, runupWet * (1.0 - riverKeep));
albedo = lerp(albedo, ShoreFoamColor.rgb, runupFoam * (1.0 - riverKeep));
'''

# the values of UHexPlanetWaterConfig that RUNUP_CODE takes
RUNUP_PARAMETERS = ['TileSize', 'SurfReach', 'SurfWobble', 'SurfPeriod', 'SurfRunShare', 'SurfShoreDistance', 'SurfShoreSpeed', 'SurfChance', 'SurfBorderStart',
    'SurfBorderEnd', 'SurfLength', 'ShoreLaceScale', 'RollerChance', 'RollerScale', 'RunupHeight', 'RunupRiseTime', 'RunupFallTime', 'RunupEdge',
    'RunupFoam', 'WetDarkening', 'ShoreFoamColor', 'ShoreFarStart', 'ShoreFarEnd']

# Forests from far away: the objects are culled there, so the ground of a cell takes the color of its cover (CellCanopy, one pixel per cell,
# the root of the linear color and its share, AHexPlanet::UpdateCanopy), all of it from CanopyFadeEnd and CanopyNearShare of it close by,
# where it darkens the ground under the trees. It goes right up to a side whose neighbor has a cover too (bits in the alpha of the second
# pixel of the cell in CellBiomes), and fades out before another side, along a ragged line, and a noise mottles it
CANOPY_PARAMETERS = ['CanopyNearShare', 'CanopyFadeStart', 'CanopyFadeEnd', 'CanopyEdge', 'CanopyEdgeSoftness', 'CanopyEdgeNoise',
    'CanopyEdgeNoiseScale', 'CanopyMottle', 'CanopyMottleScale']
CANOPY_CODE = r'''
float4 canopy = CellCanopy.Load(int3(biomeCell, 0));
[branch] if (canopy.a > 0.0)
{
    float canopyAngle = atan2(LocalUV.y, LocalUV.x);
    int canopySide = ((int)round(canopyAngle / biomeTurn) + biomeSides) % biomeSides;
    bool canopyNext = ((biomeNear.a >> canopySide) & 1u) != 0u;
    float canopyEdge = CanopyEdge + (cloudNoise.Value(WorldPos / max(CanopyEdgeNoiseScale, 1.0) + 5.3) - 0.5) * 2.0 * CanopyEdgeNoise;
    float canopyInside = canopyNext ? 1.0 : 1.0 - smoothstep(canopyEdge - CanopyEdgeSoftness, canopyEdge, U);
    float canopyFar = lerp(CanopyNearShare, 1.0, smoothstep(CanopyFadeStart, max(CanopyFadeEnd, CanopyFadeStart + 1.0), distance(CameraPos, WorldPos)));
    float canopyMottle = 1.0 + (cloudNoise.Value(WorldPos / max(CanopyMottleScale, 1.0) + 1.7) - 0.5) * 2.0 * CanopyMottle;
    albedo = lerp(albedo, canopy.rgb * canopy.rgb * canopyMottle, saturate(canopy.a * canopyInside * canopyFar));
}
'''

# Rivers (UHexPlanetRivers), drawn on the bottom of the valley the surface lowers along them. They follow one line of the rivers for every cell
# (HexPlanetRivers::BuildCurve), so the water, its banks and its valley join across the sides of the cells, and each pixel finds the exact
# distance to it: CellRivers has a pixel per sector of each cell (six at six times the column of CellMarks), the first of its segments in RGB
# and their number in alpha, and RiverSegments two texels per segment, its ends as directions from the center with the narrowing near a
# spring, whether it is frozen and which way it flows, so no triangle blends distances that do not fit and a river joins another cleanly.
# The water lets the bed show through as the shallows of the sea do: the sand of RiverBedLayer, tinted darker by RiverBedTint, under the color of
# the water, RiverWaterEdge of it at the edge to RiverWaterMiddle in the middle, from RiverShallowColor to RiverDeepColor, shaded under the banks.
# Lines drift downstream on it in lanes along the river (RiverStreak*), it has faint ripples and foam where the ground is steep, and it is rough
# enough that the sun makes no glints on it (RiverRoughness). Its banks are wet earth with a volume alike on both sides (RiverBankShade at their
# foot, RiverBankHighlight on their crest) and a normal bent a little across them (RiverBankRelief over RiverLeveeWidth). A frozen river
# (RiverFrozenBiomes) is ice with cracks and no flow. Below the sea it fades out, so at its mouth the water of the sea meets the bed alone, and it
# grows as light as RiverShallowColor and its rapids calm down near the coast (RiverMouthCalm, by the distance of the sea field). It needs
# biomeCell, biomeSides and biomeTurn of BIOME_CODE and runs after CANOPY_CODE, so the canopy of a forest never covers it
RIVER_PARAMETERS = ['RiverWidth', 'RiverRunupGap', 'RiverDeepColor', 'RiverShallowColor', 'RiverBedLayer', 'RiverBedTint', 'RiverWaterEdge',
    'RiverWaterMiddle', 'RiverBankColor', 'RiverBankWidth', 'RiverBankDarkening', 'RiverBankRelief', 'RiverBankShade', 'RiverBankHighlight',
    'RiverLeveeWidth', 'RiverEdgeShade', 'RiverEdgeNoise', 'RiverEdgeNoiseScale', 'RiverFlowSpeed', 'RiverRippleScale', 'RiverRippleStrength',
    'RiverStreakLength', 'RiverStreakSpacing', 'RiverStreakThin', 'RiverStreakStrength', 'RiverRapidsFlatStart', 'RiverRapidsFlatEnd', 'RiverFoamColor',
    'RiverRoughness', 'RiverMouthDepth', 'RiverMouthCalm', 'RiverIceColor', 'RiverIceCrack', 'RiverIceCrackScale', 'RiverIceRoughness', 'Roughness']
RIVER_CODE = r'''
OutRoughness = Roughness;
float riverShown = 0.0;
// how much a river and its banks keep the run-up of the sea off, out to RiverRunupGap past the banks
float riverKeep = 0.0;
// the segments of the line of the rivers near the sector of the cell under the pixel (UHexPlanetRivers::UpdateTexture): CellRivers holds the
// first and the number of them for each sector, the triangle from the center to corners K - 1 and K, which the angle of the fourth UV gives
float riverAngle = atan2(LocalUV.y, LocalUV.x);
int riverSector = ((int)round(riverAngle / biomeTurn) + biomeSides) % biomeSides;
uint4 riverList = (uint4)round(CellRivers.Load(int3(biomeCell.x * 6.0 + riverSector, biomeCell.y, 0)) * 255.0);
[branch] if (riverList.a > 0u && PreviewTile < 0.5)
{
    // the nearest of them, exactly for the pixel: RiverSegments has two texels a segment, its ends as directions from the center with the
    // narrowing near a spring at each, and in the first whether it is frozen (16) and flows from its first end to its second (32)
    float3 riverPoint = normalize(WorldPos);
    uint riverFirst = riverList.r | (riverList.g << 8) | (riverList.b << 16);
    float riverLine = 1e9;
    float3 riverNear = riverPoint;
    float3 riverRun = 0.0;
    float riverFlags = 0.0;
    for (uint riverEntry = 0u; riverEntry < riverList.a; ++riverEntry)
    {
        uint riverTexel = (riverFirst + riverEntry) * 2u;
        float4 segA = RiverSegments.Load(int3(riverTexel % 4096u, riverTexel / 4096u, 0));
        float4 segB = RiverSegments.Load(int3((riverTexel + 1u) % 4096u, (riverTexel + 1u) / 4096u, 0));
        float3 segRun = segB.xyz - segA.xyz;
        float segT = saturate(dot(riverPoint - segA.xyz, segRun) / max(dot(segRun, segRun), 1e-20));
        float3 segNear = segA.xyz + segRun * segT;
        float segFlags = segA.w;
        float segForward = segFlags >= 32.0 ? 1.0 : 0.0;
        segFlags -= segForward * 32.0;
        float segIce = segFlags >= 16.0 ? 1.0 : 0.0;
        float segDistance = distance(riverPoint, segNear) * PlanetRadius + lerp(segFlags - segIce * 16.0, segB.w, segT);
        if (segDistance < riverLine)
        {
            riverLine = segDistance;
            riverNear = segNear;
            riverRun = segRun * (segForward * 2.0 - 1.0);
            riverFlags = segIce;
        }
    }
    float riverIce = riverFlags;
    // downstream along the ground; away from the river, along the ground, for the banks; and across it, left of the flow positive, for its lanes
    float3 flowDir = riverRun - surfaceUp * dot(riverRun, surfaceUp);
    flowDir = dot(flowDir, flowDir) > 1e-20 ? normalize(flowDir) : 0.0;
    float3 riverOff = riverPoint - riverNear;
    riverOff -= surfaceUp * dot(riverOff, surfaceUp);
    float3 bankAway = dot(riverOff, riverOff) > 1e-24 ? normalize(riverOff) : 0.0;
    float riverAcross = riverLine * (dot(riverOff, cross(surfaceUp, flowDir)) >= 0.0 ? 1.0 : -1.0);

    float riverAa = max((length(PosDdx) + length(PosDdy)) * 0.5, 0.01);
    float riverEdge = (cloudNoise.Value(WorldPos / max(RiverEdgeNoiseScale, 1.0) + 8.1) - 0.5) * 2.0 * RiverEdgeNoise;
    // world units inside the water from its edge, which stays at least about a pixel wide from far away; the line is further near a source,
    // where the river narrows to a spring
    float riverHalf = max(RiverWidth * 0.5, riverAa * 0.8);
    float fromEdge = riverHalf + riverEdge - riverLine;
    // below the sea the sea draws the water, so the river fades out down to RiverMouthDepth and its bed meets the bed of the sea
    float riverAbove = smoothstep(-max(RiverMouthDepth, 0.01), 0.5, height);
    float riverWater = saturate(fromEdge / riverAa + 0.5) * riverAbove;
    float riverBank = saturate((fromEdge + RiverBankWidth) / riverAa + 0.5) * (1.0 - riverIce);
    float riverDamp = saturate((fromEdge + RiverBankWidth * 4.0) / max(RiverBankWidth * 3.0, 0.01));
    riverKeep = 1.0 - smoothstep(RiverBankWidth, RiverBankWidth + max(RiverRunupGap, 0.01), -fromEdge);

    // the banks: wet earth at the water, the ground beyond a little darker, both banks with the same volume, dark at the foot and light on
    // the crest, and a normal bent a little across them, up from the edge of the water to the crest RiverBankWidth out and down over
    // RiverLeveeWidth
    float outside = -fromEdge;
    float crest = max(RiverBankWidth, 0.5);
    float levee = max(RiverLeveeWidth, crest + 0.5);
    albedo *= 1.0 - RiverBankDarkening * riverDamp * riverAbove;
    albedo = lerp(albedo, RiverBankColor.rgb, riverBank * riverAbove);
    float bankFoot = 1.0 - smoothstep(0.0, crest, outside);
    float bankCrest = smoothstep(crest * 0.6, crest * 1.3, outside) * (1.0 - smoothstep(crest * 1.3, levee, outside));
    albedo *= (1.0 - RiverBankShade * bankFoot * (1.0 - riverWater) * riverAbove) * (1.0 + RiverBankHighlight * bankCrest * riverAbove);
    float bankSlope = smoothstep(-1.0, crest, outside) * (1.0 - smoothstep(crest, levee, outside));
    float bankRise = (outside < crest ? 1.0 / (crest + 1.0) : -1.0 / (levee - crest)) * bankSlope;
    float bankTilt = bankRise * RiverBankRelief * riverAbove * (1.0 - riverWater);
    OutNormal = normalize(OutNormal - bankAway * bankTilt);

    [branch] if (riverWater > 0.001)
    {
        // the distance to the coast of the sea field, which goes 50 units under the land: towards its mouth a river grows as light as the
        // shallow water it meets, and its rapids calm down
        float riverCoast = 50.0 - SeaField.SampleLevel(SeaFieldSampler, surfaceUp, 0).a * 450.0;
        float riverInland = smoothstep(0.0, max(RiverMouthCalm, 0.01), riverCoast);
        float riverDeep = saturate(fromEdge / max(riverHalf, 0.01));
        float3 waterColor = lerp(RiverShallowColor.rgb, RiverDeepColor.rgb, sqrt(riverDeep) * riverInland);
        // the sandy bed through the water, as in the shallows of the sea, darker
        float3 bedBend;
        float3 bed = layers.Sample(Colors, ColorsSampler, Normals, NormalsSampler, LayerParams, (float)RiverBedLayer, bedBend).rgb * RiverBedTint.rgb;
        float3 riverColor = lerp(bed, waterColor, lerp(RiverWaterEdge, RiverWaterMiddle, smoothstep(0.0, 1.0, riverDeep)));
        // shaded under the banks
        riverColor *= lerp(RiverEdgeShade, 1.0, smoothstep(0.0, max(riverHalf * 0.35, 0.5), fromEdge));
        float3 riverNormal = surfaceUp;
        float rapids = 0.0;

        [branch] if (riverIce < 0.5)
        {
            // faint ripples carried downstream, in two phases a half cycle apart that fade into each other
            float riverCycle = 2.0;
            float2 riverPhases = frac(Time / riverCycle + float2(0.0, 0.5));
            float3 riverGrad = 0.0;
            [unroll] for (int ph = 0; ph < 2; ++ph)
            {
                float3 p = (WorldPos - flowDir * (riverPhases[ph] - 0.5) * RiverFlowSpeed * riverCycle) / max(RiverRippleScale, 0.1) + ph * 17.3;
                float e = 0.35;
                float n0 = cloudNoise.Value(p);
                float3 g = float3(cloudNoise.Value(p + float3(e, 0, 0)), cloudNoise.Value(p + float3(0, e, 0)), cloudNoise.Value(p + float3(0, 0, e))) - n0;
                riverGrad += g / e * (1.0 - abs(riverPhases[ph] * 2.0 - 1.0));
            }
            riverGrad -= surfaceUp * dot(riverGrad, surfaceUp);
            riverNormal = normalize(surfaceUp - riverGrad * RiverRippleStrength);

            // lines that drift downstream in lanes along the river, broken into dashes of a noise carried with the water in the same two
            // phases, faded where they grow thinner than a pixel
            float lane = riverAcross / max(RiverStreakSpacing, 0.1);
            float laneLine = 1.0 - smoothstep(RiverStreakThin * 0.5, RiverStreakThin, abs(frac(lane) - 0.5) * 2.0);
            float dash = 0.0;
            float foamDash = 0.0;
            [unroll] for (int dp = 0; dp < 2; ++dp)
            {
                float3 carried = WorldPos - flowDir * (riverPhases[dp] - 0.5) * RiverFlowSpeed * riverCycle;
                float weight = 1.0 - abs(riverPhases[dp] * 2.0 - 1.0);
                dash += cloudNoise.Value(carried / max(RiverStreakLength, 0.1) + floor(lane) * 3.7 + dp * 11.1) * weight;
                foamDash += cloudNoise.Value(carried / max(RiverRippleScale * 0.6, 0.1) + 31.0 + dp * 7.7) * weight;
            }
            float streak = laneLine * smoothstep(0.5, 0.75, dash) * smoothstep(0.1, 0.5, riverDeep)
                * (1.0 - smoothstep(0.3, 1.0, riverAa / max(RiverStreakSpacing * RiverStreakThin, 0.01)));
            riverColor = lerp(riverColor, riverColor * 1.5 + 0.04, saturate(streak * RiverStreakStrength * 3.0) * riverInland);

            // foam on the rapids, where the ground under the water is steep, as more and brighter lines along the flow
            rapids = smoothstep(RiverRapidsFlatStart, RiverRapidsFlatEnd, flatness) * riverInland;
            float foamLane = riverAcross / max(RiverStreakSpacing * 0.5, 0.1);
            float foamLine = 1.0 - smoothstep(RiverStreakThin, RiverStreakThin * 2.5, abs(frac(foamLane) - 0.5) * 2.0);
            float foamStreak = foamLine * smoothstep(0.4, 0.7, foamDash);
            riverColor = lerp(riverColor, RiverFoamColor.rgb, saturate(rapids * (0.15 + foamStreak * 0.8)) * riverDeep);
        }
        else
        {
            // ice: still, whitish, broken by cracks
            float crackValue = cloudNoise.Value(WorldPos / max(RiverIceCrackScale, 0.1) + 13.7);
            float crack = 1.0 - smoothstep(0.0, 0.035, abs(crackValue - 0.5));
            riverColor = lerp(riverColor, RiverIceColor.rgb, smoothstep(0.0, max(riverHalf * 0.3, 0.5), fromEdge) * 0.85 + 0.15) * (1.0 - crack * RiverIceCrack);
            float3 iceBend = float3(cloudNoise.Value(WorldPos / 6.0 + 2.0), cloudNoise.Value(WorldPos / 6.0 + 5.0), cloudNoise.Value(WorldPos / 6.0 + 9.0)) - 0.5;
            riverNormal = normalize(surfaceUp + (iceBend - surfaceUp * dot(iceBend, surfaceUp)) * 0.15);
        }

        albedo = lerp(albedo, riverColor, riverWater);
        riverShown = riverWater;
        OutNormal = normalize(lerp(OutNormal, riverNormal, riverWater));
        OutRoughness = lerp(Roughness, riverIce > 0.5 ? RiverIceRoughness : RiverRoughness, riverWater * (1.0 - rapids * 0.6));
    }
}
'''

# below the sea the line of the seabed fades out over GridSeaFade, as M_HexPlanetWaterGrid draws it on the water, and so do the outlines,
# but where the debug view hides the water, which hides that strip too, the seabed draws both
PLANET_CODE = CLOUD_SHADOW_CODE + GRID_LINE_CODE + MARK_CODE.replace('MARK_HOLE_BITS', '0u') + BIOME_CODE + RUNUP_CODE + CANOPY_CODE + RIVER_CODE + RUNUP_APPLY_CODE + r'''
float seaFade = lerp(saturate(1.0 + PeakShare / GridSeaFade), 1.0, waterHidden);
// the grid line along a side with a river is the river itself, so the water hides it
float opacity = GridOpacity * (1.0 - smoothstep(GridFadeStart, max(GridFadeEnd, GridFadeStart + 1e-3), PeakShare)) * seaFade * (1.0 - riverShown);
float3 color = lerp(albedo, albedo * GridDarkness, gridLine * opacity);
// under the sea the outline is hidden at once, as clear water would show it on the seabed, apart from the one M_HexPlanetWaterGrid draws
float markOpacity = markLine * lerp(smoothstep(-0.01, 0.0, PeakShare), 1.0, waterHidden);
// the alpha of the vertex colors lights only the tiles of painting, as on the surface it holds which way a river flows (RIVER_CODE)
OutEmissive = lerp(color * (Ambient + Glow * Alpha * PreviewTile), markColor * MarkGlow, markOpacity);
color = lerp(color, markColor, markOpacity);
return color * lerp(cloudShadow, 1.0, markOpacity);
'''


# The water of the planet is a copy of the open sea of WATER_CODE in build_hex_materials.py: swell, ripples, the colors from lagoon to deep sea,
# the lighter side of waves and the glints of the sun, and flat ice over the frozen cells. Floes, the foam of the shore and the surf come below; cliffs are not here.
# What the flat map takes from its plane comes from the sphere instead:
# - noise is sampled from three planes of the world position, blended by the normal of the planet, as the sphere has no seamless 2D coordinates
# - waves run along 3D directions, and their slope is the part along the surface, which fades a wave where its direction points into the sphere
# - the depth of the seabed and the distance to land come from the cubemap SeaField, sampled by the direction from the center (HexPlanetSeaField)
# - the sun comes from the light of the level through SunDirection, and the water darkens on the night side
# TRI_NOISE samples T_HexNoise (HexNoise), the noise of water on the flat map, at a scale, an offset and a blur of the mip
TRIPLANAR = '''
float3 planetUp = normalize(WorldPos);
float3 planes = pow(abs(planetUp), 8.0);
planes /= planes.x + planes.y + planes.z;
#define TRI_NOISE(P, S, O, G) (HexNoise.SampleGrad(HexNoiseSampler, (P).yz * (S) + (O), tdx.yz * (S) * (G), tdy.yz * (S) * (G)).r * planes.x \\
    + HexNoise.SampleGrad(HexNoiseSampler, (P).xz * (S) + (O), tdx.xz * (S) * (G), tdy.xz * (S) * (G)).r * planes.y \\
    + HexNoise.SampleGrad(HexNoiseSampler, (P).xy * (S) + (O), tdx.xy * (S) * (G), tdy.xy * (S) * (G)).r * planes.z)
#define TRI_NOISE_LEVEL(P, S, O, M) (HexNoise.SampleLevel(HexNoiseSampler, (P).yz * (S) + (O), M).r * planes.x \\
    + HexNoise.SampleLevel(HexNoiseSampler, (P).xz * (S) + (O), M).r * planes.y \\
    + HexNoise.SampleLevel(HexNoiseSampler, (P).xy * (S) + (O), M).r * planes.z)
'''

WATER_CODE = TRIPLANAR + CLOUD_SHADOW_CODE + SHORE_HASH_CODE + '''
float3 tp = WorldPos / TileSize;
float3 tdx = PosDdx / TileSize;
float3 tdy = PosDdy / TileSize;
float aa = max((length(PosDdx) + length(PosDdy)) * 0.5, 0.001);

// red is the depth of the seabed / 100, as the alpha of the shore field of the flat map, alpha the signed distance to the coast ((distance + 50) / 450,
// negative on land) and green how much
// its lines bend around land (HexPlanetSeaField)
float4 sea = SeaField.SampleLevel(SeaFieldSampler, planetUp, 0);
float softDistance = sea.a * 450.0 - 50.0;
float shallowCalm = 1.0 - smoothstep(CalmDepthStart, CalmDepthEnd, sea.r * 100.0);
// the frozen sea, the blue of the sea field blended between cells, whose edge a noise moves so it does not follow the cells. Its water is open,
// as floes float on it, but calmer (IceWaveShare), as WATER_OFFSET_CODE also makes its waves lower
// the noise takes one blurred mip (PatchMip), as its fine detail speckles the tone
float fadeNoise = TRI_NOISE_LEVEL(tp, FadeNoiseScale, 0.0, PatchMip);
float ice = smoothstep(0.5 - IceEdgeSoftness, 0.5 + IceEdgeSoftness, sea.b + (fadeNoise - 0.5) * IceEdgeNoise);
float iceCalm = lerp(1.0, IceWaveShare, ice);
// the dark cold water fades in over much more of the way between the cells (IceWaterFade), so open and frozen sea do not meet on a line
float iceWater = smoothstep(0.5 - IceWaterFade, 0.5 + IceWaterFade, sea.b + (fadeNoise - 0.5) * IceEdgeNoise);

// WaveScale stretches every wave and ripple, as the camera of the planet looks from further than the flat one
float waveNumber = 1.0 / max(WaveScale, 0.1);
// long swells, bent by noise so that they never look like straight lines. Their part along the surface is their slope.
// A wave is (frequency, speed, slope, height)
// the noise that bends them is taken at the same mip from any camera (SwellBendMip), as the bend moves their phase so far that a mip chosen
// by the size of a pixel would make them run as the camera moves. WATER_OFFSET_CODE bends them alike
float bendWide = TRI_NOISE_LEVEL(tp, WideNoiseScale, Time * float2(0.0011, 0.0007), SwellBendMip);
float bendMiddle = TRI_NOISE_LEVEL(tp, MiddleNoiseScale, -Time * float2(0.0016, 0.0009), SwellBendMip);
float3 swellPos = WorldPos + (float3(bendWide, bendMiddle, (bendWide + bendMiddle) * 0.5) - 0.5) * SwellBend * WaveScale;
float3 swellA = normalize(float3(0.8, 0.6, 0.3));
float3 swellB = normalize(float3(0.45, 0.9, -0.4));
float3 swellC = normalize(float3(0.95, -0.3, 0.5));
float3 alongA = swellA - planetUp * dot(swellA, planetUp);
float3 alongB = swellB - planetUp * dot(swellB, planetUp);
float3 alongC = swellC - planetUp * dot(swellC, planetUp);
float3 slope = alongA * cos(dot(swellPos, swellA) * SwellWaveA.x * waveNumber - Time * SwellWaveA.y) * SwellWaveA.z
    + alongB * cos(dot(swellPos, swellB) * SwellWaveB.x * waveNumber - Time * SwellWaveB.y) * SwellWaveB.z
    + alongC * cos(dot(swellPos, swellC) * SwellWaveC.x * waveNumber - Time * SwellWaveC.y) * SwellWaveC.z;

// from close by shorter waves show too, and the water moves more, while from far away it calms down, as dense waves only shimmer there
float closeUp = 1.0 - smoothstep(CloseUpStart, max(CloseUpEnd, CloseUpStart + 0.01), aa);
float farCalm = smoothstep(FarCalmStart, FarCalmStart * 3.0, aa);
float3 chopA = normalize(float3(-0.5, 0.85, 0.2));
float3 chopB = normalize(float3(0.9, 0.4, -0.6));
slope += ((chopA - planetUp * dot(chopA, planetUp)) * cos(dot(swellPos, chopA) * ChopWaveA.x * waveNumber - Time * ChopWaveA.y) * ChopWaveA.z
    + (chopB - planetUp * dot(chopB, planetUp)) * cos(dot(swellPos, chopB) * ChopWaveB.x * waveNumber - Time * ChopWaveB.y) * ChopWaveB.z) * closeUp;
slope *= lerp(1.0, ShallowCalm.x, shallowCalm) * iceCalm;
// everything seen on the water sways with the swell, which is what makes it look afloat from close by
float3 swayP = tp + slope * WaterSway * WaveScale;

// ripples from the normal layer, each plane giving its two directions
float rippleScaleA = RippleScaleA * waveNumber;
float rippleScaleB = RippleScaleB * waveNumber;
float3 rippleA = (Normals.SampleGrad(NormalsSampler, float3(swayP.yz * rippleScaleA + Time * float2(0.012, 0.007), 1.0), tdx.yz * rippleScaleA, tdy.yz * rippleScaleA).xxy - 0.5) * float3(0.0, 1.0, 1.0) * planes.x
    + (Normals.SampleGrad(NormalsSampler, float3(swayP.xz * rippleScaleA + Time * float2(0.012, 0.007), 1.0), tdx.xz * rippleScaleA, tdy.xz * rippleScaleA).xxy - 0.5) * float3(1.0, 0.0, 1.0) * planes.y
    + (Normals.SampleGrad(NormalsSampler, float3(swayP.xy * rippleScaleA + Time * float2(0.012, 0.007), 1.0), tdx.xy * rippleScaleA, tdy.xy * rippleScaleA).xyy - 0.5) * float3(1.0, 1.0, 0.0) * planes.z;
float3 rippleB = (Normals.SampleGrad(NormalsSampler, float3(swayP.yz * rippleScaleB - Time * float2(0.008, 0.01), 1.0), tdx.yz * rippleScaleB, tdy.yz * rippleScaleB).xxy - 0.5) * float3(0.0, 1.0, 1.0) * planes.x
    + (Normals.SampleGrad(NormalsSampler, float3(swayP.xz * rippleScaleB - Time * float2(0.008, 0.01), 1.0), tdx.xz * rippleScaleB, tdy.xz * rippleScaleB).xxy - 0.5) * float3(1.0, 0.0, 1.0) * planes.y
    + (Normals.SampleGrad(NormalsSampler, float3(swayP.xy * rippleScaleB - Time * float2(0.008, 0.01), 1.0), tdx.xy * rippleScaleB, tdy.xy * rippleScaleB).xyy - 0.5) * float3(1.0, 1.0, 0.0) * planes.z;
float3 ripple = (rippleA + rippleB) * lerp(1.0, ShallowCalm.y, shallowCalm) * iceCalm;
ripple -= planetUp * dot(ripple, planetUp);
float3 waterNormal = normalize(planetUp + slope * lerp(NormalStrength.x, NormalStrength.y, closeUp) * lerp(1.0, FarCalm.x, farCalm)
    + ripple * lerp(NormalStrength.z, NormalStrength.w, closeUp) * lerp(1.0, FarCalm.y, farCalm));
PLUME_CODE

// from the shore: clear lagoon, then turquoise, then the blue of the open sea, whose tone drifts in wide patches.
// The water grows opaque with the depth of the seabed under it, a little unevenly by place
// the patches and the unevenness take their noise at one blurred mip (PatchMip), as its fine detail speckles the tone
float patchWide = TRI_NOISE_LEVEL(tp, WideNoiseScale, Time * float2(0.0011, 0.0007), PatchMip);
float patchMiddle = TRI_NOISE_LEVEL(tp, MiddleNoiseScale, -Time * float2(0.0016, 0.0009), PatchMip);
float seabedDepth = sea.r * 100.0 + (fadeNoise - 0.5) * SeabedDepthNoise;
float depthAmount = smoothstep(WaterClearDepth, WaterOpaqueDepth, seabedDepth);
float opacity = lerp(Clearness, 1.0, depthAmount);
float3 shallow = lerp(LagoonColor.rgb, ShallowColor.rgb, smoothstep(LagoonDistanceStart, LagoonDistanceEnd, softDistance));
float patches = saturate((patchWide - 0.5) * PatchContrast + 0.5);
float patchesFine = saturate((patchMiddle - 0.5) * PatchFineContrast + 0.5);
float3 deepColor = lerp(DeepColorDark.rgb, DeepColorLight.rgb, lerp(patches, patchesFine, PatchFineWeight));
deepColor = lerp(OffshoreColor.rgb, deepColor, smoothstep(OffshoreDistanceStart, OffshoreDistanceEnd, softDistance));
float3 color = lerp(shallow, deepColor, depthAmount);
// the water of the frozen sea is dark and cold, and deep enough to hide the seabed
color = lerp(color, IceWaterColor.rgb, iceWater * IceWaterShare);
opacity = lerp(opacity, 1.0, iceWater * IceWaterShare);
// the silty water of a river where it pours into the sea
color = lerp(color, RiverPlumeColor.rgb, plumeCover * RiverPlumeTint);

// the sky shows on what faces away from the camera, and the side of a wave towards the sun is lighter
float3 toCamera = normalize(CameraDir);
float3 sunDirection = normalize(SunDirection);
float3 sunAlong = sunDirection - planetUp * dot(sunDirection, planetUp);
float fresnel = pow(1.0 - saturate(dot(waterNormal, toCamera)), FresnelPower);
color *= 1.0 + dot(slope, sunAlong) * WaveLight * lerp(1.0, FarCalm.z, farCalm);
color = lerp(color, SkyColor.rgb, fresnel * SkyReflection);

// glints of the sun: sparkles that live on the water, drift with it and last for seconds, so they do not flicker when the camera moves.
// The camera only decides where the path of the sun lies, from the long swell only, as finer waves would make it flicker
float3 reflected = reflect(-toCamera, normalize(planetUp + slope * NormalStrength.x));
float sunPath = lerp(SunPathMin, 1.0, smoothstep(SunPathStart, SunPathEnd, dot(reflected, sunDirection)));
float sparkleA = TRI_NOISE(swayP, GlintScaleA, Time * float2(0.02, 0.011), 2.0);
float sparkleB = TRI_NOISE(swayP, GlintScaleB, -Time * float2(0.014, 0.022), 2.0);
float sunUp = dot(planetUp, sunDirection);
// from far away each would be one pixel, which only looks like noise
color += smoothstep(GlintThresholdStart, GlintThresholdEnd, sparkleA * sparkleB) * sunPath * (1.0 - smoothstep(GlintFarStart, GlintFarEnd, aa))
    * GlintColor.rgb * GlintAmount * lerp(1.0, ShallowCalm.z, shallowCalm) * smoothstep(0.0, GlintSunHeight, sunUp);

// between the floes of the frozen sea (instances of AHexPlanet::UpdateFloes): slush and brash ice that drift with the water, foam where the
// water lies shallow over the ledge of a floe, and from far away, where the floes are not drawn, their color.
// How deep the water is over what lies behind it, the ground or a floe, along the normal of the planet, also finds the shore below, so it follows
// the real coast and not the cells. The scene depth runs along the axis of the view, so the length of the ray in the water takes the distance
// to the camera over the pixel depth
float waterDepth = max(SceneDepth - PixelDepth, 0.0) * distance(CameraPos, WorldPos) / max(PixelDepth, 1.0) * saturate(dot(toCamera, planetUp));
if (ice > 0.001)
{
    float iceShade = lerp(IceShade, 1.0, saturate(sunUp * 2.0));
    float slushA = TRI_NOISE(swayP, IceSlushScale, Time * float2(0.004, 0.003), 1.0);
    float slushB = TRI_NOISE(swayP, IceSlushScale * 2.3, -Time * float2(0.003, 0.005), 1.0);
    float slush = smoothstep(1.0 - IceSlush, 1.0 - IceSlush * 0.5, slushA * 0.6 + slushB * 0.4);
    float behind = max(SceneDepth - PixelDepth, 0.0);
    // foam over the wide ledge of a floe, laced by the slush, which lies under the water there
    float foam = (1.0 - smoothstep(0.0, IceFoamWidth, waterDepth)) * IceFoam * smoothstep(0.25, 0.6, slushB * 0.5 + slushA * 0.5);
    // the part of a floe just under the water shows through it as a lighter skirt, while the seabed further down stays hidden
    opacity = lerp(opacity, min(opacity, lerp(IceUnderClearness, 1.0, smoothstep(0.0, IceClearDepth, behind))), ice);
    float far = smoothstep(IceFarStart, max(IceFarEnd, IceFarStart + 1.0), distance(CameraPos, WorldPos)) * IceFarCover;
    color = lerp(color, IceSlushColor.rgb * iceShade, saturate(max(slush * 0.8, foam)) * ice);
    color = lerp(color, IceFarColor.rgb * iceShade, far * ice);
}

// never narrower than a few pixels of the change of the depth, so from far away a line stays a line
float depthStep = fwidth(waterDepth) * ShoreFoamPixels;
float filmDepth = max(ShoreFoamDepth, min(depthStep, ShoreFoamDepth * 2.0));
float shoreFar = smoothstep(ShoreFarStart, max(ShoreFarEnd, ShoreFarStart + 0.01), aa);
''' + SHORE_WAVE_CODE.replace('SHORE_DIR', 'planetUp') + '''
// the breakers follow the distance to the coast (alpha of the sea field), which runs evenly away from it, divided by how fast it grows (alpha
// of the shore field), so a wave rolls at an even speed right up to the shore, where the land cells around pull the soft minimum different ways
// and it grows slowly, bent a little by a noise.
// The frozen sea is calmer and has less of them
float surfDistance = (sea.a * 450.0 - 50.0) / max(shoreStretch.a, 0.35) + (cloudNoise.Value(WorldPos / max(SurfReach * 1.2, 1.0) + 11.0) - 0.5) * SurfWobble;
float surfZone = (1.0 - smoothstep(SurfReach + SurfCurl, SurfReach + SurfCurl + 40.0, surfDistance)) * (1.0 - shoreFar)
    * lerp(1.0, IceWaveShare, ice)
    // none where the lines of the distance wrap around a cape (green of the sea field), as their arcs would cross the run of the coast there;
    // close to the shore every land cell bends them a little, and the arcs follow the shore anyway
    * (1.0 - smoothstep(SurfCapeStart, max(SurfCapeEnd, SurfCapeStart + 0.01), sea.g) * smoothstep(SurfReach * 0.15, SurfReach * 0.5, softDistance));
// how fast the distance grows across the screen, in units of it per world unit, so the widths of the breakers are in world units and a line does
// not swell where the soft minimum grows slowly, close to the shore
float surfGrad = clamp(length(float2(ddx(surfDistance) / max(length(PosDdx), 1e-4), ddy(surfDistance) / max(length(PosDdy), 1e-4))), 0.1, 1.5);
float stretchFoam = stretchPresent * stretchEnds * lerp(Surf, RollerFoam, stretchRoller) * (1.0 - shoreFar) * (1.0 - plumeCover * RiverPlumeCalm);
if (waterDepth < max(BackwashDepth, filmDepth) * 1.5 || surfZone * stretchFoam > 0.001)
{
    float laceNoise = TRI_NOISE(swayP, ShoreLaceScale, Time * float2(0.006, -0.004), 1.0);
    float lace = lerp(1.0, smoothstep(0.3, 0.7, laceNoise), ShoreLace);
    // a film where the water touches the ground or a floe
    float shore = (1.0 - smoothstep(0.0, filmDepth, waterDepth)) * ShoreFoam * lerp(1.0, ShoreFarFilm, shoreFar) * lerp(1.0, lace, 0.5);
    if (surfZone * stretchFoam > 0.001)
    {
        // the breaker of the stretch: it forms SurfReach from land and rolls in over SurfRunShare of SurfPeriod, a thin line (SurfWidthStart to
        // SurfWidthEnd world units) that grows from nothing to full white, a crisp front towards land with a short tail of streaks behind it.
        // Towards the ends of its stretch it starts up to SurfCurl further out and catches up by the shore zone, so it bends into a C
        // towards land. Over the last SurfShoreDistance before the coast line of the field it speeds up (SHORE_WAVE_CODE), and at full strength it
        // rolls on to the real edge of the water, wherever that lies against the line, where just then its water runs up the beach (RUNUP_CODE)
        float far = saturate(stretchTime / stretchFarTime);
        float offset = surfDistance - stretchCrest - SurfCurl * (1.0 - stretchEnds) * (1.0 - far);
        float width = lerp(SurfWidthStart, SurfWidthEnd, far) * surfGrad;
        float front = offset < 0.0 ? 1.0 - smoothstep(0.0, width * 0.35, -offset) : 1.0 - smoothstep(width * 0.5, width, offset);
        float tail = offset > width * 0.5 ? exp(-(offset - width * 0.5) / max(width * 1.5, 1.0)) * smoothstep(0.45, 0.65, laceNoise) * 0.7 : 0.0;
        // at full strength right up to the edge of the water, then on under the land, whose ground hides it; well past any shore, where the
        // field ends 50 below the coast line, it is gone
        float grow = smoothstep(0.0, 0.3, far) * lerp(0.4, 1.0, far) * (1.0 - smoothstep(40.0, 48.0, -stretchCrest));
        shore = max(shore, max(front, tail) * grow * stretchFoam * surfZone);
    }
    // as the run-up slides back down the beach its foam slides back into the water, a thin laced sheet over the edge that thins out and fades,
    // timed as on the beach, from when the crest reached the edge of the water here
    float backAge = stretchTime - SHORE_CREST_TIME(surfDistance) - RunupRiseTime;
    float backShare = saturate(backAge / max(RunupFallTime, 0.1));
    float backwash = (1.0 - smoothstep(0.0, BackwashDepth * lerp(0.4, 1.0, backShare) + 0.01, waterDepth)) * smoothstep(0.35, 0.75, backShare)
        * (1.0 - smoothstep(0.8, 1.0, backShare)) * smoothstep(0.25 + backShare * 0.35, 0.4 + backShare * 0.35, laceNoise);
    shore = max(shore, backwash * (backAge > 0.0 ? stretchFoam : 0.0) * (1.0 - smoothstep(0.95, 1.0, stretchProgress)));
    // where a river pours in, its water pushes the foam of the shore away
    shore *= 1.0 - plumeCover * RiverPlumeCalm;
    color = lerp(color, ShoreFoamColor.rgb, shore);
    opacity = max(opacity, shore);
}

// the lines of the river running out into the sea
color = lerp(color, ShoreFoamColor.rgb, saturate(plumeLines * RiverPlumeFoam));
opacity = max(opacity, saturate(plumeLines * RiverPlumeFoam));

// unlit water keeps its colors, so it darkens by itself where the sun is low or gone, and under clouds, which also dim its glints
color *= lerp(NightBrightness, 1.0, smoothstep(NightStart, NightEnd, sunUp)) * cloudShadow;
// the haze of the air over it, as it is drawn after the post process that lays it over the land (HAZE_CODE)
float3 hazeCam = CameraPos;
float hazeEnd = length(WorldPos - CameraPos);
float3 hazeDir = (WorldPos - CameraPos) / max(hazeEnd, 1e-3);
HAZE_SNIPPET
color = lerp(color, hazeColor, hazeAmount);
OutOpacity = opacity;
HIDE_CODE
return color;
#undef TRI_NOISE
#undef TRI_NOISE_LEVEL
#undef SHORE_CREST_TIME
'''

# the mouths of rivers the water takes, MaxRiverMouths of AHexPlanet
RIVER_MOUTHS = 8
PLUME_MOUTH = r'''
if (RiverMouth%(i)d.w > 0.5)
{
    float3 mouthRel = WorldPos - RiverMouth%(i)d.xyz;
    float3 mouthDir = RiverMouthDir%(i)d.xyz;
    float mouthAlong = dot(mouthRel, mouthDir);
    float mouthAcross = dot(mouthRel, cross(planetUp, mouthDir));
    float mouthHalf = RiverPlumeWidth + max(mouthAlong, 0.0) * RiverPlumeSpread;
    [branch] if (mouthAlong > -RiverPlumeBack && mouthAlong < RiverPlumeLength && abs(mouthAcross) < mouthHalf)
    {
        float cover = smoothstep(-RiverPlumeBack, -RiverPlumeBack * 0.4, mouthAlong) * (1.0 - smoothstep(RiverPlumeLength * 0.3, RiverPlumeLength, mouthAlong))
            * (1.0 - smoothstep(mouthHalf * 0.5, mouthHalf, abs(mouthAcross)));
        // the lines fan out from a point behind the mouth, so each keeps its share of the width, and wander more the further out they run
        float3 mouthOut = WorldPos - (RiverMouth%(i)d.xyz - mouthDir * (RiverPlumeWidth / max(RiverPlumeSpread, 0.01)));
        mouthOut = normalize(mouthOut - planetUp * dot(mouthOut, planetUp) + mouthDir * 1e-3);
        float wander = (cloudNoise.Value(WorldPos / max(RiverPlumeDashLength * 1.5, 1.0) + %(i)d.0 * 7.3) - 0.5) * RiverPlumeWobble * saturate(mouthAlong / RiverPlumeLength + 0.2);
        float lane = (mouthAcross / mouthHalf + wander) * RiverPlumeLanes * 0.5;
        float laneLine = 1.0 - smoothstep(RiverPlumeLineWidth * 0.5, RiverPlumeLineWidth, abs(frac(lane) - 0.5) * 2.0);
        float dash = 0.0;
        float3 chop = 0.0;
        [unroll] for (int ph = 0; ph < 2; ++ph)
        {
            float3 carried = WorldPos - mouthOut * (plumePhases[ph] - 0.5) * RiverPlumeSpeed * 2.0;
            float weight = 1.0 - abs(plumePhases[ph] * 2.0 - 1.0);
            dash += cloudNoise.Value(carried / max(RiverPlumeDashLength, 0.1) + floor(lane) * 3.7 + ph * 11.1) * weight;
            float3 q = carried / max(RiverPlumeDashLength * 0.4, 0.1) + ph * 5.7;
            chop += (float3(cloudNoise.Value(q), cloudNoise.Value(q + 3.1), cloudNoise.Value(q + 6.2)) - 0.5) * weight;
        }
        // thinner than a pixel from far away they would only flicker
        float laneWidth = mouthHalf * 2.0 / max(RiverPlumeLanes, 1.0);
        float lines = laneLine * smoothstep(0.45, 0.7, dash) * cover * (1.0 - smoothstep(0.5, 1.5, aa / max(laneWidth * RiverPlumeLineWidth, 0.01)));
        // and where the river meets the sea it churns into froth
        float froth = smoothstep(0.42, 0.68, dash * 0.6 + cloudNoise.Value(WorldPos / max(RiverPlumeDashLength * 0.35, 0.1) - plumePhases.x * 0.3) * 0.4)
            * (1.0 - smoothstep(RiverPlumeLength * 0.05, RiverPlumeLength * 0.3, mouthAlong)) * smoothstep(-RiverPlumeBack * 0.6, 0.0, mouthAlong) * cover * RiverPlumeFroth;
        plumeCover = max(plumeCover, cover);
        plumeLines = max(plumeLines, max(lines, froth));
        plumeChop += (chop - planetUp * dot(chop, planetUp)) * cover;
    }
}
'''

PLUME_CODE = r'''
// the water a river pours into the sea, from the mouths of open rivers nearest the camera (RiverMouth0.., RiverMouthDir0.., which
// AHexPlanet::UpdateRiverMouths gives): a fan RiverPlumeWidth wide at the mouth that spreads by RiverPlumeSpread out to RiverPlumeLength,
// beginning RiverPlumeBack inland, so it covers the edge of the water there. In it the foam of the shore and the breakers give way
// (RiverPlumeCalm), the water is rougher and silty, and lines in RiverPlumeLanes lanes run out with the river at RiverPlumeSpeed
float plumeCover = 0.0;
float plumeLines = 0.0;
float3 plumeChop = 0.0;
float2 plumePhases = frac(Time / 2.0 + float2(0.0, 0.5));
''' + ''.join(PLUME_MOUTH % {'i': i} for i in range(RIVER_MOUTHS)) + r'''
waterNormal = normalize(waterNormal + plumeChop * RiverPlumeChop);
'''
WATER_CODE = WATER_CODE.replace('PLUME_CODE', PLUME_CODE)

# the debug view hex.Water hides the water of the cell under the mouse, and maybe its neighbors: AHexPlanet gives the centers of that cell,
# its neighbors and theirs, and the first HideCount of them are hidden. A pixel lies in the cell whose center is nearest
HIDE_CELLS = 19
WATER_CODE = WATER_CODE.replace('HIDE_CODE', '''if (HideCount > 0.5)
{
    float hideBest = -2.0;
    float hideIndex = 99.0;
''' + ''.join('    if (dot(planetUp, HideCell%d.xyz) > hideBest) { hideBest = dot(planetUp, HideCell%d.xyz); hideIndex = %d.0; }\n' % (i, i, i) for i in range(HIDE_CELLS)) + '''    OutOpacity = hideIndex < HideCount ? 0.0 : OutOpacity;
}''').replace('HAZE_SNIPPET', HAZE_CODE)

# the mask of M_HexPlanet_Cut, which AHexPlanet uses while only the triangles of the cells around the cursor are drawn, so the rest keeps its normal render:
# 0 in the first HideCount of the cells, found by the nearest center as the water does. M_HexPlanet is opaque and ignores it
CUT_CODE = '''float3 planetUp = normalize(WorldPos);
float keep = 1.0;
LOD_MASK
if (HideCount < 0.5) { return keep; }
float hideBest = -2.0;
float hideIndex = 99.0;
''' + ''.join('if (dot(planetUp, HideCell%d.xyz) > hideBest) { hideBest = dot(planetUp, HideCell%d.xyz); hideIndex = %d.0; }\n' % (i, i, i) for i in range(HIDE_CELLS)) + '''return hideIndex < HideCount ? 0.0 : keep;
'''

# the surface of M_HexPlanet_Cut also hands a chunk over between its pieces and its coarse mesh: while the chunk lies within CoarseBand of
# CoarseDistance from the camera, AHexPlanet draws both, the pieces with LodSide 1 and the coarse mesh with LodSide 2, and each keeps the pixels the
# other drops, by a noise of the pixel that changes every frame, which the temporal filter blends, against how far the point lies across that
# band from LodCamera, the camera that AHexPlanet gives, as in the shadow pass the camera of the view is the light
SURFACE_CUT_CODE = CUT_CODE.replace('LOD_MASK', '''if (LodSide > 0.5)
{
    float lodShare = saturate((distance(WorldPos, LodCamera.xyz) - (CoarseDistance - CoarseBand)) / max(2.0 * CoarseBand, 1.0));
    float2 lodPixel = Parameters.SvPosition.xy + 5.588238 * (float)View.StateFrameIndexMod8;
    float lodNoise = frac(52.9829189 * frac(dot(lodPixel, float2(0.06711056, 0.00583715))));
    keep = ((LodSide < 1.5) == (lodShare < lodNoise)) ? 1.0 : 0.0;
}''')
CUT_CODE = CUT_CODE.replace('LOD_MASK\n', '')

# the grid lines of the cells of the sea over the water, from the strips of AHexPlanet::BuildWaterGrid: translucent black, so the water keeps
# WaterGridDarkness of its color under a line, as the surface keeps GridDarkness, and hidden with the water of the cells that hex.Water hides
# and the outlines of marked cells of the sea, in their colors (MARK_CODE)
WATER_GRID_CODE = GRID_LINE_CODE + MARK_CODE.replace('MARK_HOLE_BITS', '(uint)round(markPixel.a * 255.0)') + '''
float lineOpacity = gridLine * GridOpacity * (1.0 - WaterGridDarkness);
float opacity = lerp(lineOpacity, 1.0, markLine);
OutColor = markColor * markLine / max(opacity, 1e-4);
return opacity * Visible;
'''

# the debug wireframe of the planet: one color for the whole surface, or the colors of the vertices for the cells around the mouse.
# A point past the horizon of the planet faces away from the camera, and its triangles are hidden, as no surface covers them while only they are drawn
WIRE_CODE = '''
float3 color = UseVertexColor > 0.5 ? pow(max(VertexColor, 0.0), 2.2) : WireColor.rgb;
OutOpacity = WireOpacity * step(0.0, dot(WorldPos, CameraPos - WorldPos));
return color;
'''

# the triangles are drawn a little towards the camera, more from further away, so they do not fight with the surface they lie on
WIRE_OFFSET_CODE = '''
float3 toCamera = CameraPos - WorldPos;
return normalize(toCamera) * WireBias * max(1.0, length(toCamera) / 1000.0);
'''

# the real height of the swell of WATER_CODE, with the same waves, along the normal of the planet. Lower over shallows and close to land
WATER_OFFSET_CODE = TRIPLANAR + '''
// the cap turns with the camera, so its vertices slide over the waves, and what changes faster than their spacing (Spacing, the U of the cap)
// would flicker fast as the camera moves. Each wave fades out of the height as the step of its phase from one vertex to the next nears a
// wavelength, measured at two points a spacing away, as the noise bends the waves, which shortens them. What fades stays in the normal of WATER_CODE.
// The noise is at the same mip as there (SwellBendMip), the same from any camera
float spacing = max(Spacing.x, 1.0);
float waveNumber = 1.0 / max(WaveScale, 0.1);
float3 sideA = normalize(cross(planetUp, abs(planetUp.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
float3 sideB = cross(planetUp, sideA);
float3 points[3] = { WorldPos, WorldPos + sideA * spacing, WorldPos + sideB * spacing };
float3 bent[3];
[unroll] for (int index = 0; index < 3; ++index)
{
    float3 tp = points[index] / TileSize;
    float noiseWide = TRI_NOISE_LEVEL(tp, WideNoiseScale, Time * float2(0.0011, 0.0007), SwellBendMip);
    float noiseMiddle = TRI_NOISE_LEVEL(tp, MiddleNoiseScale, -Time * float2(0.0016, 0.0009), SwellBendMip);
    bent[index] = points[index] + (float3(noiseWide, noiseMiddle, (noiseWide + noiseMiddle) * 0.5) - 0.5) * SwellBend * WaveScale;
}
float3 swellPos = bent[0];
float3 swellPosA = bent[1];
float3 swellPosB = bent[2];
float3 swellA = normalize(float3(0.8, 0.6, 0.3));
float3 swellB = normalize(float3(0.45, 0.9, -0.4));
float3 swellC = normalize(float3(0.95, -0.3, 0.5));
float3 chopA = normalize(float3(-0.5, 0.85, 0.2));
float3 chopB = normalize(float3(0.9, 0.4, -0.6));
// a wave whose direction points into the sphere has no slope there, so it has no height either. Longer waves stand as steep, so they are higher
#define VERTEX_FADE(W, D) (1.0 - smoothstep(VertexWaveFadeStart, max(VertexWaveFadeEnd, VertexWaveFadeStart + 0.01), \\
    max(abs(dot(swellPosA - swellPos, D)), abs(dot(swellPosB - swellPos, D))) * (W).x * waveNumber / 6.2831853))
float height = sin(dot(swellPos, swellA) * SwellWaveA.x * waveNumber - Time * SwellWaveA.y) * SwellWaveA.w * length(swellA - planetUp * dot(swellA, planetUp)) * VERTEX_FADE(SwellWaveA, swellA)
    + sin(dot(swellPos, swellB) * SwellWaveB.x * waveNumber - Time * SwellWaveB.y) * SwellWaveB.w * length(swellB - planetUp * dot(swellB, planetUp)) * VERTEX_FADE(SwellWaveB, swellB)
    + sin(dot(swellPos, swellC) * SwellWaveC.x * waveNumber - Time * SwellWaveC.y) * SwellWaveC.w * length(swellC - planetUp * dot(swellC, planetUp)) * VERTEX_FADE(SwellWaveC, swellC);
float closeUp = 1.0 - smoothstep(ChopLiftStart, max(ChopLiftEnd, ChopLiftStart + 1.0), length(CameraPos - WorldPos));
height += (sin(dot(swellPos, chopA) * ChopWaveA.x * waveNumber - Time * ChopWaveA.y) * ChopWaveA.w * length(chopA - planetUp * dot(chopA, planetUp)) * VERTEX_FADE(ChopWaveA, chopA)
    + sin(dot(swellPos, chopB) * ChopWaveB.x * waveNumber - Time * ChopWaveB.y) * ChopWaveB.w * length(chopB - planetUp * dot(chopB, planetUp)) * VERTEX_FADE(ChopWaveB, chopB)) * closeUp;
#undef VERTEX_FADE
height *= WaveScale;
float4 sea = SeaField.SampleLevel(SeaFieldSampler, planetUp, 0);
height *= lerp(1.0, ShallowCalm.w, 1.0 - smoothstep(CalmDepthStart, CalmDepthEnd, sea.r * 100.0));
height *= lerp(NearLandHeight, 1.0, smoothstep(LagoonDistanceStart, LagoonDistanceEnd, sea.a * 450.0 - 50.0));
// lower waves on the frozen sea, where floes float (WATER_CODE)
height *= lerp(1.0, IceWaveShare, smoothstep(0.5 - IceEdgeSoftness, 0.5 + IceEdgeSoftness, sea.b));
return planetUp * height * WaveHeight;
#undef TRI_NOISE
#undef TRI_NOISE_LEVEL
'''

# the values of UHexPlanetWaterConfig that WATER_CODE and WATER_OFFSET_CODE take
WATER_PARAMETERS = ['TileSize', 'WaveScale', 'WideNoiseScale', 'MiddleNoiseScale', 'SwellBend', 'SwellBendMip', 'SwellWaveA', 'SwellWaveB', 'SwellWaveC',
    'ChopWaveA', 'ChopWaveB', 'CloseUpStart', 'CloseUpEnd', 'WaterSway', 'RippleScaleA', 'RippleScaleB', 'NormalStrength', 'FarCalmStart', 'FarCalm',
    'CalmDepthStart', 'CalmDepthEnd', 'ShallowCalm', 'Clearness', 'WaterClearDepth', 'WaterOpaqueDepth', 'SeabedDepthNoise', 'FadeNoiseScale',
    'LagoonColor', 'ShallowColor', 'LagoonDistanceStart', 'LagoonDistanceEnd', 'OffshoreColor', 'OffshoreDistanceStart', 'OffshoreDistanceEnd',
    'DeepColorDark', 'DeepColorLight', 'PatchContrast', 'PatchFineContrast', 'PatchFineWeight', 'PatchMip', 'WaveLight', 'SkyColor', 'SkyReflection',
    'FresnelPower', 'NightBrightness', 'NightStart', 'NightEnd', 'GlintAmount', 'GlintColor', 'GlintScaleA', 'GlintScaleB', 'GlintThresholdStart',
    'GlintThresholdEnd', 'SunPathMin', 'SunPathStart', 'SunPathEnd', 'GlintFarStart', 'GlintFarEnd', 'GlintSunHeight', 'IceWaterColor', 'IceWaterShare',
    'IceWaveShare', 'IceSlushColor', 'IceSlush', 'IceSlushScale', 'IceFoam', 'IceFoamWidth', 'IceUnderClearness', 'IceClearDepth', 'IceFarColor', 'IceFarCover', 'IceFarStart', 'IceFarEnd',
    'IceEdgeSoftness', 'IceEdgeNoise', 'IceWaterFade', 'IceShade', 'ShoreFoamColor', 'ShoreFoam', 'ShoreFoamDepth', 'ShoreFoamPixels', 'ShoreLaceScale', 'ShoreLace',
    'ShoreFarStart', 'ShoreFarEnd', 'ShoreFarFilm', 'Surf', 'SurfChance', 'SurfReach', 'SurfPeriod', 'SurfRunShare', 'SurfWidthStart', 'SurfWidthEnd',
    'SurfBorderStart', 'SurfBorderEnd', 'SurfLength', 'SurfShoreDistance', 'SurfShoreSpeed', 'SurfCurl', 'SurfWobble', 'SurfCapeStart', 'SurfCapeEnd', 'RollerChance', 'RollerFoam', 'BackwashDepth', 'RunupRiseTime', 'RunupFallTime',
    'RiverPlumeLength', 'RiverPlumeWidth', 'RiverPlumeSpread', 'RiverPlumeBack', 'RiverPlumeCalm', 'RiverPlumeLanes', 'RiverPlumeLineWidth',
    'RiverPlumeDashLength', 'RiverPlumeSpeed', 'RiverPlumeFoam', 'RiverPlumeColor', 'RiverPlumeTint', 'RiverPlumeChop', 'RiverPlumeWobble', 'RiverPlumeFroth']
WATER_OFFSET_PARAMETERS = ['TileSize', 'WaveScale', 'WaveHeight', 'WideNoiseScale', 'MiddleNoiseScale', 'SwellBend', 'SwellBendMip', 'SwellWaveA', 'SwellWaveB',
    'SwellWaveC', 'ChopWaveA', 'ChopWaveB', 'ChopLiftStart', 'ChopLiftEnd', 'VertexWaveFadeStart', 'VertexWaveFadeEnd', 'CalmDepthStart', 'CalmDepthEnd', 'ShallowCalm', 'NearLandHeight',
    'LagoonDistanceStart', 'LagoonDistanceEnd', 'IceEdgeSoftness', 'IceWaveShare']

COLORS = '/Game/HexMap/Textures/Biomes/TA_Biome_Color'
NORMALS = '/Game/HexMap/Textures/Biomes/TA_Biome_Normal'
# the noise of water and ice, once the alpha of the snow layer, so an artist can change the snow (build_biome_textures.py)
NOISE = '/Game/HexMap/Textures/T_HexNoise'
DEFAULT_CUBE = '/Engine/EngineResources/GrayTextureCube'
# AHexPlanet gives the materials its CellMarks, read with Load, so the default only has to be a 2D texture
DEFAULT_MARKS = '/Engine/EngineResources/Black'
MARK_PARAMETERS = ['NeighborOutlineColor', 'WaterHideOutlineColor', 'MarkWidth', 'MarkMinPixels', 'MarkGlow']
WATER_MATERIAL = FOLDER + '/M_HexPlanetWater'
WIRE_MATERIAL = FOLDER + '/M_HexPlanetWireframe'
WATER_GRID_MATERIAL = FOLDER + '/M_HexPlanetWaterGrid'
CLOUD_MATERIAL = FOLDER + '/M_HexPlanetClouds'
HAZE_MATERIAL = FOLDER + '/M_HexPlanetHaze'
CONFIGS = [(FOLDER + '/DA_HexPlanet', unreal.HexPlanetConfig), (FOLDER + '/DA_HexPlanetWater', unreal.HexPlanetWaterConfig),
    (FOLDER + '/DA_HexPlanetClouds', unreal.HexPlanetCloudConfig), (FOLDER + '/DA_HexPlanetCamera', unreal.HexPlanetCameraConfig)]
# the config of the underground, which the surface gives the underground it spawns: made once as a copy of DA_HexPlanet, so its cells are as
# large and its surface is drawn alike, with these values, and then tuned by hand. Its features are off, as it has no sea and no sky
UNDERGROUND_CONFIG = FOLDER + '/DA_HexUnderground'
UNDERGROUND_VALUES = {
    'sea': False, 'clouds': False, 'haze': False, 'auroras': False, 'rivers': False, 'generate_objects': False, 'fog': True,
    # a passage holds its sides and a void carries its whole cliff, so passages stay flat ledges over the voids
    'edge_rule': unreal.HexEdgeRule.HIGHER_HOLDS,
    'deep_depth': 200.0,
    # a dim, cool light of the sky, a placeholder until the light of the game is designed
    'sky_light_color': unreal.LinearColor(0.32, 0.42, 0.48, 1.0), 'sky_light_intensity': 0.3,
}


def clear_expressions(material):
    """Deletes every expression of a material. One call of delete_all_material_expressions deletes only about half of them in this engine,
    and the rest would stay in the asset unconnected, growing it with every run"""
    library = unreal.MaterialEditingLibrary
    for _ in range(64):
        if library.get_num_material_expressions(material) == 0:
            return
        library.delete_all_material_expressions(material)
    unreal.log_warning('%s still has %d expressions' % (material.get_name(), library.get_num_material_expressions(material)))


def add_scalar(library, material, name, value, y):
    node = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, y)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', value)
    return node


def add_vector(library, material, name, value, y):
    node = library.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, y)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', value)
    return node


def add_vector4(library, material, name, value, y):
    """A parameter of four values: a vector parameter gives its RGB to a pin, so its alpha is appended to make all four."""
    vector = add_vector(library, material, name, value, y)
    append = library.create_material_expression(material, unreal.MaterialExpressionAppendVector, -450, y)
    library.connect_material_expressions(vector, '', append, 'A')
    library.connect_material_expressions(vector, 'A', append, 'B')
    return append


def add_config_parameters(
library, material, config_class, names, y):
    """Parameters named as values of a config class, with its defaults: AHexPlanet::ApplyConfig sets them from the config asset."""
    defaults = unreal.get_default_object(config_class)
    connections = []
    for index, name in enumerate(names):
        value = defaults.get_editor_property(re.sub(r'(?<!^)(?=[A-Z])', '_', name).lower())
        node_y = y + index * 50
        if isinstance(value, unreal.LinearColor):
            connections.append((name, add_vector(library, material, name, value, node_y), ''))
        elif isinstance(value, unreal.Vector4):
            connections.append((name, add_vector4(library, material, name, unreal.LinearColor(value.x, value.y, value.z, value.w), node_y), ''))
        elif isinstance(value, unreal.Vector):
            connections.append((name, add_vector(library, material, name, unreal.LinearColor(value.x, value.y, value.z, 0.0), node_y), ''))
        else:
            connections.append((name, add_scalar(library, material, name, value, node_y), ''))
    return connections


def add_cloud_parameters(library, material, y, extra=()):
    # PlanetRadius comes from AHexPlanet, and its default is only about the size of the planet
    return [('PlanetRadius', add_scalar(library, material, 'PlanetRadius', 11503.0, y), '')] + add_config_parameters(
        library, material, unreal.HexPlanetCloudConfig, CLOUD_PARAMETERS + list(extra), y + 50)


def add_sun(library, material, y):
    sun = library.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, y)
    sun.set_editor_property('parameter_name', 'SunDirection')
    sun.set_editor_property('default_value', unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
    return sun


def build_configs():
    # created only when missing, as their values are tuned by hand; new values start at the defaults of the class
    for path, config_class in CONFIGS:
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.AssetToolsHelpers.get_asset_tools().create_asset(path.split('/')[-1], FOLDER, config_class, unreal.DataAssetFactory())
            unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    build_underground_config()


def build_underground_config():
    if unreal.EditorAssetLibrary.does_asset_exist(UNDERGROUND_CONFIG):
        return
    config = unreal.EditorAssetLibrary.duplicate_asset(CONFIGS[0][0], UNDERGROUND_CONFIG)
    if not config:
        config = unreal.AssetToolsHelpers.get_asset_tools().create_asset(UNDERGROUND_CONFIG.split('/')[-1], FOLDER, unreal.HexPlanetConfig, unreal.DataAssetFactory())
    for name, value in UNDERGROUND_VALUES.items():
        config.set_editor_property(name, value)
    unreal.EditorAssetLibrary.save_asset(UNDERGROUND_CONFIG, only_if_is_dirty=False)
    unreal.log('PLANET created %s from %s' % (UNDERGROUND_CONFIG, CONFIGS[0][0]))


def build_material():
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(MATERIAL) if assets.does_asset_exist(MATERIAL) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        MATERIAL.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)

    custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -200, 0)
    custom.set_editor_property('code', PLANET_CODE)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    outputs = []
    for output_name, output_type in (('OutEmissive', unreal.CustomMaterialOutputType.CMOT_FLOAT3), ('OutNormal', unreal.CustomMaterialOutputType.CMOT_FLOAT3),
                                     ('OutRoughness', unreal.CustomMaterialOutputType.CMOT_FLOAT1)):
        output = unreal.CustomOutput()
        output.set_editor_property('output_name', output_name)
        output.set_editor_property('output_type', output_type)
        outputs.append(output)
    custom.set_editor_property('additional_outputs', outputs)
    # the normal comes in world space, as the planet has no tangents that fit the projection of the layers
    material.set_editor_property('tangent_space_normal', False)

    vertex_color = library.create_material_expression(material, unreal.MaterialExpressionVertexColor, -900, -300)
    uv = library.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1100, -100)
    u = library.create_material_expression(material, unreal.MaterialExpressionComponentMask, -900, -100)
    u.set_editor_property('r', True)
    u.set_editor_property('g', False)
    u.set_editor_property('b', False)
    u.set_editor_property('a', False)
    library.connect_material_expressions(uv, '', u, '')
    v = library.create_material_expression(material, unreal.MaterialExpressionComponentMask, -900, 100)
    v.set_editor_property('r', False)
    v.set_editor_property('g', True)
    v.set_editor_property('b', False)
    v.set_editor_property('a', False)
    library.connect_material_expressions(uv, '', v, '')
    u_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -700, -100)
    u_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -700, 0)
    library.connect_material_expressions(u, '', u_ddx, '')
    library.connect_material_expressions(u, '', u_ddy, '')

    connections = [('Color', vertex_color, ''), ('Alpha', vertex_color, 'A'), ('U', u, ''), ('UDdx', u_ddx, ''), ('UDdy', u_ddy, ''), ('PeakShare', v, '')
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig,
        ['GridWidth', 'GridMinPixels', 'GridMaxPixels', 'GridOpacity', 'GridDarkness', 'GridFadeStart', 'GridFadeEnd', 'GridSeaFade', 'Ambient', 'Glow'], 100) + [
        ('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 450), ''),
        ('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -1000, 500), ''),
        ('SunDirection', add_sun(library, material, 420), ''),
        ] + add_cloud_parameters(library, material, 450, SHADOW_PARAMETERS) + add_mark_inputs(library, material, 2500)

    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1100, 3200)
    position_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -900, 3200)
    position_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -900, 3250)
    library.connect_material_expressions(position, '', position_ddx, '')
    library.connect_material_expressions(position, '', position_ddy, '')
    connections += [('PosDdx', position_ddx, ''), ('PosDdy', position_ddy, ''),
        ('VertexNormal', library.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -1000, 3300), ''),
        # AHexPlanet gives the materials its CellBiomes, read with Load, so the default only has to be a 2D texture
        ('CellBiomes', add_texture(library, material, DEFAULT_MARKS, 'CellBiomes', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 3350), ''),
        # the stretches of the coast, for the run-up of the breakers on the beach (RUNUP_CODE)
        ('ShoreField', add_texture(library, material, DEFAULT_CUBE, 'ShoreField', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, 3375), ''),
        # the distance to the coast, for where the edge of the water lies below a point of the beach (RUNUP_CODE)
        ('SeaField', add_texture(library, material, DEFAULT_CUBE, 'SeaField', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, 3390), ''),
        ('Colors', add_texture(library, material, COLORS, 'Colors', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 3400), ''),
        ('HexNoise', add_texture(library, material, NOISE, 'HexNoise', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, 3410), ''),
        ('Normals', add_texture(library, material, NORMALS, 'Normals', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 3450), ''),
        # the values of each texture layer (UHexTerrainLayersConfig::CreateParamsTexture), read with Load; the black default keeps BiomeTileSize
        ('LayerParams', add_texture(library, material, DEFAULT_MARKS, 'LayerParams', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 3460), ''),
        # 1 on the tiles of painting, which draws their vertex colors
        ('PreviewTile', add_scalar(library, material, 'PreviewTile', 0.0, 3500), ''),
        # the share of the faces of high ground, X of the second UV
        ('FacetShare', add_uv_channel(library, material, 1, 'r', 3525), ''),
        ('CreviceShare', add_uv_channel(library, material, 1, 'g', 3540), ''),
        # the tint of the covers of the cells from far away (CANOPY_CODE), read with Load
        ('CellCanopy', add_texture(library, material, DEFAULT_MARKS, 'CellCanopy', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 3542), ''),
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1000, 3543), ''),
        # the rivers of the sectors of the cells and their segments (RIVER_CODE), read with Load
        ('CellRivers', add_texture(library, material, DEFAULT_MARKS, 'CellRivers', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 3544), ''),
        ('RiverSegments', add_texture(library, material, DEFAULT_MARKS, 'RiverSegments', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, 3546), '')
        # the beach and the rock of each layer of the biome textures, which AHexPlanet::SetBiomeLayers gives from the biomes; these defaults are
        # those of create_biome_assets.py
        ] + [('%sLayers%d' % (kind, group), add_vector4(library, material, '%sLayers%d' % (kind, group), unreal.LinearColor(*table[group * 4:group * 4 + 4]),
            3545 + group * 5), '') for kind, table in (('Beach', BEACH_LAYERS), ('Rock', ROCK_LAYERS), ('Seabed', SEABED_LAYERS)) for group in range(3)
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig, BIOME_PARAMETERS + CANOPY_PARAMETERS + RIVER_PARAMETERS, 3550) + add_config_parameters(library, material, unreal.HexPlanetWaterConfig, RUNUP_PARAMETERS, 5500)

    inputs = []
    for name, _, _ in connections:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)

    for name, node, pin in connections:
        library.connect_material_expressions(node, pin, custom, name)

    # Roughness of DA_HexPlanet, and RiverRoughness on the water of rivers (RIVER_CODE)
    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_BASE_COLOR)
    library.connect_material_property(custom, 'OutEmissive', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.connect_material_property(custom, 'OutNormal', unreal.MaterialProperty.MP_NORMAL)
    library.connect_material_property(custom, 'OutRoughness', unreal.MaterialProperty.MP_ROUGHNESS)

    cut_connections = [('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 700), ''),
        ('HideCount', add_scalar(library, material, 'HideCount', 0.0, 750), ''),
        ('LodSide', add_scalar(library, material, 'LodSide', 0.0, 1800), ''),
        ('LodCamera', add_vector(library, material, 'LodCamera', unreal.LinearColor(0.0, 0.0, 0.0, 0.0), 1850), '')
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig, ['CoarseDistance', 'CoarseBand'], 1900)
    for index in range(HIDE_CELLS):
        cut_connections.append(('HideCell%d' % index, add_vector(library, material, 'HideCell%d' % index, unreal.LinearColor(0.0, 0.0, 0.0, 0.0), 800 + index * 50), ''))
    cut = add_custom(library, material, SURFACE_CUT_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT1, cut_connections, 700)
    library.connect_material_property(cut, '', unreal.MaterialProperty.MP_OPACITY_MASK)

    library.recompile_material(material)
    assets.save_asset(MATERIAL)

    cut_path = MATERIAL + '_Cut'
    instance = unreal.load_asset(cut_path) if assets.does_asset_exist(cut_path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        cut_path.split('/')[-1], FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    library.set_material_instance_parent(instance, material)
    overrides = instance.get_editor_property('base_property_overrides')
    overrides.set_editor_property('override_blend_mode', True)
    overrides.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    instance.set_editor_property('base_property_overrides', overrides)
    library.update_material_instance(instance)
    assets.save_asset(cut_path, only_if_is_dirty=False)
    return material


def add_uv_channel(library, material, index, channel, y):
    """One channel of a UV"""
    uv = library.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1100, y)
    uv.set_editor_property('coordinate_index', index)
    mask = library.create_material_expression(material, unreal.MaterialExpressionComponentMask, -900, y)
    for name in 'rgba':
        mask.set_editor_property(name, name == channel)
    library.connect_material_expressions(uv, '', mask, '')
    return mask


# the boulders at the foot of mountains (FHexPlanetBoulders): the rock layer of the biome of their mountain, which comes as the first custom data of
# an instance, projected from the three planes of the world as the surface does, a little lighter or darker by instance (BoulderTint)
BOULDER_CODE = r'''
float3 n = normalize(VertexNormal);
float3 planes = pow(abs(n), TriplanarSharpness);
planes /= planes.x + planes.y + planes.z;
float3 pos = WorldPos / BiomeTileSize;
float3 dx = PosDdx / BiomeTileSize;
float3 dy = PosDdy / BiomeTileSize;
float layer = round(RockLayer);
float3 color = Colors.SampleGrad(ColorsSampler, float3(pos.yz, layer), dx.yz, dy.yz).rgb * planes.x
    + Colors.SampleGrad(ColorsSampler, float3(pos.xz, layer), dx.xz, dy.xz).rgb * planes.y
    + Colors.SampleGrad(ColorsSampler, float3(pos.xy, layer), dx.xy, dy.xy).rgb * planes.z;
return color * (1.0 + (InstanceRandom * 2.0 - 1.0) * BoulderTint);
'''
BOULDER_MATERIAL = FOLDER + '/M_HexPlanetBoulder'


def build_boulder_material():
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(BOULDER_MATERIAL) if assets.does_asset_exist(BOULDER_MATERIAL) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        BOULDER_MATERIAL.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)
    material.set_editor_property('used_with_instanced_static_meshes', True)

    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1100, 0)
    position_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -900, 0)
    position_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -900, 50)
    library.connect_material_expressions(position, '', position_ddx, '')
    library.connect_material_expressions(position, '', position_ddy, '')
    custom_data = library.create_material_expression(material, unreal.MaterialExpressionPerInstanceCustomData, -900, 150)
    custom_data.set_editor_property('data_index', 0)
    connections = [('WorldPos', position, ''), ('PosDdx', position_ddx, ''), ('PosDdy', position_ddy, ''),
        ('VertexNormal', library.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -1000, 100), ''),
        ('RockLayer', custom_data, ''),
        ('InstanceRandom', library.create_material_expression(material, unreal.MaterialExpressionPerInstanceRandom, -900, 200), ''),
        ('Colors', add_texture(library, material, COLORS, 'Colors', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 250), ''),
        ('HexNoise', add_texture(library, material, NOISE, 'HexNoise', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, 260), '')
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig, ['BiomeTileSize', 'TriplanarSharpness', 'BoulderTint'], 300)
    custom = add_custom(library, material, BOULDER_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, connections, 0)
    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = add_config_parameters(library, material, unreal.HexPlanetConfig, ['Roughness'], 600)[0][1]
    library.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    library.recompile_material(material)
    assets.save_asset(BOULDER_MATERIAL)
    return material


# the floes of sea ice (FHexPlanetFloes, AHexPlanet::UpdateFloes): snow and old ice on top with the snow layer of the biomes, blue ice on the
# sides, and lighter blue under the water, which the water of the frozen sea shows through (IceUnderClearness), a little lighter or darker by floe
FLOE_CODE = r'''
float3 n = normalize(VertexNormal);
float3 up = normalize(WorldPos);
float3 planes = pow(abs(n), TriplanarSharpness);
planes /= planes.x + planes.y + planes.z;
float3 pos = WorldPos / BiomeTileSize;
float3 dx = PosDdx / BiomeTileSize;
float3 dy = PosDdy / BiomeTileSize;
float3 snow = Colors.SampleGrad(ColorsSampler, float3(pos.yz, 3.0), dx.yz, dy.yz).rgb * planes.x
    + Colors.SampleGrad(ColorsSampler, float3(pos.xz, 3.0), dx.xz, dy.xz).rgb * planes.y
    + Colors.SampleGrad(ColorsSampler, float3(pos.xy, 3.0), dx.xy, dy.xy).rgb * planes.z;
float3 topColor = lerp(FloeTopColor.rgb, snow * FloeTopColor.rgb * 1.2, FloeSnowDetail);
float3 color = lerp(FloeSideColor.rgb, topColor, smoothstep(0.55, 0.85, dot(n, up)));
color = lerp(color, FloeUnderColor.rgb, smoothstep(0.3, -0.6, length(WorldPos) - PlanetRadius));
return color * (1.0 + (InstanceRandom * 2.0 - 1.0) * FloeTint);
'''

# how a floe moves around its place, in its world position offset, each at its own pace by its random: it drifts, turns back and forth around
# its up, and bobs and rocks, a large floe less. The axes of the instance come from its transform, so the place of a floe is the world position
# less its local position along them
FLOE_OFFSET_CODE = r'''
float scale = max(length(AxisZ), 0.01);
float3 up = AxisZ / scale;
float3 ex = normalize(AxisX);
float3 ey = normalize(AxisY);
float3 rel = LocalPos.x * AxisX + LocalPos.y * AxisY + LocalPos.z * AxisZ;
float phase = InstanceRandom * 6.2831853;
float size = sqrt(max(scale / 10.0, 0.3));
float spin = FloeSpin * sin(Time * FloeSpinSpeed * (0.6 + 0.8 * InstanceRandom) + phase);
float3 relUp = up * dot(rel, up);
float3 relFlat = rel - relUp;
float3 turned = relFlat * cos(spin) + cross(up, relFlat) * sin(spin) + relUp;
float rock = FloeRock / size * sin(Time * FloeRockSpeed * (0.8 + 0.4 * InstanceRandom) + phase * 1.7);
float axisAngle = phase * 3.1 + Time * 0.05;
float3 rockAxis = normalize(ex * cos(axisAngle) + ey * sin(axisAngle));
turned = turned * cos(rock) + cross(rockAxis, turned) * sin(rock) + rockAxis * dot(rockAxis, turned) * (1.0 - cos(rock));
float bob = FloeBob / size * sin(Time * FloeBobSpeed * (0.8 + 0.4 * InstanceRandom) + phase * 2.9);
float3 drift = (ex * sin(Time * FloeDriftSpeed * (0.7 + 0.6 * InstanceRandom) + phase) + ey * cos(Time * FloeDriftSpeed * (0.5 + 0.7 * InstanceRandom) + phase * 2.3)) * FloeDrift;
return turned - rel + up * bob + drift;
'''
FLOE_MATERIAL = FOLDER + '/M_HexPlanetFloe'


def build_floe_material():
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(FLOE_MATERIAL) if assets.does_asset_exist(FLOE_MATERIAL) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        FLOE_MATERIAL.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)
    material.set_editor_property('used_with_instanced_static_meshes', True)

    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1100, 0)
    position_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -900, 0)
    position_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -900, 50)
    library.connect_material_expressions(position, '', position_ddx, '')
    library.connect_material_expressions(position, '', position_ddy, '')
    connections = [('WorldPos', position, ''), ('PosDdx', position_ddx, ''), ('PosDdy', position_ddy, ''),
        ('VertexNormal', library.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -1000, 100), ''),
        ('InstanceRandom', library.create_material_expression(material, unreal.MaterialExpressionPerInstanceRandom, -900, 200), ''),
        ('Colors', add_texture(library, material, COLORS, 'Colors', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 250), ''),
        ('HexNoise', add_texture(library, material, NOISE, 'HexNoise', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, 260), ''),
        # AHexPlanet gives the radius of the sea, and the default is only about the size of Terra
        ('PlanetRadius', add_scalar(library, material, 'PlanetRadius', 11503.0, 280), '')
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig,
        ['BiomeTileSize', 'TriplanarSharpness', 'FloeTopColor', 'FloeSideColor', 'FloeUnderColor', 'FloeSnowDetail', 'FloeTint'], 300)
    custom = add_custom(library, material, FLOE_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, connections, 0)
    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = add_config_parameters(library, material, unreal.HexPlanetConfig, ['FloeRoughness'], 700)[0][1]
    library.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)

    axes = []
    for index, (name, value) in enumerate((('AxisX', unreal.LinearColor(1.0, 0.0, 0.0, 0.0)), ('AxisY', unreal.LinearColor(0.0, 1.0, 0.0, 0.0)),
                                           ('AxisZ', unreal.LinearColor(0.0, 0.0, 1.0, 0.0)))):
        constant = library.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -1300, 1000 + index * 100)
        constant.set_editor_property('constant', value)
        transform = library.create_material_expression(material, unreal.MaterialExpressionTransform, -1100, 1000 + index * 100)
        transform.set_editor_property('transform_source_type', unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
        transform.set_editor_property('transform_type', unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
        library.connect_material_expressions(constant, '', transform, '')
        axes.append((name, transform, ''))
    offset_connections = axes + [
        ('LocalPos', library.create_material_expression(material, unreal.MaterialExpressionLocalPosition, -1100, 1300), ''),
        ('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -1100, 1350), ''),
        ('InstanceRandom', library.create_material_expression(material, unreal.MaterialExpressionPerInstanceRandom, -1100, 1400), '')
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig,
        ['FloeDrift', 'FloeDriftSpeed', 'FloeSpin', 'FloeSpinSpeed', 'FloeBob', 'FloeBobSpeed', 'FloeRock', 'FloeRockSpeed'], 1450)
    offset = add_custom(library, material, FLOE_OFFSET_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, offset_connections, 1000)
    library.connect_material_property(offset, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    library.recompile_material(material)
    assets.save_asset(FLOE_MATERIAL)
    return material


# the trees, bushes, flowers and decor of UHexPlanetObjects: the color of the vertices, whose alpha is how leafy a vertex is and how much it sways,
# lighter or darker and warmer or cooler by instance (FoliageTint, FoliageHueShift), the leaves broken into clumps by a noise and lit through on
# the side away from the sun (FoliageTranslucency), and snow of the snow layer on what faces up, as much as the second custom data of an instance
# says (the Snow of its variant), broken by the noise
FOLIAGE_CODE = r"""
float3 n = normalize(VertexNormal);
float3 up = normalize(WorldPos);
float3 planes = pow(abs(n), TriplanarSharpness);
planes /= planes.x + planes.y + planes.z;
float3 np = WorldPos / FoliageNoiseScale;
float3 ndx = PosDdx / FoliageNoiseScale;
float3 ndy = PosDdy / FoliageNoiseScale;
float noise = HexNoise.SampleGrad(HexNoiseSampler, np.yz, ndx.yz, ndy.yz).r * planes.x
    + HexNoise.SampleGrad(HexNoiseSampler, np.xz, ndx.xz, ndy.xz).r * planes.y
    + HexNoise.SampleGrad(HexNoiseSampler, np.xy, ndx.xy, ndy.xy).r * planes.z;
float leafy = saturate(VertexAlpha * 1.5);
float3 color = VertexRGB.rgb * (1.0 + (InstanceRandom * 2.0 - 1.0) * FoliageTint);
float warm = frac(InstanceRandom * 7.31) * 2.0 - 1.0;
color *= float3(1.0 + warm * FoliageHueShift, 1.0, 1.0 - warm * FoliageHueShift);
color *= lerp(1.0, 0.55 + noise * 0.9, FoliageNoiseStrength * leafy);
color *= 1.0 + FoliageTranslucency * leafy * saturate(-dot(n, SunDirection.xyz));
float3 pos = WorldPos / BiomeTileSize;
float3 dx = PosDdx / BiomeTileSize;
float3 dy = PosDdy / BiomeTileSize;
float3 snow = Colors.SampleGrad(ColorsSampler, float3(pos.yz, 3.0), dx.yz, dy.yz).rgb * planes.x
    + Colors.SampleGrad(ColorsSampler, float3(pos.xz, 3.0), dx.xz, dy.xz).rgb * planes.y
    + Colors.SampleGrad(ColorsSampler, float3(pos.xy, 3.0), dx.xy, dy.xy).rgb * planes.z;
float snowCover = smoothstep(FoliageSnowUpStart, FoliageSnowUpEnd, dot(n, up)) * Snow;
snowCover = saturate(snowCover * (1.0 + (noise - 0.5) * 2.0 * FoliageSnowBreakup) * 1.3);
return lerp(color, snow, snowCover);
"""
# the tops sway around the foot of an instance across the up of the planet, in gusts, each at its own phase
FOLIAGE_OFFSET_CODE = r"""
float3 up = normalize(ObjectPos);
float3 side = normalize(cross(up, float3(0.0, 0.0, 1.0)) + float3(1.0e-4, 0.0, 0.0));
float3 across = cross(up, side);
float phase = Time * WindSpeed + InstanceRandom * 6.2831 + dot(ObjectPos, float3(0.002, 0.003, 0.0025));
float gust = 0.6 + 0.4 * sin(Time * WindSpeed * 0.37 + dot(ObjectPos, float3(0.0007, 0.0005, 0.0006)));
float sway = sin(phase) * gust * WindStrength * VertexAlpha;
return side * sway + across * sway * 0.4 * sin(phase * 1.7);
"""
FOLIAGE_MATERIAL = FOLDER + '/M_HexPlanetFoliage'


def build_foliage_material(collection):
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(FOLIAGE_MATERIAL) if assets.does_asset_exist(FOLIAGE_MATERIAL) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        FOLIAGE_MATERIAL.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)
    material.set_editor_property('used_with_instanced_static_meshes', True)

    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1100, 0)
    position_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -900, 0)
    position_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -900, 50)
    library.connect_material_expressions(position, '', position_ddx, '')
    library.connect_material_expressions(position, '', position_ddy, '')
    vertex_color = library.create_material_expression(material, unreal.MaterialExpressionVertexColor, -1000, 150)
    snow = library.create_material_expression(material, unreal.MaterialExpressionPerInstanceCustomData, -900, 200)
    snow.set_editor_property('data_index', 1)
    sun = library.create_material_expression(material, unreal.MaterialExpressionCollectionParameter, -1000, 240)
    sun.set_editor_property('collection', collection)
    sun.set_editor_property('parameter_name', 'SunDirection')
    connections = [('WorldPos', position, ''), ('PosDdx', position_ddx, ''), ('PosDdy', position_ddy, ''),
        ('VertexNormal', library.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -1000, 100), ''),
        ('VertexRGB', vertex_color, ''), ('VertexAlpha', vertex_color, 'A'), ('Snow', snow, ''), ('SunDirection', sun, ''),
        ('InstanceRandom', library.create_material_expression(material, unreal.MaterialExpressionPerInstanceRandom, -900, 220), ''),
        ('Colors', add_texture(library, material, COLORS, 'Colors', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 250), ''),
        ('HexNoise', add_texture(library, material, NOISE, 'HexNoise', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, 260), '')
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig,
        ['BiomeTileSize', 'TriplanarSharpness', 'FoliageTint', 'FoliageHueShift', 'FoliageNoiseScale', 'FoliageNoiseStrength', 'FoliageTranslucency',
         'FoliageSnowUpStart', 'FoliageSnowUpEnd', 'FoliageSnowBreakup'], 300)
    custom = add_custom(library, material, FOLIAGE_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, connections, 0)
    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = add_config_parameters(library, material, unreal.HexPlanetConfig, ['Roughness'], 700)[0][1]
    library.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)

    offset_color = library.create_material_expression(material, unreal.MaterialExpressionVertexColor, -1100, 1000)
    offset_connections = [
        ('ObjectPos', library.create_material_expression(material, unreal.MaterialExpressionObjectPositionWS, -1100, 1100), ''),
        ('VertexAlpha', offset_color, 'A'),
        ('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -1100, 1150), ''),
        ('InstanceRandom', library.create_material_expression(material, unreal.MaterialExpressionPerInstanceRandom, -1100, 1200), '')
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig, ['WindStrength', 'WindSpeed'], 1250)
    offset = add_custom(library, material, FOLIAGE_OFFSET_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, offset_connections, 1000)
    library.connect_material_property(offset, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    library.recompile_material(material)
    assets.save_asset(FOLIAGE_MATERIAL)
    return material


def add_texture(library, material, path, name, sampler_type, y):
    node = library.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -700, y)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('texture', unreal.load_asset(path))
    node.set_editor_property('sampler_type', sampler_type)
    return node

def add_mark_inputs(library, material, y):
    """The inputs of MARK_CODE: the third and fourth UVs, the texture of cell marks and their values from HexPlanetConfig"""
    connections = []
    for name, index in (('CellUV', 2), ('LocalUV', 3)):
        uv = library.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1100, y + index * 50)
        uv.set_editor_property('coordinate_index', index)
        connections.append((name, uv, ''))
    connections.append(('CellMarks', add_texture(library, material, DEFAULT_MARKS, 'CellMarks', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, y + 200), ''))
    # OutlineColor or BrushOutlineColor, which AHexPlanet::SetHoveredCell gives
    connections.append(('MarkPrimaryColor', add_vector(library, material, 'MarkPrimaryColor', unreal.LinearColor(1.0, 1.0, 1.0, 1.0), y + 225), ''))
    return connections + add_config_parameters(library, material, unreal.HexPlanetConfig, MARK_PARAMETERS, y + 250)


def add_custom(library, material, code, output_type, connections, y, extra_output=None):
    custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -200, y)
    custom.set_editor_property('code', code)
    custom.set_editor_property('output_type', output_type)
    if extra_output:
        output = unreal.CustomOutput()
        output.set_editor_property('output_name', extra_output[0])
        output.set_editor_property('output_type', extra_output[1])
        custom.set_editor_property('additional_outputs', [output])

    inputs = []
    for name, _, _ in connections:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)

    for name, node, pin in connections:
        library.connect_material_expressions(node, pin, custom, name)
    return custom


def build_water():
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(WATER_MATERIAL) if assets.does_asset_exist(WATER_MATERIAL) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        WATER_MATERIAL.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)
    # unlit, as translucent surfaces get little light and water should keep its color
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)

    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, -300)
    position_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -850, -400)
    position_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -850, -300)
    library.connect_material_expressions(position, '', position_ddx, '')
    library.connect_material_expressions(position, '', position_ddy, '')
    sun = library.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, 500)
    sun.set_editor_property('parameter_name', 'SunDirection')
    sun.set_editor_property('default_value', unreal.LinearColor(0.0, 0.0, 1.0, 0.0))

    connections = [('WorldPos', position, ''), ('PosDdx', position_ddx, ''), ('PosDdy', position_ddy, ''),
        ('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -700, -200), ''),
        ('CameraDir', library.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, -700, -150), ''),
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -700, -100), ''),
        # the foam around floes: how far the opaque scene lies behind the water
        ('SceneDepth', library.create_material_expression(material, unreal.MaterialExpressionSceneDepth, -700, -50), ''),
        ('PixelDepth', library.create_material_expression(material, unreal.MaterialExpressionPixelDepth, -700, 0), ''),
        ('SunDirection', sun, '')] + add_config_parameters(library, material, unreal.HexPlanetWaterConfig, WATER_PARAMETERS, 2000) + [
        ('SeaField', add_texture(library, material, DEFAULT_CUBE, 'SeaField', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, 600), ''),
        # the stretches of the coast, for the breakers (SHORE_WAVE_CODE)
        ('ShoreField', add_texture(library, material, DEFAULT_CUBE, 'ShoreField', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, 650), ''),
        ('Colors', add_texture(library, material, COLORS, 'Colors', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 700), ''),
        ('HexNoise', add_texture(library, material, NOISE, 'HexNoise', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, 710), ''),
        ('Normals', add_texture(library, material, NORMALS, 'Normals', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 800), ''),
        ('HideCount', add_scalar(library, material, 'HideCount', 0.0, 900), '')] + add_cloud_parameters(library, material, -700, SHADOW_PARAMETERS + HAZE_PARAMETERS)
    for index in range(HIDE_CELLS):
        connections.append(('HideCell%d' % index, add_vector(library, material, 'HideCell%d' % index, unreal.LinearColor(0.0, 0.0, 0.0, 0.0), 950 + index * 50), ''))
    # the mouths of rivers near the camera (PLUME_CODE), whose alpha says whether they are given
    for index in range(RIVER_MOUTHS):
        connections.append(('RiverMouth%d' % index, add_vector4(library, material, 'RiverMouth%d' % index, unreal.LinearColor(0.0, 0.0, 0.0, 0.0), 2950 + index * 60), ''))
        connections.append(('RiverMouthDir%d' % index, add_vector(library, material, 'RiverMouthDir%d' % index, unreal.LinearColor(1.0, 0.0, 0.0, 0.0), 2980 + index * 60), ''))
    custom = add_custom(library, material, WATER_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, connections, 0,
        ('OutOpacity', unreal.CustomMaterialOutputType.CMOT_FLOAT1))
    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.connect_material_property(custom, 'OutOpacity', unreal.MaterialProperty.MP_OPACITY)

    offset_connections = [('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 1000), ''),
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1000, 1050), ''),
        ('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -1000, 1100), ''),
        ('Spacing', library.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1000, 1150), ''),
        ('SeaField', add_texture(library, material, DEFAULT_CUBE, 'SeaField', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, 1250), ''),
        ('Colors', add_texture(library, material, COLORS, 'Colors', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 1300), ''),
        ('HexNoise', add_texture(library, material, NOISE, 'HexNoise', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, 1310), '')
        ] + add_config_parameters(library, material, unreal.HexPlanetWaterConfig, WATER_OFFSET_PARAMETERS, 4500)
    offset = add_custom(library, material, WATER_OFFSET_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, offset_connections, 1000)
    library.connect_material_property(offset, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    library.recompile_material(material)
    assets.save_asset(WATER_MATERIAL)
    return material


def build_water_grid():
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(WATER_GRID_MATERIAL) if assets.does_asset_exist(WATER_GRID_MATERIAL) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        WATER_GRID_MATERIAL.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)

    uv = library.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1100, -100)
    u = library.create_material_expression(material, unreal.MaterialExpressionComponentMask, -900, -100)
    u.set_editor_property('r', True)
    u.set_editor_property('g', False)
    u.set_editor_property('b', False)
    u.set_editor_property('a', False)
    library.connect_material_expressions(uv, '', u, '')
    u_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -700, -100)
    u_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -700, 0)
    library.connect_material_expressions(u, '', u_ddx, '')
    library.connect_material_expressions(u, '', u_ddy, '')

    hide_connections = [('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 700), ''),
        ('HideCount', add_scalar(library, material, 'HideCount', 0.0, 750), '')]
    for index in range(HIDE_CELLS):
        hide_connections.append(('HideCell%d' % index, add_vector(library, material, 'HideCell%d' % index, unreal.LinearColor(0.0, 0.0, 0.0, 0.0), 800 + index * 50), ''))
    visible = add_custom(library, material, CUT_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT1, hide_connections, 700)

    connections = [('U', u, ''), ('UDdx', u_ddx, ''), ('UDdy', u_ddy, ''), ('Visible', visible, '')] + add_config_parameters(
        library, material, unreal.HexPlanetConfig, ['GridWidth', 'GridMinPixels', 'GridMaxPixels', 'GridOpacity', 'WaterGridDarkness'], 100)
    connections += add_mark_inputs(library, material, 2500)
    custom = add_custom(library, material, WATER_GRID_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT1, connections, 0,
        ('OutColor', unreal.CustomMaterialOutputType.CMOT_FLOAT3))
    library.connect_material_property(custom, 'OutColor', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_OPACITY)

    library.recompile_material(material)
    assets.save_asset(WATER_GRID_MATERIAL)
    return material


def build_wireframe():
    """An unlit translucent wireframe, which AHexPlanet draws over the surface or instead of it."""
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(WIRE_MATERIAL) if assets.does_asset_exist(WIRE_MATERIAL) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        WIRE_MATERIAL.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('wireframe', True)

    connections = [('VertexColor', library.create_material_expression(material, unreal.MaterialExpressionVertexColor, -700, -300), ''),
        ('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -700, -200), ''),
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -700, -100), ''),
        ('UseVertexColor', add_scalar(library, material, 'UseVertexColor', 0.0, 100), '')
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig, ['WireColor', 'WireOpacity'], 0)
    custom = add_custom(library, material, WIRE_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, connections, 0,
        ('OutOpacity', unreal.CustomMaterialOutputType.CMOT_FLOAT1))
    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.connect_material_property(custom, 'OutOpacity', unreal.MaterialProperty.MP_OPACITY)

    offset_connections = [('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 500), ''),
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1000, 550), ''),
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig, ['WireBias'], 600)
    offset = add_custom(library, material, WIRE_OFFSET_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, offset_connections, 500)
    library.connect_material_property(offset, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    library.recompile_material(material)
    assets.save_asset(WIRE_MATERIAL)
    return material


def build_level(material, water_material, water_grid_material, wire_material):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        levels.load_level(LEVEL)
        for actor in actors.get_all_level_actors():
            actors.destroy_actor(actor)
    else:
        levels.new_level(LEVEL)

    planet = actors.spawn_actor_from_class(unreal.HexPlanet, unreal.Vector(0, 0, 0))
    planet.set_editor_property('material', material)
    planet.set_editor_property('water_material_asset', water_material)
    planet.set_editor_property('water_grid_material_asset', water_grid_material)
    planet.set_editor_property('wireframe_material_asset', wire_material)

    # the camera turns the sun, so it must be movable
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 0), unreal.Rotator(pitch=-50.0, yaw=0.0, roll=0.0))
    light = sun.get_component_by_class(unreal.DirectionalLightComponent)
    light.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    light.set_editor_property('intensity', 7.0)
    light.set_editor_property('light_color', unreal.Color(r=255, g=236, b=208, a=255))

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode', unreal.HexPlanetGameMode)
    levels.save_current_level()


def build_clouds():
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(CLOUD_MATERIAL) if assets.does_asset_exist(CLOUD_MATERIAL) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        CLOUD_MATERIAL.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    # from under the shell, as at the closest camera, its inside shows the clouds towards the horizon
    material.set_editor_property('two_sided', True)

    connections = [('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, -300), ''),
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1000, -200), ''),
        # 1 on the copy that AHexPlanet draws as the sky
        ('CloudSkyLayer', add_scalar(library, material, 'CloudSkyLayer', 0.0, -400), ''),
        ('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -1000, -100), ''),
        ('SunDirection', add_sun(library, material, -50), '')] + add_cloud_parameters(library, material, 0, SHELL_PARAMETERS + HAZE_PARAMETERS)
    custom = add_custom(library, material, CLOUDS_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, connections, 0,
        ('OutOpacity', unreal.CustomMaterialOutputType.CMOT_FLOAT1))
    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.connect_material_property(custom, 'OutOpacity', unreal.MaterialProperty.MP_OPACITY)
    lift = add_custom(library, material, SHELL_LIFT_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, connections, 400)
    library.connect_material_property(lift, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    library.recompile_material(material)
    assets.save_asset(CLOUD_MATERIAL)
    return material


# the haze over the opaque scene and over the space behind the planet, before the translucency (the water, the clouds and the auroras draw
# after the depth of field and lay their own haze over themselves); a pixel with nothing on it is a view that goes on for ever
HAZE_POST_CODE = r'''
float3 hazeCam = CameraPos;
float3 hazeDir = -normalize(CameraVector);
float hazeEnd = SceneDepth > 1e6 ? 1e9 : length(WorldPos - CameraPos);
HAZE_CODE
return lerp(SceneColor.rgb, hazeColor, hazeAmount);
'''.replace('HAZE_CODE', HAZE_CODE)


def build_haze():
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(HAZE_MATERIAL) if assets.does_asset_exist(HAZE_MATERIAL) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        HAZE_MATERIAL.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF)

    scene = library.create_material_expression(material, unreal.MaterialExpressionSceneTexture, -1000, -400)
    scene.set_editor_property('scene_texture_id', unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    connections = [('SceneColor', scene, 'Color'),
        ('SceneDepth', library.create_material_expression(material, unreal.MaterialExpressionSceneDepth, -1000, -300), ''),
        ('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, -250), ''),
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1000, -200), ''),
        ('CameraVector', library.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, -1000, -150), ''),
        ('SunDirection', add_sun(library, material, -100), ''),
        ('PlanetRadius', add_scalar(library, material, 'PlanetRadius', 0.0, -50), '')
        ] + add_config_parameters(library, material, unreal.HexPlanetCloudConfig, HAZE_PARAMETERS, 0)
    custom = add_custom(library, material, HAZE_POST_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, connections, 0)
    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    library.recompile_material(material)
    assets.save_asset(HAZE_MATERIAL)
    return material


# The auroras over the pentagons are NS_HexPlanetAurora, a Niagara system tuned by hand, whose renderers draw with these materials: the
# curtains are ribbons whose first UV runs along them and up them (AuroraFlipV turns that around), the light that rises is sprites. Both add
# their light, take the color of the particle, are brighter at night by the sun of MPC_HexPlanet, and hide where the line from the camera
# dips below the sea, as the water writes no depth. The values of their look are parameters of the material with these defaults, which a
# material instance can change.
AURORA_COMMON_CODE = r'''
float3 planetUp = normalize(WorldPos);
float night = lerp(AuroraDayShare, 1.0, smoothstep(0.15, -0.2, dot(planetUp, normalize(SunDirection.xyz))));
// the nearest point to the center of the planet on the line from the camera, which hides the light where it lies under the sea
float3 toPoint = WorldPos - CameraPos;
float along = saturate(dot(-CameraPos, toPoint) / max(dot(toPoint, toPoint), 1.0));
// 0 outside the game, such as in the preview of Niagara, where nothing hides it
float clear = PlanetRadius > 0.0 ? smoothstep(PlanetRadius - 3.0, PlanetRadius + 1.0, length(CameraPos + toPoint * along)) : 1.0;
float near = smoothstep(AuroraNearFadeStart, max(AuroraNearFadeEnd, AuroraNearFadeStart + 1.0), length(toPoint));
// brighter from far away, so the wonders read as landmarks over the whole planet
float far = smoothstep(AuroraFarStart, max(AuroraFarEnd, AuroraFarStart + 1.0), length(toPoint));
float shown = night * clear * near * lerp(1.0, AuroraFarBoost, far) * AuroraBrightness * ColorAlpha;
'''

# The curtains fold in and out and sway along themselves, more the higher up, so they move as drapes of an aurora. Out is the normal of the
# ribbon across the up of the planet, as the position of the object a Niagara material reads is not that of its system
AURORA_OFFSET_CODE = r'''
float3 planetUp = normalize(WorldPos);
float3 flat = Normal - planetUp * dot(Normal, planetUp);
float3 outward = dot(flat, flat) > 1e-6 ? normalize(flat) : float3(0.0, 0.0, 0.0);
float3 along = cross(planetUp, outward);
float up = AuroraFlipV > 0.5 ? 1.0 - UV.y : UV.y;
float phase = UV.x * 6.2831853 * max(round(AuroraFoldCount), 1.0);
float fold = sin(phase + Time * AuroraFoldSpeed + up * 2.5) + 0.5 * sin(phase * 2.3 - Time * AuroraFoldSpeed * 1.7 + 1.3);
float sway = sin(UV.x * 6.2831853 + Time * AuroraFoldSpeed * 0.6 + up * 1.5);
return outward * AuroraFold * fold * (0.3 + up) + along * AuroraSway * sway * up;
'''

AURORA_CODE = r'''
struct FAuroraNoise
{
    float Hash(float2 P)
    {
        return frac(sin(dot(P, float2(127.1, 311.7))) * 43758.5453);
    }
    float Value(float2 P)
    {
        float2 I = floor(P);
        float2 F = P - I;
        F = F * F * (3.0 - 2.0 * F);
        return lerp(lerp(Hash(I), Hash(I + float2(1.0, 0.0)), F.x), lerp(Hash(I + float2(0.0, 1.0)), Hash(I + float2(1.0, 1.0)), F.x), F.y);
    }
};
FAuroraNoise auroraNoise;
''' + AURORA_COMMON_CODE + r'''
float up = AuroraFlipV > 0.5 ? 1.0 - UV.y : UV.y;
// rays along the curtain that drift along it, stretched up it, with light running upwards
float x = UV.x * AuroraRays + Time * AuroraDrift;
float y = up * 1.5 - Time * AuroraRise;
float ray = auroraNoise.Value(float2(x, y * 0.6)) * 0.7 + auroraNoise.Value(float2(x * 2.0 + 5.0, y * 1.2 + 3.0)) * 0.45;
ray = pow(saturate(ray), AuroraRaySharpness) * 2.2;
float foot = smoothstep(0.0, AuroraFootFade, up);
float fade = pow(saturate(1.0 - up), AuroraTopFade);
float edge = 1.0 + 1.5 * exp(-up / 0.06);
float3 color = up < AuroraMidShare ? lerp(AuroraColorLow.rgb, AuroraColorMid.rgb, up / AuroraMidShare)
    : lerp(AuroraColorMid.rgb, AuroraColorHigh.rgb, saturate((up - AuroraMidShare) / (1.0 - AuroraMidShare)));
// drapes along the curtain with dark gaps between them, drifting slowly, so it does not read as a closed wall
float drape = 0.5 + 0.5 * sin(UV.x * 6.2831853 * max(round(AuroraDrapes), 1.0) + Time * AuroraDrapeDrift);
float band = lerp(1.0, smoothstep(0.25, 0.85, drape), AuroraGaps);
return color * Color.rgb * ray * band * foot * fade * edge * shown;
'''

# a soft round spot of light, the sprites of the light that rises
AURORA_SPARK_CODE = AURORA_COMMON_CODE + r'''
float spot = pow(saturate(1.0 - length(UV * 2.0 - 1.0)), AuroraSparkSoftness);
return Color.rgb * spot * shown;
'''

AURORA_DEFAULTS = [('AuroraBrightness', 1.6), ('AuroraDayShare', 0.45), ('AuroraNearFadeStart', 40.0), ('AuroraNearFadeEnd', 160.0),
    ('AuroraFarStart', 1500.0), ('AuroraFarEnd', 8000.0), ('AuroraFarBoost', 1.3)]
AURORA_OFFSET_DEFAULTS = [('AuroraFlipV', 1.0), ('AuroraFold', 16.0), ('AuroraFoldCount', 3.0), ('AuroraFoldSpeed', 0.3), ('AuroraSway', 12.0)]
AURORA_CURTAIN_DEFAULTS = [('AuroraFlipV', 1.0), ('AuroraRays', 24.0), ('AuroraRaySharpness', 2.5), ('AuroraDrift', 0.6), ('AuroraRise', 0.12),
    ('AuroraFootFade', 0.03), ('AuroraTopFade', 1.4), ('AuroraMidShare', 0.35), ('AuroraDrapes', 3.0), ('AuroraGaps', 0.85), ('AuroraDrapeDrift', 0.12)]
AURORA_COLORS = [('AuroraColorLow', unreal.LinearColor(0.15, 1.0, 0.45, 1.0)), ('AuroraColorMid', unreal.LinearColor(0.1, 0.65, 1.0, 1.0)),
    ('AuroraColorHigh', unreal.LinearColor(0.7, 0.25, 1.0, 1.0))]
AURORA_MATERIAL = FOLDER + '/M_HexPlanetAurora'
AURORA_SPARK_MATERIAL = FOLDER + '/M_HexPlanetAuroraSpark'
PARAMETER_COLLECTION = FOLDER + '/MPC_HexPlanet'


def build_parameter_collection():
    """MPC_HexPlanet: values of the planet that any material may read, which AHexPlanet sets every frame"""
    assets = unreal.EditorAssetLibrary
    collection = unreal.load_asset(PARAMETER_COLLECTION) if assets.does_asset_exist(PARAMETER_COLLECTION) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        PARAMETER_COLLECTION.split('/')[-1], FOLDER, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    # parameters made again would get new ids, and the materials that read them by id would fail to compile
    names = [str(each.get_editor_property('parameter_name')) for each in collection.get_editor_property('scalar_parameters')] + \
        [str(each.get_editor_property('parameter_name')) for each in collection.get_editor_property('vector_parameters')]
    if 'PlanetRadius' in names and 'SunDirection' in names:
        return collection
    radius = unreal.CollectionScalarParameter()
    radius.set_editor_property('parameter_name', 'PlanetRadius')
    # 0 until AHexPlanet sets it, so the preview of a material that hides what lies under the sea shows it
    radius.set_editor_property('default_value', 0.0)
    sun = unreal.CollectionVectorParameter()
    sun.set_editor_property('parameter_name', 'SunDirection')
    sun.set_editor_property('default_value', unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
    collection.set_editor_property('scalar_parameters', [radius])
    collection.set_editor_property('vector_parameters', [sun])
    assets.save_asset(PARAMETER_COLLECTION, only_if_is_dirty=False)
    return collection


def build_aurora_material(path, code, scalars, colors, collection, offset_scalars=None):
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(path) if assets.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        path.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)
    # light added to what lies behind, from both sides, drawn by Niagara
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', True)
    material.set_editor_property('used_with_niagara_sprites', True)
    material.set_editor_property('used_with_niagara_ribbons', True)

    uv = library.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1000, -500)
    # its default pin is only RGB, so the alpha comes from its own pin
    particle_color = library.create_material_expression(material, unreal.MaterialExpressionParticleColor, -1000, -450)
    connections = [('UV', uv, ''),
        ('Color', particle_color, ''), ('ColorAlpha', particle_color, 'A'),
        ('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, -400), ''),
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1000, -350), ''),
        ('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -1000, -300), '')]
    for index, name in enumerate(('SunDirection', 'PlanetRadius')):
        node = library.create_material_expression(material, unreal.MaterialExpressionCollectionParameter, -1000, -250 + index * 50)
        node.set_editor_property('collection', collection)
        node.set_editor_property('parameter_name', name)
        connections.append((name, node, ''))
    for index, (name, value) in enumerate(scalars):
        connections.append((name, add_scalar(library, material, name, value, index * 50), ''))
    for index, (name, value) in enumerate(colors):
        connections.append((name, add_vector(library, material, name, value, 600 + index * 50), ''))
    custom = add_custom(library, material, code, unreal.CustomMaterialOutputType.CMOT_FLOAT3, connections, 0)
    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    if offset_scalars:
        nodes = dict((name, (node, pin)) for name, node, pin in connections)
        offset = [(name,) + nodes[name] for name in ('UV', 'WorldPos', 'Time')]
        offset.append(('Normal', library.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -1000, 1000), ''))
        for index, (name, value) in enumerate(offset_scalars):
            offset.append((name, add_scalar(library, material, name, value, 1100 + index * 50), ''))
        moved = add_custom(library, material, AURORA_OFFSET_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, offset, 1000)
        library.connect_material_property(moved, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    library.recompile_material(material)
    assets.save_asset(path)
    return material


def build_auroras():
    collection = build_parameter_collection()
    build_aurora_material(AURORA_MATERIAL, AURORA_CODE, AURORA_DEFAULTS + AURORA_CURTAIN_DEFAULTS, AURORA_COLORS, collection, AURORA_OFFSET_DEFAULTS)
    build_aurora_material(AURORA_SPARK_MATERIAL, AURORA_SPARK_CODE, AURORA_DEFAULTS + [('AuroraSparkSoftness', 2.0)], [], collection)


# The underground (DA_HexUnderground, EdgeRule HigherHolds): M_HexUnderground draws its surface as M_HexPlanet does, from the layers of its cave
# biomes with the grid lines and the outlines, but without the sea, the beach, rivers, the run-up, the canopy and the shadows of clouds. Every
# height is measured below the top of the passages, the level 0 that lies LevelHeight above the sea: the rock of a cliff darkens down into the
# abyss (AbyssFadeStart, AbyssFadeEnd), and the floor of a void draws a teal grey mist as its light, a noise taken in AbyssSlices layers under the
# floor along the view, so they slide against the floor and each other as the camera moves and the floor reads as bottomless. Along the sides
# of a passage towards a void a faint lip lights the edge of the ledge (AbyssLip*), found from the bits of CellBiomes that say which neighbors
# lie below the sea, the voids here. The grid lines fade out below the passages (AbyssGridFade), and the outlines show on the voids too. In the
# Links tab of the editor the blue of CellMarks tints each passage by the color of its network (NetworkColor0..7) and what is wrong in ProblemColor
UNDERGROUND_MATERIAL = FOLDER + '/M_HexUnderground'
UNDERGROUND_FOG_MATERIAL = FOLDER + '/M_HexUndergroundFog'
_BEACH_START = BIOME_CODE.index('// the beach of each layer (BeachLayers0..2')
_ROCK_START = BIOME_CODE.index('// rock on slopes above the coast')
UNDERGROUND_BIOME_CODE = BIOME_CODE[:_BEACH_START] + '''float slopeRock = 1.0 - smoothstep(SlopeStart, max(SlopeEnd, SlopeStart + 1e-3), flatness);
float shoreSlope = 0.0;
''' + BIOME_CODE[_ROCK_START:]
_SEABED_DARKENING = 'albedo *= lerp(1.0, SeabedDarkening, saturate(-height / max(DeepDepth, 1.0)));\n'
assert _SEABED_DARKENING in UNDERGROUND_BIOME_CODE
UNDERGROUND_BIOME_CODE = UNDERGROUND_BIOME_CODE.replace(_SEABED_DARKENING, '')
# the value noise of the clouds without the clouds, which BIOME_CODE and the mist take
NOISE_CODE = CLOUD_CODE[:CLOUD_CODE.index('float cloudAngle')]
UNDERGROUND_CODE = NOISE_CODE + GRID_LINE_CODE + MARK_CODE.replace('MARK_HOLE_BITS', '0u') + UNDERGROUND_BIOME_CODE + r'''
float depthBelow = LevelHeight - height;
float3 viewRay = normalize(WorldPos - CameraPos);

// the rock darkens down the cliffs, into the dark of the abyss
albedo *= lerp(1.0, AbyssDarkness, smoothstep(AbyssFadeStart, max(AbyssFadeEnd, AbyssFadeStart + 1.0), depthBelow));

// the mist of the floor of a void: a noise in slices under the floor along the view, deeper ones fainter, drifting each at its own pace
float mistShare = smoothstep(AbyssMistStart, max(AbyssMistEnd, AbyssMistStart + 1.0), depthBelow);
float mist = 0.0;
[branch] if (mistShare > 0.001)
{
    float cosView = max(dot(-viewRay, surfaceUp), AbyssViewFloor);
    float3 drift = float3(0.37, -0.21, 0.13) * Time * AbyssSpeed / max(AbyssScale, 1.0);
    // the finer octave fades where a pixel covers much of its cells, as from far away it only speckles
    float mistDetail = AbyssDetail * (1.0 - smoothstep(AbyssDetailFadeStart, max(AbyssDetailFadeEnd, AbyssDetailFadeStart + 1e-3),
        (length(PosDdx) + length(PosDdy)) * AbyssDetailScale / max(AbyssScale, 1.0)));
    float sliceSum = 0.0;
    int slices = (int)clamp(round(AbyssSlices), 1.0, 6.0);
    [loop] for (int slice = 0; slice < 6; ++slice)
    {
        if (slice >= slices)
        {
            break;
        }
        float3 at = WorldPos + viewRay * ((slice + 0.5) * AbyssSliceDepth / cosView);
        float3 p = at / max(AbyssScale, 1.0) + drift * (1.0 + slice * AbyssSliceDrift) + slice * 7.31;
        float n = lerp(cloudNoise.Value(p), cloudNoise.Value(p * AbyssDetailScale + 3.3), mistDetail * 0.5);
        float weight = exp2(-slice * AbyssSliceFalloff);
        mist += saturate((n - 0.5) * AbyssContrast + 0.5) * weight;
        sliceSum += weight;
    }
    mist = mist / max(sliceSum, 1e-3);
}
float3 mistLight = AbyssColor.rgb * AbyssBrightness * mistShare * lerp(AbyssMistFloor, 1.0, mist * mist);
// the floor itself gives way to the mist, so nothing of it shows through
albedo *= 1.0 - mistShare * AbyssFloorHide;

// the lip of a passage: the sides between a passage and a void, seen from either cell, near the top of the passages
uint lipBits = biomeOwn.a;
uint lipOwn = (lipBits >> 6) & 1u;
float lipDistance = 1.0;
[unroll] for (int lipSide = 0; lipSide < 6; ++lipSide)
{
    if (lipSide < biomeSides && ((lipBits >> lipSide) & 1u) != lipOwn)
    {
        lipDistance = min(lipDistance, apothem - dot(LocalUV, float2(cos(lipSide * biomeTurn), sin(lipSide * biomeTurn))));
    }
}
float lip = (1.0 - smoothstep(0.0, AbyssLipWidth, lipDistance)) * (1.0 - smoothstep(0.0, AbyssLipDepth, depthBelow));

// the Links tab of the editor (AHexPlanet::UpdateLinkMarks): the blue of CellMarks is 1 + the network of a passage, 9 where something is wrong and
// 10 for a passage no exit reaches; the cell takes LinksTint of that color and glows by LinksGlow of it, also on the mist of a void
uint linkMark = (uint)round(markPixel.b * 255.0);
float3 linkGlow = 0.0;
[branch] if (linkMark > 0u && PreviewTile < 0.5)
{
    float3 networkColors[8] = {NetworkColor0.rgb, NetworkColor1.rgb, NetworkColor2.rgb, NetworkColor3.rgb, NetworkColor4.rgb, NetworkColor5.rgb, NetworkColor6.rgb, NetworkColor7.rgb};
    float3 linkColor = linkMark == 9u ? ProblemColor.rgb : linkMark >= 10u ? LinksOrphanColor.rgb : networkColors[min(linkMark - 1u, 7u)];
    albedo = lerp(albedo, linkColor, LinksTint);
    linkGlow = linkColor * LinksGlow;
}

// the grid lines show on the passages and fade out down the cliffs; the outlines show everywhere, for painting the voids
float opacity = GridOpacity * (1.0 - smoothstep(0.0, AbyssGridFade, depthBelow - 0.5));
float3 color = lerp(albedo, albedo * GridDarkness, gridLine * opacity);
OutRoughness = Roughness;
OutEmissive = lerp(color * (Ambient + Glow * Alpha * PreviewTile) + mistLight + AbyssLipColor.rgb * lip + linkGlow, markColor * MarkGlow, markLine);
return lerp(color, markColor, markLine);
'''
UNDERGROUND_PARAMETERS = ['LevelHeight', 'AbyssFadeStart', 'AbyssFadeEnd', 'AbyssDarkness', 'AbyssColor', 'AbyssBrightness', 'AbyssMistStart', 'AbyssMistEnd',
    'AbyssScale', 'AbyssDetail', 'AbyssContrast', 'AbyssSlices', 'AbyssSliceDepth', 'AbyssSpeed', 'AbyssViewFloor', 'AbyssDetailScale', 'AbyssDetailFadeStart',
    'AbyssDetailFadeEnd', 'AbyssSliceDrift', 'AbyssSliceFalloff', 'AbyssMistFloor', 'AbyssFloorHide', 'AbyssLipColor', 'AbyssLipWidth', 'AbyssLipDepth', 'AbyssGridFade', 'Roughness',
    'ProblemColor', 'LinksOrphanColor', 'LinksTint', 'LinksGlow']
# the colors of the networks, NetworkColors of the surface, which AHexPlanet::SetLinksShown gives the material, as a config gives no arrays
LINK_NETWORK_COLORS = [(1.0, 0.55, 0.1), (0.1, 0.7, 1.0), (0.95, 0.25, 0.8), (0.45, 0.95, 0.15), (0.6, 0.4, 1.0), (1.0, 0.22, 0.18), (0.15, 0.95, 0.75), (1.0, 0.9, 0.25)]


def build_underground_material():
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(UNDERGROUND_MATERIAL) if assets.does_asset_exist(UNDERGROUND_MATERIAL) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        UNDERGROUND_MATERIAL.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)
    material.set_editor_property('tangent_space_normal', False)

    custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -200, 0)
    custom.set_editor_property('code', UNDERGROUND_CODE)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    outputs = []
    for output_name, output_type in (('OutEmissive', unreal.CustomMaterialOutputType.CMOT_FLOAT3), ('OutNormal', unreal.CustomMaterialOutputType.CMOT_FLOAT3),
                                     ('OutRoughness', unreal.CustomMaterialOutputType.CMOT_FLOAT1)):
        output = unreal.CustomOutput()
        output.set_editor_property('output_name', output_name)
        output.set_editor_property('output_type', output_type)
        outputs.append(output)
    custom.set_editor_property('additional_outputs', outputs)

    vertex_color = library.create_material_expression(material, unreal.MaterialExpressionVertexColor, -900, -300)
    u = add_uv_channel(library, material, 0, 'r', -100)
    v = add_uv_channel(library, material, 0, 'g', 100)
    u_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -700, -100)
    u_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -700, 0)
    library.connect_material_expressions(u, '', u_ddx, '')
    library.connect_material_expressions(u, '', u_ddy, '')
    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1100, 3200)
    position_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -900, 3200)
    position_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -900, 3250)
    library.connect_material_expressions(position, '', position_ddx, '')
    library.connect_material_expressions(position, '', position_ddy, '')

    connections = [('Color', vertex_color, ''), ('Alpha', vertex_color, 'A'), ('U', u, ''), ('UDdx', u_ddx, ''), ('UDdy', u_ddy, ''), ('PeakShare', v, '')
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig, ['GridWidth', 'GridMinPixels', 'GridMaxPixels', 'GridOpacity', 'GridDarkness', 'Ambient', 'Glow'], 100) + [
        ('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 450), ''),
        ('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -1000, 500), ''),
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1000, 550), ''),
        ('PlanetRadius', add_scalar(library, material, 'PlanetRadius', 11503.0, 600), ''),
        ] + add_mark_inputs(library, material, 2500) + [
        ('PosDdx', position_ddx, ''), ('PosDdy', position_ddy, ''),
        ('VertexNormal', library.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -1000, 3300), ''),
        ('CellBiomes', add_texture(library, material, DEFAULT_MARKS, 'CellBiomes', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 3350), ''),
        ('Colors', add_texture(library, material, COLORS, 'Colors', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 3400), ''),
        ('Normals', add_texture(library, material, NORMALS, 'Normals', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 3450), ''),
        ('LayerParams', add_texture(library, material, DEFAULT_MARKS, 'LayerParams', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 3460), ''),
        ('PreviewTile', add_scalar(library, material, 'PreviewTile', 0.0, 3500), ''),
        ('FacetShare', add_uv_channel(library, material, 1, 'r', 3525), ''),
        ('CreviceShare', add_uv_channel(library, material, 1, 'g', 3540), ''),
        ] + [('%sLayers%d' % (kind, group), add_vector4(library, material, '%sLayers%d' % (kind, group), unreal.LinearColor(*table[group * 4:group * 4 + 4]),
            3545 + group * 5), '') for kind, table in (('Beach', BEACH_LAYERS), ('Rock', ROCK_LAYERS), ('Seabed', SEABED_LAYERS)) for group in range(3)
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig, BIOME_PARAMETERS + UNDERGROUND_PARAMETERS, 3600) + [
        ('NetworkColor%d' % index, add_vector(library, material, 'NetworkColor%d' % index, unreal.LinearColor(*color, 1.0), 5600 + index * 50), '')
        for index, color in enumerate(LINK_NETWORK_COLORS)]

    inputs = []
    for name, _, _ in connections:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)
    for name, node, pin in connections:
        library.connect_material_expressions(node, pin, custom, name)

    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_BASE_COLOR)
    library.connect_material_property(custom, 'OutEmissive', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.connect_material_property(custom, 'OutNormal', unreal.MaterialProperty.MP_NORMAL)
    library.connect_material_property(custom, 'OutRoughness', unreal.MaterialProperty.MP_ROUGHNESS)

    # the same mask as M_HexPlanet, for the debug view and the band where the pieces dissolve into the coarse mesh, which the masked copy
    # M_HexUnderground_Cut draws (AHexPlanet finds it by name)
    cut_connections = [('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 700), ''),
        ('HideCount', add_scalar(library, material, 'HideCount', 0.0, 750), ''),
        ('LodSide', add_scalar(library, material, 'LodSide', 0.0, 1800), ''),
        ('LodCamera', add_vector(library, material, 'LodCamera', unreal.LinearColor(0.0, 0.0, 0.0, 0.0), 1850), '')
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig, ['CoarseDistance', 'CoarseBand'], 1900)
    for index in range(HIDE_CELLS):
        cut_connections.append(('HideCell%d' % index, add_vector(library, material, 'HideCell%d' % index, unreal.LinearColor(0.0, 0.0, 0.0, 0.0), 800 + index * 50), ''))
    cut = add_custom(library, material, SURFACE_CUT_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT1, cut_connections, 700)
    library.connect_material_property(cut, '', unreal.MaterialProperty.MP_OPACITY_MASK)

    library.recompile_material(material)
    assets.save_asset(UNDERGROUND_MATERIAL)

    cut_path = UNDERGROUND_MATERIAL + '_Cut'
    instance = unreal.load_asset(cut_path) if assets.does_asset_exist(cut_path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        cut_path.split('/')[-1], FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    library.set_material_instance_parent(instance, material)
    overrides = instance.get_editor_property('base_property_overrides')
    overrides.set_editor_property('override_blend_mode', True)
    overrides.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    instance.set_editor_property('base_property_overrides', overrides)
    library.update_material_instance(instance)
    assets.save_asset(cut_path, only_if_is_dirty=False)
    return material


# the fog of the underground, a post process before the translucency in place of the haze (bFog of DA_HexUnderground): the scene fades to FogColor
# with the length of the view past FogStart, and the part of the view below FogMistTop under the passages goes through a mist that thickens over
# FogMistDepth, marched in eight steps, so a view into a chasm meets more of it the deeper it goes; the space past the globe is FogSpaceColor
UNDERGROUND_FOG_CODE = r'''
float3 fogDir = -normalize(CameraVector);
bool fogSpace = SceneDepth > 1e6;
float fogEnd = fogSpace ? 1e9 : length(WorldPos - CameraPos);
float darkAmount = FogMax * (1.0 - exp(-max(min(fogEnd, 1e6) - FogStart, 0.0) * FogDensity));
float mistTop = PlanetRadius + LevelHeight - FogMistTop;
float mistB = dot(CameraPos, fogDir);
float mistDisc = mistB * mistB - (dot(CameraPos, CameraPos) - mistTop * mistTop);
float mistAmount = 0.0;
if (mistDisc > 0.0 && PlanetRadius > 0.0)
{
    float mistRoot = sqrt(mistDisc);
    float mistIn = max(-mistB - mistRoot, 0.0);
    float mistOut = min(-mistB + mistRoot, fogEnd);
    if (mistOut > mistIn)
    {
        float mistStep = (mistOut - mistIn) / 8.0;
        float mistDepth = 0.0;
        for (int mistIndex = 0; mistIndex < 8; ++mistIndex)
        {
            float3 mistP = CameraPos + fogDir * (mistIn + (mistIndex + 0.5) * mistStep);
            mistDepth += smoothstep(0.0, max(FogMistDepth, 1.0), mistTop - length(mistP));
        }
        mistAmount = FogMistMax * (1.0 - exp(-mistDepth * mistStep * FogMistDensity));
    }
}
float3 fogColor = lerp(SceneColor.rgb, FogMistColor.rgb, mistAmount);
return lerp(fogColor, fogSpace ? FogSpaceColor.rgb : FogColor.rgb, fogSpace ? 1.0 : darkAmount);
'''
UNDERGROUND_FOG_PARAMETERS = ['LevelHeight', 'FogColor', 'FogStart', 'FogDensity', 'FogMax', 'FogSpaceColor', 'FogMistColor', 'FogMistTop', 'FogMistDepth',
    'FogMistDensity', 'FogMistMax']


def build_underground_fog():
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(UNDERGROUND_FOG_MATERIAL) if assets.does_asset_exist(UNDERGROUND_FOG_MATERIAL) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        UNDERGROUND_FOG_MATERIAL.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF)

    scene = library.create_material_expression(material, unreal.MaterialExpressionSceneTexture, -1000, -400)
    scene.set_editor_property('scene_texture_id', unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    connections = [('SceneColor', scene, 'Color'),
        ('SceneDepth', library.create_material_expression(material, unreal.MaterialExpressionSceneDepth, -1000, -300), ''),
        ('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, -250), ''),
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1000, -200), ''),
        ('CameraVector', library.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, -1000, -150), ''),
        ('PlanetRadius', add_scalar(library, material, 'PlanetRadius', 0.0, -50), '')
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig, UNDERGROUND_FOG_PARAMETERS, 0)
    custom = add_custom(library, material, UNDERGROUND_FOG_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, connections, 0)
    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(material)
    assets.save_asset(UNDERGROUND_FOG_MATERIAL)
    return material


# the markers of the entrances (AHexPlanet::UpdateMarkers): the cave mouth on the surface and the ring of rocks underground are rock of the layer of
# the first custom data of an instance, projected as the boulders are, and glow in the color of their network (custom data 1 to 3) by the alpha of
# the color of their vertices, the mouth of the cave and the glyph in the ring, which slowly breathes (MarkerGlow, MarkerPulse*), while the rock
# gives Ambient and MarkerRockLight of that color as its own light; the shaft over an exit is light alone, added, brightest at its foot by the red
# of its vertices, softer at its edges, flickering up it and gone next to the camera (Shaft*)
MARKER_MATERIAL = FOLDER + '/M_HexPlanetMarker'
SHAFT_MATERIAL = FOLDER + '/M_HexPlanetShaft'
MARKER_CODE = r'''
float3 n = normalize(VertexNormal);
float3 planes = pow(abs(n), TriplanarSharpness);
planes /= planes.x + planes.y + planes.z;
float3 pos = WorldPos / BiomeTileSize;
float3 dx = PosDdx / BiomeTileSize;
float3 dy = PosDdy / BiomeTileSize;
float layer = round(RockLayer);
float3 rock = Colors.SampleGrad(ColorsSampler, float3(pos.yz, layer), dx.yz, dy.yz).rgb * planes.x
    + Colors.SampleGrad(ColorsSampler, float3(pos.xz, layer), dx.xz, dy.xz).rgb * planes.y
    + Colors.SampleGrad(ColorsSampler, float3(pos.xy, layer), dx.xy, dy.xy).rgb * planes.z;
float glowMask = saturate(VertexAlpha);
float breathe = 1.0 - MarkerPulse + MarkerPulse * sin(Time * MarkerPulseSpeed + InstanceRandom * 6.2831853);
float3 albedo = rock * (1.0 - glowMask * MarkerRockDarkening);
// the rock gives light of its own as the ground does (Ambient), and some of the glow of its network, so its stones do not stand black
OutEmissive = NetworkColor * MarkerGlow * glowMask * breathe + albedo * (Ambient + NetworkColor * MarkerRockLight) * (1.0 - glowMask);
return albedo;
'''
SHAFT_CODE = r'''
float3 n = normalize(VertexNormal);
float facing = abs(dot(n, normalize(CameraVector)));
float rise = saturate(VertexRed);
float flicker = 1.0 - ShaftFlicker + ShaftFlicker * sin(Time * ShaftFlickerSpeed + rise * ShaftFlickerRise + InstanceRandom * 6.2831853);
float near = smoothstep(ShaftNearFadeStart, max(ShaftNearFade, ShaftNearFadeStart + 1.0), length(WorldPos - CameraPos));
return NetworkColor * ShaftBrightness * pow(facing, ShaftFacingPower) * (1.0 - rise) * (1.0 - rise) * flicker * near;
'''


def add_network_color(library, material, y):
    """The color of the network of an instance, its custom data 1 to 3"""
    channels = []
    for index in range(3):
        data = library.create_material_expression(material, unreal.MaterialExpressionPerInstanceCustomData, -1100, y + index * 50)
        data.set_editor_property('data_index', index + 1)
        channels.append(data)
    first = library.create_material_expression(material, unreal.MaterialExpressionAppendVector, -900, y)
    library.connect_material_expressions(channels[0], '', first, 'A')
    library.connect_material_expressions(channels[1], '', first, 'B')
    color = library.create_material_expression(material, unreal.MaterialExpressionAppendVector, -800, y)
    library.connect_material_expressions(first, '', color, 'A')
    library.connect_material_expressions(channels[2], '', color, 'B')
    return color


def build_marker_materials():
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    material = unreal.load_asset(MARKER_MATERIAL) if assets.does_asset_exist(MARKER_MATERIAL) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        MARKER_MATERIAL.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(material)
    material.set_editor_property('used_with_instanced_static_meshes', True)
    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1100, 0)
    position_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -900, 0)
    position_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -900, 50)
    library.connect_material_expressions(position, '', position_ddx, '')
    library.connect_material_expressions(position, '', position_ddy, '')
    rock_layer = library.create_material_expression(material, unreal.MaterialExpressionPerInstanceCustomData, -900, 150)
    rock_layer.set_editor_property('data_index', 0)
    vertex_color = library.create_material_expression(material, unreal.MaterialExpressionVertexColor, -900, 175)
    connections = [('WorldPos', position, ''), ('PosDdx', position_ddx, ''), ('PosDdy', position_ddy, ''),
        ('VertexNormal', library.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -1000, 100), ''),
        ('RockLayer', rock_layer, ''), ('VertexAlpha', vertex_color, 'A'), ('NetworkColor', add_network_color(library, material, 400), ''),
        ('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -900, 200), ''),
        ('InstanceRandom', library.create_material_expression(material, unreal.MaterialExpressionPerInstanceRandom, -900, 225), ''),
        ('Colors', add_texture(library, material, COLORS, 'Colors', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 250), '')
        ] + add_config_parameters(library, material, unreal.HexPlanetConfig, ['BiomeTileSize', 'TriplanarSharpness', 'MarkerGlow', 'MarkerPulse',
            'MarkerPulseSpeed', 'MarkerRockDarkening', 'MarkerRockLight', 'Ambient'], 300)
    custom = add_custom(library, material, MARKER_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, connections, 0,
        ('OutEmissive', unreal.CustomMaterialOutputType.CMOT_FLOAT3))
    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_BASE_COLOR)
    library.connect_material_property(custom, 'OutEmissive', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    roughness = add_config_parameters(library, material, unreal.HexPlanetConfig, ['Roughness'], 600)[0][1]
    library.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    library.recompile_material(material)
    assets.save_asset(MARKER_MATERIAL)

    shaft = unreal.load_asset(SHAFT_MATERIAL) if assets.does_asset_exist(SHAFT_MATERIAL) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        SHAFT_MATERIAL.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    clear_expressions(shaft)
    shaft.set_editor_property('used_with_instanced_static_meshes', True)
    shaft.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    shaft.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    shaft.set_editor_property('two_sided', True)
    vertex_color = library.create_material_expression(shaft, unreal.MaterialExpressionVertexColor, -900, 175)
    connections = [('WorldPos', library.create_material_expression(shaft, unreal.MaterialExpressionWorldPosition, -1100, 0), ''),
        ('CameraPos', library.create_material_expression(shaft, unreal.MaterialExpressionCameraPositionWS, -1100, 50), ''),
        ('CameraVector', library.create_material_expression(shaft, unreal.MaterialExpressionCameraVectorWS, -1100, 100), ''),
        ('VertexNormal', library.create_material_expression(shaft, unreal.MaterialExpressionVertexNormalWS, -1000, 150), ''),
        ('VertexRed', vertex_color, 'R'), ('NetworkColor', add_network_color(library, shaft, 400), ''),
        ('Time', library.create_material_expression(shaft, unreal.MaterialExpressionTime, -900, 200), ''),
        ('InstanceRandom', library.create_material_expression(shaft, unreal.MaterialExpressionPerInstanceRandom, -900, 225), '')
        ] + add_config_parameters(library, shaft, unreal.HexPlanetConfig, ['ShaftBrightness', 'ShaftFlicker', 'ShaftFlickerSpeed', 'ShaftFlickerRise',
            'ShaftFacingPower', 'ShaftNearFadeStart', 'ShaftNearFade'], 300)
    custom = add_custom(library, shaft, SHAFT_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, connections, 0)
    library.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(shaft)
    assets.save_asset(SHAFT_MATERIAL)


def build_underground():
    build_underground_material()
    build_underground_fog()
    build_marker_materials()


def main():
    build_configs()
    # with the argument configs it only makes the data assets that are missing, so the materials and the level stay as they are
    if 'configs' in sys.argv[1:]:
        unreal.log('PLANET configs created')
        return
    # with underground only the materials of the underground and the markers of the entrances
    if 'underground' in sys.argv[1:]:
        build_underground()
        unreal.log('PLANET underground assets created')
        return
    material = build_material()
    water_material = build_water()
    water_grid_material = build_water_grid()
    build_clouds()
    build_haze()
    build_auroras()
    build_boulder_material()
    build_floe_material()
    build_foliage_material(build_parameter_collection())
    build_underground()
    wire_material = build_wireframe()
    build_level(material, water_material, water_grid_material, wire_material)
    unreal.log('PLANET assets created')


main()
