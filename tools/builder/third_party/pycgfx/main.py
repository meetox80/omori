#!/usr/bin/env python3

from cgfx.cgfx import CGFX
from cgfx.cmdl import CMDL, CMDLWithSkeleton
from cgfx.shared import StringTable, Vector3, Vector4, Matrix
from cgfx.dict import DictInfo
from cgfx.txob import ImageTexture, PixelBasedImage, ReferenceTexture
from cgfx.sobj import (
    SOBJMesh,
    SOBJShape,
    SOBJSkeleton,
    Bone,
    BillboardMode,
    BoneFlag,
    SkeletonFlag,
)
from cgfx.primitives import (
    Primitive,
    PrimitiveSet,
    InterleavedVertexStream,
    IndexStream,
    VertexStream,
    VertexAttributeUsage,
    VertexAttributeFlag,
    DataType,
    VertexParamAttribute,
)
from cgfx.mtob import (
    MTOB,
    BlendEquation,
    BlendFunction,
    ColorFloat,
    DepthFlag,
    TexInfo,
    PicaCommand,
    LinkedShader,
    LightingLookupTable,
    MTOBFlag,
    ConstantColorSource,
    BumpMode,
    FragmentLightingFlags,
)
from cgfx.animation import (
    GraphicsAnimationGroup,
    AnimationGroupMember,
    AnimationGroupMemberType,
)
from cgfx.luts import LUTS, LutTable
from cgfx.cenv import CENV, CENVLight, CENVLightSet
from cgfx.cflt import CFLT
from cgfx.canm import (
    CANM,
    FloatAnimationCurve,
    FloatSegment,
    CANMBoneTransform,
    InterpolationType,
    QuantizationType,
    StepLinear64Key,
    Hermite128Key,
    CANMBoneRgbaColor,
)
import itertools
import struct
from cgfx import swizzler
from PIL import Image
import gltflib
from io import BytesIO
import math
import argparse
import os.path


def quat_to_euler(x: float, y: float, z: float, w: float) -> Vector3:
    t0 = 2 * (w * x + y * z)
    t1 = 1 - 2 * (x * x + y * y)
    ex = math.atan2(t0, t1)

    t2 = max(-1, min(1, 2 * (w * y - z * x)))
    ey = math.asin(t2)

    t3 = 2 * (w * z + x * y)
    t4 = 1 - 2 * (y * y + z * z)
    ez = math.atan2(t3, t4)

    return Vector3(ex, ey, ez)


