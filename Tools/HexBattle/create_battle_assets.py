"""Builds the materials of the battles (M_HexBattleGround, M_HexBattleMark, M_HexBattleUnit, M_HexBattleSky, M_HexBattleOutline and
M_HexBattleWater, the moat of a siege, M_HexBattleProp, the gate, the bridge and the steps of a siege, and M_HexBattleFog, the fog of war
around the field; the arguments water, ground, prop and fog build those alone, and textures imports the timber of fetch_battle_textures.ps1)
in /Game/HexPlanet/Battle and
creates DA_HexBattle when it is missing, run with the commandlet:

    UnrealEditor-Cmd.exe HexHero.uproject -run=pythonscript -script="Tools/HexBattle/create_battle_assets.py" -unattended -nullrhi

Edit this script, not the materials. DA_HexBattle belongs to whoever tunes it once it is there; import_battle_units.py gives it the looks of the
units and the grounds of the field while it has none."""

import json
import os
import sys

import unreal

FOLDER = '/Game/HexPlanet/Battle'
COLORS = '/Game/HexMap/Textures/Biomes/TA_Biome_Color'
NORMALS = '/Game/HexMap/Textures/Biomes/TA_Biome_Normal'
NOISE = '/Game/HexMap/Textures/T_HexNoise'
GROUND = FOLDER + '/M_HexBattleGround'
MARK = FOLDER + '/M_HexBattleMark'
UNIT = FOLDER + '/M_HexBattleUnit'
SKY = FOLDER + '/M_HexBattleSky'
OUTLINE = FOLDER + '/M_HexBattleOutline'
WATER = FOLDER + '/M_HexBattleWater'
PROP = FOLDER + '/M_HexBattleProp'
FOG = FOLDER + '/M_HexBattleFog'
CONFIG = FOLDER + '/DA_HexBattle'

library = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary


def open_material(path):
    material = unreal.load_asset(path) if assets.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        path.split('/')[-1], FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    for _ in range(64):
        if library.get_num_material_expressions(material) == 0:
            break
        library.delete_all_material_expressions(material)
    return material


def scalar(material, name, value, y):
    node = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, y)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', value)
    return node


def vector(material, name, value, y):
    node = library.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, y)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', value)
    return node


def noise_texture(material, y):
    """T_HexNoise as a texture object, for custom nodes"""
    noise = library.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -700, y)
    noise.set_editor_property('parameter_name', 'HexNoise')
    noise.set_editor_property('texture', unreal.load_asset(NOISE))
    noise.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    return noise


def vector4(material, name, value, y):
    """A vector parameter with its alpha, through an append node, as a vector parameter gives a custom node only its RGB"""
    node = vector(material, name, value, y)
    append = library.create_material_expression(material, unreal.MaterialExpressionAppendVector, -550, y)
    library.connect_material_expressions(node, '', append, 'A')
    library.connect_material_expressions(node, 'A', append, 'B')
    return append


def custom(material, code, output_type, connections, y):
    node = library.create_material_expression(material, unreal.MaterialExpressionCustom, -200, y)
    node.set_editor_property('code', code)
    node.set_editor_property('output_type', output_type)
    inputs = []
    for name, _, _ in connections:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    node.set_editor_property('inputs', inputs)
    for name, source, pin in connections:
        library.connect_material_expressions(source, pin, node, name)
    return node


# The shore of the moat of a siege, which the ground and the water both take, so the bed and the water meet, as the developer asked on 2026-11-13:
# the water reaches from under the front wall (MoatWall.x, its face along Y, as the rows of the field run along X) out to the far sides of the
# hexes of the moat, which zigzag by half a hex from row to row (MoatRows: the X of row 0 and the step of a row, the Y of the far flat edge of
# an even row and of an odd one); each corner between two rows is rounded by a share of its own up to MoatCornerCut of a hex and the line drawn
# towards a straight one by chance, so it shows no pattern, and it winds by MoatShoreNoise of a hex; its ends lie at MoatWall.y and .z.
# shore is how far a point lies out of the water, below 0 in it, and moatInto how far into it
MOAT_SHORE_CODE = r"""
float moatR = max(MoatWall.w, 1.0);
float moatRowF = (WorldPos.x - MoatRows.x) / (abs(MoatRows.y) > 0.001 ? MoatRows.y : 1.0);
float moatRow0 = floor(moatRowF);
float moatFar = 1.0e9;
for (int moatK = -1; moatK <= 2; moatK++)
{
    float row = moatRow0 + moatK;
    float rowX = MoatRows.x + row * MoatRows.y;
    float flatEdge = abs(row - 2.0 * floor(row * 0.5)) < 0.5 ? MoatRows.z : MoatRows.w;
    // a hex reaches out to its flat edge and draws back to its points at its ends
    float edge = flatEdge + max(abs(WorldPos.x - rowX) - 0.5 * moatR, 0.0) * 1.7320508;
    float cut = MoatCornerCut * moatR * (0.3 + 0.7 * frac(sin(row * 12.9898 + MoatSeed * 7.13) * 43758.5453)) + 0.001;
    float soft = max(cut - abs(edge - moatFar), 0.0) / cut;
    moatFar = min(edge, moatFar) - soft * soft * cut * 0.25;
}
float moatT = moatRowF - moatRow0;
float moatH0 = frac(sin(moatRow0 * 78.233 + MoatSeed * 3.71) * 24634.6345);
float moatH1 = frac(sin((moatRow0 + 1.0) * 78.233 + MoatSeed * 3.71) * 24634.6345);
moatFar = lerp(moatFar, 0.5 * (MoatRows.z + MoatRows.w), MoatCornerCut * lerp(0.2, 0.75, lerp(moatH0, moatH1, smoothstep(0.0, 1.0, moatT))));
moatFar += MoatShoreNoise * moatR * (0.6 * sin(WorldPos.x * 0.047 + MoatSeed) + 0.4 * sin(WorldPos.x * 0.131 + 1.7 + MoatSeed * 0.3));
float moatWallGap = MoatWall.x - WorldPos.y;
// its ends round off over MoatEndRound of a hex, raggedly and each its own way, as the developer asked on 2026-11-13 when they ended sharply
float moatMid = 0.5 * (MoatRows.z + MoatRows.w);
float moatHalfW = max(0.5 * (MoatWall.x + moatR * 0.3 - moatMid), 1.0);
float moatCenterY = moatMid + moatHalfW;
float moatEndA = WorldPos.x - MoatWall.y;
float moatEndB = MoatWall.z - WorldPos.x;
float moatSide = moatEndA < moatEndB ? 0.0 : 1.0;
float moatEnd = min(moatEndA, moatEndB) + MoatShoreNoise * moatR * 3.0 * sin(WorldPos.y * 0.07 + MoatSeed + moatSide * 2.3)
    + moatR * 0.5 * (frac(sin((MoatSeed + moatSide) * 91.7) * 4375.85) - 0.5);
float moatRound = moatR * MoatEndRound * (0.75 + 0.5 * frac(sin((MoatSeed + moatSide) * 37.1) * 1735.3));
float moatAllowed = moatHalfW * sqrt(saturate(moatEnd / max(moatRound, 1.0)));
float moatInto = min(min(WorldPos.y - moatFar, moatWallGap + moatR * 0.3), moatAllowed - abs(WorldPos.y - moatCenterY));
float shore = -moatInto;
"""

