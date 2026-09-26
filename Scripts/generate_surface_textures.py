"""Generate small, seamless original detail maps for Nova City's PBR surfaces.

The maps are deterministic, tileable, and intentionally subtle so they add
material response without competing with street lighting or consuming large
amounts of texture memory. Run with the repository's Python + Pillow + NumPy.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parent / "GeneratedAssets"
SIZE = 512
DETAIL_SIZE = 1024


def periodic_noise(seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    fine = rng.random((SIZE, SIZE), dtype=np.float32)
    medium = rng.random((SIZE, SIZE), dtype=np.float32)
    coarse = rng.random((SIZE, SIZE), dtype=np.float32)

    def soften(field: np.ndarray, offsets: tuple[int, ...]) -> np.ndarray:
        samples = [field]
        for offset in offsets:
            samples.extend(
                (
                    np.roll(field, offset, axis=0),
                    np.roll(field, -offset, axis=0),
                    np.roll(field, offset, axis=1),
                    np.roll(field, -offset, axis=1),
                )
            )
        return np.mean(samples, axis=0)

    fine = soften(fine, (1, 2))
    medium = soften(medium, (4, 8, 16))
    coarse = soften(coarse, (24, 48, 96, 128))
    field = fine * 0.56 + medium * 0.29 + coarse * 0.15
    field = (field - field.mean()) / max(float(field.std()), 1e-5)
    return np.clip(0.5 + field * 0.18, 0.0, 1.0)


def pack_surface(seed: int, albedo_range: tuple[float, float], roughness_range: tuple[float, float]) -> np.ndarray:
    noise = periodic_noise(seed)
    grit = periodic_noise(seed + 401)
    albedo = albedo_range[0] + noise * (albedo_range[1] - albedo_range[0])
    roughness = roughness_range[0] + grit * (roughness_range[1] - roughness_range[0])
    ao = 0.84 + periodic_noise(seed + 919) * 0.16
    return np.clip(np.stack((albedo, roughness, ao), axis=2) * 255.0, 0, 255).astype(np.uint8)


def pack_normal(seed: int, strength: float) -> np.ndarray:
    height = periodic_noise(seed)
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * strength
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * strength
    nx, ny = -dx, -dy
    nz = np.ones_like(nx)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    normal = np.stack((nx / length, ny / length, nz / length), axis=2)
    return np.clip((normal * 0.5 + 0.5) * 255.0, 0, 255).astype(np.uint8)


def save(name: str, pixels: np.ndarray) -> None:
    target = ROOT / name
    target.parent.mkdir(parents=True, exist_ok=True)
    # Match the terminal texel to the first texel so the repeat boundary stays
    # continuous even on low-quality bilinear filtering hardware.
    pixels[-1, :, :] = pixels[0, :, :]
    pixels[:, -1, :] = pixels[:, 0, :]
    image = Image.fromarray(pixels, "RGB")
    image.save(target, format="PNG", optimize=True)
    data = target.read_bytes()
    # Edge equality is a basic guard against visible repeating seams.
    if not np.array_equal(pixels[:, 0], pixels[:, -1]) or not np.array_equal(pixels[0, :], pixels[-1, :]):
        raise RuntimeError(f"Generated map is not tileable: {name}")
    print(f"{target.relative_to(Path(__file__).resolve().parent)}  sha256={hashlib.sha256(data).hexdigest()}")


def periodic_value_noise(seed: int, size: int, cells_x: int, cells_y: int) -> np.ndarray:
    """Bilinear, wrapped value noise with an exact periodic boundary."""
    rng = np.random.default_rng(seed)
    lattice = rng.random((cells_y, cells_x), dtype=np.float32)
    x = np.arange(size, dtype=np.float32) * (cells_x / size)
    y = np.arange(size, dtype=np.float32) * (cells_y / size)
    x0 = np.floor(x).astype(np.int32)
    y0 = np.floor(y).astype(np.int32)
    tx = x - x0
    ty = y - y0
    tx = tx * tx * (3.0 - 2.0 * tx)
    ty = ty * ty * (3.0 - 2.0 * ty)
    x0 %= cells_x
    y0 %= cells_y
    x1 = (x0 + 1) % cells_x
    y1 = (y0 + 1) % cells_y
    top = lattice[y0[:, None], x0[None, :]] * (1.0 - tx[None, :]) + lattice[y0[:, None], x1[None, :]] * tx[None, :]
    bottom = lattice[y1[:, None], x0[None, :]] * (1.0 - tx[None, :]) + lattice[y1[:, None], x1[None, :]] * tx[None, :]
    return top * (1.0 - ty[:, None]) + bottom * ty[:, None]


def normal_from_height(height: np.ndarray, strength: float) -> np.ndarray:
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * strength
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * strength
    nx, ny, nz = -dx, -dy, np.ones_like(dx)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.clip((np.stack((nx / length, ny / length, nz / length), axis=2) * .5 + .5) * 255.0,
                   0, 255).astype(np.uint8)


def pack_data(albedo: np.ndarray, roughness: np.ndarray, ao: np.ndarray) -> np.ndarray:
    """R=albedo multiplier, G=roughness, B=ambient occlusion; all linear data."""
    return np.clip(np.stack((albedo, roughness, ao), axis=2) * 255.0, 0, 255).astype(np.uint8)


def save_quality(name: str, pixels: np.ndarray) -> None:
    target = ROOT / name
    target.parent.mkdir(parents=True, exist_ok=True)
    if pixels.shape[:2] != (DETAIL_SIZE, DETAIL_SIZE) or pixels.dtype != np.uint8:
        raise ValueError(f"Expected a {DETAIL_SIZE}x{DETAIL_SIZE} uint8 texture: {name}")
    # Wrapped noise is periodic without forcing matching edge texels; replacing
    # one border row would create a flat seam in the tangent-space normal map.
    data_pixels = pixels.astype(np.int16)
    border_x = float(np.abs(data_pixels[:, 0] - data_pixels[:, -1]).mean())
    inner_x = float(np.abs(data_pixels[:, 1:] - data_pixels[:, :-1]).mean())
    border_y = float(np.abs(data_pixels[0, :] - data_pixels[-1, :]).mean())
    inner_y = float(np.abs(data_pixels[1:, :] - data_pixels[:-1, :]).mean())
    if border_x > max(7.0, inner_x * 2.0) or border_y > max(7.0, inner_y * 2.0):
        raise RuntimeError(
            f"Texture border discontinuity is too visible: {name} "
            f"(x {border_x:.2f}/{inner_x:.2f}, y {border_y:.2f}/{inner_y:.2f})"
        )
    if float(pixels.std()) < 2.0:
        raise RuntimeError(f"Texture map contains too little surface detail: {name}")
    image = Image.fromarray(pixels, "RGB")
    image.save(target, format="PNG", optimize=True)
    data = target.read_bytes()
    print(f"{target.relative_to(Path(__file__).resolve().parent)}  {DETAIL_SIZE}x{DETAIL_SIZE}  sha256={hashlib.sha256(data).hexdigest()}")


def generate_quality_surfaces() -> None:
    size = DETAIL_SIZE

    # Dark aggregate asphalt with broad binder variation and fine mineral grain.
    asphalt_broad = periodic_value_noise(6101, size, 8, 8)
    asphalt_mid = periodic_value_noise(6103, size, 96, 80)
    asphalt_grain = periodic_value_noise(6107, size, 512, 448)
    asphalt_height = .50 + (asphalt_broad - .5) * .10 + (asphalt_mid - .5) * .27 + (asphalt_grain - .5) * .11
    asphalt_albedo = .935 + (asphalt_broad - .5) * .035 + (asphalt_mid - .5) * .055 + (asphalt_grain - .5) * .035
    asphalt_roughness = .76 + (asphalt_broad - .5) * .11 + (asphalt_mid - .5) * .12 + (asphalt_grain - .5) * .08
    asphalt_ao = .965 - np.maximum(0., .5 - asphalt_mid) * .07
    save_quality("T_Asphalt_Surface_V3.png", pack_data(asphalt_albedo, asphalt_roughness, asphalt_ao))
    save_quality("T_Asphalt_Normal_V3.png", normal_from_height(asphalt_height, 115.0))

    # Concrete uses soft pour marks, tiny air voids and subtle height variation.
    concrete_broad = periodic_value_noise(6203, size, 7, 7)
    concrete_mid = periodic_value_noise(6209, size, 64, 64)
    concrete_pits = periodic_value_noise(6211, size, 384, 384)
    concrete_height = .5 + (concrete_broad - .5) * .09 + (concrete_mid - .5) * .20 + (concrete_pits - .5) * .06
    concrete_albedo = .965 + (concrete_broad - .5) * .04 + (concrete_mid - .5) * .045 + (concrete_pits - .5) * .025
    concrete_roughness = .91 + (concrete_broad - .5) * .05 + (concrete_mid - .5) * .07
    concrete_ao = .95 - np.maximum(0., .5 - concrete_pits) * .09
    save_quality("T_Concrete_Surface_V3.png", pack_data(concrete_albedo, concrete_roughness, concrete_ao))
    save_quality("T_Concrete_Normal_V3.png", normal_from_height(concrete_height, 76.0))

    # Facade panels have a slight cast-concrete grain and very shallow joints.
    facade_broad = periodic_value_noise(6301, size, 8, 6)
    facade_mid = periodic_value_noise(6307, size, 72, 64)
    facade_grain = periodic_value_noise(6311, size, 320, 384)
    yy = np.arange(size, dtype=np.float32)[:, None]
    xx = np.arange(size, dtype=np.float32)[None, :]
    # Place cast-panel seams at the tile's quarter points, not on its edge, so
    # both the height and its derived tangent-space normal wrap without a seam.
    x_panel = xx % 512.0
    y_panel = yy % 512.0
    vertical_joint = np.exp(-np.square(np.abs(x_panel - 256.0) / 2.1))
    horizontal_joint = np.exp(-np.square(np.abs(y_panel - 256.0) / 2.1))
    joints = np.maximum(vertical_joint, horizontal_joint)
    facade_height = .5 + (facade_broad - .5) * .065 + (facade_mid - .5) * .13 + (facade_grain - .5) * .035 - joints * .10
    facade_albedo = .96 + (facade_broad - .5) * .035 + (facade_mid - .5) * .04 - joints * .045
    facade_roughness = .78 + (facade_mid - .5) * .12 + joints * .08
    facade_ao = .97 - joints * .17 - np.maximum(0., .5 - facade_grain) * .025
    save_quality("T_Facade_Surface_V3.png", pack_data(facade_albedo, facade_roughness, facade_ao))
    save_quality("T_Facade_Normal_V3.png", normal_from_height(facade_height, 94.0))

    # Satin industrial steel is directional, with restrained brushing and seams.
    metal_long = periodic_value_noise(6401, size, 96, 12)
    metal_mid = periodic_value_noise(6407, size, 256, 64)
    metal_height = .5 + (metal_long - .5) * .08 + (metal_mid - .5) * .06
    metal_albedo = .96 + (metal_long - .5) * .07 + (metal_mid - .5) * .025
    metal_roughness = .48 + (metal_long - .5) * .18 + (metal_mid - .5) * .07
    metal_ao = .97 - np.maximum(0., .5 - metal_mid) * .045
    save_quality("T_IndustrialMetal_Surface_V3.png", pack_data(metal_albedo, metal_roughness, metal_ao))
    save_quality("T_IndustrialMetal_Normal_V3.png", normal_from_height(metal_height, 54.0))

    # Tire rubber gets a shallow, repeatable diagonal block tread and matte grain.
    rubber_grain = periodic_value_noise(6503, size, 256, 256)
    rubber_mid = periodic_value_noise(6509, size, 64, 96)
    x = np.arange(size, dtype=np.float32)[None, :]
    y = np.arange(size, dtype=np.float32)[:, None]
    tread_phase = (x + .18 * size * np.sin(2.0 * np.pi * y / 256.0)) % 128.0
    tread = np.exp(-np.square(np.minimum(tread_phase, 128.0 - tread_phase) / 4.2))
    rubber_height = .5 + (rubber_mid - .5) * .12 + (rubber_grain - .5) * .035 - tread * .10
    rubber_albedo = .975 + (rubber_mid - .5) * .035 + (rubber_grain - .5) * .025 - tread * .035
    rubber_roughness = .88 + (rubber_grain - .5) * .06 + tread * .035
    rubber_ao = .97 - tread * .11
    save_quality("T_Rubber_Surface_V3.png", pack_data(rubber_albedo, rubber_roughness, rubber_ao))
    save_quality("T_Rubber_Normal_V3.png", normal_from_height(rubber_height, 112.0))


def main() -> None:
    save("T_Asphalt_Surface_V2.png", pack_surface(4417, (0.90, 0.98), (0.58, 0.74)))
    save("T_Asphalt_Normal_V2.png", pack_normal(4529, 0.35))
    save("T_Concrete_Surface_V2.png", pack_surface(4423, (0.93, 0.99), (0.82, 0.97)))
    save("T_Concrete_Normal_V2.png", pack_normal(4531, 0.12))
    save("T_Facade_Surface_V2.png", pack_surface(4429, (0.94, 1.0), (0.58, 0.80)))
    save("T_Facade_Normal_V2.png", pack_normal(4547, 0.10))
    generate_quality_surfaces()


if __name__ == "__main__":
    main()
