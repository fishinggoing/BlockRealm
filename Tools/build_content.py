"""Import BlockRealm content in the full, hidden Unreal 5.8.3 editor.

Launch UnrealEditor.exe with -ExecutePythonScript=<this file> -nullrhi
-unattended -nop4 -nosplash. Do not use -run=pythonscript: FontFactory needs
Slate. The host must build BlockRealmEditor before invoking this file.

Existing material graphs are preserved. A failed, incomplete graph can be
completed by adding a new connected graph; no expression deletion is used.
"""
from __future__ import annotations

import json
import math
import random
import struct
import traceback
import zlib
from pathlib import Path

import unreal


ROOT = Path(__file__).resolve().parents[1]
BASE = "/Game/BlockRealm"
OUTPUT = ROOT / "Deliverables" / "Validation" / "content-import.json"
ASSETS = unreal.AssetToolsHelpers.get_asset_tools()
LIBRARY = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary
VERSION = "BR.Pixel.World100.v4"
PIXEL_MATERIALS = ("Grass", "Dirt", "Stone", "Sand", "Wood", "Leaves", "WoodEnd")
SKY_AMBIENT_MATERIALS = ("Grass", "Dirt", "Stone", "Sand", "Wood", "Leaves")
REPORT = {"success": False, "engine": "5.8.3", "expenditure_cny": 0,
          "stage": "starting", "errors": [], "materials": [], "textures": [], "audio": []}


def log(message):
    unreal.log("BR_CONTENT " + message)


def report(stage=None):
    if stage:
        REPORT["stage"] = stage
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding="utf-8")


def import_asset(source, folder, name, factory=None):
    if not source.is_file():
        raise RuntimeError("Required source missing: " + str(source))
    task = unreal.AssetImportTask()
    for key, value in {
        "filename": str(source), "destination_path": BASE + "/" + folder,
        "destination_name": name, "automated": True,
        "replace_existing": True, "save": True,
    }.items():
        task.set_editor_property(key, value)
    if factory:
        task.set_editor_property("factory", factory)
    ASSETS.import_asset_tasks([task])
    paths = list(task.get_editor_property("imported_object_paths"))
    if not paths:
        raise RuntimeError("Import produced no asset: " + str(source))
    imported = [LIBRARY.load_asset(path) for path in paths]
    if any(asset is None for asset in imported):
        raise RuntimeError("Imported asset could not be loaded: " + str(paths))
    log("Imported " + name)
    return imported