# The ground of the field: a layer of the terrain (TerrainLayer of the biome fought on) from above at two sizes, broken in patches, its
# hollows darker by the height in its alpha, darker away from the field so the field stands out, and darker still underground; since
# 2026-11-13, as the developer found it flat, patches of the rock of the biome (RockLayer, GroundRockShare), more on the hills around the field,
# blended by the heights in the alpha of the layers, and the relief of the layers by their normals (GROUND_NORMAL_CODE, GroundRelief); since
# 2026-11-14, as the developer found it one color, its tone by its relief, which the field gives in the colors of its vertices (BuildGround):
# red how far a point lies above (1) or below (0) the ground around it, green how steep it is, so hollows are darker (GroundHollowShade),
# tops lighter and drier (GroundTopLight, GroundDryTint) and slopes rockier (GroundSlopeRock)
GROUND_MIX_CODE = r"""
float2 uv = WorldPos.xy / TileSize;
float2 dx = PosDdx.xy / TileSize;
float2 dy = PosDdy.xy / TileSize;
float rise = saturate((WorldPos.z - FieldCenter.z) / max(HillHeight, 1.0));
float patchNoise = 0.5 + 0.3 * sin(WorldPos.x * 0.0031 + sin(WorldPos.y * 0.0023) * 2.3) * sin(WorldPos.y * 0.0037 + 0.8)
    + 0.2 * sin(WorldPos.x * 0.0083 + 2.1) * sin(WorldPos.y * 0.0071 + sin(WorldPos.x * 0.004));
float4 soil = Colors.SampleGrad(ColorsSampler, float3(uv, TerrainLayer), dx, dy);
float4 rock = Colors.SampleGrad(ColorsSampler, float3(uv * 0.8 + 0.13, RockLayer), dx * 0.8, dy * 0.8);
float convex = Relief.r * 2.0 - 1.0;
float slope = Relief.g;
float rockShare = saturate(GroundRockShare + rise * 0.8 + slope * GroundSlopeRock);
float rockMask = smoothstep(1.0 - rockShare - 0.12, 1.0 - rockShare + 0.12, patchNoise + (rock.a - soil.a) * 0.15);
"""
GROUND_CODE = GROUND_MIX_CODE + r"""
float4 far = Colors.SampleGrad(ColorsSampler, float3(uv * 0.27 + 0.31, TerrainLayer), dx * 0.27, dy * 0.27);
float3 color = lerp(soil.rgb, far.rgb, 0.35) * lerp(0.78, 1.08, soil.a);
color = lerp(color, rock.rgb * lerp(0.75, 1.1, rock.a), rockMask);
// patches of tone
float patch = sin(WorldPos.x * 0.0047 + sin(WorldPos.y * 0.0031) * 2.0) * sin(WorldPos.y * 0.0053 + 1.7);
color *= 1.0 + 0.08 * patch;
// darker in hollows, lighter and drier on tops
color *= convex < 0.0 ? lerp(1.0, GroundHollowShade, -convex) : lerp(1.0, GroundTopLight, convex);
color = lerp(color, color * GroundDryTint.rgb, saturate(convex));
// the field and the ground around it
float2 away = abs(WorldPos.xy - FieldCenter.xy) / max(FieldHalf.xy, 1.0);
float outside = saturate(max(away.x, away.y) - 1.0);
color *= lerp(FieldGroundTone, 0.72, smoothstep(0.0, 0.6, outside));
// the moat of a siege: wet banks darker by MoatBankColor out to MoatBankWidth and its bed in MoatBedColor under the water
if (MoatOn > 0.5)
{
""" + MOAT_SHORE_CODE + r"""
    float bank = 1.0 - smoothstep(0.0, max(MoatBankWidth, 1.0), shore);
    color = lerp(color, color * MoatBankColor.rgb, bank);
    color = lerp(color, MoatBedColor.rgb, smoothstep(0.0, moatR * 0.35, moatInto));
}
color *= lerp(1.0, 0.55, Underground);
return color;
"""
# the normal of the ground in tangent space, of the layers drawn there, GroundRelief strong
GROUND_NORMAL_CODE = GROUND_MIX_CODE + r"""
float2 soilN = Normals.SampleGrad(NormalsSampler, float3(uv, TerrainLayer), dx, dy).xy * 2.0 - 1.0;
float2 rockN = Normals.SampleGrad(NormalsSampler, float3(uv * 0.8 + 0.13, RockLayer), dx * 0.8, dy * 0.8).xy * 2.0 - 1.0;
float2 n = lerp(soilN, rockN, rockMask) * GroundRelief;
return normalize(float3(n, sqrt(saturate(1.0 - dot(n, n))) + 0.001));
"""


def build_ground():
    defaults = unreal.get_default_object(unreal.HexBattleConfig)
    material = open_material(GROUND)
    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 0)
    relief = library.create_material_expression(material, unreal.MaterialExpressionVertexColor, -1000, -100)
    ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -850, 50)
    ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -850, 100)
    library.connect_material_expressions(position, '', ddx, '')
    library.connect_material_expressions(position, '', ddy, '')
    colors = library.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -700, 150)
    colors.set_editor_property('parameter_name', 'Colors')
    colors.set_editor_property('texture', unreal.load_asset(COLORS))
    colors.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    normals = library.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -700, 175)
    normals.set_editor_property('parameter_name', 'Normals')
    normals.set_editor_property('texture', unreal.load_asset(NORMALS))
    normals.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    mix = [('WorldPos', position, ''), ('PosDdx', ddx, ''), ('PosDdy', ddy, ''), ('Colors', colors, ''),
        ('TerrainLayer', scalar(material, 'TerrainLayer', 0.0, 200), ''), ('TileSize', scalar(material, 'TileSize', 260.0, 250), ''),
        ('RockLayer', scalar(material, 'RockLayer', 2.0, 260), ''),
        ('GroundRockShare', scalar(material, 'GroundRockShare', defaults.get_editor_property('ground_rock_share'), 270), ''),
        ('HillHeight', scalar(material, 'HillHeight', defaults.get_editor_property('hill_height'), 280), ''),
        ('FieldCenter', vector(material, 'FieldCenter', unreal.LinearColor(0.0, 0.0, 0.0, 0.0), 350), ''),
        ('Relief', relief, ''),
        ('GroundSlopeRock', scalar(material, 'GroundSlopeRock', defaults.get_editor_property('ground_slope_rock'), 290), '')]
    connections = mix + [('Underground', scalar(material, 'Underground', 0.0, 300), ''),
        ('FieldHalf', vector(material, 'FieldHalf', unreal.LinearColor(500.0, 700.0, 0.0, 0.0), 400), '')] + moat_connections(material, defaults) + [
        ('MoatBankColor', vector(material, 'MoatBankColor', defaults.get_editor_property('moat_bank_color'), 550), ''),
        ('MoatBedColor', vector(material, 'MoatBedColor', defaults.get_editor_property('moat_bed_color'), 600), ''),
        ('MoatBankWidth', scalar(material, 'MoatBankWidth', defaults.get_editor_property('moat_bank_width'), 650), ''),
        ('MoatOn', scalar(material, 'MoatOn', 0.0, 660), ''),
        ('GroundHollowShade', scalar(material, 'GroundHollowShade', defaults.get_editor_property('ground_hollow_shade'), 670), ''),
        ('GroundTopLight', scalar(material, 'GroundTopLight', defaults.get_editor_property('ground_top_light'), 680), ''),
        ('GroundDryTint', vector(material, 'GroundDryTint', defaults.get_editor_property('ground_dry_tint'), 690), ''),
        ('FieldGroundTone', scalar(material, 'FieldGroundTone', defaults.get_editor_property('field_ground_tone'), 695), '')]
    node = custom(material, GROUND_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, connections, 0)
    library.connect_material_property(node, '', unreal.MaterialProperty.MP_BASE_COLOR)
    normal = custom(material, GROUND_NORMAL_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, mix + [('Normals', normals, ''),
        ('GroundRelief', scalar(material, 'GroundRelief', defaults.get_editor_property('ground_relief'), 700), '')], 400)
    library.connect_material_property(normal, '', unreal.MaterialProperty.MP_NORMAL)
    library.connect_material_property(scalar(material, 'Roughness', 0.92, 500), '', unreal.MaterialProperty.MP_ROUGHNESS)
    library.recompile_material(material)
    assets.save_asset(GROUND)