def make_material_animation(material_animation: GraphicsAnimationGroup, mtob: MTOB):
    member = AnimationGroupMember()
    material_animation.members.add(
        f'Materials["{mtob.name}"].MaterialColor.Emission', member
    )
    member.object_type = AnimationGroupMemberType.MaterialColor
    member.path = f'Materials["{mtob.name}"].MaterialColor.Emission'
    member.member = mtob.name
    member.blend_operation_index = "MaterialColor"
    member.value_offset = 16 * 0
    member.value_size = 16
    member.field_type = 1
    member.parent_name = mtob.name

    member = AnimationGroupMember()
    material_animation.members.add(
        f'Materials["{mtob.name}"].MaterialColor.Ambient', member
    )
    member.object_type = AnimationGroupMemberType.MaterialColor
    member.path = f'Materials["{mtob.name}"].MaterialColor.Ambient'
    member.member = mtob.name
    member.blend_operation_index = "MaterialColor"
    member.value_offset = 16 * 1
    member.value_size = 16
    member.field_type = 1
    member.value_index = 1
    member.parent_name = mtob.name

    member = AnimationGroupMember()
    material_animation.members.add(
        f'Materials["{mtob.name}"].MaterialColor.Diffuse', member
    )
    member.object_type = AnimationGroupMemberType.MaterialColor
    member.path = f'Materials["{mtob.name}"].MaterialColor.Diffuse'
    member.member = mtob.name
    member.blend_operation_index = "MaterialColor"
    member.value_offset = 16 * 2
    member.value_size = 16
    member.field_type = 1
    member.value_index = 2
    member.parent_name = mtob.name

    member = AnimationGroupMember()
    material_animation.members.add(
        f'Materials["{mtob.name}"].MaterialColor.Specular0', member
    )
    member.object_type = AnimationGroupMemberType.MaterialColor
    member.path = f'Materials["{mtob.name}"].MaterialColor.Specular0'
    member.member = mtob.name
    member.blend_operation_index = "MaterialColor"
    member.value_offset = 16 * 3
    member.value_size = 16
    member.field_type = 1
    member.value_index = 3
    member.parent_name = mtob.name

    member = AnimationGroupMember()
    material_animation.members.add(
        f'Materials["{mtob.name}"].MaterialColor.Specular1', member
    )
    member.object_type = AnimationGroupMemberType.MaterialColor
    member.path = f'Materials["{mtob.name}"].MaterialColor.Specular1'
    member.member = mtob.name
    member.blend_operation_index = "MaterialColor"
    member.value_offset = 16 * 4
    member.value_size = 16
    member.field_type = 1
    member.value_index = 4
    member.parent_name = mtob.name

    member = AnimationGroupMember()
    material_animation.members.add(
        f'Materials["{mtob.name}"].MaterialColor.Constant0', member
    )
    member.object_type = AnimationGroupMemberType.MaterialColor
    member.path = f'Materials["{mtob.name}"].MaterialColor.Constant0'
    member.member = mtob.name
    member.blend_operation_index = "MaterialColor"
    member.value_offset = 16 * 5
    member.value_size = 16
    member.field_type = 1
    member.value_index = 5
    member.parent_name = mtob.name

    member = AnimationGroupMember()
    material_animation.members.add(
        f'Materials["{mtob.name}"].MaterialColor.Constant1', member
    )
    member.object_type = AnimationGroupMemberType.MaterialColor
    member.path = f'Materials["{mtob.name}"].MaterialColor.Constant1'
    member.member = mtob.name
    member.blend_operation_index = "MaterialColor"
    member.value_offset = 16 * 6
    member.value_size = 16
    member.field_type = 1
    member.value_index = 6
    member.parent_name = mtob.name

    member = AnimationGroupMember()
    material_animation.members.add(
        f'Materials["{mtob.name}"].MaterialColor.Constant2', member
    )
    member.object_type = AnimationGroupMemberType.MaterialColor
    member.path = f'Materials["{mtob.name}"].MaterialColor.Constant2'
    member.member = mtob.name
    member.blend_operation_index = "MaterialColor"
    member.value_offset = 16 * 7
    member.value_size = 16
    member.field_type = 1
    member.value_index = 7
    member.parent_name = mtob.name

    member = AnimationGroupMember()
    material_animation.members.add(
        f'Materials["{mtob.name}"].MaterialColor.Constant3', member
    )
    member.object_type = AnimationGroupMemberType.MaterialColor
    member.path = f'Materials["{mtob.name}"].MaterialColor.Constant3'
    member.member = mtob.name
    member.blend_operation_index = "MaterialColor"
    member.value_offset = 16 * 8
    member.value_size = 16
    member.field_type = 1
    member.value_index = 8
    member.parent_name = mtob.name

    member = AnimationGroupMember()
    material_animation.members.add(
        f'Materials["{mtob.name}"].MaterialColor.Constant4', member
    )
    member.object_type = AnimationGroupMemberType.MaterialColor
    member.path = f'Materials["{mtob.name}"].MaterialColor.Constant4'
    member.member = mtob.name
    member.blend_operation_index = "MaterialColor"
    member.value_offset = 16 * 9
    member.value_size = 16
    member.field_type = 1
    member.value_index = 9
    member.parent_name = mtob.name

    member = AnimationGroupMember()
    material_animation.members.add(
        f'Materials["{mtob.name}"].MaterialColor.Constant5', member
    )
    member.object_type = AnimationGroupMemberType.MaterialColor
    member.path = f'Materials["{mtob.name}"].MaterialColor.Constant5'
    member.member = mtob.name
    member.blend_operation_index = "MaterialColor"
    member.value_offset = 16 * 10
    member.value_size = 16
    member.field_type = 1
    member.value_index = 10
    member.parent_name = mtob.name

    for i in range(len(mtob.texture_mappers)):
        if mtob.texture_mappers[i] is None:
            continue

        # this one seems to crash somtimes when uncommenting it
        # member = AnimationGroupMember()
        # material_animation.members.add(f'Materials["{mtob.name}"].TextureMappers[{i}].Sampler.BorderColor', member)
        # member.object_type = AnimationGroupMemberType.TextureSampler
        # member.path = f'Materials["{mtob.name}"].TextureMappers[{i}].Sampler.BorderColor'
        # member.member = mtob.name
        # member.blend_operation_index = f'TextureMappers[{i}].Sampler'
        # member.value_offset = 12
        # member.value_size = 16
        # member.field_type = 2
        # member.field_index = i
        # member.value_index = 0
        # member.parent_name = mtob.name

        member = AnimationGroupMember()
        material_animation.members.add(
            f'Materials["{mtob.name}"].TextureMappers[{i}].Texture', member
        )
        member.object_type = AnimationGroupMemberType.TextureMapper
        member.path = f'Materials["{mtob.name}"].TextureMappers[{i}].Texture'
        member.member = mtob.name
        member.blend_operation_index = f"TextureMappers[{i}]"
        member.value_offset = 8
        member.value_size = 4
        member.unknown = 1
        member.field_type = 3
        member.field_index = i
        member.value_index = 0
        member.parent_name = mtob.name

        member = AnimationGroupMember()
        material_animation.members.add(
            f'Materials["{mtob.name}"].TextureCoordinators[{i}].Scale', member
        )
        member.object_type = AnimationGroupMemberType.TextureCoordinator
        member.path = (
            f'Materials["{mtob.name}"].FragmentOperation.TextureCoordinators[{i}].Scale'
        )
        member.member = mtob.name
        member.blend_operation_index = f"TextureCoordinators[{i}]"
        member.value_offset = 16
        member.value_size = 8
        member.unknown = 2
        member.field_type = 5
        member.field_index = i
        member.value_index = 0
        member.parent_name = mtob.name

        member = AnimationGroupMember()
        material_animation.members.add(
            f'Materials["{mtob.name}"].TextureCoordinators[{i}].Rotate', member
        )
        member.object_type = AnimationGroupMemberType.TextureCoordinator
        member.path = f'Materials["{mtob.name}"].FragmentOperation.TextureCoordinators[{i}].Rotate'
        member.member = mtob.name
        member.blend_operation_index = f"TextureCoordinators[{i}]"
        member.value_offset = 24
        member.value_size = 4
        member.unknown = 3
        member.field_type = 5
        member.field_index = i
        member.value_index = 1
        member.parent_name = mtob.name

        member = AnimationGroupMember()
        material_animation.members.add(
            f'Materials["{mtob.name}"].TextureCoordinators[{i}].Translate', member
        )
        member.object_type = AnimationGroupMemberType.TextureCoordinator
        member.path = f'Materials["{mtob.name}"].FragmentOperation.TextureCoordinators[{i}].Translate'
        member.member = mtob.name
        member.blend_operation_index = f"TextureCoordinators[{i}]"
        member.value_offset = 28
        member.value_size = 8
        member.unknown = 2
        member.field_type = 5
        member.field_index = i
        member.value_index = 2
        member.parent_name = mtob.name

    member = AnimationGroupMember()
    material_animation.members.add(
        f'Materials["{mtob.name}"].FragmentOperation.BlendOperation.BlendColor', member
    )
    member.object_type = AnimationGroupMemberType.BlendOperation
    member.path = (
        f'Materials["{mtob.name}"].FragmentOperation.BlendOperation.BlendColor'
    )
    member.member = mtob.name
    member.blend_operation_index = "FragmentOperation.BlendOperation"
    member.value_offset = 4
    member.value_size = 16
    member.field_type = 4
    member.value_index = 0
    member.parent_name = mtob.name


def gltf_get_bv_data(gltf: gltflib.GLTF, bv_id: int) -> bytes:
    bv = gltf.model.bufferViews[bv_id]
    buf = gltf.model.buffers[bv.buffer]
    if buf.uri is None:
        buf_res = gltf.get_glb_resource()
    else:
        buf_res = gltf.get_resource(buf.uri)
    return buf_res.data[bv.byteOffset : bv.byteOffset + bv.byteLength]


def gltf_get_accessor_data_vertices(
    gltf: gltflib.GLTF, acc: int | gltflib.Accessor
) -> list[bytes]:
    if isinstance(acc, int):
        acc = gltf.model.accessors[acc]
    bv = gltf.model.bufferViews[acc.bufferView]
    bv_data = gltf_get_bv_data(gltf, acc.bufferView)
    start = acc.byteOffset or 0
    component_sizes = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}
    type_sizes = {
        "SCALAR": 1,
        "VEC2": 2,
        "VEC3": 3,
        "VEC4": 4,
        "MAT2": 4,
        "MAT3": 9,
        "MAT4": 16,
    }
    element_size = component_sizes[acc.componentType] * type_sizes[acc.type]
    stride = bv.byteStride or element_size
    return list(
        bv_data[i : i + element_size]
        for i in range(start, start + acc.count * stride, stride)
    )


def gltf_get_accessor_data_raw(gltf: gltflib.GLTF, acc: gltflib.Accessor) -> bytes:
    return b"".join(gltf_get_accessor_data_vertices(gltf, acc))


