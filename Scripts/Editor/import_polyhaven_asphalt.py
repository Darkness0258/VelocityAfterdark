"""Import Poly Haven's CC0 Asphalt Track PBR maps and create a wettable road material.

Run through Scripts/Invoke-Afterdark.ps1 -Action UpgradeVisuals after the
source maps have been downloaded to Content/ExternalAssets/PolyHaven/AsphaltTrack.
"""

from pathlib import Path

import unreal


PROJECT_ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = PROJECT_ROOT / "Content" / "ExternalAssets" / "PolyHaven" / "AsphaltTrack"
TEXTURE_ROOT = "/Game/Velocity/External/AsphaltTrack"
MATERIAL_ROOT = "/Game/Velocity/Materials"
TEXTURES = {
    "T_AsphaltTrack_Diffuse_2K": (
        "asphalt_track_diff_2k.jpg", unreal.TextureCompressionSettings.TC_DEFAULT, True
    ),
    "T_AsphaltTrack_NormalDX_2K": (
        "asphalt_track_nor_dx_2k.jpg", unreal.TextureCompressionSettings.TC_NORMALMAP, False
    ),
    "T_AsphaltTrack_Roughness_2K": (
        "asphalt_track_rough_2k.jpg", unreal.TextureCompressionSettings.TC_DEFAULT, False
    ),
}


def checked(ok, message):
    if not ok:
        raise RuntimeError(message)


def import_texture(name, filename, compression, srgb):
    source = SOURCE_ROOT / filename
    if not source.is_file():
        raise RuntimeError(f"Missing publisher texture: {source}")
    destination = f"{TEXTURE_ROOT}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(destination):
        texture = unreal.EditorAssetLibrary.load_asset(destination)
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError(f"Asset path is occupied by a non-texture: {destination}")
    else:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(source))
        task.set_editor_property("destination_path", TEXTURE_ROOT)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", True)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = unreal.EditorAssetLibrary.load_asset(destination)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Unreal did not import {source} as Texture2D")
    texture.set_editor_property("srgb", srgb)
    texture.set_editor_property("compression_settings", compression)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
    checked(unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False),
            f"Could not save texture {destination}")
    unreal.log(f"AFTERDARK_POLYHAVEN_TEXTURE_OK: {destination}")
    return texture


def expression(material, expression_class, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y)
    if not node:
        raise RuntimeError(f"Could not create {expression_class.__name__} in {material.get_path_name()}")
    return node


def connect(source, target, input_name):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, "", target, input_name):
        outputs = unreal.MaterialEditingLibrary.get_material_expression_output_names(source)
        inputs = unreal.MaterialEditingLibrary.get_material_expression_input_names(target)
        raise RuntimeError(
            f"Material connection failed: {source.get_class().get_name()} outputs={list(outputs)} "
            f"target={target.get_class().get_name()} inputs={list(inputs)} requested={input_name}"
        )


def create_material(textures):
    path = f"{MATERIAL_ROOT}/M_Asphalt_PolyHaven"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"Asset path is occupied by a non-material: {path}")
        nodes = unreal.MaterialEditingLibrary.get_material_expressions(material)
        kinds = {node.get_class().get_name() for node in nodes}
        required = {
            "MaterialExpressionTextureCoordinate",
            "MaterialExpressionTextureSampleParameter2D",
            "MaterialExpressionMultiply",
        }
        if len(nodes) < 10 or not required.issubset(kinds):
            raise RuntimeError(f"Existing asphalt material is incomplete; it was not overwritten: {path}")
        material.set_editor_property("used_with_instanced_static_meshes", True)
        checked(unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False),
                f"Could not save instanced-mesh usage for {path}")
        return material

    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_Asphalt_PolyHaven", MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew()
    )
    if not isinstance(material, unreal.Material):
        raise RuntimeError(f"Could not create road material {path}")
    material.set_editor_property("used_with_instanced_static_meshes", True)
    coords = expression(material, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    # The source tile is 2 m across. Each generated road segment is hundreds
    # of metres long and 24 m wide, so scale UVs to a consistent physical grain.
    coords.set_editor_property("u_tiling", 400.0)
    coords.set_editor_property("v_tiling", 12.0)

    def sample(texture, parameter, x, y):
        node = expression(material, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("texture", texture)
        connect(coords, node, "UVs")
        return node

    diffuse = sample(textures["T_AsphaltTrack_Diffuse_2K"], "AsphaltColor", -650, -180)
    base = expression(material, unreal.MaterialExpressionVectorParameter, -650, 60)
    base.set_editor_property("parameter_name", "BaseColor")
    base.set_editor_property("default_value", unreal.LinearColor(1.45, 1.45, 1.45, 1.0))
    tint = expression(material, unreal.MaterialExpressionMultiply, -340, -80)
    connect(diffuse, tint, "A")
    connect(base, tint, "B")
    checked(unreal.MaterialEditingLibrary.connect_material_property(
        tint, "", unreal.MaterialProperty.MP_BASE_COLOR
    ), "Could not connect the asphalt color")

    normal = sample(textures["T_AsphaltTrack_NormalDX_2K"], "AsphaltNormal", -650, 290)
    checked(unreal.MaterialEditingLibrary.connect_material_property(
        normal, "", unreal.MaterialProperty.MP_NORMAL
    ), "Could not connect the asphalt normal")

    rough_sample = sample(textures["T_AsphaltTrack_Roughness_2K"], "AsphaltRoughnessMap", -650, 600)
    rough_mask = expression(material, unreal.MaterialExpressionComponentMask, -380, 500)
    rough_mask.set_editor_property("r", True)
    rough_mask.set_editor_property("g", False)
    rough_mask.set_editor_property("b", False)
    rough_mask.set_editor_property("a", False)
    connect(rough_sample, rough_mask, "None")
    rough_scale = expression(material, unreal.MaterialExpressionScalarParameter, -380, 700)
    rough_scale.set_editor_property("parameter_name", "Roughness")
    rough_scale.set_editor_property("default_value", 1.0)
    rough = expression(material, unreal.MaterialExpressionMultiply, -100, 540)
    connect(rough_mask, rough, "A")
    connect(rough_scale, rough, "B")
    checked(unreal.MaterialEditingLibrary.connect_material_property(
        rough, "", unreal.MaterialProperty.MP_ROUGHNESS
    ), "Could not connect the asphalt roughness")

    metallic = expression(material, unreal.MaterialExpressionScalarParameter, -100, 760)
    metallic.set_editor_property("parameter_name", "Metallic")
    metallic.set_editor_property("default_value", 0.0)
    checked(unreal.MaterialEditingLibrary.connect_material_property(
        metallic, "", unreal.MaterialProperty.MP_METALLIC
    ), "Could not connect asphalt metallic")
    unreal.MaterialEditingLibrary.recompile_material(material)
    checked(unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False),
            f"Could not save asphalt material {path}")
    unreal.log(f"AFTERDARK_POLYHAVEN_MATERIAL_OK: {path}")
    return material


def main():
    unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    imported = {name: import_texture(name, *specification) for name, specification in TEXTURES.items()}
    create_material(imported)
    unreal.log("AFTERDARK_POLYHAVEN_ASPHALT_READY: three 2K PBR maps retain dynamic wet-road controls")


if __name__ == "__main__":
    main()