def moat_connections(material, defaults):
    """The parameters of the shore of the moat, which the ground and the water share"""
    return [('MoatRows', vector4(material, 'MoatRows', unreal.LinearColor(0.0, 1.0, 0.0, 0.0), 450), ''),
        ('MoatWall', vector4(material, 'MoatWall', unreal.LinearColor(0.0, 0.0, 0.0, 1.0), 500), ''),
        ('MoatCornerCut', scalar(material, 'MoatCornerCut', defaults.get_editor_property('moat_corner_cut'), 510), ''),
        ('MoatShoreNoise', scalar(material, 'MoatShoreNoise', defaults.get_editor_property('moat_shore_noise'), 520), ''),
        ('MoatEndRound', scalar(material, 'MoatEndRound', defaults.get_editor_property('moat_end_round'), 525), ''),
        ('MoatSeed', scalar(material, 'MoatSeed', 0.0, 530), '')]

def build_mark():
    """The lines of the hexes and the marks on them: unlit, the color and the alpha of the vertices"""
    material = open_material(MARK)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    color = library.create_material_expression(material, unreal.MaterialExpressionVertexColor, -700, 0)
    brightness = scalar(material, 'Brightness', 1.4, 100)
    times = library.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, 0)
    library.connect_material_expressions(color, '', times, 'A')
    library.connect_material_expressions(brightness, '', times, 'B')
    library.connect_material_property(times, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.connect_material_property(color, 'A', unreal.MaterialProperty.MP_OPACITY)
    library.recompile_material(material)
    assets.save_asset(MARK)


# A unit: the colors of its vertices times its tint, and by their alpha the color of its side, which glows a little; a mesh without colors
# of its own (the fallback) is all the color of its side
# the surface of a figure is not one smooth color, as the developer found them like plasticine on 2026-11-13: a noise of T_HexNoise taken
# from the three planes of the body at rest (its position before skinning, so it stays on the body as it moves), DetailScale units across,
# two octaves; UNIT_DETAIL_CODE gives that height, UNIT_CODE lightens and darkens the color by DetailTone of it, UNIT_NORMAL_CODE bumps the
# normal by DetailBump of its slope on the screen (the derivatives come in from nodes, for ray tracing)
UNIT_DETAIL_CODE = r"""
float3 n = abs(normalize(LocalNormal));
float3 w = pow(n, 4.0);
w /= max(w.x + w.y + w.z, 0.0001);
float s = 1.0 / max(DetailScale, 0.1);
float3 p = LocalPos * s;
// filtered by how much of the body a pixel covers, as one level of the noise shimmered as the figures moved (2026-11-13)
float3 dx = LocalDdx * s;
float3 dy = LocalDdy * s;
float a = HexNoise.SampleGrad(HexNoiseSampler, p.yz * 0.05, dx.yz * 0.05, dy.yz * 0.05).r * w.x
    + HexNoise.SampleGrad(HexNoiseSampler, p.xz * 0.05, dx.xz * 0.05, dy.xz * 0.05).r * w.y
    + HexNoise.SampleGrad(HexNoiseSampler, p.xy * 0.05, dx.xy * 0.05, dy.xy * 0.05).r * w.z;
float b = HexNoise.SampleGrad(HexNoiseSampler, p.yz * 0.17 + 0.37, dx.yz * 0.17, dy.yz * 0.17).r * w.x
    + HexNoise.SampleGrad(HexNoiseSampler, p.xz * 0.17 + 0.37, dx.xz * 0.17, dy.xz * 0.17).r * w.y
    + HexNoise.SampleGrad(HexNoiseSampler, p.xy * 0.17 + 0.37, dx.xy * 0.17, dy.xy * 0.17).r * w.z;
return a * 0.6 + b * 0.4;
"""
UNIT_CODE = r"""
float3 own = VertexRGB * Tint.rgb;
float3 color = lerp(own, SideColor.rgb * max(max(VertexRGB.r, VertexRGB.g), max(VertexRGB.b, 0.35)), saturate(VertexA * SideColorShare));
return color * (1.0 + (Detail - 0.5) * 2.0 * DetailTone);
"""
UNIT_NORMAL_CODE = r"""
float3 N = normalize(Normal);
float3 r1 = cross(PosDdy, N);
float3 r2 = cross(N, PosDdx);
float det = dot(PosDdx, r1);
float3 grad = sign(det) * (DetailDdx * r1 + DetailDdy * r2);
return normalize(abs(det) * N - grad * DetailBump * 12.0);
"""


def build_unit():
    material = open_material(UNIT)
    material.set_editor_property('two_sided', True)
    material.set_editor_property('used_with_skeletal_mesh', True)
    material.set_editor_property('used_with_static_lighting', False)
    color = library.create_material_expression(material, unreal.MaterialExpressionVertexColor, -1000, 0)
    tint = vector(material, 'Tint', unreal.LinearColor(1.0, 1.0, 1.0, 1.0), 100)
    side = vector(material, 'SideColor', unreal.LinearColor(0.6, 0.6, 0.6, 1.0), 150)
    share = scalar(material, 'SideColorShare', 0.6, 200)
    material.set_editor_property('tangent_space_normal', False)
    # the body at rest is known to the vertex shader alone, so it comes to the pixels through interpolators
    local = library.create_material_expression(material, unreal.MaterialExpressionVertexInterpolator, -1000, 450)
    library.connect_material_expressions(library.create_material_expression(material, unreal.MaterialExpressionPreSkinnedPosition, -1200, 450), '', local, '')
    local_normal = library.create_material_expression(material, unreal.MaterialExpressionVertexInterpolator, -1000, 500)
    library.connect_material_expressions(library.create_material_expression(material, unreal.MaterialExpressionPreSkinnedNormal, -1200, 500), '', local_normal, '')
    local_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -850, 420)
    local_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -850, 440)
    library.connect_material_expressions(local, '', local_ddx, '')
    library.connect_material_expressions(local, '', local_ddy, '')
    detail = custom(material, UNIT_DETAIL_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT1, [('LocalPos', local, ''), ('LocalNormal', local_normal, ''),
        ('LocalDdx', local_ddx, ''), ('LocalDdy', local_ddy, ''),
        ('HexNoise', noise_texture(material, 550), ''), ('DetailScale', scalar(material, 'DetailScale', 10.0, 600), '')], 450)
    base = custom(material, UNIT_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, [('VertexRGB', color, ''), ('VertexA', color, 'A'), ('Tint', tint, ''),
        ('SideColor', side, ''), ('SideColorShare', share, ''), ('Detail', detail, ''), ('DetailTone', scalar(material, 'DetailTone', 0.12, 650), '')], 0)
    library.connect_material_property(base, '', unreal.MaterialProperty.MP_BASE_COLOR)
    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 700)
    pos_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -850, 700)
    pos_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -850, 750)
    library.connect_material_expressions(position, '', pos_ddx, '')
    library.connect_material_expressions(position, '', pos_ddy, '')
    detail_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -850, 800)
    detail_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -850, 850)
    library.connect_material_expressions(detail, '', detail_ddx, '')
    library.connect_material_expressions(detail, '', detail_ddy, '')
    normal = custom(material, UNIT_NORMAL_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, [
        ('Normal', library.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -1000, 900), ''),
        ('PosDdx', pos_ddx, ''), ('PosDdy', pos_ddy, ''), ('DetailDdx', detail_ddx, ''), ('DetailDdy', detail_ddy, ''),
        ('DetailBump', scalar(material, 'DetailBump', 0.35, 950), '')], 700)
    library.connect_material_property(normal, '', unreal.MaterialProperty.MP_NORMAL)
    glow = custom(material, 'return SideColor.rgb * saturate(VertexA * SideColorShare) * Glow;', unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        [('VertexA', color, 'A'), ('SideColor', side, ''), ('SideColorShare', share, ''), ('Glow', scalar(material, 'Glow', 0.12, 250), '')], 300)
    library.connect_material_property(glow, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.connect_material_property(scalar(material, 'Roughness', 0.65, 400), '', unreal.MaterialProperty.MP_ROUGHNESS)
    library.recompile_material(material)
    assets.save_asset(UNIT)


# The outline of the unit under the cursor, as the developer asked on 2026-11-12: a copy of its figure pushed out along its normals by
# OutlineWidth plus OutlineWidthPerDistance a unit of distance from the camera, so it is as thin from afar, and only its back faces drawn, in
# OutlineColor, unlit, which shows as a thin line along the silhouette
OUTLINE_OFFSET_CODE = r"""
return Normal * (OutlineWidth + distance(CameraPosition, WorldPosition) * OutlineWidthPerDistance);
"""


def build_outline():
    material = open_material(OUTLINE)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', True)
    material.set_editor_property('used_with_skeletal_mesh', True)
    material.set_editor_property('used_with_static_lighting', False)
    color = vector(material, 'OutlineColor', unreal.LinearColor(1.0, 0.8, 0.4, 1.0), 0)
    library.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    sign = library.create_material_expression(material, unreal.MaterialExpressionTwoSidedSign, -1000, 200)
    mask = custom(material, 'return Sign < 0.0 ? 1.0 : 0.0;', unreal.CustomMaterialOutputType.CMOT_FLOAT1, [('Sign', sign, '')], 200)
    library.connect_material_property(mask, '', unreal.MaterialProperty.MP_OPACITY_MASK)
    offset = custom(material, OUTLINE_OFFSET_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, [
        ('Normal', library.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -1000, 400), ''),
        ('CameraPosition', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1000, 450), ''),
        ('WorldPosition', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 500), ''),
        ('OutlineWidth', scalar(material, 'OutlineWidth', 0.4, 550), ''),
        ('OutlineWidthPerDistance', scalar(material, 'OutlineWidthPerDistance', 0.0012, 600), '')], 400)
    library.connect_material_property(offset, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    library.recompile_material(material)
    assets.save_asset(OUTLINE)


# The water of the moat before the wall of a castle, as the developer asked on 2026-11-13, part of the ground of the biome: dark and
# see-through, clearer at its shore (MOAT_SHORE_CODE, as the ground draws its bed), under the wall to its face, with foam in a line along the
# shore that comes and goes, foam where it meets the wall and, on the band of the mesh up the face of the wall (its second UV 2 and more),
# water that washes up the wall and falls back; ripples bend its normal and it lifts a little (MoatWaveHeight); its values are MoatShallowColor,
# MoatDeepColor, MoatOpacity, MoatFoamColor, MoatFoamWidth, MoatWaveHeight, MoatRippleScale, MoatRippleSpeed, MoatRippleStrength and
# MoatRoughness of DA_HexBattle
WATER_FOAM_CODE = MOAT_SHORE_CODE + r"""
float t = Time * MoatRippleSpeed;
float broken = HexNoise.SampleLevel(HexNoiseSampler, WorldPos.xy * 0.011 + float2(t * 0.013, -t * 0.009), 0).r;
// how wide the foam is here, by a second noise that drifts the other way, as a sine along the wall drew a thick wave that ran along it (the
// developer saw it on 2026-11-13)
float reach = HexNoise.SampleLevel(HexNoiseSampler, WorldPos.xy * 0.023 + float2(-t * 0.021, t * 0.017), 0).r;
float foam = 0.0;
float band = UV.y >= 1.5 ? 1.0 : 0.0;
if (band > 0.5)
{
    // up the face of the wall, as high as the water washes there now
    float up = UV.y - 2.0;
    float surge = 0.15 + 0.5 * reach + 0.15 * broken;
    foam = (1.0 - smoothstep(surge - 0.3, surge, up)) * saturate(min(WorldPos.x - MoatWall.y, MoatWall.z - WorldPos.x) / moatR);
    foam *= 0.55 + 0.45 * broken;
}
else
{
    float shoreFoam = (1.0 - smoothstep(0.0, MoatFoamWidth * (0.35 + 0.9 * reach) + 1.0, moatInto)) * step(-1.0, moatInto);
    float wallFoam = 1.0 - smoothstep(0.0, MoatWallFoamWidth * (0.35 + 1.0 * reach) + 0.5, moatWallGap);
    foam = saturate(max(shoreFoam, wallFoam) * smoothstep(0.3, 0.75, broken + 0.2));
}
"""
# the water is unlit and lights itself by the sun (SunDir, towards it) and the sky, as a lit translucent surface took its light from the coarse
# volume of the translucency of Lumen, which drew round blots in a lattice on it (the circles the developer saw on 2026-11-13)
WATER_COLOR_CODE = WATER_FOAM_CODE + r"""
float depth = saturate(moatInto / (moatR * 0.6));
float3 water = lerp(MoatShallowColor.rgb, MoatDeepColor.rgb, smoothstep(0.0, 1.0, depth));
float2 rp = WorldPos.xy * MoatRippleScale * 0.05;
float re = 0.01;
float2 ra = rp + float2(t * 0.031, t * 0.017);
float2 rb = rp * 1.9 + float2(-t * 0.023, t * 0.029);
float r0 = HexNoise.SampleLevel(HexNoiseSampler, ra, 0).r + 0.5 * HexNoise.SampleLevel(HexNoiseSampler, rb, 0).r;
float rx = HexNoise.SampleLevel(HexNoiseSampler, ra + float2(re, 0.0), 0).r + 0.5 * HexNoise.SampleLevel(HexNoiseSampler, rb + float2(re * 1.9, 0.0), 0).r;
float ry = HexNoise.SampleLevel(HexNoiseSampler, ra + float2(0.0, re), 0).r + 0.5 * HexNoise.SampleLevel(HexNoiseSampler, rb + float2(0.0, re * 1.9), 0).r;
float3 n = band > 0.5 ? float3(0.0, -1.0, 0.0) : normalize(float3(-float2(rx - r0, ry - r0) / re * MoatRippleStrength * 0.04, 1.0));
float3 sun = normalize(SunDir.xyz);
float lit = 0.55 + 0.45 * saturate(dot(n, sun));
float3 view = normalize(CameraVector);
float3 halfway = normalize(view + sun);
float glint = pow(saturate(dot(n, halfway)), 90.0) * MoatGlint;
float sky = pow(1.0 - saturate(dot(n, view)), 4.0) * MoatSkyReflection;
float3 shaded = water * lit * MoatLight + MoatSkyColor.rgb * sky + glint;
return lerp(shaded, MoatFoamColor.rgb * MoatLight * lit, foam);
"""
WATER_OPACITY_CODE = WATER_FOAM_CODE + r"""
float depth = saturate(moatInto / (moatR * 0.6));
if (band > 0.5)
{
    return foam * 0.85;
}
float edge = smoothstep(-1.5, 4.0, moatInto);
return max(edge * lerp(0.45, MoatOpacity, depth), foam * edge * 0.95);
"""
WATER_OFFSET_CODE = MOAT_SHORE_CODE + r"""
float t = Time * MoatRippleSpeed;
if (UV.y >= 1.5)
{
    return float3(0.0, 0.0, 0.0);
}
float wave = sin(WorldPos.x * 0.083 + WorldPos.y * 0.031 + t * 1.9) * 0.5 + sin(WorldPos.x * -0.037 + WorldPos.y * 0.071 + t * 1.3) * 0.5;
return float3(0.0, 0.0, MoatWaveHeight * wave * saturate(moatInto / 10.0));
"""
def build_water(defaults):
    material = open_material(WATER)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('two_sided', True)
    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 300)
    time = library.create_material_expression(material, unreal.MaterialExpressionTime, -1000, 350)
    uv = library.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1000, 380)
    noise = library.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -700, 900)
    noise.set_editor_property('parameter_name', 'HexNoise')
    noise.set_editor_property('texture', unreal.load_asset(NOISE))
    noise.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    shore = [('WorldPos', position, ''), ('UV', uv, '')] + moat_connections(material, defaults)
    foam = shore + [('Time', time, ''), ('HexNoise', noise, ''),
        ('MoatRippleSpeed', scalar(material, 'MoatRippleSpeed', defaults.get_editor_property('moat_ripple_speed'), 850), ''),
        ('MoatFoamWidth', scalar(material, 'MoatFoamWidth', defaults.get_editor_property('moat_foam_width'), 860), ''),
        ('MoatWallFoamWidth', scalar(material, 'MoatWallFoamWidth', defaults.get_editor_property('moat_wall_foam_width'), 870), '')]
    color = custom(material, WATER_COLOR_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, foam + [
        ('MoatShallowColor', vector(material, 'MoatShallowColor', defaults.get_editor_property('moat_shallow_color'), 0), ''),
        ('MoatDeepColor', vector(material, 'MoatDeepColor', defaults.get_editor_property('moat_deep_color'), 50), ''),
        ('MoatFoamColor', vector(material, 'MoatFoamColor', defaults.get_editor_property('moat_foam_color'), 100), ''),
        ('MoatSkyColor', vector(material, 'MoatSkyColor', defaults.get_editor_property('moat_sky_color'), 120), ''),
        ('SunDir', vector(material, 'SunDir', unreal.LinearColor(0.4, 0.2, 0.9, 0.0), 140), ''),
        ('CameraVector', library.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, -1000, 420), ''),
        ('MoatLight', scalar(material, 'MoatLight', defaults.get_editor_property('moat_light'), 160), ''),
        ('MoatGlint', scalar(material, 'MoatGlint', defaults.get_editor_property('moat_glint'), 170), ''),
        ('MoatSkyReflection', scalar(material, 'MoatSkyReflection', defaults.get_editor_property('moat_sky_reflection'), 180), ''),
        ('MoatRippleScale', scalar(material, 'MoatRippleScale', defaults.get_editor_property('moat_ripple_scale'), 190), ''),
        ('MoatRippleStrength', scalar(material, 'MoatRippleStrength', defaults.get_editor_property('moat_ripple_strength'), 195), '')], 0)
    library.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = custom(material, WATER_OPACITY_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT1, foam + [
        ('MoatOpacity', scalar(material, 'MoatOpacity', defaults.get_editor_property('moat_opacity'), 750), '')], 150)
    library.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    offset = custom(material, WATER_OFFSET_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, shore + [('Time', time, ''),
        ('MoatWaveHeight', scalar(material, 'MoatWaveHeight', defaults.get_editor_property('moat_wave_height'), 800), ''),
        ('MoatRippleSpeed', scalar(material, 'MoatRippleSpeed', defaults.get_editor_property('moat_ripple_speed'), 850), '')], 450)
    library.connect_material_property(offset, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    library.recompile_material(material)
    assets.save_asset(WATER)

# The timber of the gate and the bridge of a siege, downloaded by fetch_battle_textures.ps1, a color and a normal each, which repeat
TEXTURE_FOLDER = FOLDER + '/Textures'
TEXTURE_SOURCES = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'battle_texture_sources.json')
TEXTURE_DOWNLOADS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'TextureSources', 'battle')