def gltf_get_texture(
    cgfx: CGFX, gltf: gltflib.GLTF, image_id: int, normal: bool = False
) -> ImageTexture:
    image = gltf.model.images[image_id]
    tex_name = image.name or image.uri or f"image{image_id}"
    if normal:
        tex_name = f"NORM~{tex_name}"
    if tex_name in cgfx.data.textures:
        return cgfx.data.textures[tex_name]

    if image.uri is not None:
        image_data = gltf.get_resource(image.uri).data
    elif image.bufferView is not None:
        image_data = gltf_get_bv_data(gltf, image.bufferView)

    im: Image.Image = Image.open(BytesIO(image_data))
    if im.width > 256:
        im = im.resize((256, im.height))
    if im.height > 256:
        im = im.resize((im.width, 256))
    if normal:
        im = im.convert("RGBA")
        for x in range(im.width):
            for y in range(im.height):
                px = im.getpixel((x, y))
                im.putpixel((x, y), (255 - px[0], 255 - px[1], px[2]))
    txob = swizzler.to_txob(im.transpose(Image.Transpose.FLIP_TOP_BOTTOM))
    txob.name = tex_name
    cgfx.data.textures.add(tex_name, txob)
    return txob


def make_bones(
    gltf: gltflib.GLTF, node_ids: list[int], bone_dict: DictInfo[Bone]
) -> list[Bone]:
    bones = []
    for node_id in node_ids:
        node = gltf.model.nodes[node_id]
        bone = Bone()
        bone.name = node.name or f"Node {node_id}"
        bone_dict.add(bone.name, bone)
        bone.joint_id = node_id
        bone.flags = (
            BoneFlag.IsNeedRendering
            | BoneFlag.IsLocalMatrixCalculate
            | BoneFlag.IsWorldMatrixCalculate
        )
        if bones:
            bone.previous_sibling = bones[-1]
            bones[-1].next_sibling = bone
        bones.append(bone)

        if node.matrix:
            bone.position = Vector3(node.matrix[12], node.matrix[13], node.matrix[14])
            bone.scale = Vector3(
                math.hypot(*node.matrix[:3]),
                math.hypot(*node.matrix[4:7]),
                math.hypot(*node.matrix[8:11]),
            )

            if abs(node.matrix[2]) != 1:
                rot_y = -math.asin(node.matrix[2] / bone.scale.x)
                rot_x = math.atan2(
                    node.matrix[6] / math.cos(rot_y), node.matrix[10] / math.cos(rot_y)
                )
                rot_z = math.atan2(
                    node.matrix[1] / math.cos(rot_y), node.matrix[0] / math.cos(rot_y)
                )
            else:
                rot_z = 0
                if node.matrix[2] == -1:
                    rot_y = math.pi / 2
                    rot_x = math.atan(node.matrix[4], node.matrix[8])
            bone.rotation = Vector3(rot_x, rot_y, rot_z)

        else:
            if node.translation:
                bone.position = Vector3(*node.translation)
            if node.scale:
                bone.scale = Vector3(*node.scale)
            if node.rotation:
                bone.rotation = quat_to_euler(*node.rotation)

        if bone.position == Vector3(0, 0, 0):
            bone.flags |= BoneFlag.IsTranslateZero
        if bone.scale == Vector3(1, 1, 1):
            bone.flags |= BoneFlag.IsScaleOne
        if bone.scale.x == bone.scale.y == bone.scale.z:
            bone.flags |= BoneFlag.IsUniformScale
        if bone.rotation == Vector3(0, 0, 0):
            bone.flags |= BoneFlag.IsRotateZero

        all_flags = (
            BoneFlag.IsTranslateZero | BoneFlag.IsRotateZero | BoneFlag.IsScaleOne
        )
        if (bone.flags & all_flags) == all_flags:
            bone.flags |= BoneFlag.IsIdentity

        if node.children:
            child_bones = make_bones(gltf, node.children, bone_dict)
            if child_bones:
                bone.child = child_bones[0]
                for c in child_bones:
                    c.parent = bone
    return bones


