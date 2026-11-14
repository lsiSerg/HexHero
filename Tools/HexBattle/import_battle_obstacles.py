"""Imports the photo-scanned obstacles that blender_build_obstacles.py wrote (Saved/BattleObstacles/obstacles.json) to
/Game/HexPlanet/Battle/Obstacles, with their textures (color, normal in the DirectX convention, and AO, roughness and metal in one, ARM), builds
M_HexBattleScan, which draws them, and a material instance of it for each, and gives DA_HexBattle the obstacles of each ground by the grounds of
battle_model_sources.json (the open grounds, RockyObstacles, the underground), the tint of the underground, the border rocks and the rocks
that stand for the wood of a desert (RockyDecor). As the
developer asked on 2026-11-13 for obstacles real, textured and light, in place of the meshes of the planet. Run with the commandlet after the
module is built:

  UnrealEditor-Cmd.exe HexHero.uproject -run=pythonscript -script="Tools/HexBattle/import_battle_obstacles.py" -unattended -nullrhi
"""
import json
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.abspath(os.path.join(HERE, '..', '..', 'Saved', 'BattleObstacles'))
FOLDER = '/Game/HexPlanet/Battle/Obstacles'
TEXTURES = FOLDER + '/Textures'
MATERIAL = '/Game/HexPlanet/Battle/M_HexBattleScan'
CONFIG = '/Game/HexPlanet/Battle/DA_HexBattle'
# the LODs blender_build_obstacles.py reduced, imported here and copied into each mesh
LOD_FOLDER = FOLDER + '/Lods'
# the rocks of the underground darker and cooler than in the sun
CAVE_TINT = unreal.LinearColor(0.55, 0.55, 0.6, 1.0)
# the border rocks around the field, large and sunk into the ground
BORDER = ['Boulder', 'GraniteBoulder', 'GraniteMound', 'DesertBoulder']

library = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary


