# Creates the input actions, their mapping context and DA_HexInput, and DA_HexKeyLegend if it is missing. Run it with the editor closed:
# UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi
# Running it again resets the keys to the ones below, keeping the assets and whatever references them.
# It never changes the lines of an existing DA_HexKeyLegend, whose lines are edited by hand and read their keys from the actions,
# but adds those of ADDED_LEGEND_ACTIONS that it lacks.

import unreal

FOLDER = "/Game/HexMap/Input"
CONTEXT_NAME = "IMC_HexMap"
CONFIG_NAME = "DA_HexInput"
LEGEND_NAME = "DA_HexKeyLegend"

BOOLEAN = unreal.InputActionValueType.BOOLEAN
AXIS1D = unreal.InputActionValueType.AXIS1D
AXIS2D = unreal.InputActionValueType.AXIS2D

# the number keys of the slots of the bookmarks of the camera, 1 to 9 and 0, which the key gives through a scalar as slot 10
NUMBER_KEYS = ["One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine", "Zero"]


def bookmark_keys(*chords):
    return [(key, False, False, float(slot), *chords) for slot, key in enumerate(NUMBER_KEYS, start=1)]


# config property, action asset, description, value type, keys as (key name, swizzle to the Y axis, negate) and optionally the actions held with
# it and a scalar the value is multiplied by.
# A key held with another action is a chord, and the same key without it then stops firing while that action is held; a key held with two
# needs both, and must come before the same key held with one of them, which its chords then block, as the context evaluates them in order.
# A new DA_HexKeyLegend gets a line for each action the game binds, labelled by its description, in this order
ACTIONS = [
    # held keys that other actions are chorded with, which the game does not bind
    (None, "IA_HexShift", "Shift", BOOLEAN, [("LeftShift", False, False), ("RightShift", False, False)]),
    (None, "IA_HexCtrl", "Ctrl", BOOLEAN, [("LeftControl", False, False), ("RightControl", False, False)]),
    (None, "IA_HexAlt", "Alt", BOOLEAN, [("LeftAlt", False, False), ("RightAlt", False, False)]),
    ("move_action", "IA_HexMove", "Move", AXIS2D, [
        ("W", True, False), ("A", False, True), ("S", True, True), ("D", False, False),
        ("Up", True, False), ("Left", False, True), ("Down", True, True), ("Right", False, False),
    ]),
    ("drag_action", "IA_HexDrag", "Drag map", BOOLEAN, [("LeftMouseButton", False, False)]),
    ("turn_action", "IA_HexTurn", "Turn", BOOLEAN, [("RightMouseButton", False, False)]),
    ("turn_keys_action", "IA_HexTurnKeys", "Turn", AXIS1D, [("Q", False, True), ("E", False, False)]),
    ("free_camera_action", "IA_HexFreeCamera", "Free camera", BOOLEAN, [("C", False, False)]),
    ("focus_hex_action", "IA_HexFocusHex", "Center hex", BOOLEAN, [("MiddleMouseButton", False, False)]),
    ("copy_camera_action", "IA_HexCopyCamera", "Copy camera", BOOLEAN, [("C", False, False, "IA_HexCtrl")]),
    ("save_view_action", "IA_HexSaveView", "Save view", BOOLEAN, [("S", False, False, "IA_HexCtrl")]),
    # the bookmarks of the camera, each planet and flat map its own (CameraBookmarks.json): Ctrl+Shift and a number key save the camera in its
    # slot, Ctrl and the key go back there; plain 1 to 5 still choose the level of the brush
    ("save_bookmark_action", "IA_HexSaveBookmark", "Save camera to a slot", AXIS1D, bookmark_keys("IA_HexCtrl", "IA_HexShift")),
    ("jump_bookmark_action", "IA_HexJumpBookmark", "Camera from a slot", AXIS1D, bookmark_keys("IA_HexCtrl")),
    ("zoom_action", "IA_HexZoom", "Zoom", AXIS1D, [("MouseWheelAxis", False, False)]),
    ("toggle_grid_action", "IA_HexToggleGrid", "Grid", BOOLEAN, [("G", False, False)]),
    # Escape stops a play session in the editor before the game gets it, so there is a second key
    ("map_menu_action", "IA_HexMapMenu", "Map menu", BOOLEAN, [("M", False, False), ("Escape", False, False)]),
    # debug wireframe; F1 and the keys near it are taken by the editor during a play session
    ("wireframe_mode_action", "IA_HexWireframeMode", "Wireframe", BOOLEAN, [("F", False, False)]),
    ("wireframe_scope_action", "IA_HexWireframeScope", "Wireframe hexes", BOOLEAN, [("F", False, False, "IA_HexShift")]),
    ("wireframe_target_action", "IA_HexWireframeTarget", "Wireframe geometry", BOOLEAN, [("F", False, False, "IA_HexCtrl")]),
    ("water_view_action", "IA_HexWaterView", "Water", BOOLEAN, [("V", False, False)]),
    # the forced light of the planet, which lights both layers evenly (hex.PlanetForcedLight); until 2026-11-10 L made the sun follow the camera or
    # stay, which only hex.PlanetSun does now (REPLACED_ACTIONS)
    ("forced_light_action", "IA_HexForcedLight", "Forced light", BOOLEAN, [("L", False, False)]),
    # the planet goes between its surface and its underground, through the entrance under the mouse (AHexPlanetCameraPawn::TravelTo)
    ("layer_action", "IA_HexLayer", "Underground and back", BOOLEAN, [("U", False, False)]),
    ("key_legend_action", "IA_HexKeyLegend", "This legend", BOOLEAN, [("H", False, False)]),
    # painting the levels of the planet; a number key gives its level through a scalar, 1 deep sea to 5 mountains
    ("paint_mode_action", "IA_HexPaintMode", "Paint levels", BOOLEAN, [("B", False, False)]),
    ("paint_level_action", "IA_HexPaintLevel", "Brush level", AXIS1D, [("One", False, False, 1.0), ("Two", False, False, 2.0), ("Three", False, False, 3.0), ("Four", False, False, 4.0), ("Five", False, False, 5.0)]),
    ("brush_size_action", "IA_HexBrushSize", "Brush size", BOOLEAN, [("X", False, False)]),
    ("undo_action", "IA_HexUndo", "Undo", BOOLEAN, [("Z", False, False, "IA_HexCtrl")]),
    ("redo_action", "IA_HexRedo", "Redo", BOOLEAN, [("Y", False, False, "IA_HexCtrl")]),
    # the game, as the developer set its rules on 2026-11-11: P starts and stops it, a click of the left button chooses a hero or points the hero
    # chosen to a cell, a second click on that cell or Space sends it; held and moved, the left button still drags the planet
    ("play_mode_action", "IA_HexPlayMode", "Play the game", BOOLEAN, [("P", False, False)]),
    ("hero_select_action", "IA_HexHeroSelect", "Choose a hero, point it, send it", BOOLEAN, [("LeftMouseButton", False, False)]),
    ("hero_send_action", "IA_HexHeroSend", "Walk the way shown", BOOLEAN, [("SpaceBar", False, False)]),
    ("next_hero_action", "IA_HexNextHero", "Next hero", BOOLEAN, [("Tab", False, False)]),
    ("end_turn_action", "IA_HexEndTurn", "End the turn", BOOLEAN, [("Enter", False, False)]),
    ("skip_hero_action", "IA_HexSkipHero", "Skip the hero today", BOOLEAN, [("Z", False, False)]),
]