def import_textures():
    with open(TEXTURE_SOURCES, encoding='utf-8') as file:
        sets = json.load(file)['sets']
    tasks = []
    names = []
    for entry in sets:
        for suffix, kind in (('', 'color'), ('_N', 'normal')):
            source = os.path.abspath(os.path.join(TEXTURE_DOWNLOADS, entry['id'], '%s_%s.jpg' % (entry['id'], kind)))
            if not os.path.exists(source):
                raise RuntimeError('%s is missing: run fetch_battle_textures.ps1 first' % source)
            task = unreal.AssetImportTask()
            task.set_editor_property('filename', source)
            task.set_editor_property('destination_path', TEXTURE_FOLDER)
            task.set_editor_property('destination_name', 'T_Battle%s%s' % (entry['name'], suffix))
            task.set_editor_property('automated', True)
            task.set_editor_property('replace_existing', True)
            task.set_editor_property('save', True)
            tasks.append(task)
            names.append(('%s/T_Battle%s%s' % (TEXTURE_FOLDER, entry['name'], suffix), bool(suffix)))
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for path, normal in names:
        texture = unreal.load_asset(path)
        if not texture:
            raise RuntimeError('could not import %s' % path)
        if normal:
            texture.set_editor_property('srgb', False)
            texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property('address_x', unreal.TextureAddress.TA_WRAP)
        texture.set_editor_property('address_y', unreal.TextureAddress.TA_WRAP)
        assets.save_loaded_asset(texture, only_if_is_dirty=False)
        unreal.log('BATTLE %s imported' % path)