def build_material(first):
    """Lit and opaque: the color of the scan times the tint of the instance (its custom values 2 to 4, white by default) and Tint, its normal,
    and of ARM the AO (red) and the roughness (green) times RoughnessScale, never under MinRoughness"""
    if assets.does_asset_exist(MATERIAL):
        material = unreal.load_asset(MATERIAL)
        for _ in range(64):
            if library.get_num_material_expressions(material) == 0:
                break
            library.delete_all_material_expressions(material)
    else:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_HexBattleScan', '/Game/HexPlanet/Battle', unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('used_with_instanced_static_meshes', True)

    # a sample of a parameter needs a texture to compile, those of the first obstacle
    def sample(name, kind, suffix, y):
        node = library.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -700, y)
        node.set_editor_property('parameter_name', name)
        node.set_editor_property('texture', unreal.load_asset('%s/T_BattleObstacle_%s%s' % (TEXTURES, first, suffix)))
        node.set_editor_property('sampler_type', kind)
        return node

    color = sample('Color', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, '_C', 0)
    normal = sample('Normal', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, '_N', 250)
    arm = sample('Arm', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, '_ARM', 500)
    tint = library.create_material_expression(material, unreal.MaterialExpressionPerInstanceCustomData3Vector, -700, -200)
    tint.set_editor_property('data_index', 2)
    tint.set_editor_property('const_default_value', unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    param = library.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, -350)
    param.set_editor_property('parameter_name', 'Tint')
    param.set_editor_property('default_value', unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    # weathered and dry, as the developer found the rocks wet on 2026-11-13: the crevices of its AO darker in the color itself (CavityShade),
    # dust of DustColor on what faces up (Dust), the relief of its normal NormalStrength times as deep
    def param_scalar(name, value, y):
        node = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, y)
        node.set_editor_property('parameter_name', name)
        node.set_editor_property('default_value', value)
        return node
    dust_color = library.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, -450)
    dust_color.set_editor_property('parameter_name', 'DustColor')
    dust_color.set_editor_property('default_value', unreal.LinearColor(0.55, 0.48, 0.38, 1.0))
    up = library.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -1000, -500)
    code = library.create_material_expression(material, unreal.MaterialExpressionCustom, -250, -100)
    code.set_editor_property('code', '''float3 c = Color * Tint * Instance;
c *= lerp(1.0, Ao, CavityShade);
float top = saturate((Up.z - 0.45) * 2.5);
return lerp(c, DustColor * Instance, top * Dust);''')
    code.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs = []
    for name in ('Color', 'Tint', 'Instance', 'Ao', 'CavityShade', 'Up', 'DustColor', 'Dust'):
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    code.set_editor_property('inputs', inputs)
    library.connect_material_expressions(color, 'RGB', code, 'Color')
    library.connect_material_expressions(param, '', code, 'Tint')
    library.connect_material_expressions(tint, '', code, 'Instance')
    library.connect_material_expressions(arm, 'R', code, 'Ao')
    library.connect_material_expressions(param_scalar('CavityShade', 0.7, 750), '', code, 'CavityShade')
    library.connect_material_expressions(up, '', code, 'Up')
    library.connect_material_expressions(dust_color, '', code, 'DustColor')
    library.connect_material_expressions(param_scalar('Dust', 0.35, 900), '', code, 'Dust')
    library.connect_material_property(code, '', unreal.MaterialProperty.MP_BASE_COLOR)
    deeper = library.create_material_expression(material, unreal.MaterialExpressionCustom, -250, 250)
    deeper.set_editor_property('code', 'float3 n = Normal * 2.0 - 1.0; n.xy *= Strength; return normalize(n);' if False else 'float3 n = Normal; n.xy *= Strength; return normalize(n);')
    deeper.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    deeper_inputs = []
    for name in ('Normal', 'Strength'):
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        deeper_inputs.append(item)
    deeper.set_editor_property('inputs', deeper_inputs)
    library.connect_material_expressions(normal, 'RGB', deeper, 'Normal')
    library.connect_material_expressions(param_scalar('NormalStrength', 1.8, 1000), '', deeper, 'Strength')
    library.connect_material_property(deeper, '', unreal.MaterialProperty.MP_NORMAL)
    library.connect_material_property(param_scalar('Specular', 0.3, 1050), '', unreal.MaterialProperty.MP_SPECULAR)
    rough = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 700)
    rough.set_editor_property('parameter_name', 'RoughnessScale')
    rough.set_editor_property('default_value', 1.0)
    roughness = library.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, 550)
    library.connect_material_expressions(arm, 'G', roughness, 'A')
    library.connect_material_expressions(rough, '', roughness, 'B')
    # never wet: some scans hold a low roughness that shone like glass on the field
    least = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 800)
    least.set_editor_property('parameter_name', 'MinRoughness')
    least.set_editor_property('default_value', 0.85)
    floor = library.create_material_expression(material, unreal.MaterialExpressionMax, -250, 600)
    library.connect_material_expressions(roughness, '', floor, 'A')
    library.connect_material_expressions(least, '', floor, 'B')
    library.connect_material_property(floor, '', unreal.MaterialProperty.MP_ROUGHNESS)
    library.connect_material_property(arm, 'R', unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    library.recompile_material(material)
    assets.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def import_textures(entries):
    tasks = []
    names = []
    for entry in entries:
        for suffix, key in (('_C', 'color'), ('_N', 'normal'), ('_ARM', 'arm')):
            task = unreal.AssetImportTask()
            task.set_editor_property('filename', entry[key])
            task.set_editor_property('destination_path', TEXTURES)
            task.set_editor_property('destination_name', 'T_BattleObstacle_%s%s' % (entry['name'], suffix))
            task.set_editor_property('automated', True)
            task.set_editor_property('replace_existing', True)
            task.set_editor_property('save', True)
            tasks.append(task)
            names.append(('%s/T_BattleObstacle_%s%s' % (TEXTURES, entry['name'], suffix), suffix))
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for path, suffix in names:
        texture = unreal.load_asset(path)
        if not texture:
            raise RuntimeError('could not import %s' % path)
        if suffix == '_N':
            texture.set_editor_property('srgb', False)
            texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        elif suffix == '_ARM':
            texture.set_editor_property('srgb', False)
            texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
        assets.save_loaded_asset(texture, only_if_is_dirty=False)


def find_imported(name, folder=FOLDER):
    """Moves a mesh the glTF importer put into folders of its own to its folder, and returns it"""
    target = '%s/%s' % (folder, name)
    nested = '%s/%s/StaticMeshes/%s' % (folder, name, name)
    if assets.does_asset_exist(nested):
        if assets.does_asset_exist(target):
            assets.delete_asset(target)
        assets.rename_asset(nested, target)
        assets.delete_directory('%s/%s' % (folder, name))
    mesh = unreal.load_asset(target)
    if not mesh:
        raise RuntimeError('could not import %s' % name)
    return mesh


def import_meshes(entries, material):
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)
    tasks = []
    for entry in entries:
        for name, folder in [('SM_BattleObstacle_%s' % entry['name'], FOLDER)] + [(lod, LOD_FOLDER) for lod in entry['lods']]:
            task = unreal.AssetImportTask()
            task.set_editor_property('filename', os.path.join(SOURCE, name + '.glb'))
            task.set_editor_property('destination_path', folder)
            task.set_editor_property('automated', True)
            task.set_editor_property('replace_existing', True)
            task.set_editor_property('save', True)
            task.set_editor_property('options', pipeline)
            tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    meshes = {}
    for entry in entries:
        mesh = find_imported('SM_BattleObstacle_%s' % entry['name'])
        nanite = mesh.get_editor_property('nanite_settings')
        nanite.set_editor_property('enabled', False)
        mesh.set_editor_property('nanite_settings', nanite)
        path = '%s/MI_BattleObstacle_%s' % (FOLDER, entry['name'])
        instance = unreal.load_asset(path) if assets.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'MI_BattleObstacle_%s' % entry['name'], FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        library.set_material_instance_parent(instance, material)
        for parameter, suffix in (('Color', '_C'), ('Normal', '_N'), ('Arm', '_ARM')):
            library.set_material_instance_texture_parameter_value(instance, parameter, unreal.load_asset('%s/T_BattleObstacle_%s%s' % (TEXTURES, entry['name'], suffix)))
        assets.save_loaded_asset(instance, only_if_is_dirty=False)
        mesh.set_material(0, instance)
        lods = unreal.HexPlanetObjectSet.set_mesh_lods_from(mesh, [find_imported(lod, LOD_FOLDER) for lod in entry['lods']], entry['screens'])
        if lods != len(entry['lods']) + 1:
            raise RuntimeError('%s has %d LODs' % (entry['name'], lods))
        assets.save_loaded_asset(mesh, only_if_is_dirty=False)
        meshes[entry['name']] = mesh
        unreal.log('OBSTACLES %s: %d triangles, %d LODs' % (entry['name'], entry['triangles'], lods))
    if assets.does_directory_exist(LOD_FOLDER):
        assets.delete_directory(LOD_FOLDER)
    return meshes


