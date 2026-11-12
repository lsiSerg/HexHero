"""Builds the materials of the battles (M_HexBattleGround, M_HexBattleMark, M_HexBattleUnit, M_HexBattleSky) in /Game/HexPlanet/Battle and
creates DA_HexBattle when it is missing, run with the commandlet:

    UnrealEditor-Cmd.exe HexHero.uproject -run=pythonscript -script="Tools/HexBattle/create_battle_assets.py" -unattended -nullrhi

Edit this script, not the materials. DA_HexBattle belongs to whoever tunes it once it is there; import_battle_units.py gives it the looks of the
units and the grounds of the field while it has none."""

import unreal

FOLDER = '/Game/HexPlanet/Battle'
COLORS = '/Game/HexMap/Textures/Biomes/TA_Biome_Color'
GROUND = FOLDER + '/M_HexBattleGround'
MARK = FOLDER + '/M_HexBattleMark'
UNIT = FOLDER + '/M_HexBattleUnit'
SKY = FOLDER + '/M_HexBattleSky'
OUTLINE = FOLDER + '/M_HexBattleOutline'
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


# The ground of the field: a layer of the terrain (TerrainLayer of the biome fought on) from above at two sizes, broken in patches, its
# hollows darker by the height in its alpha, darker away from the field so the field stands out, and darker still underground
GROUND_CODE = r"""
float2 uv = WorldPos.xy / TileSize;
float2 dx = PosDdx.xy / TileSize;
float2 dy = PosDdy.xy / TileSize;
float4 near = Colors.SampleGrad(ColorsSampler, float3(uv, TerrainLayer), dx, dy);
float4 far = Colors.SampleGrad(ColorsSampler, float3(uv * 0.27 + 0.31, TerrainLayer), dx * 0.27, dy * 0.27);
float3 color = lerp(near.rgb, far.rgb, 0.35) * lerp(0.78, 1.08, near.a);
// patches of tone
float patch = sin(WorldPos.x * 0.0047 + sin(WorldPos.y * 0.0031) * 2.0) * sin(WorldPos.y * 0.0053 + 1.7);
color *= 1.0 + 0.08 * patch;
// the field and the ground around it
float2 away = abs(WorldPos.xy - FieldCenter.xy) / max(FieldHalf.xy, 1.0);
float outside = saturate(max(away.x, away.y) - 1.0);
color *= lerp(1.0, 0.62, smoothstep(0.0, 0.6, outside));
color *= lerp(1.0, 0.55, Underground);
return color;
"""


def build_ground():
    material = open_material(GROUND)
    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1000, 0)
    ddx = library.create_material_expression(material, unreal.MaterialExpressionDDX, -850, 50)
    ddy = library.create_material_expression(material, unreal.MaterialExpressionDDY, -850, 100)
    library.connect_material_expressions(position, '', ddx, '')
    library.connect_material_expressions(position, '', ddy, '')
    colors = library.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -700, 150)
    colors.set_editor_property('parameter_name', 'Colors')
    colors.set_editor_property('texture', unreal.load_asset(COLORS))
    colors.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    connections = [('WorldPos', position, ''), ('PosDdx', ddx, ''), ('PosDdy', ddy, ''), ('Colors', colors, ''),
        ('TerrainLayer', scalar(material, 'TerrainLayer', 0.0, 200), ''), ('TileSize', scalar(material, 'TileSize', 260.0, 250), ''),
        ('Underground', scalar(material, 'Underground', 0.0, 300), ''),
        ('FieldCenter', vector(material, 'FieldCenter', unreal.LinearColor(0.0, 0.0, 0.0, 0.0), 350), ''),
        ('FieldHalf', vector(material, 'FieldHalf', unreal.LinearColor(500.0, 700.0, 0.0, 0.0), 400), '')]
    node = custom(material, GROUND_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, connections, 0)
    library.connect_material_property(node, '', unreal.MaterialProperty.MP_BASE_COLOR)
    library.connect_material_property(scalar(material, 'Roughness', 0.92, 500), '', unreal.MaterialProperty.MP_ROUGHNESS)
    library.recompile_material(material)
    assets.save_asset(GROUND)


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
UNIT_CODE = r"""
float3 own = VertexRGB * Tint.rgb;
return lerp(own, SideColor.rgb * max(max(VertexRGB.r, VertexRGB.g), max(VertexRGB.b, 0.35)), saturate(VertexA * SideColorShare));
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
    base = custom(material, UNIT_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, [('VertexRGB', color, ''), ('VertexA', color, 'A'), ('Tint', tint, ''),
        ('SideColor', side, ''), ('SideColorShare', share, '')], 0)
    library.connect_material_property(base, '', unreal.MaterialProperty.MP_BASE_COLOR)
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
build_ground()
build_mark()
build_unit()
build_sky()
build_outline()
build_config()
unreal.log('BATTLE assets done')
