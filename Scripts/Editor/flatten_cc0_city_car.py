"""Flatten the publisher's glTF default pose to one material-grouped OBJ for static traffic dressing."""

import hashlib
import json
import math
import struct
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
SOURCE = PROJECT_ROOT / "Content/ExternalAssets/3DAssetsDev/CityCar/city_car.glb"
OUTPUT = PROJECT_ROOT / "Content/ExternalAssets/3DAssetsDev/CityCar/static"
EXPECTED_SHA256 = "6246ac14c89d41c31714cd3524c3e07d5270ad63038bfc38a8d7fbd6435c4877"


def matrix_multiply(a, b):
    return [[sum(a[row][k] * b[k][column] for k in range(4)) for column in range(4)] for row in range(4)]


def node_matrix(node):
    translation = node.get("translation", [0.0, 0.0, 0.0])
    scale = node.get("scale", [1.0, 1.0, 1.0])
    x, y, z, w = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
    rotation = [
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0.0],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0.0],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]
    scale_matrix = [[scale[0], 0, 0, 0], [0, scale[1], 0, 0], [0, 0, scale[2], 0], [0, 0, 0, 1]]
    translation_matrix = [[1, 0, 0, translation[0]], [0, 1, 0, translation[1]], [0, 0, 1, translation[2]], [0, 0, 0, 1]]
    return matrix_multiply(translation_matrix, matrix_multiply(rotation, scale_matrix))


def read_accessor(document, binary, accessor_index):
    accessor = document["accessors"][accessor_index]
    view = document["bufferViews"][accessor["bufferView"]]
    component = accessor["componentType"]
    component_formats = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
    formats = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
    if component not in component_formats or accessor["type"] not in formats:
        raise RuntimeError(f"Unsupported glTF accessor {accessor_index}: {accessor}")
    width = formats[accessor["type"]]
    code = component_formats[component]
    packed = struct.Struct("<" + code * width)
    stride = view.get("byteStride", packed.size)
    offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    values = []
    for index in range(accessor["count"]):
        row = packed.unpack_from(binary, offset + index * stride)
        if accessor.get("normalized"):
            if component in (5120, 5122):
                maximum = 127.0 if component == 5120 else 32767.0
                row = tuple(max(value / maximum, -1.0) for value in row)
            elif component in (5121, 5123):
                maximum = 255.0 if component == 5121 else 65535.0
                row = tuple(value / maximum for value in row)
        values.append(row)
    return values


def transform_point(matrix, point):
    x, y, z = point
    return (
        matrix[0][0] * x + matrix[0][1] * y + matrix[0][2] * z + matrix[0][3],
        matrix[1][0] * x + matrix[1][1] * y + matrix[1][2] * z + matrix[1][3],
        matrix[2][0] * x + matrix[2][1] * y + matrix[2][2] * z + matrix[2][3],
    )


def transform_normal(matrix, normal):
    # The source uses uniform scales for each car part; normalize after rotation.
    vector = tuple(sum(matrix[row][column] * normal[column] for column in range(3)) for row in range(3))
    magnitude = math.sqrt(sum(component * component for component in vector))
    if magnitude <= 1e-8:
        return (0.0, 0.0, 1.0)
    return tuple(component / magnitude for component in vector)


def convert_axis(vector):
    # glTF is +Y up/+Z forward; the game is +Z up/+X forward/+Y right.
    return (vector[2] * 100.0, vector[0] * 100.0, vector[1] * 100.0)


def material_name(document, index):
    material = document.get("materials", [])[index]
    source_name = material.get("name", f"material_{index}")
    safe_name = "".join(char if char.isalnum() or char == "_" else "_" for char in source_name)
    return f"citycar_{index}_{safe_name}"


