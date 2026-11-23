from .shared import (
    Vector3,
    Vector4,
    Matrix,
    ColorByte,
    ColorFloat,
    StandardObject,
    Signature,
)
from .dict import DictInfo
from .animation import GraphicsAnimationGroup
from struct import Struct


def float_to_20bit(f: float) -> int:
    s = Struct("f")
    data = s.pack(f)
    casted = int.from_bytes(data, "little", signed=False)
    mantissa = casted & 0x7FFFFF
    exponent = max(-0x3F, min(0x40, (((casted >> 23) & 0xFF) - 0x7F)))
    sign = casted >> 31
    return (sign << 19) | (((exponent + 0x3F) & 0x7F) << 12) | (mantissa >> 13)


class CFLT(StandardObject):
    struct = Struct(
        "i4siiiiiiixxxxiifffffffff"
        + "f" * 12 * 2
        + "ii"
        + "f" * 16
        + "B" * 16
        + "fffiixxxxxxxxiixxxx"
    )
    type = 0x400000A2
    signature = Signature("CFLT")
    revision = 0x6000000
    name = ""
    user_data: DictInfo
    flags = 1
    branch_visible = False
    nr_children = 0
    animation_group_descriptions: DictInfo[GraphicsAnimationGroup]
    scale: Vector3
    rotation: Vector3
    translation: Vector3
    local: Matrix
    world: Matrix
    enabled = True
    light_type = 0
    ambient: ColorFloat
    diffuse: ColorFloat
    specular: list[ColorFloat]
    position_or_direction: Vector3
    attenuation_lut = None
    spotlight_lut = None
    attenuation_scale = 1
    attenuation_bias = -0.0

    def __init__(self) -> None:
        super().__init__()
        self.scale = Vector3(1, 1, 1)
        self.rotation = Vector3(0, 0, 0)
        self.translation = Vector3(0, 0, 0)
        self.local = Matrix(
            Vector4(1, 0, 0, 0), Vector4(0, 1, 0, 0), Vector4(0, 0, 1, 0)
        )
        self.world = Matrix(
            Vector4(1, 0, 0, 0), Vector4(0, 1, 0, 0), Vector4(0, 0, 1, 0)
        )
        self.user_data = DictInfo()
        self.animation_group_descriptions = DictInfo()
        self.position_or_direction = Vector3(0, 0, -1)
        self.ambient = ColorFloat(1, 1, 1, 1)
        self.diffuse = ColorFloat(1, 1, 1, 1)
        self.specular = [ColorFloat(1, 1, 1, 1), ColorFloat(1, 1, 1, 1)]

    def values(self) -> tuple:
        return (
            self.type,
            self.signature,
            self.revision,
            self.name,
            self.user_data,
            self.flags,
            self.branch_visible,
            self.nr_children,
            self.animation_group_descriptions,
            self.scale,
            self.rotation,
            self.translation,
            self.local,
            self.world,
            self.enabled,
            self.light_type,
            self.ambient,
            self.diffuse,
            *self.specular,
            self.ambient.as_byte(),
            self.diffuse.as_byte(),
            *(x.as_byte() for x in self.specular),
            self.position_or_direction,
            self.attenuation_lut,
            self.spotlight_lut,
            float_to_20bit(self.attenuation_scale),
            float_to_20bit(self.attenuation_bias),
        )
