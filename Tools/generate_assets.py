"""Author original, periodic BlockRealm textures and PCM16 sound effects.

Run with workstation Python (NumPy + Pillow), never inside Unreal:
    python Tools/generate_assets.py
All sources are mathematical synthesis; no downloaded textures or audio.
The official, unmodified MiSans font is copied separately with its license.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import wave

import numpy as np
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
SIZE = 512
RATE = 48000
SEED = 42137
TAU = math.tau


def smooth_noise(size: int, seed: int, cycles: float) -> np.ndarray:
    """Gaussian-filter white noise in Fourier space: exactly periodic domains."""
    random = np.random.default_rng(seed).normal(0, 1, (size, size))
    k = np.fft.fftfreq(size) * size
    window = np.exp(-(k[:, None] ** 2 + k[None, :] ** 2) / (cycles * cycles))
    noise = np.fft.ifft2(np.fft.fft2(random) * window).real
    return np.clip(noise / max(noise.std(), 1e-6) * 0.17 + 0.5, 0, 1)


def palette(amount: np.ndarray, dark, light) -> np.ndarray:
    amount = np.clip(amount, 0, 1)[..., None]
    return (np.asarray(dark) * (1 - amount) + np.asarray(light) * amount) / 255.0


def wrapped_shapes(count: int, seed: int, style: str):
    """Draw onto a 3x3 periodic atlas, preserving every edge-crossing shape."""
    rng = np.random.default_rng(seed)
    colour = Image.new("RGB", (SIZE * 3, SIZE * 3), (0, 0, 0))
    mask = Image.new("L", colour.size, 0)
    height = Image.new("L", colour.size, 0)
    painter, alpha, relief = ImageDraw.Draw(colour), ImageDraw.Draw(mask), ImageDraw.Draw(height)
    for _ in range(count):
        x, y = rng.uniform(0, SIZE, 2)
        angle = rng.uniform(0, TAU)
        if style == "grass":
            length, width = rng.uniform(3, 14), rng.uniform(0.7, 2.0)
            rgb = tuple(int(v) for v in palette(np.array(rng.uniform(0.25, 0.95)), (31, 76, 45), (131, 177, 75)) * 255)
            value = int(rng.uniform(110, 230))
        elif style == "leaf":
            length, width = rng.uniform(13, 38), rng.uniform(4, 11)
            rgb = tuple(int(v) for v in palette(np.array(rng.uniform(0.1, 0.9)), (18, 59, 39), (91, 148, 65)) * 255)
            value = int(rng.uniform(125, 235))
        else:
            length, width = rng.uniform(2, 7), rng.uniform(1, 4)
            rgb = tuple(int(v) for v in palette(np.array(rng.uniform(0.15, 0.8)), (79, 57, 38), (173, 137, 88)) * 255)
            value = int(rng.uniform(70, 175))
        u = np.array([math.cos(angle), math.sin(angle)])
        v = np.array([-u[1], u[0]])
        centre = np.array([x, y])
        if style == "grass":
            vertices = [centre - u * length / 2 - v * width, centre + u * length / 2, centre - u * length / 2 + v * width]
        else:
            vertices = [centre - u * length / 2, centre - u * length / 6 + v * width,
                        centre + u * length / 2, centre + u * length / 6 - v * width]
        for ox in (0, SIZE, SIZE * 2):
            for oy in (0, SIZE, SIZE * 2):
                shifted = [(float(p[0] + ox), float(p[1] + oy)) for p in vertices]
                painter.polygon(shifted, fill=rgb)
                alpha.polygon(shifted, fill=255 if style == "leaf" else 190)
                relief.polygon(shifted, fill=value)
                if style == "leaf":
                    line = [tuple(centre - u * length * .36 + (ox, oy)), tuple(centre + u * length * .36 + (ox, oy))]
                    painter.line(line, fill=tuple(min(255, n + 22) for n in rgb), width=1)
                    relief.line(line, fill=min(255, value + 16), width=1)
    crop = (SIZE, SIZE, SIZE * 2, SIZE * 2)
    return tuple(np.asarray(im.crop(crop), dtype=np.float64) / 255 for im in (colour, mask, height))


def normal_map(height: np.ndarray, strength: float) -> np.ndarray:
    # Coordinates are projected explicitly into the world U/V basis in the
    # material; both slopes point away from increasing height in that basis.
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * strength
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * strength
    normal = np.stack((-dx, -dy, np.ones_like(height)), axis=-1)
    normal /= np.linalg.norm(normal, axis=-1)[..., None]
    return normal * .5 + .5


def texture_data(name: str, index: int):
    coarse = smooth_noise(SIZE, SEED + index * 13, 4)
    middle = smooth_noise(SIZE, SEED + index * 13 + 1, 18)
    fine = smooth_noise(SIZE, SEED + index * 13 + 2, 75)
    rng = np.random.default_rng(SEED + index * 13 + 3)
    grain = rng.random((SIZE, SIZE))
    y, x = np.mgrid[0:SIZE, 0:SIZE] / SIZE
    strength = 5.0
    if name == "Grass":
        height = .43 * coarse + .2 * middle + .05 * fine
        rgb = palette(coarse * .55 + middle * .32 + fine * .13, (28, 77, 48), (108, 151, 68))
        paint, mask, relief = wrapped_shapes(3300, SEED + 601, "grass")
        rgb = rgb * (1 - mask[..., None] * .60) + paint * mask[..., None] * .60
        height += relief * .12
        rough = .82 + fine * .13 - mask * .07
        strength = 6
    elif name == "Dirt":
        height = .36 * coarse + .27 * middle + .10 * fine + .025 * grain
        rgb = palette(coarse * .43 + middle * .38 + fine * .19, (63, 43, 29), (144, 106, 64))
        paint, mask, relief = wrapped_shapes(1250, SEED + 602, "pebble")
        rgb = rgb * (1 - mask[..., None] * .70) + paint * mask[..., None] * .70
        height += relief * .10
        rough = .84 + middle * .12
    elif name in ("Stone", "Glow"):
        # Toroidal Voronoi seams form irregular rock fissures and mineral veins.
        points = rng.uniform(0, 1, (31, 2))
        nearest = np.full((SIZE, SIZE), 10.)
        second = nearest.copy()
        warp_x, warp_y = x + (coarse - .5) * .075, y + (middle - .5) * .045
        for px, py in points:
            dx, dy = np.abs((warp_x - px + .5) % 1 - .5), np.abs((warp_y - py + .5) % 1 - .5)
            distance = dx * dx + dy * dy
            new_second = np.where(distance < nearest, nearest, np.minimum(second, distance))
            nearest, second = np.minimum(nearest, distance), new_second
        seam = np.exp(-(second - nearest) / .0006)
        vein = np.exp(-(second - nearest) / .0017)
        bands = .5 + .5 * np.sin(TAU * (y * 5 + coarse * .16 + middle * .08))
        height = coarse * .40 + middle * .23 + fine * .07 - seam * .18 + bands * .025
        if name == "Stone":
            rgb = palette(coarse * .52 + middle * .32 + fine * .10 + bands * .06, (65, 81, 84), (146, 152, 137))
            rgb *= 1 - seam[..., None] * .42
            rgb += (vein - seam)[..., None] * np.array([.065, .046, .021])
            rough = .73 + coarse * .20
        else:
            rgb = palette(coarse * .60 + middle * .40, (92, 61, 28), (190, 135, 44))
            rgb = rgb * (1 - vein[..., None]) + palette(middle, (232, 139, 42), (255, 240, 155)) * vein[..., None]
            rough = .38 + fine * .24
        strength = 5.5
    elif name == "Sand":
        dunes = .5 + .5 * np.sin(TAU * (y * 11 + .22 * np.sin(x * TAU * 3) + coarse * .07))
        height = dunes * .065 + middle * .05 + coarse * .16 + grain * .025
        rgb = palette(coarse * .45 + middle * .28 + dunes * .17 + grain * .10, (180, 145, 91), (238, 215, 164))
        flecks = (grain > .98)[..., None]
        rgb -= flecks * .055
        rough = .87 + fine * .08
        strength = 5.2
    elif name == "Wood":
        phase = x * 29 + coarse * .19 + .1 * np.sin(y * TAU * 2)
        ridges = (.5 + .5 * np.sin(TAU * phase)) ** .45
        fine_ridges = .5 + .5 * np.sin(TAU * (x * 88 + middle * .12))
        height = ridges * .19 + fine_ridges * .045 + middle * .08
        rgb = palette(ridges * .40 + middle * .31 + coarse * .20 + fine_ridges * .09, (61, 39, 22), (161, 111, 58))
        cracks = ridges < .16
        rgb *= 1 - cracks[..., None] * .32
        rough = .69 + fine * .22
        strength = 4.5
    elif name == "WoodEnd":
        # Sine-wrapped radial coordinates keep endgrain slopes continuous at
        # the repeat boundary, including the generated normal-map edges.
        dx, dy = .5 * np.sin((x - .5) * math.pi), .5 * np.sin((y - .5) * math.pi)
        radius = np.sqrt(dx * dx + dy * dy)
        rings = .5 + .5 * np.sin(TAU * (radius * 19 + middle * .09))
        height = coarse * .12 + rings * .027 + fine * .018
        rgb = palette(rings * .21 + coarse * .35 + middle * .44, (123, 78, 39), (207, 159, 85))
        crack = (np.abs(np.sin(np.arctan2(dy, dx) * 3)) < .027) * (radius > .17)
        rgb *= 1 - crack[..., None] * .30
        rough = .76 + fine * .13
        strength = 4
    elif name == "Leaves":
        rgb = palette(coarse * .7 + middle * .3, (18, 48, 36), (46, 102, 53))
        paint, mask, relief = wrapped_shapes(1500, SEED + 605, "leaf")
        rgb = rgb * (1 - mask[..., None]) + paint * mask[..., None]
        rgb *= (.90 + fine[..., None] * .20)
        height = .16 * coarse + relief * .13 + fine * .012
        rough = .61 + fine * .26
        strength = 4.5
    elif name == "Water":
        wave_a = np.sin(TAU * (x * 5 + y * 3 + coarse * .05))
        wave_b = np.sin(TAU * (x * 9 - y * 4 + middle * .025))
        height = wave_a * .055 + wave_b * .026 + fine * .012
        rgb = palette(coarse * .6 + middle * .4, (22, 94, 104), (63, 145, 151))
        rough = .085 + fine * .07
        strength = 3.3
    elif name == "Glass":
        height = fine * .0025 + middle * .002
        rgb = palette(coarse * .7 + middle * .3, (148, 202, 206), (186, 229, 225))
        rough = .065 + fine * .048
        strength = 1.2
    else:
        raise ValueError(name)
    return np.clip(rgb, 0, 1), normal_map(height, strength), np.clip(rough, .03, .99)


def save_image(path: Path, data: np.ndarray):
    pixels = np.rint(np.clip(data, 0, 1) * 255).astype(np.uint8)
    Image.fromarray(pixels).save(path, optimize=True)
    return {
        "file": str(path.relative_to(ROOT)).replace("\\", "/"),
        "size": [SIZE, SIZE], "channels": 3 if data.ndim == 3 else 1,
        "min": int(pixels.min()), "max": int(pixels.max()),
        "stddev": round(float(pixels.std()), 3),
        "edge_delta_mean": round(float(np.abs(pixels[:, 0].astype(float) - pixels[:, -1]).mean()
                                         + np.abs(pixels[0].astype(float) - pixels[-1]).mean()) * .5, 3),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "cost_cny": 0,
    }


def textures():
    target = ROOT / "RawAssets" / "Textures"
    target.mkdir(parents=True, exist_ok=True)
    records, previews = [], []
    for index, name in enumerate(("Grass", "Dirt", "Stone", "Sand", "Wood", "Leaves", "WoodEnd", "Glass", "Water", "Glow")):
        colour, normal, rough = texture_data(name, index)
        for suffix, data in (("BC", colour), ("N", normal), ("R", rough)):
            records.append(save_image(target / f"T_{name}_{suffix}.png", data))
        previews.append((name, Image.fromarray(np.rint(colour * 255).astype(np.uint8))))
    sheet = Image.new("RGB", (300 * 5, 340 * 2), (23, 32, 35))
    draw = ImageDraw.Draw(sheet)
    # Prefer the redistributable project font so asset generation works on a
    # fresh checkout.  An environment override keeps CI and accessibility
    # builds free to choose another installed font without embedding a machine
    # specific path in the generator.
    font_candidates = []
    configured_font = os.environ.get("BLOCKREALM_FONT")
    if configured_font:
        font_candidates.append(Path(configured_font))
    font_candidates.append(ROOT / "RawAssets" / "Fonts" / "MiSans-Regular.ttf")
    windir = os.environ.get("WINDIR")
    if windir:
        font_candidates.append(Path(windir) / "Fonts" / "segoeui.ttf")
    for system_font_root in (Path("/usr/share/fonts"), Path("/usr/local/share/fonts"), Path.home() / ".fonts"):
        for system_font_name in (
            "truetype/dejavu/DejaVuSans.ttf",
            "truetype/noto/NotoSansCJK-Regular.ttc",
            "truetype/liberation2/LiberationSans-Regular.ttf",
        ):
            font_candidates.append(system_font_root / system_font_name)
    font = ImageFont.load_default()
    for font_path in font_candidates:
        if font_path.is_file():
            font = ImageFont.truetype(str(font_path), 20)
            break
    for i, (name, tile) in enumerate(previews):
        x, y = (i % 5) * 300, (i // 5) * 340
        sheet.paste(tile.resize((280, 280), Image.Resampling.LANCZOS), (x + 10, y + 10))
        draw.text((x + 12, y + 296), name, font=font, fill=(219, 234, 227))
    preview_path = ROOT / "RawAssets" / "Previews" / "TextureContactSheet.png"
    preview_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(preview_path)
    return records


def timebase(seconds):
    return np.arange(round(seconds * RATE)) / RATE


def envelope(t, decay=.08, attack=.003):
    return (1 - np.exp(-t / attack)) * np.exp(-t / decay)


def noise_sound(t, seed, low=120, high=3000):
    rng = np.random.default_rng(seed)
    white = rng.normal(0, 1, len(t))
    freqs = np.fft.rfftfreq(len(t), 1 / RATE)
    window = np.exp(-(freqs / high) ** 2) * (1 - np.exp(-(freqs / low) ** 2))
    sound = np.fft.irfft(np.fft.rfft(white) * window, n=len(t))
    return sound / max(float(sound.std()), 1e-6)


def one_shot(name, duration, seed):
    t = timebase(duration)
    noise = noise_sound(t, seed)
    if name == "MineSoil":
        audio = noise * envelope(t, .047) * .065 + np.sin(TAU * (130 * t - 95 * t * t)) * envelope(t, .052) * .15
    elif name == "MineStone":
        audio = noise * envelope(t, .045) * .06
        for frequency, amount in ((620, .15), (1110, .065), (1790, .038)):
            audio += np.sin(TAU * frequency * t) * envelope(t, .050) * amount
    elif name == "MineWood":
        audio = noise * envelope(t, .032) * .045
        for frequency, amount in ((205, .19), (440, .07), (870, .03)):
            audio += np.sin(TAU * frequency * t) * envelope(t, .035) * amount
    elif name == "PlaceBlock":
        audio = noise * envelope(t, .022) * .04 + np.sin(TAU * (170 * t - 175 * t * t)) * envelope(t, .035) * .22
    elif name == "FootstepGrass":
        audio = noise * envelope(t, .027, .008) * .056 + np.sin(TAU * 86 * t) * envelope(t, .030, .010) * .07
    elif name in ("SelectSlot", "UIConfirm"):
        pitch = 880 if name == "SelectSlot" else 660
        audio = (np.sin(TAU * pitch * t) + .14 * np.sin(TAU * pitch * 2 * t)) * envelope(t, .024, .005) * .16
    elif name == "SaveConfirm":
        audio = np.zeros_like(t)
        for at, pitch in ((.02, 660), (.11, 880), (.20, 1100)):
            st = np.maximum(t - at, 0)
            audio += (np.sin(TAU * pitch * st) + .15 * np.sin(TAU * pitch * 2 * st)) * envelope(st, .11, .004) * .15
    else:
        raise ValueError(name)
    return np.column_stack((audio, audio * .97))


def ambience():
    seconds = 24.0
    t = timebase(seconds)
    wind_l, wind_r = noise_sound(t, SEED + 901, 35, 750), noise_sound(t, SEED + 902, 40, 820)
    gust = .65 + .18 * np.sin(TAU * t / seconds) + .12 * np.sin(TAU * t * 3 / seconds + .5)
    stream = noise_sound(t, SEED + 903, 900, 5000)
    left, right = (wind_l * .009 + stream * .004) * gust, (wind_r * .009 + stream * .0035) * gust
    # Original sparse three-note chimes and birds; all tails wrap the period.
    for at, frequency, pan in ((1.0, 660, -.2), (9.2, 880, .2), (17.4, 990, -.1)):
        st = (t - at) % seconds
        note = (np.sin(TAU * frequency * st) + .18 * np.sin(TAU * frequency * 2.001 * st)) * envelope(st, .58, .04) * .022
        left += note * (1 - pan)
        right += note * (1 + pan)
    for at, pitch, pan in ((4.1, 1740, .4), (4.4, 2140, .4), (13.7, 1820, -.35), (21.2, 2370, .3)):
        st = (t - at) % seconds
        bird = np.sin(TAU * (pitch * st + 210 * st * st)) * np.sin(np.clip(st / .18, 0, 1) * math.pi) ** 2 * (st < .18) * .015
        left += bird * (1 - pan)
        right += bird * (1 + pan)
    return np.column_stack((left, right))


def export_audio(path: Path, data: np.ndarray, loop: bool):
    if not loop:
        fade = min(round(.009 * RATE), len(data) // 8)
        attack = .5 - .5 * np.cos(np.linspace(0, math.pi, fade))
        data[:fade] *= attack[:, None]
        data[-fade:] *= attack[::-1, None]
    peak = float(np.abs(data).max())
    gain = min(1., (.12 if loop else .40) / max(peak, 1e-6))
    pcm = np.rint(data * gain * 32767).astype("<i2")
    assert np.abs(pcm.astype(np.int32)).max() < 32767
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(pcm.tobytes())
    return {
        "file": str(path.relative_to(ROOT)).replace("\\", "/"), "duration_seconds": len(pcm) / RATE,
        "sample_rate": RATE, "channels": 2, "sample_width_bits": 16, "frames": len(pcm), "loop": loop,
        "peak_dbfs": round(20 * math.log10(max(peak * gain, 1e-9)), 2),
        "rms_dbfs": round(20 * math.log10(max(float(np.sqrt(np.mean((data * gain) ** 2))), 1e-9)), 2),
        "boundary_delta_pcm16": int(np.abs(pcm[0].astype(int) - pcm[-1].astype(int)).max()),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "cost_cny": 0,
    }


def audio():
    target = ROOT / "RawAssets" / "Audio"
    target.mkdir(parents=True, exist_ok=True)
    records = [export_audio(target / "AmbientValley.wav", ambience(), True)]
    for index, (name, seconds) in enumerate((
        ("MineSoil", .24), ("MineStone", .30), ("MineWood", .28), ("PlaceBlock", .22),
        ("SelectSlot", .13), ("UIConfirm", .18), ("SaveConfirm", .70), ("FootstepGrass", .16),
    )):
        records.append(export_audio(target / f"{name}.wav", one_shot(name, seconds, SEED + 800 + index), False))
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", choices=("textures", "audio", "all"), default="all")
    args = parser.parse_args()
    output = ROOT / "RawAssets" / "asset-manifest.json"
    report = json.loads(output.read_text(encoding="utf-8")) if output.exists() else {}
    if args.only in ("textures", "all"):
        report["textures"] = textures()
    if args.only in ("audio", "all"):
        report["audio"] = audio()
    report.update({"generator": "Tools/generate_assets.py", "seed": SEED, "expenditure_cny": 0,
                   "texture_world_scale_cm": 100, "license": "Original procedural game artwork and mathematical audio synthesis"})
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"textures": len(report.get("textures", [])), "audio": len(report.get("audio", [])),
                      "manifest": str(output), "expenditure_cny": 0}, ensure_ascii=False))


if __name__ == "__main__":
    main()
