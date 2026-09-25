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


def main() -> None:
    save("T_Asphalt_Surface_V2.png", pack_surface(4417, (0.90, 0.98), (0.58, 0.74)))
    save("T_Asphalt_Normal_V2.png", pack_normal(4529, 0.35))
    save("T_Concrete_Surface_V2.png", pack_surface(4423, (0.93, 0.99), (0.82, 0.97)))
    save("T_Concrete_Normal_V2.png", pack_normal(4531, 0.12))
    save("T_Facade_Surface_V2.png", pack_surface(4429, (0.94, 1.0), (0.58, 0.80)))
    save("T_Facade_Normal_V2.png", pack_normal(4547, 0.10))


if __name__ == "__main__":
    main()
