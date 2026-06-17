#!/usr/bin/env python3
"""Convert assets/*.glb into firmware/imu_demo/include/wiimote_mesh.h (mesh + RGB565 texture)."""

from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
REPO_ROOT = PROJECT_DIR.parent.parent
ASSETS_DIR = REPO_ROOT / "assets"
GLB_PATH = ASSETS_DIR / "Remesh_high_reduction_Remesh_Hunyuan_109965c3.glb"
OUT_PATH = PROJECT_DIR / "include" / "wiimote_mesh.h"

TARGET_TRIS = 420
TEX_SIZE = 128
# Match legacy prism half-extents after axis remap (glb Y -> model +Z length).
MODEL_HALF_DEPTH = 18.0


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def decode_png(data: bytes) -> tuple[int, int, list[tuple[int, int, int]]]:
    import zlib

    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("embedded image is not PNG")

    pos = 8
    width = height = 0
    bit_depth = color_type = 0
    idat = bytearray()

    while pos < len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        chunk_type = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, _ = struct.unpack(">IIBBBBB", chunk)
        elif chunk_type == b"IDAT":
            idat.extend(chunk)
        elif chunk_type == b"IEND":
            break

    if color_type != 2 or bit_depth != 8:
        raise ValueError("expected 8-bit RGB PNG in GLB")

    def paeth(a: int, b: int, c: int) -> int:
        p = a + b - c
        pa = abs(p - a)
        pb = abs(p - b)
        pc = abs(p - c)
        if pa <= pb and pa <= pc:
            return a
        if pb <= pc:
            return b
        return c

    raw = zlib.decompress(bytes(idat))
    stride = width * 3
    prev_row = bytearray(width * 3)
    pixels: list[tuple[int, int, int]] = []
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        row = bytearray(raw[offset : offset + stride])
        offset += stride
        if filter_type == 0:
            pass
        elif filter_type == 1:
            for i in range(stride):
                left = row[i - 3] if i >= 3 else 0
                row[i] = (row[i] + left) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                row[i] = (row[i] + prev_row[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                left = row[i - 3] if i >= 3 else 0
                up = prev_row[i]
                up_left = prev_row[i - 3] if i >= 3 else 0
                row[i] = (row[i] + paeth(left, up, up_left)) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                left = row[i - 3] if i >= 3 else 0
                up = prev_row[i]
                up_left = prev_row[i - 3] if i >= 3 else 0
                row[i] = (row[i] + ((left + up) // 2)) & 0xFF
        else:
            raise ValueError(f"unsupported PNG filter {filter_type}")
        for x in range(width):
            i = x * 3
            pixels.append((row[i], row[i + 1], row[i + 2]))
        prev_row = row
    return width, height, pixels


def downscale_rgb(
    width: int, height: int, pixels: list[tuple[int, int, int]], out_w: int, out_h: int
) -> list[tuple[int, int, int]]:
    out: list[tuple[int, int, int]] = []
    for y in range(out_h):
        sy = y * height // out_h
        for x in range(out_w):
            sx = x * width // out_w
            out.append(pixels[sy * width + sx])
    return out


def parse_glb(path: Path) -> tuple[dict, bytes]:
    data = path.read_bytes()
    if data[:4] != b"glTF":
        raise ValueError(f"{path} is not a GLB")
    jlen = struct.unpack("<I", data[12:16])[0]
    blen = struct.unpack("<I", data[16:20])[0]
    meta = json.loads(data[20 : 20 + jlen])
    bin_data = data[20 + jlen + 8 : 20 + jlen + 8 + blen]
    return meta, bin_data


def read_accessor(meta: dict, bin_data: bytes, index: int) -> list[float]:
    accessor = meta["accessors"][index]
    bv = meta["bufferViews"][accessor["bufferView"]]
    start = bv.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    count = accessor["count"]
    ctype = accessor["componentType"]
    atype = accessor["type"]
    comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[atype]
    if ctype == 5126:
        fmt = "<" + "f" * (count * comps)
        return list(struct.unpack(fmt, bin_data[start : start + count * comps * 4]))
    if ctype == 5123:
        fmt = "<" + "H" * (count * comps)
        return [float(v) for v in struct.unpack(fmt, bin_data[start : start + count * comps * 2])]
    raise ValueError(f"unsupported accessor component type {ctype}")


def tri_area(a: tuple[float, float, float], b: tuple[float, float, float], c: tuple[float, float, float]) -> float:
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    cx = uy * vz - uz * vy
    cy = uz * vx - ux * vz
    cz = ux * vy - uy * vx
    return (cx * cx + cy * cy + cz * cz) ** 0.5 * 0.5


def decimate_centroid(
    positions: list[tuple[float, float, float]],
    uvs: list[tuple[float, float]],
    indices: list[int],
    target_tris: int,
) -> tuple[list[tuple[float, float, float]], list[tuple[float, float]], list[int]]:
    tris: list[tuple[float, int, float, float, float]] = []
    for t in range(0, len(indices), 3):
        i0, i1, i2 = indices[t], indices[t + 1], indices[t + 2]
        a, b, c = positions[i0], positions[i1], positions[i2]
        cx = (a[0] + b[0] + c[0]) / 3.0
        cy = (a[1] + b[1] + c[1]) / 3.0
        cz = (a[2] + b[2] + c[2]) / 3.0
        tris.append((tri_area(a, b, c), t // 3, cx, cy, cz))

    lo = 1e-5
    hi = max(max(abs(c) for c in positions[i]) for i in range(len(positions))) * 2.0
    kept: list[int] = []
    for _ in range(24):
        mid = (lo + hi) * 0.5
        cells: dict[tuple[int, int, int], tuple[float, int]] = {}
        for area, tri_idx, cx, cy, cz in tris:
            key = (int(cx / mid), int(cy / mid), int(cz / mid))
            prev = cells.get(key)
            if prev is None or area > prev[0]:
                cells[key] = (area, tri_idx)
        kept = [tri_idx for _, tri_idx in cells.values()]
        if len(kept) > target_tris:
            lo = mid
        else:
            hi = mid
        if target_tris * 0.85 <= len(kept) <= target_tris * 1.15:
            break

    if len(kept) > target_tris:
        kept_set = set(kept)
        scored: list[tuple[float, int]] = []
        for t in range(0, len(indices), 3):
            tri_idx = t // 3
            if tri_idx not in kept_set:
                continue
            i0, i1, i2 = indices[t], indices[t + 1], indices[t + 2]
            scored.append((tri_area(positions[i0], positions[i1], positions[i2]), tri_idx))
        kept = [tri for _, tri in sorted(scored, reverse=True)[:target_tris]]

    new_positions: list[tuple[float, float, float]] = []
    new_uvs: list[tuple[float, float]] = []
    new_indices: list[int] = []
    for tri_idx in sorted(set(kept)):
        base = tri_idx * 3
        i0, i1, i2 = indices[base], indices[base + 1], indices[base + 2]
        for src in (i0, i1, i2):
            new_indices.append(len(new_positions))
            new_positions.append(positions[src])
            new_uvs.append(uvs[src])
    return new_positions, new_uvs, new_indices


def glb_to_model_positions(
    positions: list[tuple[float, float, float]],
) -> list[tuple[float, float, float]]:
    # GLB: Y = long body axis. Model: +Z front/length, +Y top, +X right.
    remapped = [(x, z, y) for x, y, z in positions]
    cx = sum(v[0] for v in remapped) / len(remapped)
    cy = sum(v[1] for v in remapped) / len(remapped)
    cz = sum(v[2] for v in remapped) / len(remapped)
    centered = [(x - cx, y - cy, z - cz) for x, y, z in remapped]
    max_z = max(abs(v[2]) for v in centered)
    scale = MODEL_HALF_DEPTH / max_z if max_z > 0 else 1.0
    return [(x * scale, y * scale, z * scale) for x, y, z in centered]


def emit_array_float(name: str, values: list[float], per_line: int = 6) -> list[str]:
    lines = [f"const float {name}[{len(values)}] PROGMEM = {{"]
    row = "  "
    for i, value in enumerate(values):
        row += f"{value:.6f}f,"
        if (i + 1) % per_line == 0:
            lines.append(row)
            row = "  "
    if row.strip():
        lines.append(row.rstrip(","))
    lines.append("};")
    return lines


def emit_array_u16(name: str, values: list[int], per_line: int = 12) -> list[str]:
    lines = [f"const uint16_t {name}[{len(values)}] PROGMEM = {{"]
    row = "  "
    for i, value in enumerate(values):
        row += f"{value},"
        if (i + 1) % per_line == 0:
            lines.append(row)
            row = "  "
    if row.strip():
        lines.append(row.rstrip(","))
    lines.append("};")
    return lines


def main() -> int:
    if not GLB_PATH.exists():
        print(f"Missing GLB: {GLB_PATH}", file=sys.stderr)
        return 1

    meta, bin_data = parse_glb(GLB_PATH)
    mesh = meta["meshes"][0]
    prim = mesh["primitives"][0]
    pos_raw = read_accessor(meta, bin_data, prim["attributes"]["POSITION"])
    uv_raw = read_accessor(meta, bin_data, prim["attributes"]["TEXCOORD_0"])
    idx_raw = [int(v) for v in read_accessor(meta, bin_data, prim["indices"])]

    positions = [(pos_raw[i], pos_raw[i + 1], pos_raw[i + 2]) for i in range(0, len(pos_raw), 3)]
    uvs = [(uv_raw[i], uv_raw[i + 1]) for i in range(0, len(uv_raw), 2)]
    positions = glb_to_model_positions(positions)
    positions, uvs, indices = decimate_centroid(positions, uvs, idx_raw, TARGET_TRIS)

    tex_bv = meta["bufferViews"][meta["images"][0]["bufferView"]]
    tex_bytes = bin_data[tex_bv["byteOffset"] : tex_bv["byteOffset"] + tex_bv["byteLength"]]
    tex_w, tex_h, tex_pixels = decode_png(tex_bytes)
    tex_pixels = downscale_rgb(tex_w, tex_h, tex_pixels, TEX_SIZE, TEX_SIZE)
    tex_rgb565 = [rgb888_to_rgb565(r, g, b) for r, g, b in tex_pixels]

    uv_u16: list[int] = []
    for u, v in uvs:
        uv_u16.append(max(0, min(65535, int(u * 65535.0 + 0.5))))
        uv_u16.append(max(0, min(65535, int((1.0 - v) * 65535.0 + 0.5))))

    pos_flat: list[float] = []
    for x, y, z in positions:
        pos_flat.extend((x, y, z))

    tri_count = len(indices) // 3
    lines: list[str] = [
        "#pragma once",
        "",
        "// Auto-generated by firmware/imu_demo/scripts/embed_glb.py",
        "#include <Arduino.h>",
        "#include <pgmspace.h>",
        "",
        "struct WiimoteMeshTexture {",
        "  const uint16_t* data;",
        "  uint16_t width;",
        "  uint16_t height;",
        "};",
        "",
        f"constexpr uint16_t kMeshVertexCount = {len(positions)};",
        f"constexpr uint16_t kMeshIndexCount = {len(indices)};",
        f"constexpr uint16_t kMeshTriangleCount = {tri_count};",
        "",
    ]
    lines.extend(emit_array_float("kMeshPos", pos_flat))
    lines.append("")
    lines.extend(emit_array_u16("kMeshUV", uv_u16))
    lines.append("")
    lines.extend(emit_array_u16("kMeshIndex", indices))
    lines.append("")
    lines.extend(
        [
            f"constexpr uint16_t kMeshTexWidth = {TEX_SIZE};",
            f"constexpr uint16_t kMeshTexHeight = {TEX_SIZE};",
            f"const uint16_t kMeshTex[{len(tex_rgb565)}] PROGMEM = {{",
        ]
    )
    row = "  "
    for i, value in enumerate(tex_rgb565):
        row += f"0x{value:04X},"
        if (i + 1) % 12 == 0:
            lines.append(row)
            row = "  "
    if row.strip():
        lines.append(row.rstrip(","))
    lines.extend(
        [
            "};",
            "",
            "constexpr WiimoteMeshTexture kMeshTexture = {kMeshTex, kMeshTexWidth, kMeshTexHeight};",
            "",
        ]
    )

    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(
        f"Wrote {OUT_PATH} ({OUT_PATH.stat().st_size} bytes) "
        f"verts={len(positions)} tris={tri_count} tex={TEX_SIZE}x{TEX_SIZE}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
