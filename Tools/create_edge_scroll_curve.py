# Creates the edge scroll curve and assigns it to DA_HexCamera. Run it with the editor closed:
# UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi
# Python cannot reach the keys of a curve, so they go through a CSV import, which always makes them linear.

import os
import tempfile

import unreal

CURVE_FOLDER = "/Game/HexMap"
CURVE_NAME = "Curve_EdgeScroll"
CONFIG_PATH = "/Game/HexMap/DA_HexCamera"

# the strength rises smoothly over the first half of the zone and stays at its maximum in the second
# the keys are linear, so the rise is sampled from a smoothstep
KEYS = [(0.0, 0.0), (0.1, 0.104), (0.2, 0.352), (0.3, 0.648), (0.4, 0.896), (0.5, 1.0), (1.0, 1.0)]

csv_path = os.path.join(tempfile.mkdtemp(), f"{CURVE_NAME}.csv")

with open(csv_path, "w") as csv_file:
    csv_file.writelines(f"{time},{value}\n" for time, value in KEYS)

settings = unreal.CSVImportSettings()
settings.import_type = unreal.CSVImportType.ECSV_CURVE_FLOAT

factory = unreal.CSVImportFactory()
factory.automated_import_settings = settings

task = unreal.AssetImportTask()
task.filename = csv_path
task.destination_path = CURVE_FOLDER
task.destination_name = CURVE_NAME
task.factory = factory
task.automated = True
task.replace_existing = True
task.save = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

curve = unreal.EditorAssetLibrary.load_asset(f"{CURVE_FOLDER}/{CURVE_NAME}")

config = unreal.EditorAssetLibrary.load_asset(CONFIG_PATH)
config.set_editor_property("edge_scroll_curve", curve)
unreal.EditorAssetLibrary.save_loaded_asset(config)

samples = [round(curve.get_float_value(i / 20.0), 3) for i in range(21)]
unreal.log(f"Edge scroll curve {curve.get_time_range()} samples: {samples}")
unreal.log(f"Edge scroll config: {config.get_editor_property('edge_scroll_curve').get_path_name()}")