# heading of each line of a new DA_HexKeyLegend, by its action
LEGEND_CATEGORIES = {
    "IA_HexMove": "Camera",
    "IA_HexDrag": "Camera",
    "IA_HexTurn": "Camera",
    "IA_HexTurnKeys": "Camera",
    "IA_HexFreeCamera": "Camera",
    "IA_HexFocusHex": "Camera",
    "IA_HexCopyCamera": "Camera",
    "IA_HexSaveView": "Camera",
    "IA_HexSaveBookmark": "Camera",
    "IA_HexJumpBookmark": "Camera",
    "IA_HexZoom": "Camera",
    "IA_HexToggleGrid": "View",
    "IA_HexWaterView": "View",
    "IA_HexForcedLight": "View",
    "IA_HexLayer": "View",
    "IA_HexWireframeMode": "Wireframe",
    "IA_HexWireframeScope": "Wireframe",
    "IA_HexWireframeTarget": "Wireframe",
    "IA_HexMapMenu": "Menus",
    "IA_HexKeyLegend": "Menus",
    "IA_HexPaintMode": "Painting",
    "IA_HexPaintLevel": "Painting",
    "IA_HexBrushSize": "Painting",
    "IA_HexUndo": "Painting",
    "IA_HexRedo": "Painting",
    "IA_HexPlayMode": "Heroes",
    "IA_HexHeroSelect": "Heroes",
    "IA_HexHeroSend": "Heroes",
    "IA_HexNextHero": "Heroes",
    "IA_HexEndTurn": "Heroes",
    "IA_HexSkipHero": "Heroes",
}


