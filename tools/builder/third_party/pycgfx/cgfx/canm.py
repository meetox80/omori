from .shared import (
    ColorFloat,
    InlineObject,
    StandardObject,
    Signature,
    Vector3,
    Vector4,
)
from .dict import DictInfo
from enum import IntEnum, IntFlag
from struct import Struct


class Vector2Flag(IntFlag):
    XConst = 1
    YConst = 2
    XIgnore = 4
    YIgnore = 8


class TransformFlag(IntFlag):
    ScaleXConst = 0x40
    ScaleYConst = 0x80
    ScaleZConst = 0x100
    RotXConst = 0x200
    RotYConst = 0x400
    RotZConst = 0x800
    PosXConst = 0x2000
    PosYConst = 0x4000
    PosZConst = 0x8000
    ScaleXIgnore = 0x10000
    ScaleYIgnore = 0x20000
    ScaleZIgnore = 0x40000
    RotXIgnore = 0x80000
    RotYIgnore = 0x100000
    RotZIgnore = 0x200000
    PosXIgnore = 0x800000
    PosYIgnore = 0x1000000
    PosZIgnore = 0x2000000


class BakedTransformFlag(IntFlag):
    TranslationIgnore = 8
    RotationIgnore = 16
    ScaleIgnore = 32


class RgbaColorFlags(IntFlag):
    RConst = 0x1
    BConst = 0x2
    GConst = 0x4
    AConst = 0x8
    RIgnore = 0x10
    GIgnore = 0x20
    BIgnore = 0x40
    AIgnore = 0x80


class PrimitiveType(IntEnum):
    Float = 0
    Int = 1
    Boolean = 2
    Vector2 = 3
    Vector3 = 4
    Transform = 5
    RgbaColor = 6
    Texture = 7
    BakedTransform = 8
    TransformMatrix = 9


class RepeatMethod(IntEnum):
    Clamp = 0
    Repeat = 1
    MirroredRepeat = 2


class AnimationCurve(StandardObject):
    struct = Struct("ffbbxxi")
    start_frame = 0.0
    end_frame = 600.0
    pre_repeat_method = RepeatMethod.Clamp
    post_repeat_method = RepeatMethod.Clamp
    flags = 0

    def values(self) -> tuple:
        return (
            self.start_frame,
            self.end_frame,
            self.pre_repeat_method,
            self.post_repeat_method,
            self.flags,
        )


class InterpolationType(IntEnum):
    Nearest = 0
    Linear = 1
    CubicSpline = 2


class InterpolationKey(InlineObject):
    frame = 0.0
    value = 0.0


class UnifiedHermiteKey(InterpolationKey):
    in_out_slope = 0.0


class HermiteKey(InterpolationKey):
    in_slope = 0.0
    out_slope = 0.0


class Hermite128Key(HermiteKey):
    struct = Struct("ffff")

    def __init__(self, frame=0, value=0, in_slope=0, out_slope=0):
        self.frame = frame
        self.value = value
        self.in_slope = in_slope
        self.out_slope = out_slope

    def values(self):
        return (self.frame, self.value, self.in_slope, self.out_slope)


class UnifiedHermite96Key(UnifiedHermiteKey):
    struct = Struct("fff")

    def values(self):
        return (self.frame, self.value, self.in_out_slope)


class StepLinear64Key(InterpolationKey):
    struct = Struct("ff")

    def __init__(self, frame=0, value=0):
        self.frame = frame
        self.value = value

    def values(self):
        return (self.frame, self.value)


class QuantizationType(IntEnum):
    Hermite128 = 0
    Hermite64 = 1
    Hermite48 = 2
    UnifiedHermite96 = 3
    UnifiedHermite48 = 4
    UnifiedHermite32 = 5
    StepLinear64 = 6
    StepLinear32 = 7


class FloatSegment(StandardObject):
    start_frame = 0.0
    end_frame = 0.0
    single_value: float | None = None
    interpolation = InterpolationType.Linear
    quantization = QuantizationType.Hermite128
    keys: list[InterpolationKey]
    scale = 1
    offset = 0
    frame_scale = 1

    def __init__(self):
        self.keys = []

    def refresh_struct(self):
        struct = "ffi"
        if self.single_value is not None:
            struct += "f"
        else:
            struct += "if"
            if self.quantization not in (
                QuantizationType.Hermite128,
                QuantizationType.UnifiedHermite96,
                QuantizationType.StepLinear64,
            ):
                struct += "fff"
            for k in self.keys:
                k.refresh_struct()
                struct += k.struct.format
        self.struct = Struct(struct)

    def values(self):
        return (
            self.start_frame,
            self.end_frame,
            (
                (self.single_value is not None)
                | (self.interpolation << 2)
                | (self.quantization << 5)
            ),
        ) + (
            (self.single_value,)
            if self.single_value is not None
            else (
                len(self.keys),
                # speed is used to accelerate lookups
                # for this, time * speed * (num_keys-1) must < num_keys
                # so speed < 1 / time
                1 / (self.end_frame - self.start_frame),
            )
            + (
                (self.scale, self.offset, self.frame_scale)
                if self.quantization
                not in (
                    QuantizationType.Hermite128,
                    QuantizationType.UnifiedHermite96,
                    QuantizationType.StepLinear64,
                )
                else ()
            )
            + tuple(self.keys)
        )


