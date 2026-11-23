from .shared import (
    InlineObject,
    StandardObject,
    Signature,
    Matrix,
    Vector3,
    Vector4,
    Reference,
    ColorByte,
    ColorFloat,
)
from .dict import DictInfo
from struct import Struct
from .txob import TXOB
from enum import IntEnum, IntFlag
import itertools


class MTOBFlag(IntFlag):
    FragmentLight = 1
    VertexLight = 2
    HemisphereLight = 4
    HemisphereOcclusion = 8
    Fog = 16
    PolygonOffset = 32


class CullMode(IntEnum):
    Never = 0
    FrontFace = 1
    BackFace = 2


class TextureProjection(IntEnum):
    UVMap = 0
    CameraCubeMap = 1
    CameraSphereMap = 2
    ProjectionMap = 3
    ShadowMap = 4
    ShadowCubeMap = 5


class FresnelConfig(IntFlag):
    Primary = 1
    Secondary = 2


class BumpMode(IntEnum):
    NotUsed = 0
    AsBump = 1
    AsTangent = 2


class DepthFlag(IntFlag):
    TestEnabled = 1  # depth read
    MaskEnabled = 2  # depth write


class PicaCommand(InlineObject):
    struct = Struct("II")

    def __init__(self, param, head):
        self.param = param
        self.head = head

    def values(self):
        return (self.param, self.head)


class MaterialColor(InlineObject):
    struct = Struct("ffff" * (3 + 2 + 6) + "BBBB" * (3 + 2 + 6) + "i")
    emission = ColorFloat(0, 0, 0, 0)
    ambient = ColorFloat(0, 0, 0, 1)
    diffuse = ColorFloat(1, 1, 1, 1)
    specular: list[ColorFloat]
    constant: list[ColorFloat]
    command_cache = 0

    def __init__(self):
        self.specular = [ColorFloat(1, 1, 1, 0), ColorFloat(0, 0, 0, 0)]
        self.constant = [ColorFloat(0, 0, 0, 0) for _ in range(6)]

    def values(self):
        return (
            self.emission,
            self.ambient,
            self.diffuse,
            *self.specular,
            *self.constant,
            self.emission.as_byte(),
            self.ambient.as_byte(),
            self.diffuse.as_byte(),
            *(x.as_byte() for x in self.specular + self.constant),
            self.command_cache,
        )


class Rasterization(InlineObject):
    struct = Struct("iifii")
    flags = 0
    cull_mode = CullMode.BackFace  # ignored by the home menu
    polygon_offset_unit = 0
    command: PicaCommand

    def __init__(self):
        # this command actually defines the cull mode
        self.command = PicaCommand(CullMode.BackFace, 0x00010040)

    def values(self):
        return (self.flags, self.cull_mode, self.polygon_offset_unit, self.command)


class DepthOperation(InlineObject):
    struct = Struct("iiiii")
    flags = DepthFlag.MaskEnabled | DepthFlag.TestEnabled
    commands: list[PicaCommand]

    def __init__(self):
        self.commands = [PicaCommand(0x41, 0x10107), PicaCommand(0x3000000, 0x80126)]

    def values(self):
        return (self.flags, *self.commands)


class BlendEquation(IntEnum):
    Add = 0
    Subtract = 1
    RevSubtract = 2
    Minimum = 3
    Maximum = 4


class BlendFunction(IntEnum):
    Zero = 0
    One = 1
    SrcColor = 2
    InvSrccolor = 3
    DstColor = 4
    InvDstColor = 5
    SrcAlpha = 6
    InvSrcAlpha = 7
    DstAlpha = 8
    InvDstAlpha = 9
    ConstColor = 10
    InvConstColor = 11
    ConstAlpha = 12
    InvConstAlpha = 13
    SrcAlphaSaturate = 14


class BlendOperation(InlineObject):
    struct = Struct("iffffIIIIII")
    mode = 1
    blend = ColorFloat(0, 0, 0, 1)
    equation_color = BlendEquation.Add
    equation_alpha = BlendEquation.Add
    src_color = BlendFunction.One
    dst_color = BlendFunction.Zero
    src_alpha = BlendFunction.One
    dst_alpha = BlendFunction.Zero

    def values(self):
        return (
            self.mode,
            self.blend,
            PicaCommand(0xE40100, 0x803F0100),
            PicaCommand(
                self.equation_color
                | (self.equation_alpha << 8)
                | (self.src_color << 16)
                | (self.dst_color << 20)
                | (self.src_alpha << 24)
                | (self.dst_alpha << 28),
                0,
            ),
            PicaCommand(
                int.from_bytes(bytes(self.blend.as_byte().values()), "little"), 0
            ),
        )


