"""Create missing Phase 1 materials and the empty Dockside map in Unreal Editor.

Run with Unreal's Python editor plugin after compiling the C++ module. Assets
already present are deliberately preserved, including artist-authored replacements.
The map contains no district or lights: ADGameMode creates exactly one at runtime.
This script authors native .uasset/.umap files; ordinary Python cannot execute it.
"""

from pathlib import Path

import unreal


MATERIAL_ROOT = "/Game/Velocity/Materials"
MAP_PATH = "/Game/Velocity/Maps/L_Dockside"
VOICE_SOURCE = Path(__file__).resolve().parents[2] / "Assets/VoiceSource/VO_ArrivalBroadcast.wav"
VOICE_DESTINATION = "/Game/Velocity/Audio/Voice"
VOICE_ASSET_NAME = "VO_ArrivalBroadcast"

# Linear color, roughness, metallic, emissive multiplier. Opaque glass is an
# explicit blockout approximation; replace it with authored automotive glass.
MATERIALS = {
    "M_Asphalt": ((0.022, 0.028, 0.034), 0.27, 0.08, 0.0),
    "M_Concrete": ((0.12, 0.14, 0.15), 0.83, 0.0, 0.0),
    "M_Building": ((0.048, 0.071, 0.082), 0.43, 0.32, 0.0),
    "M_WindowWarm": ((1.0, 0.50, 0.18), 0.25, 0.1, 2.5),
    "M_WindowCool": ((0.35, 0.67, 0.90), 0.25, 0.1, 1.7),
    "M_Metal": ((0.07, 0.08, 0.09), 0.36, 0.82, 0.0),
    "M_Paint": ((0.022, 0.25, 0.27), 0.20, 0.72, 0.0),
    "M_Glass": ((0.015, 0.027, 0.038), 0.08, 0.30, 0.0),
    "M_Rubber": ((0.012, 0.014, 0.016), 0.90, 0.0, 0.0),
    "M_EmissiveWhite": ((0.68, 0.83, 1.0), 0.22, 0.0, 6.0),
    "M_EmissiveRed": ((1.0, 0.025, 0.01), 0.22, 0.0, 4.0),
    "M_RoadMarking": ((0.62, 0.65, 0.61), 0.48, 0.0, 0.0),
    "M_RoadYellow": ((0.73, 0.46, 0.08), 0.52, 0.0, 0.0),
    "M_ContainerBlue": ((0.025, 0.10, 0.14), 0.72, 0.38, 0.0),
    "M_ContainerRust": ((0.30, 0.075, 0.027), 0.78, 0.34, 0.0),
    "M_Water": ((0.012, 0.032, 0.055), 0.13, 0.22, 0.0),
}


def _connect(expression, material_property):
    if not unreal.MaterialEditingLibrary.connect_material_property(
        expression, "", material_property
    ):
        raise RuntimeError(f"Cannot connect material property {material_property}")


def _scalar(material, name, value, material_property, y):
    expression = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -420, y
    )
    if not expression:
        raise RuntimeError(f"Cannot create material scalar {name}")
    expression.set_editor_property("parameter_name", name)
    expression.set_editor_property("default_value", value)
    _connect(expression, material_property)