class FloatAnimationCurve(AnimationCurve):
    segments: list[FloatSegment]

    def __init__(self) -> None:
        self.segments = []

    def refresh_struct(self):
        self.struct = Struct(
            AnimationCurve.struct.format + "i" + "i" * len(self.segments)
        )

    def values(self) -> tuple:
        return super().values() + (len(self.segments), *self.segments)


class Vector3AndFlags(InlineObject):
    struct = Struct("fffi")
    value: Vector3
    flags = 0

    def __init__(self) -> None:
        self.value = Vector3()

    def values(self) -> tuple:
        return (self.value, self.flags)


class Vector3AnimationCurve(AnimationCurve):
    frames: list[Vector3AndFlags]

    def __init__(self) -> None:
        self.frames = []

    def refresh_struct(self):
        self.struct = Struct(
            AnimationCurve.struct.format + Vector3AndFlags.format * len(self.frames)
        )

    def values(self) -> tuple:
        if (self.flags[0] & 1) == 0:
            assert len(self.frames) == self.end_frame - self.start_frame
        return super().values() + (
            (self.frames[0],) if self.flags[0] & 1 else tuple(self.frames)
        )


class QuaternionAndFlags(InlineObject):
    struct = Struct("ffffi")
    value: Vector4
    flags = 0

    def __init__(self) -> None:
        self.value = Vector4()

    def values(self) -> tuple:
        return (self.value, self.flags)


class QuaternionAnimationCurve(AnimationCurve):
    frames: list[QuaternionAndFlags]

    def __init__(self) -> None:
        self.frames = []

    def refresh_struct(self):
        self.struct = Struct(
            AnimationCurve.struct.format + QuaternionAndFlags.format * len(self.frames)
        )

    def values(self) -> tuple:
        if (self.flags[0] & 1) == 0:
            assert len(self.frames) == self.end_frame - self.start_frame
        return super.values() + (
            (self.frames[0],) if self.flags[0] & 1 else tuple(self.frames)
        )


class CANMBone(StandardObject):
    struct = Struct("iiiii")
    flags = 0
    bone_path = ""
    unknown1 = ""
    unknown2 = ""
    primitive_type = 0

    def values(self):
        return (
            self.flags,
            self.bone_path,
            self.unknown1,
            self.unknown2,
            self.primitive_type,
        )


class CANMBoneVector2(CANMBone):
    flags = Vector2Flag(0)
    primitive_type = PrimitiveType.Vector2
    x: None | float | FloatAnimationCurve = None
    y: None | float | FloatAnimationCurve = None

    def refresh_struct(self):
        flags = Vector2Flag(0)
        struct = ""
        values = (self.x, self.y)
        ignore_flags = (Vector2Flag.XIgnore, Vector2Flag.YIgnore)
        const_flags = (Vector2Flag.XConst, Vector2Flag.YConst)
        for i in range(len(values)):
            if isinstance(values[i], float):
                struct += "f"
                flags |= const_flags[i]
            else:
                struct += "i"
                if values[i] is None:
                    flags |= ignore_flags[i]
        self.flags = flags
        self.flags = flags
        self.struct = Struct(CANMBone.struct.format + struct)

    def values(self):
        return super().values() + (self.x, self.y)