# The props of a siege that are no meshes of the castle, as the developer asked on 2026-11-13 for a gate and a bridge more real and steps with a
# texture: a color and a normal texture (Color, Normal) with Tint, by the first UV of the mesh in units of TileSize when UseUV is 1 (the meshes
# the field builds, which lay their UV in world units), else projected from the three planes of the world (cubes), the planks turned by Turn;
# the normal goes out in world space
PROP_CODE = r"""
float3 n = normalize(Normal);
float3 color;
float3 worldN;
if (UseUV > 0.5)
{
    float2 uv = UV / TileSize;
    uv = Turn > 0.5 ? uv.yx : uv;
    float2 dx = UVDdx / TileSize;
    float2 dy = UVDdy / TileSize;
    dx = Turn > 0.5 ? dx.yx : dx;
    dy = Turn > 0.5 ? dy.yx : dy;
    color = ColorTex.SampleGrad(ColorTexSampler, uv, dx, dy).rgb;
    float2 tn = NormalTex.SampleGrad(NormalTexSampler, uv, dx, dy).xy * 2.0 - 1.0;
    tn = Turn > 0.5 ? tn.yx : tn;
    float3 t = normalize(Tangent - n * dot(Tangent, n));
    float3 b = cross(n, t);
    worldN = normalize(t * tn.x * NormalStrength - b * tn.y * NormalStrength + n);
}
else
{
    float3 w = pow(abs(n), 4.0);
    w /= max(w.x + w.y + w.z, 0.0001);
    float2 uvs[3] = { WorldPos.yz / TileSize, WorldPos.xz / TileSize, WorldPos.xy / TileSize };
    float2 ddxs[3] = { PosDdx.yz / TileSize, PosDdx.xz / TileSize, PosDdx.xy / TileSize };
    float2 ddys[3] = { PosDdy.yz / TileSize, PosDdy.xz / TileSize, PosDdy.xy / TileSize };
    color = 0.0;
    float3 tns[3];
    for (int i = 0; i < 3; i++)
    {
        float2 uv = Turn > 0.5 ? uvs[i].yx : uvs[i];
        float2 dx = Turn > 0.5 ? ddxs[i].yx : ddxs[i];
        float2 dy = Turn > 0.5 ? ddys[i].yx : ddys[i];
        color += ColorTex.SampleGrad(ColorTexSampler, uv, dx, dy).rgb * w[i];
        float2 tn = NormalTex.SampleGrad(NormalTexSampler, uv, dx, dy).xy * 2.0 - 1.0;
        tns[i] = float3((Turn > 0.5 ? tn.yx : tn) * NormalStrength, 1.0);
    }
    // the normals of the three planes, blended as whiteout
    float3 nx = float3(tns[0].xy + n.zy, abs(tns[0].z) * n.x);
    float3 ny = float3(tns[1].xy + n.xz, abs(tns[1].z) * n.y);
    float3 nz = float3(tns[2].xy + n.xy, abs(tns[2].z) * n.z);
    worldN = normalize(nx.zyx * w.x + ny.xzy * w.y + nz.xyz * w.z);
}
return float4(color * Tint.rgb, 1.0);
"""
PROP_NORMAL_CODE = PROP_CODE.replace('return float4(color * Tint.rgb, 1.0);', 'return worldN;')


