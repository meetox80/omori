import argparse
import json
import os
import struct
import sys
import tempfile

LOOP_SECONDS = 1.0
TICK = 1.0 / 60.0
BANNER_W, BANNER_H = 256, 128
LEFT, RIGHT, TOP, BOTTOM = -13.0, 13.0, 5.5, -7.5
OVERLAY_Z = 0.02

SLOTS = [
    {"name": "background", "rect": (0, 0, 256, 128), "texture": (256, 128), "frames": (0, 0), "keys": None},
    {"name": "omori1", "rect": (86, 34, 170, 128), "texture": (128, 128), "frames": (1, 0), "keys": [(0, 0), (20, 1), (40, 0), (60, 0)]},
    {"name": "omori2", "rect": (86, 34, 170, 128), "texture": (128, 128), "frames": (2, 0), "keys": [(0, 0), (40, 1), (60, 1)]},
    {"name": "lines1", "rect": (114, 0, 144, 34), "texture": (32, 64), "frames": (0, 1), "keys": [(0, 0), (15, 1), (30, 0), (45, 1), (60, 1)]},
    {"name": "lines2", "rect": (114, 0, 144, 34), "texture": (32, 64), "frames": (0, 2), "keys": [(0, 0), (30, 1), (45, 0), (60, 0)]},
]


def units_x(px):
    return LEFT + px * (RIGHT - LEFT) / BANNER_W


def units_y(py):
    return TOP - py * (TOP - BOTTOM) / BANNER_H


def build_gltf(folder, gltflib, Image):
    blob = bytearray()
    views, accessors, meshes, nodes, materials, textures, images = [], [], [], [], [], [], []

    def add_view(data, target=None):
        while len(blob) % 4:
            blob.append(0)
        views.append(gltflib.BufferView(buffer=0, byteOffset=len(blob), byteLength=len(data), target=target))
        blob.extend(data)
        return len(views) - 1

    def add_accessor(data, count, ctype, kind, target=None, lo=None, hi=None):
        accessors.append(gltflib.Accessor(bufferView=add_view(data, target), componentType=ctype, count=count, type=kind, min=lo, max=hi))
        return len(accessors) - 1

    animation_channels, animation_samplers = [], []
    for index, slot in enumerate(SLOTS):
        x0, y0, x1, y1 = slot["rect"]
        tw, th = slot["texture"]
        w = units_x(x1) - units_x(x0)
        h = units_y(y0) - units_y(y1)
        cx = (units_x(x0) + units_x(x1)) / 2
        cy = (units_y(y0) + units_y(y1)) / 2
        u1 = (x1 - x0) / tw
        v1 = (y1 - y0) / th
        corners = [(-w / 2, -h / 2, 0.0), (w / 2, -h / 2, 0.0), (w / 2, h / 2, 0.0), (-w / 2, h / 2, 0.0)]
        uvs = [(0.0, v1), (u1, v1), (u1, 0.0), (0.0, 0.0)]
        positions = b"".join(struct.pack("<3f", *c) for c in corners)
        normals = b"".join(struct.pack("<3f", 0.0, 0.0, 1.0) for _ in corners)
        coords = b"".join(struct.pack("<2f", *uv) for uv in uvs)
        indices = struct.pack("<6H", 0, 1, 2, 0, 2, 3)
        pos = add_accessor(positions, 4, 5126, "VEC3", 34962, [-w / 2, -h / 2, 0.0], [w / 2, h / 2, 0.0])
        nrm = add_accessor(normals, 4, 5126, "VEC3", 34962)
        tex = add_accessor(coords, 4, 5126, "VEC2", 34962)
        idx = add_accessor(indices, 6, 5123, "SCALAR", 34963)
        image = Image.new("RGB", (tw, th), (255, 255, 255))
        image_name = f"{slot['name']}.png"
        image.save(os.path.join(folder, image_name))
        images.append(gltflib.Image(uri=image_name, name=slot["name"]))
        textures.append(gltflib.Texture(source=len(images) - 1, sampler=0, name=slot["name"]))
        materials.append(
            gltflib.Material(
                name=slot["name"],
                alphaMode="OPAQUE",
                pbrMetallicRoughness=gltflib.PBRMetallicRoughness(baseColorTexture=gltflib.TextureInfo(index=len(textures) - 1), metallicFactor=0.0, roughnessFactor=1.0),
            )
        )
        meshes.append(
            gltflib.Mesh(
                name=slot["name"],
                primitives=[gltflib.Primitive(attributes=gltflib.Attributes(POSITION=pos, NORMAL=nrm, TEXCOORD_0=tex), indices=idx, material=len(materials) - 1)],
            )
        )
        z = 0.0 if slot["keys"] is None else OVERLAY_Z
        start_scale = 1.0 if slot["keys"] is None else 0.0
        nodes.append(gltflib.Node(name=slot["name"], mesh=len(meshes) - 1, translation=[cx, cy, z], scale=[start_scale] * 3))
        if slot["keys"]:
            times = b"".join(struct.pack("<f", tick * TICK) for tick, _ in slot["keys"])
            scales = b"".join(struct.pack("<3f", *([float(on)] * 3)) for _, on in slot["keys"])
            count = len(slot["keys"])
            t_acc = add_accessor(times, count, 5126, "SCALAR", None, [0.0], [LOOP_SECONDS])
            s_acc = add_accessor(scales, count, 5126, "VEC3")
            animation_samplers.append(gltflib.AnimationSampler(input=t_acc, output=s_acc, interpolation="STEP"))
            animation_channels.append(gltflib.Channel(sampler=len(animation_samplers) - 1, target=gltflib.Target(node=len(nodes) - 1, path="scale")))
    with open(os.path.join(folder, "banner.bin"), "wb") as f:
        f.write(blob)
    model = gltflib.GLTFModel(
        asset=gltflib.Asset(version="2.0"),
        scene=0,
        scenes=[gltflib.Scene(nodes=list(range(len(nodes))))],
        nodes=nodes,
        meshes=meshes,
        materials=materials,
        textures=textures,
        images=images,
        samplers=[gltflib.Sampler(magFilter=9729, minFilter=9729, wrapS=33071, wrapT=33071)],
        accessors=accessors,
        bufferViews=views,
        buffers=[gltflib.Buffer(byteLength=len(blob), uri="banner.bin")],
        animations=[gltflib.Animation(name="title", channels=animation_channels, samplers=animation_samplers)],
    )
    path = os.path.join(folder, "banner.gltf")
    with open(path, "w") as f:
        f.write(model.to_json())
    return gltflib.GLTF.load(path, load_file_resources=True)


