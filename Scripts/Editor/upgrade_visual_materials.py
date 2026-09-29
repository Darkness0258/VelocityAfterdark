"""Import generated PBR maps and create UE5.8 instanced district materials.

Run after Scripts/generate_surface_textures.py with the Unreal Python plugin:
  UnrealEditor-Cmd.exe VelocityAfterdark.uproject -run=pythonscript \
    -script=Scripts/Editor/upgrade_visual_materials.py -unattended -NullRHI

Existing artist materials are preserved; this creates separate, data-driven
materials and uses them only as optional replacements in AADDistrict.
"""

from pathlib import Path

import unreal


PROJECT_ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = PROJECT_ROOT / "Scripts" / "GeneratedAssets"
ASSET_ROOT = "/Game/Velocity/Materials/Generated"
MATERIAL_ROOT = "/Game/Velocity/Materials"


TEXTURES = {
    "T_Asphalt_Surface_V2": ("T_Asphalt_Surface_V2.png", unreal.TextureCompressionSettings.TC_DEFAULT),
    "T_Asphalt_Normal_V2": ("T_Asphalt_Normal_V2.png", unreal.TextureCompressionSettings.TC_NORMALMAP),
    "T_Concrete_Surface_V2": ("T_Concrete_Surface_V2.png", unreal.TextureCompressionSettings.TC_DEFAULT),
    "T_Concrete_Normal_V2": ("T_Concrete_Normal_V2.png", unreal.TextureCompressionSettings.TC_NORMALMAP),
    "T_Facade_Surface_V2": ("T_Facade_Surface_V2.png", unreal.TextureCompressionSettings.TC_DEFAULT),
    "T_Facade_Normal_V2": ("T_Facade_Normal_V2.png", unreal.TextureCompressionSettings.TC_NORMALMAP),
    "T_Asphalt_Surface_V3": ("T_Asphalt_Surface_V3.png", unreal.TextureCompressionSettings.TC_DEFAULT),
    "T_Asphalt_Normal_V3": ("T_Asphalt_Normal_V3.png", unreal.TextureCompressionSettings.TC_NORMALMAP),
    "T_Concrete_Surface_V3": ("T_Concrete_Surface_V3.png", unreal.TextureCompressionSettings.TC_DEFAULT),
    "T_Concrete_Normal_V3": ("T_Concrete_Normal_V3.png", unreal.TextureCompressionSettings.TC_NORMALMAP),
    "T_Facade_Surface_V3": ("T_Facade_Surface_V3.png", unreal.TextureCompressionSettings.TC_DEFAULT),
    "T_Facade_Normal_V3": ("T_Facade_Normal_V3.png", unreal.TextureCompressionSettings.TC_NORMALMAP),
    "T_IndustrialMetal_Surface_V3": ("T_IndustrialMetal_Surface_V3.png", unreal.TextureCompressionSettings.TC_DEFAULT),
    "T_IndustrialMetal_Normal_V3": ("T_IndustrialMetal_Normal_V3.png", unreal.TextureCompressionSettings.TC_NORMALMAP),
    "T_Rubber_Surface_V3": ("T_Rubber_Surface_V3.png", unreal.TextureCompressionSettings.TC_DEFAULT),
    "T_Rubber_Normal_V3": ("T_Rubber_Normal_V3.png", unreal.TextureCompressionSettings.TC_NORMALMAP),
}


def checked(ok, message):
    if not ok:
        raise RuntimeError(message)


def import_texture(name, filename, compression):
    source = SOURCE_ROOT / filename
    if not source.is_file():
        raise RuntimeError(f"Missing generated texture source: {source}")
    destination = f"{ASSET_ROOT}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(destination):
        existing = unreal.EditorAssetLibrary.load_asset(destination)
        if not isinstance(existing, unreal.Texture2D):
            raise RuntimeError(f"Asset path is occupied by a non-texture: {destination}")
        # These checked-in maps are deterministic outputs of the adjacent
        # generator. Replacing an already loaded UTexture2D in commandlets can
        # trip UE's rooted-object assertion, so preserve the valid asset.
        texture = existing
    else:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(source))
        task.set_editor_property("destination_path", ASSET_ROOT)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", True)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = unreal.EditorAssetLibrary.load_asset(destination)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Unreal did not import {source} as Texture2D")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", compression)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
    checked(unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False),
            f"Could not save imported texture {destination}")
    unreal.log(f"AFTERDARK_PBR_TEXTURE_OK: {destination}")
    return texture