# the props dissolve in front of the hex under the cursor as the wall of the castle does (CASTLE_FADE_CODE of create_planet_assets.py), as the
# developer found the leaves of the gate did not on 2026-11-13: SiegeFade the point and its w 1 while it is on, SiegeFadeRadius, SiegeFadeKeep;
# the shadow keeps them whole
PROP_FADE_CODE = r"""
float3 siegeAxis = SiegeFade.xyz - CameraPos;
float siegeLength = length(siegeAxis);
siegeAxis /= max(siegeLength, 1.0);
float3 siegeToPixel = WorldPos - CameraPos;
float siegeAlong = dot(siegeToPixel, siegeAxis);
float siegeOff = length(siegeToPixel - siegeAxis * siegeAlong);
float siegeRadius = max(SiegeFadeRadius, 1.0);
float siegeFade = SiegeFade.w * (1.0 - smoothstep(siegeRadius * 0.6, siegeRadius, siegeOff)) * saturate((siegeLength - siegeAlong) / 20.0);
float2 pixel = Parameters.SvPosition.xy + 5.588238 * (float)View.StateFrameIndexMod8;
float noise = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
float keep = noise < 1.0 - siegeFade * (1.0 - SiegeFadeKeep) ? 1.0 : 0.0;
#if SHADOW_DEPTH_SHADER
keep = 1.0;
#endif
return keep;
"""


def build_prop():
    material = open_material(PROP)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property('tangent_space_normal', False)
    material.set_editor_property('two_sided', True)
    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 0)
    ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -850, 50)
    ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -850, 100)
    library.connect_material_expressions(position, '', ddx, '')
    library.connect_material_expressions(position, '', ddy, '')
    uv = library.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1000, 150)
    uv_ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -850, 160)
    uv_ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -850, 170)
    library.connect_material_expressions(uv, '', uv_ddx, '')
    library.connect_material_expressions(uv, '', uv_ddy, '')
    normal = library.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -1000, 200)
    tangent = library.create_material_expression(material, unreal.MaterialExpressionVertexTangentWS, -1000, 220)
    color_tex = library.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -700, 250)
    color_tex.set_editor_property('parameter_name', 'ColorTex')
    color_tex.set_editor_property('texture', unreal.load_asset(TEXTURE_FOLDER + '/T_BattleGateWood'))
    color_tex.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    normal_tex = library.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -700, 300)
    normal_tex.set_editor_property('parameter_name', 'NormalTex')
    normal_tex.set_editor_property('texture', unreal.load_asset(TEXTURE_FOLDER + '/T_BattleGateWood_N'))
    normal_tex.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    inputs = [('WorldPos', position, ''), ('PosDdx', ddx, ''), ('PosDdy', ddy, ''), ('UV', uv, ''), ('UVDdx', uv_ddx, ''), ('UVDdy', uv_ddy, ''),
        ('Normal', normal, ''), ('Tangent', tangent, ''), ('ColorTex', color_tex, ''), ('NormalTex', normal_tex, ''),
        ('Tint', vector(material, 'Tint', unreal.LinearColor(1.0, 1.0, 1.0, 1.0), 350), ''),
        ('TileSize', scalar(material, 'TileSize', 80.0, 400), ''), ('UseUV', scalar(material, 'UseUV', 0.0, 450), ''),
        ('Turn', scalar(material, 'Turn', 0.0, 500), ''), ('NormalStrength', scalar(material, 'NormalStrength', 1.0, 550), '')]
    color = custom(material, PROP_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT4, inputs, 0)
    library.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    world_normal = custom(material, PROP_NORMAL_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs, 300)
    library.connect_material_property(world_normal, '', unreal.MaterialProperty.MP_NORMAL)
    library.connect_material_property(scalar(material, 'Roughness', 0.85, 600), '', unreal.MaterialProperty.MP_ROUGHNESS)
    fade = custom(material, PROP_FADE_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT1, [('WorldPos', position, ''),
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1000, 700), ''),
        ('SiegeFade', vector4(material, 'SiegeFade', unreal.LinearColor(0.0, 0.0, 0.0, 0.0), 750), ''),
        ('SiegeFadeRadius', scalar(material, 'SiegeFadeRadius', 75.0, 800), ''),
        ('SiegeFadeKeep', scalar(material, 'SiegeFadeKeep', 0.25, 850), '')], 700)
    library.connect_material_property(fade, '', unreal.MaterialProperty.MP_OPACITY_MASK)
    library.recompile_material(material)
    assets.save_asset(PROP)


# The fog of war around the field, as the developer asked on 2026-11-13: a bank of cloud over the ground beyond the forest and the rocks that
# close the field, from FogStart out to FogEnd of the way from the box of the field and the castle (FogBox: its least and most X and Y), in
# FogColor with lighter wisps that wander (FogWispColor), unlit and see-through, so it runs on into the sky of the same color
# since 2026-11-13, as the developer asked for it alive as the fog of war of the map: banks one over another (FogLayer, FogLayerOpacity), each
# churning in three octaves of T_HexNoise that drift their own ways (FogScale), wisps along the ridges of a noise another bends (FogWisp) and
# glows that swell and fade, one to a place, each at its own time (FogPulse, FogPulseRate, FogPulseColor)
FOG_CODE = r"""
float2 nearest = clamp(WorldPos.xy, FogBox.xy, FogBox.zw);
float away = distance(WorldPos.xy, nearest);
float t = Time * FogSpeed;
float2 p = WorldPos.xy * FogScale + FogLayer * 3.7;
float n1 = HexNoise.SampleLevel(HexNoiseSampler, p + float2(t * 0.05, t * 0.03), 0).r;
float n2 = HexNoise.SampleLevel(HexNoiseSampler, p * 2.3 + float2(-t * 0.08, t * 0.05), 0).r;
float n3 = HexNoise.SampleLevel(HexNoiseSampler, p * 5.1 + float2(t * 0.11, -t * 0.07), 0).r;
float churn = n1 * 0.55 + n2 * 0.3 + n3 * 0.15;
float edge = smoothstep(FogStart, FogEnd, away + (churn - 0.5) * (FogEnd - FogStart) * 1.2);
float bend = HexNoise.SampleLevel(HexNoiseSampler, p * 0.7 + float2(t * 0.02, -t * 0.04), 0).r;
float ridge = 1.0 - abs(HexNoise.SampleLevel(HexNoiseSampler, p * 3.0 + bend * 0.8 + float2(t * 0.06, t * 0.02), 0).r * 2.0 - 1.0);
float wisp = pow(saturate(ridge), 6.0) * FogWisp;
float place = HexNoise.SampleLevel(HexNoiseSampler, p * 0.9 + 11.3, 0).r;
float pulse = pow(saturate(sin(Time * FogPulseRate * 6.2831853 + place * 40.0)), 8.0) * smoothstep(0.55, 0.75, place) * FogPulse;
float3 color = lerp(FogColor.rgb, FogWispColor.rgb, saturate(churn * 0.6 + wisp)) + FogPulseColor.rgb * pulse;
float alpha = edge * saturate(0.75 + (churn - 0.5) + wisp * 0.3) * FogLayerOpacity;
// clear next to the camera, so a bank or the skirt it looks through never hides the field
alpha *= saturate((distance(WorldPos, CameraPos) - FogCameraClear) / max(FogCameraFade, 1.0));
return float4(color, alpha);
"""