def flatten_materials(cgfx, TextureCombiner):
    for name in cgfx.data.models:
        model = cgfx.data.models[name]
        for m in model.materials:
            mtob = model.materials[m]
            mtob.flags = 0
            combiners = mtob.fragment_shader.texture_combiners
            for i in range(len(combiners)):
                combiners[i] = TextureCombiner(i)
            combiners[0].src_rgb = 0x003
            combiners[0].src_alpha = 0x003


def main():
    parser = argparse.ArgumentParser(description="Generate the 3DS HOME Menu banner model the builder fills with the title screen art.")
    here = os.path.dirname(os.path.abspath(__file__))
    parser.add_argument("--pycgfx", default=os.path.join(here, "..", "third_party", "pycgfx"), help="pycgfx (https://github.com/skyfloogle/pycgfx)")
    parser.add_argument("--out", default=os.path.join(here, "..", "assets", "banner.bin"), help="where to write the banner model")
    args = parser.parse_args()
    sys.path.insert(0, os.path.abspath(args.pycgfx))
    import gltflib
    from PIL import Image
    import main as pycgfx
    from cgfx.mtob import TextureCombiner
    from cgfx.txob import TextureFormat

    with tempfile.TemporaryDirectory() as folder:
        gltf = build_gltf(folder, gltflib, Image)
        cgfx = pycgfx.convert_gltf(gltf)
    flatten_materials(cgfx, TextureCombiner)
    textures = cgfx.data.textures
    for index, slot in enumerate(SLOTS):
        txob = textures[slot["name"]]
        tw, th = slot["texture"]
        assert (txob.width, txob.height) == (tw, th)
        txob.hw_format = TextureFormat.RGB565
        txob.pixel_based_image.data = struct.pack("<H", 0x1001 + index) * (tw * th)
    data = bytearray(pycgfx.write(cgfx))
    table = []
    for index, slot in enumerate(SLOTS):
        tw, th = slot["texture"]
        pattern = struct.pack("<H", 0x1001 + index) * (tw * th)
        at = data.find(pattern)
        assert at > 0 and data.find(pattern, at + 1) < 0
        data[at:at + len(pattern)] = bytes(len(pattern))
        table.append((at, tw, th, *slot["rect"], *slot["frames"]))
    head = struct.pack("<4sI", b"OBNR", len(table)) + b"".join(struct.pack("<IHHHHHHBBxx", *t) for t in table)
    with open(args.out, "wb") as f:
        f.write(head + data)
    print(json.dumps({"size": len(data), "slots": table}))


if __name__ == "__main__":
    main()