# actions added after the legend was made, whose lines an existing legend gets if it lacks them
ADDED_LEGEND_ACTIONS = {"IA_HexPaintMode", "IA_HexPaintLevel", "IA_HexBrushSize", "IA_HexUndo", "IA_HexRedo", "IA_HexForcedLight", "IA_HexLayer",
                        "IA_HexSaveBookmark", "IA_HexJumpBookmark", "IA_HexPlayMode", "IA_HexHeroSelect", "IA_HexHeroSend", "IA_HexNextHero", "IA_HexEndTurn", "IA_HexSkipHero"}

# lines of the legend whose action kept its name but changed what it does: its label becomes the new description only while it still holds an
# old one, so a label edited by hand stays
RELABELED_ACTIONS = {"IA_HexHeroSelect": ["Choose a hero (click)"], "IA_HexHeroSend": ["Send the hero (click)"], "IA_HexEndTurn": ["End the day"]}

# actions that another took the place of, as (old action, new action): the line of the old one in an existing legend becomes the line of the new
# one where it stands, with the description of the new one as its label, and the old asset is deleted once nothing in the legend holds it
REPLACED_ACTIONS = [("IA_HexSunFollow", "IA_HexForcedLight")]


def load_or_create(name, asset_class, factory):
    path = f"{FOLDER}/{name}"

    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)

    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, asset_class, factory)


def make_key(name):
    key = unreal.Key()
    key.set_editor_property("key_name", name)
    return key


config_factory = unreal.DataAssetFactory()
config_factory.set_editor_property("data_asset_class", unreal.HexInputConfig)

context = load_or_create(CONTEXT_NAME, unreal.InputMappingContext, unreal.InputMappingContext_Factory())
config = load_or_create(CONFIG_NAME, unreal.HexInputConfig, config_factory)

context.unmap_all()
key_modifiers = []
key_triggers = []
actions = {}

for config_property, action_name, description, value_type, keys in ACTIONS:
    action = load_or_create(action_name, unreal.InputAction, unreal.InputAction_Factory())
    action.set_editor_property("value_type", value_type)
    action.set_editor_property("action_description", description)
    actions[action_name] = action

    if config_property:
        config.set_editor_property(config_property, action)

    unreal.EditorAssetLibrary.save_loaded_asset(action, only_if_is_dirty=False)

    for key_name, swizzle, negate, *extra in keys:
        context.map_key(action, make_key(key_name))

        chord = [value for value in extra if isinstance(value, str)]
        scalar = [value for value in extra if isinstance(value, float)]
        triggers = []

        for chord_action in chord:
            trigger = unreal.new_object(unreal.InputTriggerChordAction, outer=context)
            trigger.set_editor_property("chord_action", actions[chord_action])
            triggers.append(trigger)

        key_triggers.append(triggers)

        modifiers = []

        if swizzle:
            modifiers.append(unreal.new_object(unreal.InputModifierSwizzleAxis, outer=context))

        if negate:
            modifiers.append(unreal.new_object(unreal.InputModifierNegate, outer=context))

        if scalar:
            modifier = unreal.new_object(unreal.InputModifierScalar, outer=context)
            modifier.set_editor_property("scalar", unreal.Vector(scalar[0], scalar[0], scalar[0]))
            modifiers.append(modifier)

        key_modifiers.append(modifiers)

# Python gets copies of the mappings, so the modifiers and triggers go back as a whole array
mapping_data = context.get_editor_property("default_key_mappings")
mappings = mapping_data.get_editor_property("mappings")

for index, modifiers in enumerate(key_modifiers):
    mapping = mappings[index]
    mapping.set_editor_property("modifiers", modifiers)
    mapping.set_editor_property("triggers", key_triggers[index])
    mappings[index] = mapping

mapping_data.set_editor_property("mappings", mappings)
context.set_editor_property("default_key_mappings", mapping_data)

config.set_editor_property("mapping_context", context)