class FragmentOperation(InlineObject):
    struct = Struct(
        DepthOperation.struct.format + BlendOperation.struct.format + "IIII"
    )
    depth_operation: DepthOperation
    blend_operation: BlendOperation
    # stencil commands may be modified at runtime
    stencil_commands: list[PicaCommand]

    def __init__(self):
        self.depth_operation = DepthOperation()
        self.blend_operation = BlendOperation()
        self.stencil_commands = [PicaCommand(0, 0xD0105), PicaCommand(0, 0xF0106)]

    def values(self):
        return (self.depth_operation, self.blend_operation, *self.stencil_commands)


class TextureCoordinator(InlineObject):
    # first byte of padding is modified at runtime
    struct = Struct("iiiifffff?xxx" + "f" * 12)
    source_coordinate = 0
    projection = TextureProjection.UVMap
    reference_camera = 0
    matrix_mode = 0
    scale_u = 1
    scale_v = 1
    rotate = 0
    translate_u = 0
    translate_v = 0
    # if true, matrix is generated at runtime based on matrix mode
    should_generate_matrix = False
    transform_matrix: Matrix

    def __init__(self):
        self.transform_matrix = Matrix(
            Vector4(1, 0, 0, 0), Vector4(0, 1, 0, 0), Vector4(0, 0, 1, 0)
        )

    def values(self):
        return (
            self.source_coordinate,
            self.projection,
            self.reference_camera,
            self.matrix_mode,
            self.scale_u,
            self.scale_v,
            self.rotate,
            self.translate_u,
            self.translate_v,
            self.should_generate_matrix,
            self.transform_matrix,
        )


class TextureSampler(StandardObject):
    struct = Struct("Iiiffff")
    type = 0x80000000
    owner: "TexInfo"
    min_filter = 0
    border_color: ColorFloat

    def __init__(self, owner):
        self.owner = owner
        self.border_color = ColorFloat(0, 0, 0, 0)

    def values(self):
        return (self.type, Reference(self.owner), self.min_filter, self.border_color)


class TexInfo(StandardObject):
    struct = Struct("Iiii" + "I" * 14 + "i")
    type = 0x80000000
    dynamic_allocator = 0
    txob: TXOB
    sampler: TextureSampler
    # commands + count are modified at runtime
    commands: list[PicaCommand]
    command_size_to_send = 0x38

    def __init__(self, txob: TXOB):
        self.txob = txob
        self.sampler = TextureSampler(self)
        self.commands = [
            PicaCommand(0, 0x1008E),
            PicaCommand(0xFF000000, 0x809F0081),
            PicaCommand(0, 0),
            PicaCommand(0, 0),
            PicaCommand(0, 0),
            PicaCommand(0, 0),
            PicaCommand(0, 0),
        ]

    def values(self):
        return (
            self.type,
            self.dynamic_allocator,
            self.txob,
            self.sampler,
            *self.commands,
            self.command_size_to_send,
        )


class SHDR(StandardObject):
    struct = Struct("I4siiii")
    type: int
    signature = Signature("SHDR")
    revision = 0x5000000
    name = ""
    user_data: DictInfo

    def __init__(self):
        self.user_data = DictInfo()

    def values(self):
        return (self.type, self.signature, self.revision, self.name, self.user_data)


class LinkedShader(SHDR):
    # padding is modified at runtime
    struct = Struct(SHDR.struct.format + "ixxxx")
    type = 0x80000001
    reference_shader_name = "DefaultShader"

    def values(self):
        return super().values() + (self.reference_shader_name,)


class FragmentLightingFlags(IntFlag):
    ClampHighLight = 1
    UseDistribution0 = 2
    UseDistribution1 = 4
    UseGeometricFactor0 = 8
    UseGeometricFactor1 = 16
    UseReflection = 32


