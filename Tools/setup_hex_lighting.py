"""Sets the lights of Lvl_HexWorld, so they can be tuned here and not by hand in a binary level.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi
The exposure that these values go with is in the constructor of AHexCameraPawn.
"""

import unreal

LEVEL = '/Game/HexMap/Lvl_HexWorld'

# from the right and a little from the side of the camera, so shadows fall to the left of the screen and walls facing the camera still get some sun
SUN_ROTATION = unreal.Rotator(pitch=-50.0, yaw=-75.0, roll=0.0)
SUN_INTENSITY = 7.0
SUN_COLOR = unreal.Color(r=255, g=236, b=208, a=255)

# lights what the sun does not reach, so a shadow keeps its colors
SKY_INTENSITY = 2.0
SKY_COLOR = unreal.Color(r=190, g=210, b=255, a=255)


def main():
    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)

    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        if isinstance(actor, unreal.DirectionalLight):
            # a turn alone does not count as a change, and the level would not be saved
            actor.modify()
            actor.set_actor_rotation(SUN_ROTATION, False)
            light = actor.get_component_by_class(unreal.DirectionalLightComponent)
            light.set_editor_property('intensity', SUN_INTENSITY)
            light.set_editor_property('light_color', SUN_COLOR)
            light.set_editor_property('atmosphere_sun_light', True)
        elif isinstance(actor, unreal.SkyLight):
            light = actor.get_component_by_class(unreal.SkyLightComponent)
            light.set_editor_property('intensity', SKY_INTENSITY)
            light.set_editor_property('light_color', SKY_COLOR)
            light.set_editor_property('lower_hemisphere_is_black', False)
            light.set_editor_property('lower_hemisphere_color', unreal.LinearColor(0.25, 0.3, 0.4, 1.0))
            # the sky is captured as it renders, so it lights the map as soon as a level opens
            light.set_editor_property('real_time_capture', True)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)
    unreal.log('LIGHTS set')


main()