def convert_gltf(gltf: gltflib.GLTF) -> CGFX:
    default_sampler = gltflib.Sampler(
        magFilter=9729, minFilter=9729, wrapS=10497, wrapT=10497
    )
    default_material = gltflib.Material(name="glTF default material")

    cgfx = CGFX()

    luts = LUTS()
    luts.name = "LutSet"
    cgfx.data.lookup_tables.add("LutSet", luts)

    cmdl = CMDLWithSkeleton()
    cgfx.data.models.add("COMMON", cmdl)
    cmdl.name = "COMMON"

    cmdl.skeleton = SOBJSkeleton()
    node_ids = gltf.model.scenes[gltf.model.scene].nodes
    root_bone = Bone()
    root_bone.name = "Scene root"
    # ensure name is unique
    while any(n.name == root_bone.name for n in gltf.model.nodes):
        root_bone.name += "_"
    root_bone.joint_id = len(gltf.model.nodes)
    root_bone.flags = BoneFlag.IsIdentity | BoneFlag.IsNeedRendering
    cmdl.skeleton.root_bone = root_bone
    cmdl.skeleton.bones.add(root_bone.name, root_bone)
    initial_bones = make_bones(gltf, node_ids, cmdl.skeleton.bones)

    # sort bones based on how many transluscent materials they use
    def sort_key(node):
        node = gltf.model.nodes[node.content.joint_id]
        if node.mesh is None:
            return 0
        mesh = gltf.model.meshes[node.mesh]
        return sum(
            1
            for p in mesh.primitives
            if p.material is not None
            and gltf.model.materials[p.material].alphaMode == "BLEND"
        ) / len(mesh.primitives)

    cmdl.skeleton.bones.dict.nodes[2:] = sorted(
        cmdl.skeleton.bones.dict.nodes[2:], key=sort_key
    )

    # clean up joint ids
    node_to_bone = {}
    bone_to_node = {}

    for b in cmdl.skeleton.bones:
        bone = cmdl.skeleton.bones[b]
        if bone.joint_id >= 0:
            bone_to_node[cmdl.skeleton.bones.get_index(b)] = bone.joint_id
            node_to_bone[bone.joint_id] = cmdl.skeleton.bones.get_index(b)
            bone.joint_id = cmdl.skeleton.bones.get_index(b)

    for b in initial_bones:
        b.parent = root_bone
    root_bone.child = initial_bones[0]

    for b in cmdl.skeleton.bones:
        bone = cmdl.skeleton.bones[b]
        if bone.parent:
            bone.parent_id = bone.parent.joint_id

    mesh_nodes = [
        bone_to_node[cmdl.skeleton.bones[bone_name].joint_id]
        for bone_name in cmdl.skeleton.bones
        if bone_to_node[cmdl.skeleton.bones[bone_name].joint_id] < len(gltf.model.nodes)
        and gltf.model.nodes[bone_to_node[cmdl.skeleton.bones[bone_name].joint_id]].mesh
        is not None
    ]

    for node_id in mesh_nodes:
        node = gltf.model.nodes[node_id]
        mesh = gltf.model.meshes[node.mesh]
        bone_id = node_to_bone[node_id]
        bone = cmdl.skeleton.bones[bone_id]

        skin = None
        if node.skin is not None:
            # bone.flags |= BoneFlag.HasSkinningMatrix
            skin = gltf.model.skins[node.skin]
            if skin.inverseBindMatrices is not None:
                ibms = gltf_get_accessor_data_vertices(gltf, skin.inverseBindMatrices)
                for i, joint_id in enumerate(skin.joints):
                    ibm = [
                        struct.unpack("f", bytes(s))[0]
                        for s in itertools.batched(ibms[i], 4)
                    ]
                    sub_bone = cmdl.skeleton.bones[node_to_bone[joint_id]]
                    sub_bone.flags |= BoneFlag.HasSkinningMatrix
                    sub_bone.inverse_base = Matrix(
                        Vector4(*ibm[::4]), Vector4(*ibm[1::4]), Vector4(*ibm[2::4])
                    )

        for material in (
            (
                gltf.model.materials[p.material]
                if p.material is not None
                else default_material
            )
            for p in mesh.primitives
        ):
            if material.name not in cmdl.materials:
                mtob = MTOB()
                cmdl.materials.add(material.name, mtob)
                mtob.name = material.name

                match material.alphaMode:
                    case "MASK":
                        mtob.fragment_operations.blend_operation.src_color = (
                            BlendFunction.One
                        )
                        mtob.fragment_operations.blend_operation.dst_color = (
                            BlendFunction.Zero
                        )
                        mtob.fragment_operations.blend_operation.src_alpha = (
                            BlendFunction.One
                        )
                        mtob.fragment_operations.blend_operation.dst_alpha = (
                            BlendFunction.Zero
                        )
                        mtob.fragment_shader.alpha_test.enabled = True
                        if material.alphaCutoff is not None:
                            mtob.fragment_shader.alpha_test.cutoff = int(
                                material.alphaCutoff * 255
                            )
                    case "BLEND":
                        mtob.fragment_operations.blend_operation.src_color = (
                            BlendFunction.SrcAlpha
                        )
                        mtob.fragment_operations.blend_operation.dst_color = (
                            BlendFunction.InvSrcAlpha
                        )
                        mtob.fragment_operations.blend_operation.src_alpha = (
                            BlendFunction.SrcAlpha
                        )
                        mtob.fragment_operations.blend_operation.dst_alpha = (
                            BlendFunction.InvSrcAlpha
                        )
                        mtob.fragment_operations.depth_operation.flags = (
                            DepthFlag.TestEnabled
                        )
                    case "OPAQUE" | _:
                        mtob.fragment_operations.blend_operation.src_color = (
                            BlendFunction.One
                        )
                        mtob.fragment_operations.blend_operation.dst_color = (
                            BlendFunction.Zero
                        )
                        mtob.fragment_operations.blend_operation.src_alpha = (
                            BlendFunction.One
                        )
                        mtob.fragment_operations.blend_operation.dst_alpha = (
                            BlendFunction.Zero
                        )

                # multiply base texture with primary color
                mtob.fragment_shader.texture_combiners[0].src_rgb = 0x030
                mtob.fragment_shader.texture_combiners[0].src_alpha = 0x030
                mtob.fragment_shader.texture_combiners[0].combine_rgb = 1
                mtob.fragment_shader.texture_combiners[0].combine_alpha = 1
                # multiply with diffuse lighting
                mtob.fragment_shader.texture_combiners[1].src_rgb = 0x0F1
                mtob.fragment_shader.texture_combiners[1].combine_rgb = 1
                # add specular lighting
                mtob.fragment_shader.texture_combiners[2].src_rgb = 0xFE2
                mtob.fragment_shader.texture_combiners[2].combine_rgb = 8
                mtob.fragment_shader.texture_combiners[2].constant = (
                    ConstantColorSource.Constant0
                )

                mtob.flags = MTOBFlag.FragmentLight
                pmr = material.pbrMetallicRoughness or gltflib.PBRMetallicRoughness()
                base_tex = pmr.baseColorTexture
                if pmr.baseColorFactor:
                    mtob.material_color.diffuse = ColorFloat(*pmr.baseColorFactor)

                # specular
                mtob.fragment_shader.fragment_lighting.flags = (
                    FragmentLightingFlags.UseDistribution0
                )
                mtob.fragment_shader.fragment_lighting_table.distribution_0_sampler = (
                    LightingLookupTable()
                )
                mtob.fragment_shader.fragment_lighting_table.distribution_0_sampler.sampler.binary_path = (
                    "LutSet"
                )
                mtob.fragment_shader.fragment_lighting_table.distribution_0_sampler.sampler.table_name = (
                    mtob.name
                )
                roughness_factor = (
                    pmr.roughnessFactor if pmr.roughnessFactor is not None else 1
                )
                mtob.material_color.constant[0] = ColorFloat(
                    *([1 - 0.9 * roughness_factor] * 3), 1
                )
                lut = LutTable.phong(4 * 200 / (1 + roughness_factor * 100))
                luts.tables.add(mtob.name, lut)

                if base_tex:
                    tex = gltf.model.textures[base_tex.index]
                    sampler = (
                        gltf.model.samplers[tex.sampler]
                        if tex.sampler is not None
                        else default_sampler
                    )
                    tex_info = TexInfo(
                        ReferenceTexture(
                            gltf_get_texture(cgfx, gltf, tex.source, False)
                        )
                    )
                    tex_param = 0
                    tex_param |= 1 * ((sampler.magFilter or 9729) & 1)
                    tex_param |= 2 * ((sampler.minFilter or 9729) & 1)
                    tex_param |= [33071, 0, 10497, 33648].index(
                        (sampler.wrapS or 10497)
                    ) << 12
                    tex_param |= [33071, 0, 10497, 33648].index(
                        (sampler.wrapT or 10497)
                    ) << 8
                    tex_info.commands[2].head |= tex_param
                    mtob.texture_mappers[mtob.used_texture_coordinates_count] = tex_info
                    mtob.texture_coordinators[
                        mtob.used_texture_coordinates_count
                    ].source_coordinate = (base_tex.texCoord or 0)
                    mtob.used_texture_coordinates_count += 1
                else:
                    mtob.fragment_shader.texture_combiners[0].combine_rgb = 0
                    mtob.fragment_shader.texture_combiners[0].combine_alpha = 0
                if material.normalTexture:
                    tex = gltf.model.textures[material.normalTexture.index]

                    sampler = (
                        gltf.model.samplers[tex.sampler]
                        if tex.sampler is not None
                        else default_sampler
                    )
                    tex_info = TexInfo(
                        ReferenceTexture(gltf_get_texture(cgfx, gltf, tex.source, True))
                    )
                    tex_info.commands[0].head += 8 * mtob.used_texture_coordinates_count
                    tex_info.commands[
                        1
                    ].head += 8 * mtob.used_texture_coordinates_count + 8 * bool(
                        mtob.used_texture_coordinates_count
                    )
                    tex_param = 0
                    tex_param |= 1 * (sampler.magFilter & 1)
                    tex_param |= 2 * (sampler.minFilter & 1)
                    tex_param |= [33071, 0, 10497, 33648].index(sampler.wrapS) << 12
                    tex_param |= [33071, 0, 10497, 33648].index(sampler.wrapT) << 8
                    tex_info.commands[2].head |= tex_param
                    mtob.texture_mappers[mtob.used_texture_coordinates_count] = tex_info
                    mtob.texture_coordinators[
                        mtob.used_texture_coordinates_count
                    ].source_coordinate = (material.normalTexture.texCoord or 0)
                    mtob.fragment_shader.fragment_lighting.bump_texture = (
                        mtob.used_texture_coordinates_count
                    )
                    mtob.used_texture_coordinates_count += 1
                    mtob.fragment_shader.fragment_lighting.bump_mode = BumpMode.AsBump
                    mtob.fragment_shader.fragment_lighting.is_bump_renormalize = True

        for i, p in enumerate(mesh.primitives):
            if p.mode is not None and p.mode != 4:
                raise RuntimeError(
                    "only triangle list primitives are currently supported"
                )
            sobj_mesh = SOBJMesh(cmdl)
            cmdl.meshes.add(sobj_mesh)
            sobj_mesh.name = (
                (mesh.name or cmdl.skeleton.bones[node_to_bone[node_id]])
                + "_"
                + mtob.name
            )
            sobj_mesh.mesh_node_visibility_index = 65535
            sobj_mesh.material_index = cmdl.materials.get_index(
                gltf.model.materials[p.material].name
                if p.material is not None
                else default_material.name
            )
            sobj_mesh.shape_index = len(cmdl.shapes)
            shape = SOBJShape()
            cmdl.shapes.add(shape)
            shape.name = sobj_mesh.name
            primitive_set = PrimitiveSet()
            shape.primitive_sets.add(primitive_set)
            primitive_set.related_bones.add(bone_id)
            if node.skin is not None:
                primitive_set.skinning_mode = 2
                for joint_id in skin.joints:
                    primitive_set.related_bones.add(node_to_bone[joint_id])
            primitive = Primitive()
            primitive_set.primitives.add(primitive)
            if p.indices is not None:
                indices = gltf.model.accessors[p.indices]
                index_stream = IndexStream()
                index_stream.data_type = indices.componentType
                index_stream.face_data = gltf_get_accessor_data_raw(gltf, indices)
                if material.doubleSided:
                    # duplicate all vertices backwards
                    rev = reversed(gltf_get_accessor_data_vertices(gltf, indices))
                    acc_id = list(
                        v for k, v in p.attributes.__dict__.items() if v is not None
                    )[0]
                    count = len(
                        gltf_get_accessor_data_vertices(
                            gltf, gltf.model.accessors[acc_id]
                        )
                    )
                    index_stream.face_data += b"".join(
                        (int.from_bytes(b, "little") + count).to_bytes(len(b), "little")
                        for b in rev
                    )
                primitive.index_streams.add(index_stream)
                primitive.buffer_objects.add(0)
            for ty, acc_id in p.attributes.__dict__.items():
                if acc_id is None:
                    continue
                acc = gltf.model.accessors[acc_id]
                vs = VertexStream()
                vs.usage = {
                    "POSITION": VertexAttributeUsage.Position,
                    "NORMAL": VertexAttributeUsage.Normal,
                    "TANGENT": VertexAttributeUsage.Tangent,
                    "TEXCOORD_0": VertexAttributeUsage.TextureCoordinate0,
                    "TEXCOORD_1": VertexAttributeUsage.TextureCoordinate1,
                    "COLOR_0": VertexAttributeUsage.Color,
                    "JOINTS_0": VertexAttributeUsage.BoneIndex,
                    "WEIGHTS_0": VertexAttributeUsage.BoneWeight,
                }.get(ty)
                if vs.usage is None:
                    continue
                shape.vertex_attributes.add(vs)
                vs.components_count = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[
                    acc.type
                ]
                vs.format_type = acc.componentType
                if acc.componentType == 5123:  # unsigned short doesn't work
                    vs.format_type = 5122  # turn it into signed short
                vs.vertex_stream_data = gltf_get_accessor_data_raw(gltf, acc)
                if ty == "JOINTS_0":
                    vs.vertex_stream_data = b"".join(
                        (int.from_bytes(bytes(b), "little") + 1).to_bytes(
                            len(b), "little"
                        )
                        for b in itertools.batched(
                            vs.vertex_stream_data,
                            {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}[
                                acc.componentType
                            ],
                        )
                    )
                if material.doubleSided:
                    # duplicate all vertices but with the normals reversed
                    verts = vs.vertex_stream_data
                    if ty != "NORMAL":
                        vs.vertex_stream_data += verts
                    else:
                        vs.vertex_stream_data += bytes(
                            [v ^ (0x80 * (i % 4 == 3)) for i, v in enumerate(verts)]
                        )

    visibility_animation = GraphicsAnimationGroup()
    cmdl.animation_group_descriptions.add("VisibilityAnimation", visibility_animation)
    visibility_animation.name = "VisibilityAnimation"
    visibility_animation.member_type = 3
    visibility_animation.blend_operations.add(0)

    is_visible = AnimationGroupMember()
    visibility_animation.members.add("IsVisible", is_visible)
    is_visible.object_type = AnimationGroupMemberType.Model
    is_visible.path = "IsVisible"
    is_visible.value_offset = 212
    is_visible.value_size = 1
    is_visible.field_type = 6
    is_visible.value_index = 1

    for i in range(len(cmdl.meshes)):
        mesh_is_visible = AnimationGroupMember()
        visibility_animation.members.add(f"Meshes[{i}].IsVisible", mesh_is_visible)
        mesh_is_visible.object_type = AnimationGroupMemberType.Mesh
        mesh_is_visible.path = f"Meshes[{i}].IsVisible"
        mesh_is_visible.member = str(i)
        mesh_is_visible.value_offset = 36
        mesh_is_visible.value_size = 1
        mesh_is_visible.field_type = 7
        mesh_is_visible.parent_index = i

    skeletal_animation = GraphicsAnimationGroup()
    cmdl.animation_group_descriptions.add("SkeletalAnimation", skeletal_animation)
    skeletal_animation.flags = 1
    skeletal_animation.name = "SkeletalAnimation"
    skeletal_animation.member_type = 1
    skeletal_animation.blend_operations.add(8)
    skeletal_animation.evalution_timing = 1

    for b in cmdl.skeleton.bones:
        member = AnimationGroupMember()
        skeletal_animation.members.add(b, member)
        member.object_type = AnimationGroupMemberType.Bone
        member.path = b
        member.member = b
        member.field_type = 0
        member.parent_name = b

    material_animation = GraphicsAnimationGroup()
    cmdl.animation_group_descriptions.add("MaterialAnimation", material_animation)
    material_animation.name = "MaterialAnimation"
    material_animation.member_type = 2
    material_animation.evalution_timing = 1
    material_animation.blend_operations.add(3)
    material_animation.blend_operations.add(7)
    material_animation.blend_operations.add(5)
    material_animation.blend_operations.add(2)

    for m in cmdl.materials:
        make_material_animation(material_animation, cmdl.materials[m])

    cenv = CENV()
    cenv.name = "Scene"
    cgfx.data.scenes.add(cenv.name, cenv)
    light_set = CENVLightSet()
    cenv.light_sets.add(light_set)
    cenv_light = CENVLight()
    light_set.lights.add(cenv_light)
    cenv_light.name = "TheLight"

    cflt = CFLT()
    cflt.name = "TheLight"
    cgfx.data.lights.add(cflt.name, cflt)

    if gltf.model.animations:
        skeletal_animation = CANM()
        skeletal_animation.name = "COMMON"
        cgfx.data.skeletal_animations.add(skeletal_animation.name, skeletal_animation)
        skeletal_animation.target_animation_group_name = "SkeletalAnimation"
        skeletal_animation.frame_size = 60 * max(
            struct.unpack("f", t)[0]
            for a in gltf.model.animations or []
            for c in a.channels
            for t in gltf_get_accessor_data_vertices(gltf, a.samplers[c.sampler].input)
        )
        for anim in gltf.model.animations or []:
            for node_id, channels in itertools.groupby(
                sorted(
                    (c for c in anim.channels if c.target.node is not None),
                    key=lambda c: c.target.node,
                ),
                key=lambda c: c.target.node,
            ):
                bone = CANMBoneTransform()
                bone.bone_path = cmdl.skeleton.bones[node_to_bone[node_id]].name
                skeletal_animation.member_animations_data.add(bone.bone_path, bone)
                for c in channels:
                    sampler = anim.samplers[c.sampler]
                    interpolation = (
                        InterpolationType(
                            ("STEP", "LINEAR", "CUBICSPLINE").index(
                                sampler.interpolation
                            )
                        )
                        if sampler.interpolation
                        else InterpolationType.Linear
                    )
                    inputs = tuple(
                        struct.unpack("f", t)[0]
                        for t in gltf_get_accessor_data_vertices(gltf, sampler.input)
                    )
                    outputs = tuple(
                        tuple(
                            struct.unpack("f", bytes(c))[0]
                            for c in itertools.batched(v, 4)
                        )
                        for v in gltf_get_accessor_data_vertices(gltf, sampler.output)
                    )
                    match c.target.path:
                        case "weights":
                            print("WARNING: morph target animations are not supported")
                            continue
                        case "translation":
                            bone.pos_x = FloatAnimationCurve()
                            bone.pos_y = FloatAnimationCurve()
                            bone.pos_z = FloatAnimationCurve()
                            bone.pos_x.segments.append(FloatSegment())
                            bone.pos_y.segments.append(FloatSegment())
                            bone.pos_z.segments.append(FloatSegment())
                            bone.pos_x.start_frame = bone.pos_y.start_frame = (
                                bone.pos_z.end_frame
                            ) = bone.pos_x.segments[
                                0
                            ].start_frame = bone.pos_y.segments[
                                0
                            ].start_frame = bone.pos_z.segments[
                                0
                            ].start_frame = (
                                min(inputs) * 60
                            )
                            bone.pos_x.end_frame = bone.pos_y.end_frame = (
                                bone.pos_z.end_frame
                            ) = bone.pos_x.segments[0].end_frame = bone.pos_y.segments[
                                0
                            ].end_frame = bone.pos_z.segments[
                                0
                            ].end_frame = (
                                max(inputs) * 60
                            )
                            bone.pos_x.segments[0].interpolation = bone.pos_y.segments[
                                0
                            ].interpolation = bone.pos_z.segments[0].interpolation = (
                                interpolation
                            )
                            bone.pos_x.segments[0].quantization = bone.pos_y.segments[
                                0
                            ].quantization = bone.pos_z.segments[0].quantization = (
                                QuantizationType.StepLinear64
                                if interpolation != InterpolationType.CubicSpline
                                else QuantizationType.Hermite128
                            )
                            if interpolation != InterpolationType.CubicSpline:
                                for time, (x, y, z) in zip(inputs, outputs):
                                    bone.pos_x.segments[0].keys.append(
                                        StepLinear64Key(time * 60, x)
                                    )
                                    bone.pos_y.segments[0].keys.append(
                                        StepLinear64Key(time * 60, y)
                                    )
                                    bone.pos_z.segments[0].keys.append(
                                        StepLinear64Key(time * 60, z)
                                    )
                            else:
                                for time, (
                                    (xa, ya, za),
                                    (xv, yv, zv),
                                    (xb, yb, zb),
                                ) in zip(inputs, itertools.batched(outputs, 3)):
                                    bone.pos_x.segments[0].keys.append(
                                        Hermite128Key(time * 60, xv, xa, xb)
                                    )
                                    bone.pos_y.segments[0].keys.append(
                                        Hermite128Key(time * 60, yv, ya, yb)
                                    )
                                    bone.pos_z.segments[0].keys.append(
                                        Hermite128Key(time * 60, zv, za, zb)
                                    )
                        case "scale":
                            bone.scale_x = FloatAnimationCurve()
                            bone.scale_y = FloatAnimationCurve()
                            bone.scale_z = FloatAnimationCurve()
                            bone.scale_x.segments.append(FloatSegment())
                            bone.scale_y.segments.append(FloatSegment())
                            bone.scale_z.segments.append(FloatSegment())
                            bone.scale_x.start_frame = bone.scale_y.start_frame = (
                                bone.scale_z.end_frame
                            ) = bone.scale_x.segments[
                                0
                            ].start_frame = bone.scale_y.segments[
                                0
                            ].start_frame = bone.scale_z.segments[
                                0
                            ].start_frame = (
                                min(inputs) * 60
                            )
                            bone.scale_x.end_frame = bone.scale_y.end_frame = (
                                bone.scale_z.end_frame
                            ) = bone.scale_x.segments[
                                0
                            ].end_frame = bone.scale_y.segments[
                                0
                            ].end_frame = bone.scale_z.segments[
                                0
                            ].end_frame = (
                                max(inputs) * 60
                            )
                            bone.scale_x.segments[0].interpolation = (
                                bone.scale_y.segments[0].interpolation
                            ) = bone.scale_z.segments[0].interpolation = interpolation
                            bone.scale_x.segments[0].quantization = (
                                bone.scale_y.segments[0].quantization
                            ) = bone.scale_z.segments[0].quantization = (
                                QuantizationType.StepLinear64
                                if interpolation != InterpolationType.CubicSpline
                                else QuantizationType.Hermite128
                            )
                            if interpolation != InterpolationType.CubicSpline:
                                for time, (x, y, z) in zip(inputs, outputs):
                                    bone.scale_x.segments[0].keys.append(
                                        StepLinear64Key(time * 60, x)
                                    )
                                    bone.scale_y.segments[0].keys.append(
                                        StepLinear64Key(time * 60, y)
                                    )
                                    bone.scale_z.segments[0].keys.append(
                                        StepLinear64Key(time * 60, z)
                                    )
                            else:
                                for time, (
                                    (xa, ya, za),
                                    (xv, yv, zv),
                                    (xb, yb, zb),
                                ) in zip(inputs, itertools.batched(outputs, 3)):
                                    bone.scale_x.segments[0].keys.append(
                                        Hermite128Key(time * 60, xv, xa, xb)
                                    )
                                    bone.scale_y.segments[0].keys.append(
                                        Hermite128Key(time * 60, yv, ya, yb)
                                    )
                                    bone.scale_z.segments[0].keys.append(
                                        Hermite128Key(time * 60, zv, za, zb)
                                    )
                        case "rotation":
                            if interpolation == InterpolationType.CubicSpline:
                                print(
                                    "WARNING: spline rotation animations are currently not supported"
                                )
                                continue
                            bone.rot_x = FloatAnimationCurve()
                            bone.rot_y = FloatAnimationCurve()
                            bone.rot_z = FloatAnimationCurve()
                            bone.rot_x.segments.append(FloatSegment())
                            bone.rot_y.segments.append(FloatSegment())
                            bone.rot_z.segments.append(FloatSegment())
                            bone.rot_x.start_frame = bone.rot_y.start_frame = (
                                bone.rot_z.end_frame
                            ) = bone.rot_x.segments[
                                0
                            ].start_frame = bone.rot_y.segments[
                                0
                            ].start_frame = bone.rot_z.segments[
                                0
                            ].start_frame = (
                                min(inputs) * 60
                            )
                            bone.rot_x.end_frame = bone.rot_y.end_frame = (
                                bone.rot_z.end_frame
                            ) = bone.rot_x.segments[0].end_frame = bone.rot_y.segments[
                                0
                            ].end_frame = bone.rot_z.segments[
                                0
                            ].end_frame = (
                                max(inputs) * 60
                            )
                            bone.rot_x.segments[0].interpolation = bone.rot_y.segments[
                                0
                            ].interpolation = bone.rot_z.segments[0].interpolation = (
                                interpolation
                            )
                            bone.rot_x.segments[0].quantization = bone.rot_y.segments[
                                0
                            ].quantization = bone.rot_z.segments[0].quantization = (
                                QuantizationType.StepLinear64
                                if interpolation != InterpolationType.CubicSpline
                                else QuantizationType.Hermite128
                            )
                            for time, (x, y, z, w) in zip(inputs, outputs):
                                euler = quat_to_euler(x, y, z, w)
                                # normalize rotations to smallest distance
                                if (
                                    interpolation == InterpolationType.Linear
                                    and len(bone.rot_x.segments[0].keys) > 0
                                ):
                                    while (
                                        euler.x
                                        < bone.rot_x.segments[0].keys[-1].value
                                        - math.pi
                                    ):
                                        euler.x += 2 * math.pi
                                    while (
                                        euler.x
                                        > bone.rot_x.segments[0].keys[-1].value
                                        + math.pi
                                    ):
                                        euler.x -= 2 * math.pi
                                    while (
                                        euler.y
                                        < bone.rot_y.segments[0].keys[-1].value
                                        - math.pi
                                    ):
                                        euler.y += 2 * math.pi
                                    while (
                                        euler.y
                                        > bone.rot_y.segments[0].keys[-1].value
                                        + math.pi
                                    ):
                                        euler.y -= 2 * math.pi
                                    while (
                                        euler.z
                                        < bone.rot_z.segments[0].keys[-1].value
                                        - math.pi
                                    ):
                                        euler.z += 2 * math.pi
                                    while (
                                        euler.z
                                        > bone.rot_z.segments[0].keys[-1].value
                                        + math.pi
                                    ):
                                        euler.z -= 2 * math.pi
                                bone.rot_x.segments[0].keys.append(
                                    StepLinear64Key(time * 60, euler.x)
                                )
                                bone.rot_y.segments[0].keys.append(
                                    StepLinear64Key(time * 60, euler.y)
                                )
                                bone.rot_z.segments[0].keys.append(
                                    StepLinear64Key(time * 60, euler.z)
                                )
        if (
            gltf.model.extensionsUsed
            and "KHR_animation_pointer" in gltf.model.extensionsUsed
        ):
            material_animation = CANM()
            material_animation.name = "COMMON"
            cgfx.data.material_animations.add(
                material_animation.name, material_animation
            )
            material_animation.target_animation_group_name = "MaterialAnimation"
            material_animation.frame_size = 60 * max(
                struct.unpack("f", t)[0]
                for a in gltf.model.animations or []
                for c in a.channels
                for t in gltf_get_accessor_data_vertices(
                    gltf, a.samplers[c.sampler].input
                )
            )
            for anim in gltf.model.animations or []:
                for base, channels in itertools.groupby(
                    sorted(
                        (
                            c
                            for c in anim.channels
                            if c.target.path == "pointer"
                            and "KHR_animation_pointer" in c.target.extensions
                        ),
                        key=lambda c: c.target.extensions["KHR_animation_pointer"][
                            "pointer"
                        ],
                    ),
                    key=lambda c: c.target.extensions["KHR_animation_pointer"][
                        "pointer"
                    ].split("/")[:3],
                ):
                    if base[1] != "materials":
                        print(
                            "WARNING: pointer animations currently only supported for materials"
                        )
                        continue
                    material = gltf.model.materials[int(base[2])]
                    if material.name not in cmdl.materials:
                        continue
                    for c in channels:
                        sampler = anim.samplers[c.sampler]
                        interpolation = (
                            InterpolationType(
                                ("STEP", "LINEAR", "CUBICSPLINE").index(
                                    sampler.interpolation
                                )
                            )
                            if sampler.interpolation
                            else InterpolationType.Linear
                        )
                        inputs = tuple(
                            struct.unpack("f", t)[0]
                            for t in gltf_get_accessor_data_vertices(
                                gltf, sampler.input
                            )
                        )
                        outputs = tuple(
                            tuple(
                                struct.unpack("f", bytes(c))[0]
                                for c in itertools.batched(v, 4)
                            )
                            for v in gltf_get_accessor_data_vertices(
                                gltf, sampler.output
                            )
                        )

                        bone = CANMBoneRgbaColor()
                        path = c.target.extensions["KHR_animation_pointer"][
                            "pointer"
                        ].split("/")
                        if path[3:] == ["pbrMetallicRoughness", "baseColorFactor"]:
                            bone.bone_path = (
                                f'Materials["{material.name}"].MaterialColor.Diffuse'
                            )
                        else:
                            print(
                                "WARNING: pointer "
                                + "/".join(path)
                                + " is currently not supported"
                            )
                            continue
                        material_animation.member_animations_data.add(
                            bone.bone_path, bone
                        )
                        bone.red = FloatAnimationCurve()
                        bone.green = FloatAnimationCurve()
                        bone.blue = FloatAnimationCurve()
                        bone.alpha = FloatAnimationCurve()
                        bone.red.segments.append(FloatSegment())
                        bone.green.segments.append(FloatSegment())
                        bone.blue.segments.append(FloatSegment())
                        bone.alpha.segments.append(FloatSegment())
                        bone.red.start_frame = bone.green.start_frame = (
                            bone.blue.start_frame
                        ) = bone.alpha.end_frame = bone.red.segments[
                            0
                        ].start_frame = bone.green.segments[
                            0
                        ].start_frame = bone.blue.segments[
                            0
                        ].start_frame = bone.alpha.segments[
                            0
                        ].start_frame = (
                            min(inputs) * 60
                        )
                        bone.red.end_frame = bone.green.end_frame = (
                            bone.blue.end_frame
                        ) = bone.alpha.end_frame = bone.red.segments[
                            0
                        ].end_frame = bone.green.segments[
                            0
                        ].end_frame = bone.blue.segments[
                            0
                        ].end_frame = bone.alpha.segments[
                            0
                        ].end_frame = (
                            max(inputs) * 60
                        )
                        bone.red.segments[0].interpolation = bone.green.segments[
                            0
                        ].interpolation = bone.blue.segments[
                            0
                        ].interpolation = bone.alpha.segments[
                            0
                        ].interpolation = interpolation
                        bone.red.segments[0].quantization = bone.green.segments[
                            0
                        ].quantization = bone.blue.segments[
                            0
                        ].quantization = bone.alpha.segments[
                            0
                        ].quantization = (
                            QuantizationType.StepLinear64
                            if interpolation != InterpolationType.CubicSpline
                            else QuantizationType.Hermite128
                        )

                        if interpolation != InterpolationType.CubicSpline:
                            for time, (r, g, b, a) in zip(inputs, outputs):
                                bone.red.segments[0].keys.append(
                                    StepLinear64Key(time * 60, r)
                                )
                                bone.green.segments[0].keys.append(
                                    StepLinear64Key(time * 60, g)
                                )
                                bone.blue.segments[0].keys.append(
                                    StepLinear64Key(time * 60, b)
                                )
                                bone.alpha.segments[0].keys.append(
                                    StepLinear64Key(time * 60, a)
                                )
                        else:
                            for time, (
                                (ra, ga, ba, aa),
                                (rv, gv, bv, av),
                                (rb, gb, bb, ab),
                            ) in zip(inputs, itertools.batched(outputs, 3)):
                                bone.red.segments[0].keys.append(
                                    Hermite128Key(time * 60, rv, ra, rb)
                                )
                                bone.green.segments[0].keys.append(
                                    Hermite128Key(time * 60, gv, ga, gb)
                                )
                                bone.blue.segments[0].keys.append(
                                    Hermite128Key(time * 60, bv, ba, bb)
                                )
                                bone.alpha.segments[0].keys.append(
                                    Hermite128Key(time * 60, av, aa, ab)
                                )

    # optional lighting stuff

    # light_animation = GraphicsAnimationGroup()
    # light_animation.name = 'LightAnimation'
    # cflt.animation_group_descriptions.add(light_animation.name, light_animation)
    # light_animation.member_type = 4
    # light_animation.blend_operations.add(8)
    # light_animation.blend_operations.add(3)
    # light_animation.blend_operations.add(6)
    # light_animation.blend_operations.add(2)
    # light_animation.blend_operations.add(0)

    # member = AnimationGroupMember()
    # light_animation.members.add('Transform', member)
    # member.object_type = 0x800000
    # member.path = 'Transform'
    # member.value_offset = 48
    # member.value_size = 36
    # member.field_type = 9
    # member.parent_index = 9

    # member = AnimationGroupMember()
    # light_animation.members.add('Ambient', member)
    # member.object_type = 0x100000
    # member.path = 'Ambient'
    # member.value_offset = 188
    # member.value_size = 16
    # member.unknown = 1
    # member.field_type = 13
    # member.value_index = 0

    # member = AnimationGroupMember()
    # light_animation.members.add('Diffuse', member)
    # member.object_type = 0x100000
    # member.path = 'Diffuse'
    # member.value_offset = 204
    # member.value_size = 16
    # member.unknown = 1
    # member.field_type = 13
    # member.value_index = 1

    # member = AnimationGroupMember()
    # light_animation.members.add('Specular0', member)
    # member.object_type = 0x100000
    # member.path = 'Specular0'
    # member.value_offset = 220
    # member.value_size = 16
    # member.unknown = 1
    # member.field_type = 13
    # member.value_index = 2

    # member = AnimationGroupMember()
    # light_animation.members.add('Specular1', member)
    # member.object_type = 0x100000
    # member.path = 'Specular1'
    # member.value_offset = 236
    # member.value_size = 16
    # member.unknown = 1
    # member.field_type = 13
    # member.value_index = 3

    # member = AnimationGroupMember()
    # light_animation.members.add('Direction', member)
    # member.object_type = 0x100000
    # member.path = 'Direction'
    # member.value_offset = 268
    # member.value_size = 12
    # member.unknown = 2
    # member.field_type = 13
    # member.value_index = 4

    # member = AnimationGroupMember()
    # light_animation.members.add('DistanceAttenuationStart', member)
    # member.object_type = 0x100000
    # member.path = 'DistanceAttenuationStart'
    # member.value_offset = 288
    # member.value_size = 4
    # member.unknown = 3
    # member.field_type = 13
    # member.value_index = 5

    # member = AnimationGroupMember()
    # light_animation.members.add('DistanceAttenuationEnd', member)
    # member.object_type = 0x100000
    # member.path = 'DistanceAttenuationEnd'
    # member.value_offset = 292
    # member.value_size = 4
    # member.unknown = 3
    # member.field_type = 13
    # member.value_index = 6

    # member = AnimationGroupMember()
    # light_animation.members.add('IsLightEnabled', member)
    # member.object_type = 0x100000
    # member.path = 'IsLightEnabled'
    # member.value_offset = 180
    # member.value_size = 1
    # member.unknown = 4
    # member.field_type = 12

    return cgfx