# The fog of war around the field as the fog of war of the planet is (SHROUD_CODE of create_planet_assets.py), as the developer asked on
# 2026-11-13 when the banks and the skirt of before still looked poor: a post process over the view of the field (M_HexBattleShroud, on the
# camera of the field), where a bank of cloud ShroudTop high over the ground (GroundZ) lies from FogStart past the box of the field and the
# castle (FogBox) and is whole from FogEnd on, marched in SHROUD_STEPS looks through the view, ShroudDensity thick, thinner at its top
# (ShroudTaper), darker at its foot (ShroudBaseShade), lighter and darker in patches (ShroudTone), its edge moved by a noise that drifts and
# churns (ShroudSoftness, ShroudEdgeNoise, ShroudNoiseSize, ShroudDrift, ShroudChurn), with wisps (ShroudWisp*) and glows that swell and fade
# (ShroudPulse*); what stands on the ground it covers whole is hidden, and the sky past it is the bank
BATTLE_SHROUD_STEPS = 8
BATTLE_SHROUD_CODE = r"""
struct FShroud
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
    float Known(float3 at, float4 box, float start, float finish)
    {
        float2 nearest = clamp(at.xy, box.xy, box.zw);
        return 1.0 - smoothstep(start, finish, distance(at.xy, nearest));
    }
    float Cover(float known, float3 at, float softness, float edgeNoise, float lift, out float noise)
    {
        noise = Value(at) * 0.65 + Value(at * 2.7 + 5.3) * 0.35;
        if (known > 0.995)
        {
            return 0.0;
        }
        return smoothstep(0.5 - softness, 0.5 + softness, 1.0 - known + (noise - 0.5) * min(edgeNoise, 0.7) - lift);
    }
};
FShroud shroud;
float3 scene = SceneColor.rgb;
float3 cam = CameraPos;
float3 toPixel = WorldPos - cam;
float sceneDistance = length(toPixel);
float3 dir = toPixel / max(sceneDistance, 1.0);
// the sky, far away: the bank goes on to the horizon
bool sky = sceneDistance > 60000.0;
float viewEnd = sky ? 60000.0 : sceneDistance;
float3 flow = float3(0.3, -0.5, 0.0) * Time * ShroudDrift;
float churn = Time * ShroudChurn;
float size = max(ShroudNoiseSize, 1.0);
float noise = 0.5;
float ground = 0.0;
float groundShade = ShroudBaseShade;
float3 face = cam + dir * viewEnd;
if (!sky)
{
    float known = shroud.Known(WorldPos, FogBox, FogStart, FogEnd);
    ground = shroud.Cover(known, WorldPos / size + flow + float3(0.0, 0.0, churn), ShroudSoftness, ShroudEdgeNoise, 0.0, noise);
    groundShade = lerp(ShroudBaseShade, 1.0, saturate((WorldPos.z - GroundZ) / max(ShroudTop, 1.0))) * (1.0 + (noise - 0.5) * ShroudTone);
}
// the slab of the bank, from a little under the ground to ShroudTop over it
float bottom = GroundZ - 40.0;
float top = GroundZ + ShroudTop;
float pass = 1.0;
float light = 0.0;
float enterT = 0.0;
float exitT = viewEnd;
if (abs(dir.z) > 1.0e-5)
{
    float t0 = (bottom - cam.z) / dir.z;
    float t1 = (top - cam.z) / dir.z;
    enterT = max(min(t0, t1), 0.0);
    exitT = min(max(t0, t1), viewEnd);
}
else if (cam.z > top || cam.z < bottom)
{
    exitT = -1.0;
}
if (exitT > enterT)
{
    float step = (exitT - enterT) / STEPS.0;
    face = cam + dir * (enterT + min(exitT - enterT, ShroudTop * 2.0) * 0.5);
    float jitter = frac(shroud.Hash(dir * 4096.0) + View.StateFrameIndexMod8 * 0.618);
    for (int index = 0; index < STEPS; index++)
    {
        float3 at = cam + dir * (enterT + (index + jitter) * step);
        float up = saturate((at.z - GroundZ) / max(ShroudTop, 1.0));
        float known = shroud.Known(at, FogBox, FogStart, FogEnd);
        float cover = shroud.Cover(known, at / size + flow + float3(0.0, 0.0, churn), ShroudSoftness, ShroudEdgeNoise, up * ShroudTaper, noise);
        float taken = 1.0 - exp(-cover * ShroudDensity * step);
        light += pass * taken * lerp(ShroudBaseShade, 1.0, up) * (1.0 + (noise - 0.5) * ShroudTone);
        pass *= 1.0 - taken;
    }
}
// past the bank the sky is the bank seen from above
if (sky && dir.z < 0.2)
{
    float far = shroud.Known(cam + dir * 20000.0, FogBox, FogStart, FogEnd) < 0.5 ? 1.0 : 0.0;
    ground = max(ground, far);
}
light += pass * ground * groundShade;
float total = 1.0 - pass * (1.0 - ground);
float3 look = ShroudColor.rgb * (light / max(total, 1.0e-4));
if (total > 0.01)
{
    float wispTime = Time * ShroudWispSpeed;
    float3 wispAt = face / max(ShroudWispSize, 1.0);
    float3 bend = float3(shroud.Value(wispAt * 0.6 + float3(wispTime, 0.0, 3.1)), shroud.Value(wispAt * 0.6 + float3(7.7, -wispTime, 0.0)),
        shroud.Value(wispAt * 0.6 + float3(0.0, 13.3, wispTime))) - 0.5;
    float ridge = 1.0 - abs(shroud.Value(wispAt + bend * 1.8 + float3(0.4, 0.2, -0.3) * wispTime) * 2.0 - 1.0);
    float stir = smoothstep(0.4, 0.75, shroud.Value(wispAt * 0.3 - float3(0.2, 0.5, 0.1) * wispTime + 41.0));
    ridge *= ridge * stir;
    look *= 1.0 + ridge * ShroudWisp;
    float3 place = face / max(ShroudPulseSize, 1.0);
    float3 cell = floor(place);
    float seed = shroud.Hash(cell + 0.5);
    float3 heart = float3(shroud.Hash(cell + 11.5), shroud.Hash(cell + 23.5), shroud.Hash(cell + 37.5)) * 0.4 + 0.3;
    float reach = saturate(1.0 - length(frac(place) - heart) * 3.2);
    float beat = frac(Time * ShroudPulseRate * (0.6 + 0.8 * seed) + seed * 7.3);
    float swell = smoothstep(0.0, 0.2, beat) * (1.0 - smoothstep(0.25, 0.75, beat));
    look += ShroudPulseColor.rgb * (reach * reach * swell * ShroudPulse * (0.6 + ridge));
}
return lerp(scene, look * Shade, total);
""".replace('STEPS', str(BATTLE_SHROUD_STEPS))
BATTLE_SHROUD_PARAMETERS = ['ShroudColor', 'ShroudTop', 'ShroudDensity', 'ShroudTaper', 'ShroudBaseShade', 'ShroudTone', 'ShroudSoftness', 'ShroudEdgeNoise',
    'ShroudNoiseSize', 'ShroudDrift', 'ShroudChurn', 'ShroudWisp', 'ShroudWispSize', 'ShroudWispSpeed', 'ShroudPulse', 'ShroudPulseSize', 'ShroudPulseRate',
    'ShroudPulseColor']
