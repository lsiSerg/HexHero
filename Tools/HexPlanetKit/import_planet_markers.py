"""Imports the markers of the entrances of the underground that generate_planet_kit.py wrote to Saved/HexPlanetMarkers: the cave mouth on the
surface and the ring of rocks underground, drawn with M_HexPlanetMarker, and the shaft of light over an exit, drawn with M_HexPlanetShaft, both of
create_planet_assets.py. EntranceMesh, ExitMesh and ShaftMesh of DA_HexPlanet and DA_HexUnderground name them by default.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi, after create_planet_assets.py.
It lives apart from the kit, whose import deletes its folder.
"""

import os

import unreal

SOURCE_FOLDER = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'HexPlanetMarkers')
DESTINATION = '/Game/HexPlanet/Markers'
# each mesh and its material
MARKERS = [('SM_PlanetCaveMouth', '/Game/HexPlanet/M_HexPlanetMarker'), ('SM_PlanetExitRing', '/Game/HexPlanet/M_HexPlanetMarker'),
    ('SM_PlanetExitShaft', '/Game/HexPlanet/M_HexPlanetShaft')]


def import_marker(name, material_path):
    source = os.path.join(SOURCE_FOLDER, name + '.glb')
    if not os.path.exists(source):
        raise RuntimeError('%s is missing, run generate_planet_kit.py first' % source)

    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)
    # the alpha of the colors is the glow and the red the height of the shaft
    pipeline.get_editor_property('common_meshes_properties').set_editor_property('vertex_color_import_option', unreal.InterchangeVertexColorImportOption.IVCIO_REPLACE)
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', source)
    task.set_editor_property('destination_path', DESTINATION)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)
    task.set_editor_property('options', pipeline)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    # the glTF importer gives the file its own folders
    target = '%s/%s' % (DESTINATION, name)
    nested = '%s/%s/StaticMeshes/%s' % (DESTINATION, name, name)
    if unreal.EditorAssetLibrary.does_asset_exist(nested):
        if unreal.EditorAssetLibrary.does_asset_exist(target):
            unreal.EditorAssetLibrary.delete_asset(target)
        unreal.EditorAssetLibrary.rename_asset(nested, target)
        unreal.EditorAssetLibrary.delete_directory('%s/%s' % (DESTINATION, name))
    mesh = unreal.load_asset(target)
    if not mesh:
        raise RuntimeError('could not load %s' % target)

    material = unreal.load_asset(material_path)
    if material:
        mesh.set_material(0, material)
    else:
        unreal.log_warning('PLANETMARKERS %s is missing, run Tools/create_planet_assets.py and this script again' % material_path)
    settings = mesh.get_editor_property('nanite_settings')
    settings.set_editor_property('enabled', False)
    mesh.set_editor_property('nanite_settings', settings)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    unreal.log('PLANETMARKERS imported %s' % target)


def main():
    for name, material in MARKERS:
        import_marker(name, material)


main()