PROPS = os.path.abspath(os.path.join(HERE, '..', '..', 'Saved', 'BattleProps'))


def import_props(material):
    """The cacti and bones of blender_build_desert_props.py, their textures PNG, a material instance of M_HexBattleScan for each skin"""
    with open(os.path.join(PROPS, 'props.json'), encoding='utf-8') as file:
        entries = json.load(file)['props']
    skins = sorted({entry['skin'] for entry in entries})
    tasks = []
    for skin in skins:
        for suffix in ('_C', '_N', '_ARM'):
            task = unreal.AssetImportTask()
            task.set_editor_property('filename', os.path.join(PROPS, 'T_BattleProp_%s%s.png' % (skin, suffix)))
            task.set_editor_property('destination_path', TEXTURES)
            task.set_editor_property('destination_name', 'T_BattleProp_%s%s' % (skin, suffix))
            task.set_editor_property('automated', True)
            task.set_editor_property('replace_existing', True)
            task.set_editor_property('save', True)
            tasks.append(task)
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)
    for entry in entries:
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', os.path.join(PROPS, 'SM_BattleProp_%s.glb' % entry['name']))
        task.set_editor_property('destination_path', FOLDER)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', True)
        task.set_editor_property('options', pipeline)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    instances = {}
    for skin in skins:
        for suffix in ('_N', '_ARM'):
            texture = unreal.load_asset('%s/T_BattleProp_%s%s' % (TEXTURES, skin, suffix))
            texture.set_editor_property('srgb', False)
            texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP if suffix == '_N' else unreal.TextureCompressionSettings.TC_MASKS)
            assets.save_loaded_asset(texture, only_if_is_dirty=False)
        path = '%s/MI_BattleProp_%s' % (FOLDER, skin)
        instance = unreal.load_asset(path) if assets.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'MI_BattleProp_%s' % skin, FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        library.set_material_instance_parent(instance, material)
        for parameter, suffix in (('Color', '_C'), ('Normal', '_N'), ('Arm', '_ARM')):
            library.set_material_instance_texture_parameter_value(instance, parameter, unreal.load_asset('%s/T_BattleProp_%s%s' % (TEXTURES, skin, suffix)))
        # little dust on a cactus or a bone, and their own relief as drawn
        library.set_material_instance_scalar_parameter_value(instance, 'Dust', 0.08)
        library.set_material_instance_scalar_parameter_value(instance, 'NormalStrength', 1.0)
        assets.save_loaded_asset(instance, only_if_is_dirty=False)
        instances[skin] = instance
    meshes = {}
    for entry in entries:
        mesh = find_imported('SM_BattleProp_%s' % entry['name'])
        nanite = mesh.get_editor_property('nanite_settings')
        nanite.set_editor_property('enabled', False)
        mesh.set_editor_property('nanite_settings', nanite)
        mesh.set_material(0, instances[entry['skin']])
        assets.save_loaded_asset(mesh, only_if_is_dirty=False)
        meshes[entry['name']] = (mesh, entry['places'])
        unreal.log('OBSTACLES prop %s: %d triangles' % (entry['name'], entry['triangles']))
    return meshes