SHROUD = FOLDER + '/M_HexBattleShroud'


def build_shroud():
    material = open_material(SHROUD)
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF)
    defaults = unreal.get_default_object(unreal.HexBattleConfig)
    scene = library.create_material_expression(material, unreal.MaterialExpressionSceneTexture, -1000, 0)
    scene.set_editor_property('scene_texture_id', unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    inputs = [('SceneColor', scene, 'Color'), ('WorldPos', library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 100), ''),
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1000, 150), ''),
        ('Time', library.create_material_expression(material, unreal.MaterialExpressionTime, -1000, 200), ''),
        ('FogBox', vector4(material, 'FogBox', unreal.LinearColor(-1000.0, -1000.0, 1000.0, 1000.0), 250), ''),
        ('GroundZ', scalar(material, 'GroundZ', 0.0, 300), ''),
        ('Shade', scalar(material, 'Shade', 1.0, 320), ''),
        ('FogStart', scalar(material, 'FogStart', defaults.get_editor_property('fog_start'), 350), ''),
        ('FogEnd', scalar(material, 'FogEnd', defaults.get_editor_property('fog_end'), 400), '')]
    y = 450
    for name in BATTLE_SHROUD_PARAMETERS:
        assert name in BATTLE_SHROUD_CODE, name
        value = defaults.get_editor_property(''.join('_' + c.lower() if c.isupper() else c for c in name).lstrip('_'))
        inputs.append((name, vector(material, name, value, y) if isinstance(value, unreal.LinearColor) else scalar(material, name, value, y), ''))
        y += 50
    node = custom(material, BATTLE_SHROUD_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs, 0)
    library.connect_material_property(node, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(material)
    assets.save_asset(SHROUD)


def build_fog():
    material = open_material(FOG)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('two_sided', True)
    defaults = unreal.get_default_object(unreal.HexBattleConfig)
    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 0)
    time = library.create_material_expression(material, unreal.MaterialExpressionTime, -1000, 50)
    inputs = [('WorldPos', position, ''), ('Time', time, ''), ('FogBox', vector4(material, 'FogBox', unreal.LinearColor(-1000.0, -1000.0, 1000.0, 1000.0), 100), ''),
        ('FogStart', scalar(material, 'FogStart', defaults.get_editor_property('fog_start'), 150), ''),
        ('FogEnd', scalar(material, 'FogEnd', defaults.get_editor_property('fog_end'), 200), ''),
        ('FogSpeed', scalar(material, 'FogSpeed', defaults.get_editor_property('fog_speed'), 250), ''),
        ('FogColor', vector(material, 'FogColor', defaults.get_editor_property('fog_color'), 300), ''),
        ('FogWispColor', vector(material, 'FogWispColor', defaults.get_editor_property('fog_wisp_color'), 350), ''),
        ('HexNoise', noise_texture(material, 400), ''),
        ('FogScale', scalar(material, 'FogScale', defaults.get_editor_property('fog_scale'), 450), ''),
        ('FogWisp', scalar(material, 'FogWisp', defaults.get_editor_property('fog_wisp'), 500), ''),
        ('FogPulse', scalar(material, 'FogPulse', defaults.get_editor_property('fog_pulse'), 550), ''),
        ('FogPulseRate', scalar(material, 'FogPulseRate', defaults.get_editor_property('fog_pulse_rate'), 600), ''),
        ('FogPulseColor', vector(material, 'FogPulseColor', defaults.get_editor_property('fog_pulse_color'), 650), ''),
        ('FogLayer', scalar(material, 'FogLayer', 0.0, 700), ''),
        ('FogLayerOpacity', scalar(material, 'FogLayerOpacity', 1.0, 750), ''),
        ('CameraPos', library.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1000, 850), ''),
        ('FogCameraClear', scalar(material, 'FogCameraClear', defaults.get_editor_property('fog_camera_clear'), 900), ''),
        ('FogCameraFade', scalar(material, 'FogCameraFade', defaults.get_editor_property('fog_camera_fade'), 950), '')]
    node = custom(material, FOG_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT4, inputs, 0)
    library.connect_material_property(node, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    alpha = library.create_material_expression(material, unreal.MaterialExpressionComponentMask, -400, 100)
    for channel, on in (('r', False), ('g', False), ('b', False), ('a', True)):
        alpha.set_editor_property(channel, on)
    library.connect_material_expressions(node, '', alpha, '')
    # it thins out where it nears what it lies on, the hills and the trees, as its plane cut them along a hard line, which the developer
    # found ended the field abruptly on 2026-11-13 (FogSoftness)
    soft = library.create_material_expression(material, unreal.MaterialExpressionDepthFade, -250, 150)
    library.connect_material_expressions(scalar(material, 'FogSoftness', defaults.get_editor_property('fog_softness'), 800), '', soft, 'FadeDistance')
    library.connect_material_expressions(alpha, '', soft, 'Opacity')
    library.connect_material_property(soft, '', unreal.MaterialProperty.MP_OPACITY)
    library.recompile_material(material)
    assets.save_asset(FOG)


# The sky around the field, seen from inside a sphere, and a shot: unlit, its color towards the horizon and TopColor above
SKY_CODE = r"""
float up = saturate(-CameraVector.z);
return lerp(Color.rgb, TopColor.rgb, up);
"""


def build_sky():
    material = open_material(SKY)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', True)
    node = custom(material, SKY_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, [
        ('CameraVector', library.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, -1000, 0), ''),
        ('Color', vector(material, 'Color', unreal.LinearColor(0.32, 0.45, 0.65, 1.0), 100), ''),
        ('TopColor', vector(material, 'TopColor', unreal.LinearColor(0.5, 0.7, 1.0, 1.0), 150), '')], 0)
    library.connect_material_property(node, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(material)
    assets.save_asset(SKY)


def build_config():
    if assets.does_asset_exist(CONFIG):
        return
    unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_HexBattle', FOLDER, unreal.HexBattleConfig, unreal.DataAssetFactory())
    assets.save_asset(CONFIG)
    unreal.log('BATTLE created %s' % CONFIG)


if not assets.does_directory_exist(FOLDER):
    assets.make_directory(FOLDER)
# with the argument water only the water of the moat is built, so the other materials are not saved again with the same graphs
ONLY = [Each for Each in sys.argv[1:] if Each in ('water', 'ground', 'textures', 'prop', 'fog', 'unit', 'shroud')]
if ONLY:
    if 'textures' in ONLY:
        import_textures()
    if 'ground' in ONLY:
        build_ground()
    if 'water' in ONLY:
        build_water(unreal.get_default_object(unreal.HexBattleConfig))
    if 'prop' in ONLY:
        build_prop()
    if 'fog' in ONLY:
        build_fog()
    if 'unit' in ONLY:
        build_unit()
    if 'shroud' in ONLY:
        build_shroud()
    unreal.log('BATTLE %s done' % ' '.join(ONLY))
else:
    build_ground()
    build_mark()
    build_unit()
    build_sky()
    build_outline()
    build_water(unreal.get_default_object(unreal.HexBattleConfig))
    import_textures()
    build_prop()
    build_fog()
    build_shroud()
    build_config()
    unreal.log('BATTLE assets done')