class CANMBoneTransform(CANMBone):
    flags = TransformFlag(0)
    primitive_type = PrimitiveType.Transform
    scale_x: None | float | FloatAnimationCurve = None
    scale_y: None | float | FloatAnimationCurve = None
    scale_z: None | float | FloatAnimationCurve = None
    rot_x: None | float | FloatAnimationCurve = None
    rot_y: None | float | FloatAnimationCurve = None
    rot_z: None | float | FloatAnimationCurve = None
    pos_x: None | float | FloatAnimationCurve = None
    pos_y: None | float | FloatAnimationCurve = None
    pos_z: None | float | FloatAnimationCurve = None

    def refresh_struct(self):
        flags = TransformFlag(0)
        struct = ""
        values = (
            self.scale_x,
            self.scale_y,
            self.scale_z,
            self.rot_x,
            self.rot_y,
            self.rot_z,
            self.pos_x,
            self.pos_y,
            self.pos_z,
        )
        ignore_flags = (
            TransformFlag.ScaleXIgnore,
            TransformFlag.ScaleYIgnore,
            TransformFlag.ScaleZIgnore,
            TransformFlag.RotXIgnore,
            TransformFlag.RotYIgnore,
            TransformFlag.RotZIgnore,
            TransformFlag.PosXIgnore,
            TransformFlag.PosYIgnore,
            TransformFlag.PosZIgnore,
        )
        const_flags = (
            TransformFlag.ScaleXConst,
            TransformFlag.ScaleYConst,
            TransformFlag.ScaleZConst,
            TransformFlag.RotXConst,
            TransformFlag.RotYConst,
            TransformFlag.RotZConst,
            TransformFlag.PosXConst,
            TransformFlag.PosYConst,
            TransformFlag.PosZConst,
        )
        for i in range(len(values)):
            if isinstance(values[i], float):
                struct += "f"
                flags |= const_flags[i]
            else:
                struct += "i"
                if values[i] is None:
                    flags |= ignore_flags[i]
            if const_flags[i] == TransformFlag.RotZConst:
                struct += "xxxx"
        self.flags = flags
        self.struct = Struct(CANMBone.struct.format + struct)

    def values(self):
        return super().values() + (
            self.scale_x,
            self.scale_y,
            self.scale_z,
            self.rot_x,
            self.rot_y,
            self.rot_z,
            self.pos_x,
            self.pos_y,
            self.pos_z,
        )


class CANMBoneBakedTransform(CANMBone):
    struct = Struct(CANMBone.struct.format + "iii")
    primitive_type = PrimitiveType.BakedTransform
    flags = BakedTransformFlag(0)
    rotation: None | QuaternionAnimationCurve = None
    translation: None | Vector3AnimationCurve = None
    scale: None | Vector3AnimationCurve = None

    def refresh_struct(self):
        flags = BakedTransformFlag(0)
        values = (self.rotation, self.translation, self.scale)
        ignore_flags = (
            BakedTransformFlag.RotationIgnore,
            BakedTransformFlag.TranslationIgnore,
            BakedTransformFlag.ScaleIgnore,
        )
        for i in range(len(values)):
            if values[i] is None:
                flags |= ignore_flags[i]
        self.flags = flags

    def values(self):
        return super().values() + (self.rotation, self.translation, self.scale)


class CANMBoneRgbaColor(CANMBone):
    struct = Struct(CANMBone.struct.format + "iiii")
    primitive_type = PrimitiveType.RgbaColor
    flags = RgbaColorFlags(0)
    red: None | float | FloatAnimationCurve = None
    green: None | float | FloatAnimationCurve = None
    blue: None | float | FloatAnimationCurve = None
    alpha: None | float | FloatAnimationCurve = None

    def refresh_struct(self):
        flags = RgbaColorFlags(0)
        struct = ""
        values = (self.red, self.green, self.blue, self.alpha)
        ignore_flags = (
            RgbaColorFlags.RIgnore,
            RgbaColorFlags.GIgnore,
            RgbaColorFlags.BIgnore,
            RgbaColorFlags.AIgnore,
        )
        const_flags = (
            RgbaColorFlags.RConst,
            RgbaColorFlags.GConst,
            RgbaColorFlags.BConst,
            RgbaColorFlags.AConst,
        )
        for i in range(len(values)):
            if isinstance(values[i], float):
                struct += "f"
                flags |= const_flags[i]
            else:
                struct += "i"
                if values[i] is None:
                    flags |= ignore_flags[i]
            if const_flags[i] == TransformFlag.RotZConst:
                struct += "xxxx"
        self.flags = flags
        self.struct = Struct(CANMBone.struct.format + struct)

    def values(self):
        return super().values() + (self.red, self.green, self.blue, self.alpha)


class CANM(StandardObject):
    struct = Struct("4siiiifiiii")
    signature = Signature("CANM")
    revision = 0x05000000
    name = ""
    target_animation_group_name = ""
    looping = True
    frame_size = 600
    member_animations_data: DictInfo[CANMBone]
    user_data: DictInfo

    def __init__(self):
        self.member_animations_data: DictInfo[CANMBone] = DictInfo()
        self.user_data = DictInfo()

    def values(self) -> tuple:
        return (
            self.signature,
            self.revision,
            self.name,
            self.target_animation_group_name,
            self.looping,
            self.frame_size,
            self.member_animations_data,
            self.user_data,
        )