def main():
    with open(os.path.join(SOURCE, 'obstacles.json'), encoding='utf-8') as file:
        entries = json.load(file)['obstacles']
    for folder in (FOLDER, TEXTURES):
        if not assets.does_directory_exist(folder):
            assets.make_directory(folder)
    import_textures(entries)
    material = build_material(entries[0]['name'])
    meshes = import_meshes(entries, material)
    by_ground = lambda ground: [meshes[entry['name']] for entry in entries if ground in entry['grounds']]
    config = unreal.load_asset(CONFIG)
    grounds = list(config.get_editor_property('grounds'))
    for ground in grounds:
        if ground.get_editor_property('underground'):
            ground.set_editor_property('obstacles', by_ground('cave'))
            ground.set_editor_property('obstacle_tint', CAVE_TINT)
        else:
            ground.set_editor_property('obstacles', by_ground('open'))
    config.set_editor_property('grounds', grounds)
    config.set_editor_property('rocky_obstacles', by_ground('rocky'))
    # the wood of a desert is of these rocks too
    config.set_editor_property('rocky_decor', by_ground('rocky'))
    config.set_editor_property('border_rocks', [meshes[name] for name in BORDER])
    # the cacti and the bones of a desert, and the bones that lie on any field
    if os.path.exists(os.path.join(PROPS, 'props.json')):
        props = import_props(material)
        config.set_editor_property('desert_props', [mesh for mesh, places in props.values() if 'rocky' in places])
        config.set_editor_property('field_decor', [mesh for mesh, places in props.values() if 'field' in places])
        config.set_editor_property('desert_landmarks', [mesh for mesh, places in props.values() if 'landmark' in places])
    assets.save_loaded_asset(config, only_if_is_dirty=False)
    unreal.log('OBSTACLES done: %d meshes' % len(meshes))


main()