def expression(material, expression_class, x, y):
    value = unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y)
    if not value:
        raise RuntimeError(f"Could not create {expression_class.__name__} in {material.get_path_name()}")
    return value


def connect(source, target, input_name):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, "", target, input_name):
        outputs = unreal.MaterialEditingLibrary.get_material_expression_output_names(source)
        inputs = unreal.MaterialEditingLibrary.get_material_expression_input_names(target)
        raise RuntimeError(
            f"Could not connect {source.get_class().get_name()} outputs={list(outputs)} "
            f"to {target.get_class().get_name()} inputs={list(inputs)} requested={input_name}"
        )


def channel(material, source, color_channel, y):
    mask = expression(material, unreal.MaterialExpressionComponentMask, 150, y)
    mask.set_editor_property("r", color_channel == "r")
    mask.set_editor_property("g", color_channel == "g")
    mask.set_editor_property("b", color_channel == "b")
    mask.set_editor_property("a", False)
    # UMaterialExpressionComponentMask keeps this pin's FName empty in UE 5.8.
    connect(source, mask, "None")
    return mask


def make_surface_material(name, base_color, surface_texture, normal_texture, tiling, metallic):
    path = f"{MATERIAL_ROOT}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        existing = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(existing, unreal.Material):
            raise RuntimeError(f"Asset path is occupied by a non-material: {path}")
        expressions = unreal.MaterialEditingLibrary.get_material_expressions(existing)
        expression_types = {node.get_class().get_name() for node in expressions}
        minimum_expressions = 10 if normal_texture else 9
        if (len(expressions) < minimum_expressions or "MaterialExpressionTextureCoordinate" not in expression_types
                or "MaterialExpressionTextureSampleParameter2D" not in expression_types
                or "MaterialExpressionComponentMask" not in expression_types):
            raise RuntimeError(f"Generated material is incomplete and will not be overwritten automatically: {path}")
        unreal.log(f"AFTERDARK_PBR_MATERIAL_PRESERVED: {path}")
        return existing
    else:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew()
        )
    if not isinstance(material, unreal.Material):
        raise RuntimeError(f"Could not create material {path}")
    material.set_editor_property("used_with_instanced_static_meshes", True)

    coord = expression(material, unreal.MaterialExpressionTextureCoordinate, -800, -60)
    coord.set_editor_property("u_tiling", tiling[0])
    coord.set_editor_property("v_tiling", tiling[1])

    surface = expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -560, -160)
    surface.set_editor_property("parameter_name", "SurfaceData")
    surface.set_editor_property("texture", surface_texture)
    connect(coord, surface, "UVs")

    base = expression(material, unreal.MaterialExpressionVectorParameter, -540, 90)
    base.set_editor_property("parameter_name", "BaseColor")
    base.set_editor_property("default_value", unreal.LinearColor(*base_color, 1.0))
    tint = expression(material, unreal.MaterialExpressionMultiply, -230, 70)
    connect(base, tint, "A")
    connect(channel(material, surface, "r", -220), tint, "B")
    checked(unreal.MaterialEditingLibrary.connect_material_property(
        tint, "", unreal.MaterialProperty.MP_BASE_COLOR), f"Could not connect base color for {name}")

    roughness_data = channel(material, surface, "g", 100)
    roughness_scale = expression(material, unreal.MaterialExpressionScalarParameter, -200, 170)
    roughness_scale.set_editor_property("parameter_name", "Roughness")
    roughness_scale.set_editor_property("default_value", 1.0)
    roughness = expression(material, unreal.MaterialExpressionMultiply, 80, 120)
    connect(roughness_data, roughness, "A")
    connect(roughness_scale, roughness, "B")
    checked(unreal.MaterialEditingLibrary.connect_material_property(
        roughness, "", unreal.MaterialProperty.MP_ROUGHNESS), f"Could not connect roughness for {name}")

    metal = expression(material, unreal.MaterialExpressionScalarParameter, -220, 230)
    metal.set_editor_property("parameter_name", "Metallic")
    metal.set_editor_property("default_value", metallic)
    checked(unreal.MaterialEditingLibrary.connect_material_property(
        metal, "", unreal.MaterialProperty.MP_METALLIC), f"Could not connect metallic for {name}")

    if normal_texture:
        normal = expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -540, 360)
        normal.set_editor_property("parameter_name", "NormalDetail")
        normal.set_editor_property("texture", normal_texture)
        normal.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        connect(coord, normal, "UVs")
        checked(unreal.MaterialEditingLibrary.connect_material_property(
            normal, "", unreal.MaterialProperty.MP_NORMAL), f"Could not connect normal for {name}")

    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    errors = unreal.MaterialEditingLibrary.recompile_material(material)
    checked(not errors, f"Material compiler rejected {path}: {'; '.join(errors)}")
    checked(unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False),
            f"Could not save material {path}")
    unreal.log(f"AFTERDARK_PBR_MATERIAL_OK: {path}")
    return material