def create_material(name, specification):
    path = f"{MATERIAL_ROOT}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        existing = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(existing, unreal.MaterialInterface):
            raise RuntimeError(f"Existing asset at {path} is not a material")
        # Required vertex-factory support is a compatibility repair, not an art overwrite.
        if isinstance(existing, unreal.Material) and not existing.get_editor_property("used_with_instanced_static_meshes"):
            existing.set_editor_property("used_with_instanced_static_meshes", True)
            unreal.MaterialEditingLibrary.recompile_material(existing)
            if not unreal.EditorAssetLibrary.save_loaded_asset(existing, only_if_is_dirty=False):
                raise RuntimeError(f"Failed saving instancing support for {path}")
        unreal.log(f"AFTERDARK: preserving existing {path}")
        return False

    color, roughness, metallic, emission = specification
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew()
    )
    if not material:
        raise RuntimeError(f"Cannot create material {path}")
    material.set_editor_property("used_with_instanced_static_meshes", True)
    base = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -420, -150
    )
    if not base:
        raise RuntimeError(f"Cannot create color expression for {path}")
    base.set_editor_property("parameter_name", "BaseColor")
    base.set_editor_property("default_value", unreal.LinearColor(*color, 1.0))
    _connect(base, unreal.MaterialProperty.MP_BASE_COLOR)
    _scalar(material, "Roughness", roughness, unreal.MaterialProperty.MP_ROUGHNESS, 30)
    _scalar(material, "Metallic", metallic, unreal.MaterialProperty.MP_METALLIC, 140)
    if emission:
        glow = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionVectorParameter, -420, 270
        )
        glow.set_editor_property("parameter_name", "EmissiveColor")
        glow.set_editor_property(
            "default_value", unreal.LinearColor(*(component * emission for component in color), 1.0)
        )
        _connect(glow, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if name == "M_Paint":
        # UE5.8 hides custom-data pins from Python. A small native editor bridge
        # sets the real inputs without relying on hidden numeric enum ordinals.
        if not unreal.ADMaterialLibrary.configure_clear_coat(material, 1.0, 0.14):
            raise RuntimeError("Could not configure the automotive clear-coat inputs")
    unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Failed saving {path}; check file permissions/source control")
    return True


def import_arrival_voiceover():
    """Import the authored opening radio line as a cooked SoundWave asset."""
    if not VOICE_SOURCE.is_file() or VOICE_SOURCE.stat().st_size < 12000:
        raise RuntimeError(f"Arrival voiceover source is missing or unexpectedly small: {VOICE_SOURCE}")
    asset_path = f"{VOICE_DESTINATION}/{VOICE_ASSET_NAME}.{VOICE_ASSET_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if not asset or asset.get_class().get_name() != "SoundWave":
            raise RuntimeError(f"Existing voice asset is not a SoundWave: {asset_path}")
        unreal.log(f"AFTERDARK: preserving existing {asset_path}")
        return False

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(VOICE_SOURCE))
    task.set_editor_property("destination_path", VOICE_DESTINATION)
    task.set_editor_property("destination_name", VOICE_ASSET_NAME)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", False)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not asset or asset.get_class().get_name() != "SoundWave":
        raise RuntimeError(
            f"Arrival voiceover import failed; importer returned {task.get_editor_property('imported_object_paths')}"
        )
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save imported voiceover {asset_path}")
    package_file = Path(unreal.Paths.project_content_dir()) / "Velocity/Audio/Voice" / f"{VOICE_ASSET_NAME}.uasset"
    if not package_file.is_file():
        raise RuntimeError(f"Unreal imported the voiceover but did not save {package_file}")
    unreal.log(f"AFTERDARK_VOICE_ASSET: {asset_path}")
    return True


def main():
    # Never close or save a user's dirty level or unrelated content implicitly.
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError("Save your current level before running the Afterdark bootstrap.")
    if unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():
        raise RuntimeError("Save your edited content before running the Afterdark bootstrap.")
    unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    created = sum(create_material(name, spec) for name, spec in MATERIALS.items())
    imported_voice = import_arrival_voiceover()
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        existing_map = unreal.EditorAssetLibrary.load_asset(MAP_PATH)
        if not isinstance(existing_map, unreal.World):
            raise RuntimeError(f"Existing asset at {MAP_PATH} is not a World")
        unreal.log(f"AFTERDARK: preserving existing map {MAP_PATH}")
    else:
        level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if not level_editor.new_level(MAP_PATH):
            raise RuntimeError(f"Could not create {MAP_PATH}")
        if not level_editor.save_current_level():
            raise RuntimeError(f"Could not save {MAP_PATH}")
        if not unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
            raise RuntimeError(f"Saved map missing from asset registry: {MAP_PATH}")
    unreal.log(
        f"AFTERDARK_BOOTSTRAP_OK: {created} new materials; "
        f"arrival voice {'imported' if imported_voice else 'preserved'}; {MAP_PATH} ready"
    )


if __name__ == "__main__":
    main()