class FragmentLighting(InlineObject):
    struct = Struct("iiiiii")
    flags = FragmentLightingFlags(0)
    layer_config = 0  # bits 4-7 of GPUREG_LIGHTING_CONFIG0
    fresnel_config = FresnelConfig(0)
    bump_texture = 0
    bump_mode = BumpMode.NotUsed
    is_bump_renormalize = False

    def values(self):
        return (
            self.flags,
            self.layer_config,
            self.fresnel_config,
            self.bump_texture,
            self.bump_mode,
            self.is_bump_renormalize,
        )


class ReferenceLookupTable(StandardObject):
    struct = Struct("iiixxxx")
    type = 0x40000000
    # used to look up the LUTS in the DATA block
    binary_path = ""
    # used to look up the table within the LUTS
    table_name = ""

    def values(self):
        return (self.type, self.binary_path, self.table_name)


class LightingLookupTable(StandardObject):
    struct = Struct("iii")
    input_command = 0  # GPUREG_LIGHTING_LUTINPUT_SELECT value
    scale_command = 0  # GPUREG_LIGHTING_LUTINPUT_SCALE value
    sampler: ReferenceLookupTable

    def __init__(self):
        self.sampler = ReferenceLookupTable()

    def values(self):
        return (self.input_command, self.scale_command, self.sampler)


class FragmentLightingTable(StandardObject):
    struct = Struct("iiiiii")
    reflectance_r_sampler: LightingLookupTable = None
    reflectance_g_sampler: LightingLookupTable = None
    reflectance_b_sampler: LightingLookupTable = None
    distribution_0_sampler: LightingLookupTable = None
    distribution_1_sampler: LightingLookupTable = None
    fresnel_sampler: LightingLookupTable = None

    def values(self):
        return (
            self.reflectance_r_sampler,
            self.reflectance_g_sampler,
            self.reflectance_b_sampler,
            self.distribution_0_sampler,
            self.distribution_1_sampler,
            self.fresnel_sampler,
        )


class ConstantColorSource(IntEnum):
    Constant0 = 0
    Constant1 = 1
    Constant2 = 2
    Constant3 = 3
    Constant4 = 4
    Constant5 = 5
    Emission = 6
    Ambient = 7
    Diffuse = 8
    Specular0 = 9
    Specular1 = 10


class TextureCombiner(InlineObject):
    struct = Struct("ihhIihhxxxxhh")
    constant = ConstantColorSource.Constant0
    src_rgb = 0xFFF
    src_alpha = 0xFFF
    header: int
    tev_ops = 0
    combine_rgb = 0
    combine_alpha = 0
    scale_rgb = 0
    scale_alpha = 0

    def __init__(self, i):
        self.header = 0x804F0000 | ((0xC0 if i < 4 else 0xD0) + i * 8)
        self.const_rgba = ColorByte(0, 0, 0, 255)

    def values(self):
        return (
            self.constant,
            self.src_rgb,
            self.src_alpha,
            self.header,
            self.tev_ops,
            self.combine_rgb,
            self.combine_alpha,
            self.scale_rgb,
            self.scale_alpha,
        )


class AlphaTestFunction(IntEnum):
    Never = 0
    Always = 1
    Equal = 2
    NotEqual = 3
    Less = 4
    LessEqual = 5
    Greater = 6
    GreaterEqual = 7


class AlphaTest(InlineObject):
    enabled = False
    function = AlphaTestFunction.GreaterEqual
    cutoff = 128

    def values(self):
        return (
            PicaCommand(
                self.enabled | (self.function << 4) | (self.cutoff << 8), 0xF0104
            ),
        )


class FragmentShader(StandardObject):
    struct = Struct(
        "ffff"
        + FragmentLighting.struct.format
        + "i"
        + TextureCombiner.struct.format * 6
        + "IIIIIIII"
    )
    buffer_color: ColorFloat
    fragment_lighting: FragmentLighting
    fragment_lighting_table: FragmentLightingTable
    texture_combiners: list[TextureCombiner]
    alpha_test: AlphaTest
    buffer_commands: list[PicaCommand]

    def __init__(self):
        self.buffer_color = ColorFloat(0, 0, 0, 0)
        self.fragment_lighting = FragmentLighting()
        self.fragment_lighting_table = FragmentLightingTable()
        self.texture_combiners = [TextureCombiner(i) for i in range(6)]
        self.alpha_test = AlphaTest()
        self.buffer_commands = [
            PicaCommand(0xFF000000, 0xF00FD),
            PicaCommand(0, 0x200E0),
            PicaCommand(0x400, 0x201C3),
        ]

    def values(self):
        return (
            self.buffer_color,
            self.fragment_lighting,
            self.fragment_lighting_table,
            *self.texture_combiners,
            self.alpha_test,
            *self.buffer_commands,
        )