# these changes do not mark an existing asset as dirty, so the saves are forced
unreal.EditorAssetLibrary.save_loaded_asset(context, only_if_is_dirty=False)
unreal.EditorAssetLibrary.save_loaded_asset(config, only_if_is_dirty=False)

if not unreal.EditorAssetLibrary.does_asset_exist(f"{FOLDER}/{LEGEND_NAME}"):
    legend_factory = unreal.DataAssetFactory()
    legend_factory.set_editor_property("data_asset_class", unreal.HexKeyLegendConfig)
    legend = unreal.AssetToolsHelpers.get_asset_tools().create_asset(LEGEND_NAME, FOLDER, unreal.HexKeyLegendConfig, legend_factory)
    lines = []

    for config_property, action_name, description, _, _ in ACTIONS:
        if config_property:
            line = unreal.HexKeyLegendLine()
            line.set_editor_property("category", LEGEND_CATEGORIES.get(action_name, ""))
            line.set_editor_property("label", description)
            line.set_editor_property("action", actions[action_name])
            lines.append(line)

    legend.set_editor_property("lines", lines)
    unreal.EditorAssetLibrary.save_loaded_asset(legend, only_if_is_dirty=False)
    unreal.log(f"Hex input: created {LEGEND_NAME} with {len(lines)} lines")
else:
    # the lines of an existing legend are edited by hand, so it only gets those of the actions it lacks of ADDED_LEGEND_ACTIONS, and those of
    # REPLACED_ACTIONS become the lines of the actions that took their place
    legend = unreal.EditorAssetLibrary.load_asset(f"{FOLDER}/{LEGEND_NAME}")
    lines = list(legend.get_editor_property("lines"))
    descriptions = {action_name: description for _, action_name, description, _, _ in ACTIONS}
    replaced = 0

    for index, line in enumerate(lines):
        action = line.get_editor_property("action")
        for old_name, new_name in REPLACED_ACTIONS:
            if action and action.get_name() == old_name:
                line.set_editor_property("action", actions[new_name])
                line.set_editor_property("label", descriptions[new_name])
                lines[index] = line
                replaced += 1
                unreal.log(f"Hex input: the line of {old_name} in {LEGEND_NAME} is now that of {new_name}")

        if action and str(line.get_editor_property("label")) in RELABELED_ACTIONS.get(action.get_name(), []):
            line.set_editor_property("label", descriptions[action.get_name()])
            lines[index] = line
            replaced += 1

    listed = {line.get_editor_property("action").get_name() for line in lines if line.get_editor_property("action")}
    added = 0

    for config_property, action_name, description, _, _ in ACTIONS:
        if action_name in ADDED_LEGEND_ACTIONS and action_name not in listed:
            line = unreal.HexKeyLegendLine()
            line.set_editor_property("category", LEGEND_CATEGORIES.get(action_name, ""))
            line.set_editor_property("label", description)
            line.set_editor_property("action", actions[action_name])
            lines.append(line)
            added += 1

    if added or replaced:
        legend.set_editor_property("lines", lines)
        unreal.EditorAssetLibrary.save_loaded_asset(legend, only_if_is_dirty=False)
        unreal.log(f"Hex input: added {added} lines to {LEGEND_NAME} and gave {replaced} to the actions that replaced theirs")

# the actions replaced are mapped to no key any more, as the context was unmapped above, and DA_HexInput no longer has their property
for old_name, new_name in REPLACED_ACTIONS:
    old_path = f"{FOLDER}/{old_name}"
    if unreal.EditorAssetLibrary.does_asset_exist(old_path):
        if unreal.EditorAssetLibrary.delete_asset(old_path):
            unreal.log(f"Hex input: deleted {old_name}, replaced by {new_name}")
        else:
            unreal.log_warning(f"Hex input: could not delete {old_name}, replaced by {new_name}")

for mapping in context.get_editor_property("default_key_mappings").get_editor_property("mappings"):
    modifiers = [type(modifier).__name__ for modifier in mapping.get_editor_property("modifiers")]
    chords = [trigger.get_editor_property("chord_action").get_name() for trigger in mapping.get_editor_property("triggers") if isinstance(trigger, unreal.InputTriggerChordAction)]
    key_name = mapping.get_editor_property("key").get_editor_property("key_name")
    unreal.log(f"Hex input: {mapping.get_editor_property('action').get_name()} {key_name} {modifiers} {chords}")