def write(cgfx: CGFX) -> bytes:
    strings = StringTable()
    imag = StringTable()
    offset = cgfx.prepare(0, strings, imag)
    offset = strings.prepare(offset)
    cgfx.data.section_size = offset - cgfx.data.offset
    if not imag.empty():
        cgfx.header.nr_blocks = 2
        offset += 8  # IMAG header
    offset = imag.prepare(offset)
    cgfx.header.file_size = offset
    data = cgfx.write(strings, imag)
    data += strings.write()
    if not imag.empty():
        data += b"IMAG" + imag.size().to_bytes(4, "little") + imag.write()
    if len(data) > 0x80000:
        print(f"WARNING: CGFX is too big ({len(data)} bytes, max is {0x80000} bytes)")
    return data


def main():
    parser = argparse.ArgumentParser(description="Convert a glTF model to CGFX.")
    parser.add_argument("in_gltf", type=str, help="The input glTF (.gltf or .glb)")
    parser.add_argument(
        "out_cgfx", type=str, help="The output CGFX (.cgfx)", nargs="?", default=None
    )
    args = parser.parse_args()
    if args.out_cgfx is None:
        args.out_cgfx = os.path.splitext(args.in_gltf)[0] + ".cgfx"

    gltf = gltflib.GLTF.load(args.in_gltf, load_file_resources=True)
    cgfx = convert_gltf(gltf)
    with open(args.out_cgfx, "wb") as f:
        f.write(write(cgfx))


if __name__ == "__main__":
    main()