class MTOB(StandardObject):
    # padding is written to at runtime
    struct = Struct(
        "i4siiiiiii"
        + MaterialColor.struct.format
        + Rasterization.struct.format
        + FragmentOperation.struct.format
        + "i"
        + TextureCoordinator.struct.format * 3
        + "iii"
        + "iiiiiiii"
        + "IIIIIIIIIIIII"
        + "xxxx"
    )
    type = 0x8000000
    signature = Signature("MTOB")
    revision = 0x6000000
    name = ""
    user_data: DictInfo
    flags = MTOBFlag(0)
    texture_coordinates_config = 0
    transluscency_kind = 0
    material_color: MaterialColor
    rasterization: Rasterization
    fragment_operations: FragmentOperation
    used_texture_coordinates_count = 0
    texture_coordinators: list[TextureCoordinator]
    texture_mappers: list[TexInfo]
    shader: SHDR
    fragment_shader: FragmentShader
    shader_program_description_index = 0
    shader_parameters_count = 0
    shader_parameters_pointer_table = 0
    light_set_index = 0
    fog_index = 0

    def __init__(self):
        self.user_data = DictInfo()
        self.material_color = MaterialColor()
        self.rasterization = Rasterization()
        self.fragment_operations = FragmentOperation()
        self.texture_coordinators = [TextureCoordinator() for _ in range(3)]
        self.texture_mappers: list[TexInfo] = [None, None, None, None]
        self.shader = LinkedShader()
        self.fragment_shader = FragmentShader()

    def values(self):
        return (
            self.type,
            self.signature,
            self.revision,
            self.name,
            self.user_data,
            self.flags,
            self.texture_coordinates_config,
            self.transluscency_kind,
            self.material_color,
            self.rasterization,
            self.fragment_operations,
            self.used_texture_coordinates_count,
            *self.texture_coordinators,
            *self.texture_mappers,
            self.shader,
            self.fragment_shader,
            self.shader_program_description_index,
            self.shader_parameters_count,
            self.shader_parameters_pointer_table,
            self.light_set_index,
            self.fog_index,
            self.shading_parameters_hash(),
            self.shader_parameters_hash(),
            self.texture_coordinators_hash(),
            self.texture_samplers_hash(),
            # texture mappers hash is calculated at runtime
            0,
            self.material_color_hash(),
            self.rasterization_hash(),
            self.fragment_lighting_hash(),
            # fragment lighting table hash is calculated at runtime
            0,
            self.fragmnent_lighting_table_parameters_hash(),
            self.texture_combiners_hash(),
            self.alpha_test_hash(),
            self.fragment_operations_hash(),
        )

    def shading_parameters_hash(self):
        # TODO
        return 0

    def shader_parameters_hash(self):
        # TODO
        return 0

    def texture_coordinators_hash(self):
        # TODO
        return 0

    def texture_samplers_hash(self):
        # used to calculate texture mappers hash
        return (
            hash(
                tuple(
                    v
                    for v in itertools.chain.from_iterable(
                        m.flat_values() for m in self.texture_mappers if m is not None
                    )
                    if not isinstance(v, StandardObject)
                )
            )
            & 0xFFFFFFFF
        )

    def material_color_hash(self):
        # TODO
        return 0

    def rasterization_hash(self):
        # TODO
        return 0

    def fragment_lighting_hash(self):
        # TODO
        return 0

    def fragmnent_lighting_table_parameters_hash(self):
        # used to calculate fragment lighting table hash
        return (
            hash(
                tuple(
                    v
                    for v in itertools.chain.from_iterable(
                        m.values()
                        for m in self.fragment_shader.fragment_lighting_table.values()
                        if m
                    )
                    if not isinstance(v, StandardObject)
                )
            )
            & 0xFFFFFFFF
        )

    def texture_combiners_hash(self):
        # TODO
        return 0

    def alpha_test_hash(self):
        # TODO
        return 0

    def fragment_operations_hash(self):
        # TODO
        return 0