def pixel_texture(name):
    """Original 16px tiles: small colour clusters, without photographic cracks or normal noise."""
    palettes = {
        "Grass": ((91, 149, 57), (103, 160, 66), (83, 139, 51), (112, 166, 73)),
        "Dirt": ((125, 91, 62), (137, 101, 69), (114, 81, 56), (145, 109, 76)),
        "Stone": ((135, 139, 141), (145, 149, 150), (124, 129, 132), (151, 154, 155)),
        "Sand": ((217, 207, 154), (228, 217, 165), (207, 196, 143), (222, 211, 156)),
        "Wood": ((123, 94, 60), (140, 108, 69), (105, 78, 51), (151, 117, 76)),
        "Leaves": ((65, 119, 44), (80, 135, 52), (55, 105, 38), (92, 146, 59)),
        "WoodEnd": ((170, 136, 84), (184, 149, 94), (150, 118, 72), (194, 159, 103)),
    }
    rng = random.Random(1439 + PIXEL_MATERIALS.index(name) * 107)
    palette = palettes[name]
    pixels = [[palette[0] for _ in range(16)] for _ in range(16)]
    # Overlapping 1-3 pixel marks make a quiet, readable surface rather than
    # per-pixel white noise. Modulo wrapping makes every tile seamless.
    for _ in range(64 if name == "Leaves" else 44):
        x, y = rng.randrange(16), rng.randrange(16)
        width, height = rng.choice((1, 2, 2, 3)), rng.choice((1, 1, 2))
        colour = rng.choice(palette)
        for dy in range(height):
            for dx in range(width):
                pixels[(y + dy) % 16][(x + dx) % 16] = colour
    if name == "Wood":
        # Broad, stepped bark stripes rather than fine PBR grooves.
        for x in range(16):
            for y in range(16):
                stripe = ((x + (1 if y > 10 else 0)) // 2) % 4
                pixels[y][x] = palette[stripe if rng.random() > .14 else 0]
    elif name == "WoodEnd":
        # Square growth rings keep the cut end visibly different from bark.
        for y in range(16):
            for x in range(16):
                ring = int(max(abs(x - 7.5), abs(y - 7.5)))
                pixels[y][x] = palette[2 if ring in (2, 5) else (1 if ring % 2 else 0)]
    return pixels


def write_rgb_png(path, pixels):
    """Tiny dependency-free PNG writer usable in Unreal's bundled Python."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)

    height, width = len(pixels), len(pixels[0])
    scanlines = b"".join(b"\0" + bytes(channel for rgb in row for channel in rgb) for row in pixels)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b"")
    path.write_bytes(data)


def build_pixel_texture_sources():
    folder = ROOT / "Intermediate" / "BlockRealmPixelTextures"
    folder.mkdir(parents=True, exist_ok=True)
    sources = {}
    for name in PIXEL_MATERIALS:
        maps = {"BC": pixel_texture(name),
                "N": [[(128, 128, 255)] * 16 for _ in range(16)],
                "R": [[(235, 235, 235)] * 16 for _ in range(16)]}
        for suffix, pixels in maps.items():
            stem = f"T_{name}_{suffix}"
            path = folder / (stem + ".png")
            write_rgb_png(path, pixels)
            sources[stem] = path
    REPORT["pixel_style"] = {"original_procedural_tiles": list(PIXEL_MATERIALS), "tile_pixels": 16,
                              "source_cache": str(folder), "filter": "nearest", "opaque_roughness": 235 / 255,
                              "normal_maps": "flat; block geometry supplies the face normals"}
    return sources


def build_textures():
    report("textures")
    result = {}
    pixel_sources = build_pixel_texture_sources()
    for original in sorted((ROOT / "RawAssets" / "Textures").glob("T_*.png")):
        source = pixel_sources.get(original.stem, original)
        texture = next((a for a in import_asset(source, "Textures", source.stem)
                        if isinstance(a, unreal.Texture2D)), None)
        if texture is None:
            raise RuntimeError("Texture2D import failed: " + source.name)
        suffix = source.stem.rsplit("_", 1)[1]
        texture.set_editor_property("srgb", suffix == "BC")
        texture.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
        texture.set_editor_property("address_y", unreal.TextureAddress.TA_WRAP)
        is_pixel = source.stem in pixel_sources
        texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST if is_pixel else unreal.TextureFilter.TF_DEFAULT)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
        if suffix == "N":
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
        elif suffix == "R":
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
        else:
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
            texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE if is_pixel
                                        else unreal.TextureMipGenSettings.TMGS_SHARPEN4)
        LIBRARY.save_loaded_asset(texture, only_if_is_dirty=False)
        result[source.stem] = texture
        REPORT["textures"].append(texture.get_path_name())
    required = [f"T_{name}_{suffix}" for name in
                ("Grass", "Dirt", "Stone", "Sand", "Wood", "Leaves", "WoodEnd", "Glass", "Water", "Glow")
                for suffix in ("BC", "N", "R")]
    missing = [name for name in required if name not in result]
    if missing:
        raise RuntimeError("Missing PBR textures: " + ", ".join(missing))
    return result


def node(material, cls, x=-700, y=0):
    expression = EDIT.create_material_expression(material, cls, x, y)
    if expression is None:
        raise RuntimeError("Cannot create expression " + str(cls))
    return expression


def connect(source, target, name, output=""):
    # Runtime reflection is authoritative: installed editor builds can expose
    # display labels (UVs/Tex) instead of header property names.
    names = [str(pin) for pin in EDIT.get_material_expression_input_names(target)]
    aliases = (("UVs", "Coordinates"), ("Tex", "TextureObject"))
    requested = name.casefold()
    candidates = [pin for pin in names if pin.casefold() == requested]
    for group in aliases:
        keys = {pin.casefold() for pin in group}
        if requested in keys:
            candidates.extend(pin for pin in names if pin.casefold() in keys and pin not in candidates)
    if not candidates:
        candidates = [name]
    for resolved_name in candidates:
        if EDIT.connect_material_expressions(source, output, target, resolved_name):
            return
    raise RuntimeError("Cannot connect " + name + "; attempted=" + str(candidates)
                       + "; available inputs=" + str(names))


def property_connection(source, property_type, output=""):
    if not EDIT.connect_material_property(source, output, property_type):
        raise RuntimeError("Cannot connect material property " + str(property_type))


def custom(material, description, code, output, inputs, x=-300, y=0):
    expression = node(material, unreal.MaterialExpressionCustom, x, y)
    expression.set_editor_property("description", description)
    expression.set_editor_property("code", code)
    expression.set_editor_property("output_type", output)
    pins = []
    for name in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    expression.set_editor_property("inputs", pins)
    for name, source in inputs.items():
        # Named VertexColor outputs are required here: its default output is
        # RGB, while the procedural-mesh opt-in mask lives in alpha.
        source, output_name = source if isinstance(source, tuple) else (source, "")
        connect(source, expression, name, output_name)
    return expression


def scalar(material, value, y=0):
    expression = node(material, unreal.MaterialExpressionConstant, -260, y)
    expression.set_editor_property("r", value)
    return expression


def sample(material, texture, uv, suffix, y):
    expression = node(material, unreal.MaterialExpressionTextureSample, -900, y)
    expression.set_editor_property("texture", texture)
    sampler = {"BC": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
               "N": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
               "R": unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE}[suffix]
    expression.set_editor_property("sampler_type", sampler)
    connect(uv, expression, "UVs")
    return expression


UV_CODE = """
float3 a = abs(N);
float2 uv;
if (a.z >= a.x && a.z >= a.y) uv = P.xy;
else if (a.x >= a.y) uv = P.yz;
else uv = P.xz;
return uv / 100.0;
"""
NORMAL_CODE = """
float3 a = abs(N);
float3 t = normalize(T);
if (a.z >= a.x && a.z >= a.y) return normalize(float3(t.x, t.y, t.z * sign(N.z)));
if (a.x >= a.y) return normalize(float3(t.z * sign(N.x), t.x, t.y));
return normalize(float3(t.x, t.z * sign(N.y), t.y));
"""
GRASS_CAP_CODE = """
if (N.z > 0.5) return 1.0;
if (N.z < -0.5) return 0.0;
float band = frac(P.z / 100.0 + 0.00001);
float side = abs(N.x) > 0.5 ? P.y : P.x;
float column = floor(frac(side / 100.0 + 0.00001) * 16.0);
float fringe = fmod(column * column + column * 3.0, 5.0) < 2.0 ? 12.0 / 16.0 : 14.0 / 16.0;
return step(fringe, band);
"""
SKY_AMBIENT_CODE = """
float faceWeight = FaceNormal.z > 0.5 ? 1.0 : (FaceNormal.z < -0.5 ? 0.4 : 0.75);
return BaseColor * saturate(SkyLevel) * (1.0 - saturate(VertexAlpha))
    * max(Strength, 0.0) * Tint.rgb * faceWeight;
"""


def build_sky_ambient_collection():
    report("sky_ambient_collection")
    path = BASE + "/Materials/MPC_VoxelSky"
    collection = LIBRARY.load_asset(path) if LIBRARY.does_asset_exist(path) else None
    if collection is None:
        collection = ASSETS.create_asset("MPC_VoxelSky", BASE + "/Materials",
                                         unreal.MaterialParameterCollection,
                                         unreal.MaterialParameterCollectionFactoryNew())
    if not isinstance(collection, unreal.MaterialParameterCollection):
        raise RuntimeError("Cannot create MaterialParameterCollection " + path)
    # UE 5.8's collection parameter structs/default setters are not Blueprint
    # APIs. The editor-only C++ helper preserves existing parameter GUIDs and
    # sends the collection's native pre/post-edit notifications.
    if not unreal.BlockRealmContentLibrary.configure_voxel_sky_collection(collection):
        raise RuntimeError("Voxel sky collection configuration failed")
    if not LIBRARY.save_loaded_asset(collection, only_if_is_dirty=False):
        raise RuntimeError("Voxel sky collection save failed")
    REPORT["sky_ambient"] = {
        "path": collection.get_path_name(), "mode": "surface compatibility compensation",
        "scalar_parameter": "SkyAmbientStrength", "vector_parameter": "SkyAmbientTint",
        "day_strength": 1000.0, "night_strength": 300.0,
        "day_tint": [.72, .82, 1.0], "night_tint": [.45, .58, .86],
        "face_weights": {"up": 1.0, "side": .75, "down": .4},
        "materials": list(SKY_AMBIENT_MATERIALS),
        "vertex_color": {"r": "quantized face-adjacent sky level / 15", "a": "0 opts in; default static-mesh 1 opts out"},
    }
    log("Voxel sky parameter collection saved: " + path)
    return collection


def collection_parameter(material, collection, parameter_name, x, y):
    expression = node(material, unreal.MaterialExpressionCollectionParameter, x, y)
    expression.set_editor_property("collection", collection)
    # set_editor_property calls PostEditChangeProperty, which resolves the
    # parameter GUID from ParameterName in UE 5.8.
    expression.set_editor_property("parameter_name", parameter_name)
    return expression


def make_material(name, textures, sky_collection):
    asset_name = "M_" + name
    path = BASE + "/Materials/" + asset_name
    material = LIBRARY.load_asset(path) if LIBRARY.does_asset_exist(path) else None
    if material and LIBRARY.get_metadata_tag(material, "BlockRealmMaterialVersion") == VERSION:
        errors = [str(e) for e in EDIT.recompile_material(material)]
        if errors:
            raise RuntimeError(asset_name + " compiler errors: " + "; ".join(errors))
        REPORT["materials"].append({"path": path, "version": VERSION, "preserved": True, "compiler_errors": errors})
        return material
    if not material:
        material = ASSETS.create_asset(asset_name, BASE + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Cannot create " + asset_name)
    # Never delete old expressions: earlier engine versions crashed doing this.
    material.set_editor_property("tangent_space_normal", False)
    material.set_editor_property("two_sided", name in ("Leaves", "Glass", "Water"))
    if name in ("Glass", "Water"):
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    position = node(material, unreal.MaterialExpressionWorldPosition, -1800, -100)
    normal = node(material, unreal.MaterialExpressionVertexNormalWS, -1800, 80)
    uv_inputs = {"P": position, "N": normal}
    uv_code = UV_CODE
    if name == "Water":
        clock = node(material, unreal.MaterialExpressionTime, -1800, 260)
        uv_inputs["Time"] = clock
        uv_code = UV_CODE.replace("return uv / 100.0;", "return uv / 360.0 + float2(Time * 0.012, Time * -0.009);")
    uv = custom(material, "Absolute world projection; 100cm repeat; greedy-mesh safe", uv_code,
                unreal.CustomMaterialOutputType.CMOT_FLOAT2, uv_inputs, -1450, 0)
    tex = {suffix: sample(material, textures[f"T_{name}_{suffix}"], uv, suffix, index * 240)
           for index, suffix in enumerate(("BC", "N", "R"))}
    colour, tangent_normal, roughness = tex["BC"], tex["N"], tex["R"]
    rough_output = "R"
    if name in ("Grass", "Wood"):
        side_name = "Dirt" if name == "Grass" else "WoodEnd"
        alternate = {suffix: sample(material, textures[f"T_{side_name}_{suffix}"], uv, suffix, 760 + index * 240)
                     for index, suffix in enumerate(("BC", "N", "R"))}
        cap = custom(material, "Grass top and green side fringe" if name == "Grass" else "Endgrain on horizontal wood faces",
                     GRASS_CAP_CODE if name == "Grass" else "return step(0.5, abs(N.z));",
                     unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                     {"P": position, "N": normal}, -950, -230)
        # Grass starts with dirt sides; wood starts with bark sides.
        first, second = (alternate, tex) if name == "Grass" else (tex, alternate)
        colour = custom(material, "PBR colour face blend", "return lerp(A, B, Alpha);",
                        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                        {"A": first["BC"], "B": second["BC"], "Alpha": cap}, -500, 0)
        tangent_normal = custom(material, "PBR normal face blend", "return normalize(lerp(A, B, Alpha));",
                                unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                                {"A": first["N"], "B": second["N"], "Alpha": cap}, -500, 220)
        roughness = custom(material, "PBR roughness face blend", "return lerp(A.r, B.r, Alpha);",
                           unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                           {"A": first["R"], "B": second["R"], "Alpha": cap}, -500, 440)
        rough_output = ""
    if name not in ("Glass", "Water", "Glow"):
        colour = custom(material, "Subtle valley-scale albedo variation",
                        "float m = 0.94 + 0.06 * (0.5 + 0.5 * sin(P.x * 0.0017 + sin(P.y * 0.0011))); return C * m;",
                        unreal.CustomMaterialOutputType.CMOT_FLOAT3, {"P": position, "C": colour}, -220, -60)
    world_normal = custom(material, "Project sampled normal into the face world basis", NORMAL_CODE,
                          unreal.CustomMaterialOutputType.CMOT_FLOAT3, {"N": normal, "T": tangent_normal}, -220, 220)
    if name in ("Leaves", "Glass", "Water"):
        # UE flips tangent-space normals automatically, but these DefaultLit
        # materials supply world-space normals and must orient backfaces once.
        face_sign = node(material, unreal.MaterialExpressionTwoSidedSign, -220, 1380)
        oriented_normal = node(material, unreal.MaterialExpressionMultiply, 40, 220)
        connect(world_normal, oriented_normal, "A")
        connect(face_sign, oriented_normal, "B")
        world_normal = oriented_normal
    property_connection(colour, unreal.MaterialProperty.MP_BASE_COLOR)
    property_connection(world_normal, unreal.MaterialProperty.MP_NORMAL)
    property_connection(roughness, unreal.MaterialProperty.MP_ROUGHNESS, rough_output)
    property_connection(scalar(material, 0.0, 670), unreal.MaterialProperty.MP_METALLIC)
    property_connection(scalar(material, .48 if name in ("Water", "Glass") else .12, 760), unreal.MaterialProperty.MP_SPECULAR)
    if name in ("Glass", "Water"):
        view = node(material, unreal.MaterialExpressionCameraVectorWS, -1700, -330)
        if name == "Glass":
            opacity_code = """
float3 a = abs(N);
float2 uv = (a.z > 0.5) ? P.xy : ((a.x > 0.5) ? P.yz : P.xz);
float2 fuv = frac(uv / 100.0 + 0.00001);
float2 edges = min(fuv, 1.0 - fuv);
float edge = 1.0 - smoothstep(0.004, 0.028, min(edges.x, edges.y));
float fresnel = pow(1.0 - abs(dot(normalize(N), normalize(V))), 5.0);
return saturate(0.12 + edge * 0.25 + fresnel * 0.47);
"""
        else:
            opacity_code = "float f = pow(1.0 - abs(dot(normalize(N), normalize(V))), 5.0); return 0.48 + f * 0.28;"
        opacity = custom(material, "Fresnel transparency with readable glass edges" if name == "Glass" else "Lake surface Fresnel",
                         opacity_code, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                         {"P": position, "N": normal, "V": view}, -180, 980)
        property_connection(opacity, unreal.MaterialProperty.MP_OPACITY)
        property_connection(scalar(material, 1.035 if name == "Glass" else 1.015, 1190), unreal.MaterialProperty.MP_REFRACTION)
    if name == "Glow":
        emission = custom(material, "Warm emissive mineral fissures; physical daylight exposure",
                          "float veins = smoothstep(0.22, 0.72, C.r + C.g * 0.28); return C * (420.0 + veins * 8500.0);",
                          unreal.CustomMaterialOutputType.CMOT_FLOAT3, {"C": colour}, -200, 1000)
        property_connection(emission, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if name in SKY_AMBIENT_MATERIALS:
        vertex_colour = node(material, unreal.MaterialExpressionVertexColor, -1200, 1650)
        sky_strength = collection_parameter(material, sky_collection, "SkyAmbientStrength", -1200, 1810)
        sky_tint = collection_parameter(material, sky_collection, "SkyAmbientTint", -1200, 1970)
        # This bounded surface fill uses the final blended/varied albedo and
        # geometric face normal; it is a compatibility approximation of sky
        # illumination for runtime voxel meshes, not a replacement GI solver.
        emission = custom(material, "Voxel sky surface ambient; VertexColor R=sky/15 A=opt-out",
                          SKY_AMBIENT_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                          {"BaseColor": colour, "FaceNormal": normal,
                           "SkyLevel": (vertex_colour, "R"), "VertexAlpha": (vertex_colour, "A"),
                           "Strength": sky_strength, "Tint": sky_tint}, -200, 1650)
        property_connection(emission, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    compiler_errors = [str(e) for e in EDIT.recompile_material(material)]
    if compiler_errors:
        raise RuntimeError(asset_name + " compiler errors: " + "; ".join(compiler_errors))
    LIBRARY.set_metadata_tag(material, "BlockRealmMaterialVersion", VERSION)
    LIBRARY.set_metadata_tag(material, "BlockRealmWorldRepeatCm", "360" if name == "Water" else "100")
    if name in SKY_AMBIENT_MATERIALS:
        LIBRARY.set_metadata_tag(material, "BlockRealmSkyAmbientCollection", sky_collection.get_path_name())
    LIBRARY.save_loaded_asset(material, only_if_is_dirty=False)
    REPORT["materials"].append({"path": path, "version": VERSION, "preserved": False,
                                "world_repeat_cm": 360 if name == "Water" else 100,
                                "expressions": EDIT.get_num_material_expressions(material), "compiler_errors": compiler_errors})
    log("Material saved: " + asset_name)
    return material


def build_materials(textures, sky_collection):
    report("materials")
    for name in ("Grass", "Dirt", "Stone", "Sand", "Wood", "Leaves", "Glass", "Water", "Glow"):
        make_material(name, textures, sky_collection)
        report()
    build_creature_material()


def build_creature_material():
    """A shared matte material for block creatures; each part sets its Color parameter."""
    name, path = "M_Creature", BASE + "/Materials/M_Creature"
    material = LIBRARY.load_asset(path) if LIBRARY.does_asset_exist(path) else None
    if material and LIBRARY.get_metadata_tag(material, "BlockRealmMaterialVersion") == VERSION:
        errors = [str(e) for e in EDIT.recompile_material(material)]
    else:
        if material is None:
            material = ASSETS.create_asset(name, BASE + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            raise RuntimeError("Cannot create " + path)
        colour = node(material, unreal.MaterialExpressionVectorParameter, -350, 0)
        colour.set_editor_property("parameter_name", "Color")
        colour.set_editor_property("default_value", unreal.LinearColor(.62, .44, .28, 1.0))
        property_connection(colour, unreal.MaterialProperty.MP_BASE_COLOR)
        property_connection(scalar(material, .82, 160), unreal.MaterialProperty.MP_ROUGHNESS)
        property_connection(scalar(material, 0.0, 320), unreal.MaterialProperty.MP_METALLIC)
        property_connection(scalar(material, .18, 480), unreal.MaterialProperty.MP_SPECULAR)
        errors = [str(e) for e in EDIT.recompile_material(material)]
    if errors:
        raise RuntimeError(name + " compiler errors: " + "; ".join(errors))
    LIBRARY.set_metadata_tag(material, "BlockRealmMaterialVersion", VERSION)
    LIBRARY.save_loaded_asset(material, only_if_is_dirty=False)
    REPORT["materials"].append({"path": path, "version": VERSION, "vector_parameter": "Color",
                                "roughness": .82, "compiler_errors": errors})


def build_font():
    report("font")
    factory = unreal.FontFileImportFactory()
    factory.set_editor_property("batch_create_font_asset", unreal.BatchCreateFontAsset.NO)
    face = next((asset for asset in import_asset(ROOT / "RawAssets" / "Fonts" / "MiSans-Regular.ttf",
                                               "UI", "FF_MiSans", factory)
                 if isinstance(asset, unreal.FontFace)), None)
    if face is None:
        raise RuntimeError("MiSans FontFace import failed")
    path = BASE + "/UI/F_MiSans"
    font = LIBRARY.load_asset(path) if LIBRARY.does_asset_exist(path) else None
    if not font:
        font = ASSETS.create_asset("F_MiSans", BASE + "/UI", unreal.Font, unreal.FontFactory())
    if not unreal.BlockRealmContentLibrary.configure_chinese_font(font, face):
        raise RuntimeError("Runtime composite MiSans font configuration failed")
    LIBRARY.save_loaded_asset(face, only_if_is_dirty=False)
    LIBRARY.save_loaded_asset(font, only_if_is_dirty=False)
    REPORT["font"] = {"font": font.get_path_name(), "face": face.get_path_name(),
                      "copyright": "Copyright © 2020-2025 Beijing Xiaomi Mobile Software Co.,Ltd. All Rights Reserved."}


def build_audio():
    report("audio")
    for source in sorted((ROOT / "RawAssets" / "Audio").glob("*.wav")):
        sound = next((a for a in import_asset(source, "Audio", source.stem)
                      if isinstance(a, unreal.SoundWave)), None)
        if sound is None:
            raise RuntimeError("SoundWave import failed: " + source.name)
        sound.set_editor_property("looping", source.stem == "AmbientValley")
        LIBRARY.save_loaded_asset(sound, only_if_is_dirty=False)
        REPORT["audio"].append(sound.get_path_name())
    if len(REPORT["audio"]) < 9:
        raise RuntimeError("Original sound set is incomplete")


def set_properties(obj, values):
    for name, value in values.items():
        obj.set_editor_property(name, value)


def build_map():
    report("map")
    path = BASE + "/Maps/L_Wilds"
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if LIBRARY.does_asset_exist(path):
        if not levels.load_level(path):
            raise RuntimeError("Cannot load L_Wilds")
    elif not levels.new_level(path):
        raise RuntimeError("Cannot create L_Wilds")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in list(actors.get_all_level_actors()):
        if actor.get_actor_label().startswith("BR_"):
            actors.destroy_actor(actor)

    def spawn(cls, label, location, rotation=None):
        pitch, yaw, roll = rotation or (0.0, 0.0, 0.0)
        # Python Rotator's native make function takes roll/pitch/yaw, unlike
        # C++ FRotator. Named axes prevent a below-horizon sun.
        orientation = unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll)
        actor = actors.spawn_actor_from_class(cls, unreal.Vector(*location), orientation)
        if actor is None:
            raise RuntimeError("Cannot spawn " + label)
        actor.set_actor_label("BR_" + label)
        return actor

    # Light the valley faces seen from the southwest title camera.
    sun = spawn(unreal.DirectionalLight, "MorningSun", (0, 0, 6000), (-34, 40, 0))
    sunlight = sun.get_component_by_class(unreal.DirectionalLightComponent)
    set_properties(sunlight, {
        "mobility": unreal.ComponentMobility.MOVABLE, "intensity": 32000.0,
        "light_color": unreal.Color(255, 245, 230, 255), "atmosphere_sun_light": True,
        "light_source_angle": 1.0, "cast_shadows": True,
        "indirect_lighting_intensity": 1.0, "volumetric_scattering_intensity": .45,
    })
    sky = spawn(unreal.SkyAtmosphere, "SkyAtmosphere", (0, 0, 0))
    atmosphere = sky.get_component_by_class(unreal.SkyAtmosphereComponent)
    # These are physical coefficient magnitudes, not 1.0-based multipliers.
    # UE's Earth defaults are Rayleigh=.033100 and Mie=.003996. Using 1.05
    # and .65 respectively made the air ~32x/163x denser and extinguished sun.
    set_properties(atmosphere, {"rayleigh_scattering_scale": .033100, "mie_scattering_scale": .003000,
                                "mie_anisotropy": .78, "multi_scattering_factor": 1.15,
                                "aerial_perspective_start_depth": .06})
    sky_light = spawn(unreal.SkyLight, "CoolSkyFill", (0, 0, 6000))
    sky_component = sky_light.get_component_by_class(unreal.SkyLightComponent)
    set_properties(sky_component, {"mobility": unreal.ComponentMobility.MOVABLE, "intensity": 1.4,
                                   "real_time_capture": True, "light_color": unreal.Color(242, 248, 255, 255)})
    fog = spawn(unreal.ExponentialHeightFog, "ValleyMist", (0, 0, 1500))
    fog_component = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    set_properties(fog_component, {
        "fog_density": .015, "fog_height_falloff": .32,
        "fog_inscattering_luminance": unreal.LinearColor(.42, .55, .65, 1),
        "start_distance": 3600.0, "fog_max_opacity": .88,
        "enable_volumetric_fog": True, "volumetric_fog_distance": 16000.0,
        "volumetric_fog_extinction_scale": .50,
        "volumetric_fog_scattering_distribution": .2,
        "volumetric_fog_albedo": unreal.Color(229, 238, 246, 255),
    })
    post = spawn(unreal.PostProcessVolume, "StableExposure", (0, 0, 0))
    post.set_editor_property("unbound", True)
    settings = post.get_editor_property("settings")
    effects = {
        "auto_exposure_method": unreal.AutoExposureMethod.AEM_MANUAL,
        "auto_exposure_apply_physical_camera_exposure": True,
        "camera_iso": 100.0, "camera_shutter_speed": 100.0, "depth_of_field_fstop": 5.6,
        "auto_exposure_bias": .20,
        "bloom_intensity": .08, "vignette_intensity": .04, "motion_blur_amount": 0.0,
        "film_grain_intensity": 0.0, "scene_fringe_intensity": 0.0, "lens_flare_intensity": 0.0,
        "depth_of_field_focal_distance": 0.0,
        "color_saturation": unreal.Vector4(1.02, 1.02, 1.02, 1.0),
        "color_contrast": unreal.Vector4(1.0, 1.0, 1.0, 1.0),
    }
    for name, value in effects.items():
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    post.set_editor_property("settings", settings)
    spawn(unreal.PlayerStart, "PlayerStart", (50, 50, 4200))
    game_mode = unreal.load_class(None, "/Script/BlockRealm.BlockRealmGameMode")
    if game_mode is None:
        raise RuntimeError("Compiled BlockRealmGameMode class missing")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        unreal.Vector(-2600, -2600, 5000), unreal.Rotator(pitch=-24, yaw=45, roll=0))
    if not levels.save_current_level():
        raise RuntimeError("L_Wilds save failed")
    actual_sun_rotation = sun.get_actor_rotation()
    REPORT["map"] = {"path": path, "actors": [a.get_actor_label() for a in actors.get_all_level_actors()],
                      "dynamic_world": "Generated by BlockRealmGameMode at runtime",
                      "sun_lux": sunlight.get_editor_property("intensity"),
                      "sun_rotation": [actual_sun_rotation.pitch, actual_sun_rotation.yaw, actual_sun_rotation.roll],
                      "sun_rotation_axis_order": ["pitch", "yaw", "roll"],
                      "atmosphere": {"rayleigh_scattering_scale": .033100, "mie_scattering_scale": .003000},
                      "sky_light_intensity": 1.4,
                      "fog_density": fog_component.get_editor_property("fog_density"),
                      "fog_start_cm": fog_component.get_editor_property("start_distance"),
                      "fog_max_opacity": fog_component.get_editor_property("fog_max_opacity"),
                      "volumetric_fog_distance_cm": fog_component.get_editor_property("volumetric_fog_distance"),
                      "recommended_cached_lighting_pre_exposure_ev": 8,
                      "exposure": {"method": "manual physical", "iso": 100, "shutter_inverse_seconds": 100,
                                   "fstop": 5.6, "bias_stops": .20,
                                   "camera_ev100": round(math.log2(5.6 ** 2 * 100.0), 4),
                                   "effective_manual_ev100": round(math.log2(5.6 ** 2 * 100.0) - .20, 4)}}
    log("L_Wilds morning sky map saved")


def main():
    report()
    textures = build_textures()
    sky_collection = build_sky_ambient_collection()
    build_materials(textures, sky_collection)
    build_font()
    build_audio()
    build_map()
    report("saving")
    if not LIBRARY.save_directory(BASE, only_if_is_dirty=False, recursive=True):
        raise RuntimeError("Content directory save failed")
    REPORT["assets"] = [str(a) for a in LIBRARY.list_assets(BASE, recursive=True, include_folder=False)]
    REPORT["success"] = True
    report("complete")
    log("COMPLETE: " + str(len(REPORT["assets"])) + " assets; CNY 0")


def request_exit():
    """Request a deferred full-editor exit after writing the final report."""
    unreal.SystemLibrary.quit_editor()


try:
    main()
except Exception:
    details = traceback.format_exc()
    REPORT["success"] = False
    REPORT["errors"].append(details)
    REPORT["error"] = details
    unreal.log_error("BR_CONTENT_FAILED " + details)
    report()
finally:
    request_exit()