def main():
    if not SOURCE.is_file():
        raise RuntimeError(f"Missing GLB: {SOURCE}")
    source_bytes = SOURCE.read_bytes()
    source_hash = hashlib.sha256(source_bytes).hexdigest()
    if source_hash != EXPECTED_SHA256:
        raise RuntimeError(f"CC0 model changed unexpectedly: {source_hash}")
    magic, version, total_length = struct.unpack_from("<4sII", source_bytes, 0)
    if magic != b"glTF" or version != 2 or total_length != len(source_bytes):
        raise RuntimeError("Source is not a complete GLB 2.0 container.")
    json_length, json_type = struct.unpack_from("<I4s", source_bytes, 12)
    if json_type != b"JSON":
        raise RuntimeError("GLB JSON chunk is missing.")
    document = json.loads(source_bytes[20:20 + json_length].decode("utf-8"))
    binary_header = 20 + json_length
    binary_length, binary_type = struct.unpack_from("<I4s", source_bytes, binary_header)
    if binary_type != b"BIN\x00":
        raise RuntimeError("GLB binary geometry chunk is missing.")
    binary = source_bytes[binary_header + 8:binary_header + 8 + binary_length]

    OUTPUT.mkdir(parents=True, exist_ok=True)
    obj_path = OUTPUT / "city_car_static.obj"
    mtl_path = OUTPUT / "city_car_static.mtl"
    vertices_written = 0
    normals_written = 0
    triangles_written = 0
    bounds_min = [float("inf")] * 3
    bounds_max = [float("-inf")] * 3
    obj = [
        "# Flattened default pose from a CC0 glTF city car; animations intentionally stripped.",
        "mtllib city_car_static.mtl",
        "o city_car_static",
        "",
    ]

    def emit_mesh(node, world_matrix):
        nonlocal vertices_written, normals_written, triangles_written
        mesh = document["meshes"][node["mesh"]]
        for primitive in mesh["primitives"]:
            if primitive.get("mode", 4) != 4:
                raise RuntimeError(f"Only triangle-list primitives are supported: {mesh.get('name')}")
            positions = read_accessor(document, binary, primitive["attributes"]["POSITION"])
            normals = read_accessor(document, binary, primitive["attributes"]["NORMAL"])
            indices = read_accessor(document, binary, primitive["indices"])
            if len(positions) != len(normals) or len(indices) % 3:
                raise RuntimeError(f"Invalid vertex/index data in {mesh.get('name')}")
            base_vertex = vertices_written
            material = primitive.get("material", 0)
            obj.append(f"usemtl {material_name(document, material)}")
            for point in positions:
                source_point = transform_point(world_matrix, point)
                converted = convert_axis(source_point)
                vertices_written += 1
                for axis, component in enumerate(converted):
                    bounds_min[axis] = min(bounds_min[axis], component)
                    bounds_max[axis] = max(bounds_max[axis], component)
                obj.append("v %.6f %.6f %.6f" % converted)
                obj.append("vt %.6f %.6f" % (converted[0] / 100.0, converted[1] / 100.0))
            for normal in normals:
                obj.append("vn %.7f %.7f %.7f" % convert_axis(transform_normal(world_matrix, normal)))
                normals_written += 1
            for start in range(0, len(indices), 3):
                face = [int(indices[start + offset][0]) + 1 + base_vertex for offset in range(3)]
                obj.append("f " + " ".join(f"{vertex}/{vertex}/{vertex}" for vertex in face))
                triangles_written += 1
            obj.append("")

    def visit(node_index, parent_matrix):
        node = document["nodes"][node_index]
        world_matrix = matrix_multiply(parent_matrix, node_matrix(node))
        if "mesh" in node:
            emit_mesh(node, world_matrix)
        for child in node.get("children", []):
            visit(child, world_matrix)

    scene_index = document.get("scene", 0)
    for root_index in document["scenes"][scene_index]["nodes"]:
        visit(root_index, [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]])
    if triangles_written < 20000 or triangles_written > 50000 or vertices_written != normals_written:
        raise RuntimeError(f"Unexpected flattened geometry: {triangles_written} triangles, {vertices_written} vertices.")
    dimensions = [bounds_max[i] - bounds_min[i] for i in range(3)]
    if not (350 <= dimensions[0] <= 370 and 175 <= dimensions[1] <= 190 and 145 <= dimensions[2] <= 165):
        raise RuntimeError(f"Converted axes/units look wrong; bounds are {dimensions} cm.")

    mtl = ["# Base PBR colors from the source glTF; specular converted for OBJ import."]
    for index, material in enumerate(document.get("materials", [])):
        pbr = material.get("pbrMetallicRoughness", {})
        color = pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0])
        roughness = max(0.04, min(1.0, pbr.get("roughnessFactor", 0.6)))
        metallic = max(0.0, min(1.0, pbr.get("metallicFactor", 0.0)))
        mtl.extend((
            f"newmtl {material_name(document, index)}",
            "Kd %.6f %.6f %.6f" % tuple(color[:3]),
            "Ks %.6f %.6f %.6f" % ((0.04 + 0.3 * metallic,) * 3),
            f"Ns {max(1.0, 2.0 / (roughness * roughness) - 2.0):.3f}",
            f"d {max(0.0, min(1.0, color[3])):.5f}",
            "illum 2",
            "",
        ))
    obj_path.write_text("\n".join(obj) + "\n", encoding="ascii")
    mtl_path.write_text("\n".join(mtl) + "\n", encoding="ascii")
    print(
        f"PASS: flattened {triangles_written} triangles / {vertices_written} vertices; "
        f"bounds {dimensions[0]:.1f}x{dimensions[1]:.1f}x{dimensions[2]:.1f} cm; "
        f"OBJ SHA-256 {hashlib.sha256(obj_path.read_bytes()).hexdigest()}"
    )


if __name__ == "__main__":
    main()