def enable_parked_car_instancing():
    """Compile original imported car materials for the city's HISM renderer."""
    usage = unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES
    materials = (
        "citycar_0_paintC", "citycar_1_trim", "citycar_2_glass", "citycar_3_interior",
        "citycar_4_lightWhite", "citycar_5_alloy", "citycar_6_lightAmber", "citycar_7_lightRed",
    )
    for name in materials:
        path = f"/Game/Velocity/External/CityCarStaticFinal/{name}"
        material = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(material, unreal.MaterialInterface):
            raise RuntimeError(f"Missing imported CC0 parked-car material: {path}")
        if isinstance(material, unreal.Material):
            unreal.MaterialEditingLibrary.set_base_material_usage(material, usage, True)
            errors = unreal.MaterialEditingLibrary.recompile_material(material)
            checked(not errors, f"Could not compile instanced usage for {path}: {'; '.join(errors)}")
        elif isinstance(material, unreal.MaterialInstanceConstant):
            unreal.MaterialEditingLibrary.set_material_usage_override(material, usage, True, True)
            unreal.MaterialEditingLibrary.update_material_instance(material)
        else:
            raise RuntimeError(f"Unsupported parked-car material class at {path}: {material.get_class().get_name()}")
        checked(unreal.MaterialEditingLibrary.has_material_usage(material, usage),
                f"Unreal did not enable instanced-mesh usage for {path}")
        checked(unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False),
                f"Could not save instanced usage for {path}")
    unreal.log("AFTERDARK_CITYCAR_MATERIALS_READY: 8 imported materials support instanced rendering")


def main():
    unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    imported = {name: import_texture(name, *spec) for name, spec in TEXTURES.items()}
    # Keep the wet carriageway's micro-normal response flat at this camera
    # distance; PBR roughness and restrained albedo grain carry the surface.
    make_surface_material("M_Asphalt_Smooth_V3", (0.048, 0.054, 0.059), imported["T_Asphalt_Surface_V3"],
                          imported["T_Asphalt_Normal_V3"], (12.0, 5.0), 0.0)
    make_surface_material("M_Concrete_PBR_V3", (0.20, 0.215, 0.22), imported["T_Concrete_Surface_V3"],
                          imported["T_Concrete_Normal_V3"], (7.0, 7.0), 0.0)
    make_surface_material("M_Building_PBR_V3", (0.085, 0.105, 0.12), imported["T_Facade_Surface_V3"],
                          imported["T_Facade_Normal_V3"], (8.0, 12.0), 0.08)
    # Commercial shells were nearly black under Nova City's real-time night lighting.
    # Keep the same high-resolution facade maps, but give the building layer a readable
    # midtone albedo so moon fill and street lamps retain visible material response.
    make_surface_material("M_Building_PBR_V4", (0.24, 0.27, 0.30), imported["T_Facade_Surface_V3"],
                          imported["T_Facade_Normal_V3"], (8.0, 12.0), 0.08)
    make_surface_material("M_IndustrialMetal_PBR", (0.18, 0.21, 0.24), imported["T_IndustrialMetal_Surface_V3"],
                          imported["T_IndustrialMetal_Normal_V3"], (8.0, 8.0), 0.78)
    make_surface_material("M_Rubber_PBR", (0.025, 0.028, 0.032), imported["T_Rubber_Surface_V3"],
                          imported["T_Rubber_Normal_V3"], (10.0, 4.0), 0.0)
    enable_parked_car_instancing()
    unreal.log("AFTERDARK_VISUAL_MATERIALS_READY: ten 1K PBR detail maps, five surface materials and eight instanced vehicle materials")


if __name__ == "__main__":
    main()
