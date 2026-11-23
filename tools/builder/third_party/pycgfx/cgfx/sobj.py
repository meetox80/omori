from struct import Struct

from enum import IntEnum, IntFlag

from .dict import DictInfo
from .shared import (
    Signature,
    StandardObject,
    List,
    Vector3,
    Vector4,
    OrientationMatrix,
    Reference,
    Matrix,
)

from .primitives import VertexAttribute, PrimitiveSet

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .cmdl import CMDL


class SkeletonScalingRule(IntEnum):
    Standard = 0
    Maya = 1
    SoftImage = 2


class BillboardMode(IntEnum):
    # When the non-viewpoint modes are in use, the bone always faces the camera identically.
    # With a viewpoint mode, the bone may appear to tilt as it moves towards the side.
    # Other than that, the true meaning of these modes is unclear, despite obvious
    # mathematical differences.
    Off = 0
    World = 1
    WorldViewpoint = 2
    Screen = 3
    ScreenViewpoint = 4
    YAxial = 5
    YAxialViewpoint = 6


class SOBJMesh(StandardObject):
    # padding is used at runtime
    struct = Struct("i4siiiiiii?BH" + "x" * 4 * 18 + "i")
    type = 0x1000000
    signature = Signature("SOBJ")
    revision = 0
    name = ""
    user_data: DictInfo
    shape_index = 0
    material_index = 0
    owner: "CMDL"
    is_visible = True
    priority = 0
    mesh_node_visibility_index = 0
    mesh_node_name = ""

    def __init__(self, owner) -> None:
        super().__init__()
        self.user_data = DictInfo()
        self.owner = owner

    def values(self) -> tuple:
        return (
            self.type,
            self.signature,
            self.revision,
            self.name,
            self.user_data,
            self.shape_index,
            self.material_index,
            Reference(self.owner),
            self.is_visible,
            self.priority,
            self.mesh_node_visibility_index,
            self.mesh_node_name,
        )


class OrientedBoundingBox(StandardObject):
    struct = Struct("I" + "f" * (3 + 9 + 3))
    type = 0x80000000
    center_pos: Vector3
    orientation: Matrix
    bb_size: Vector3

    def __init__(self):
        self.center_pos = Vector3(0, 0, 0)
        self.orientation = OrientationMatrix(
            Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1)
        )
        self.bb_size = Vector3(1, 1, 1)

    def values(self) -> tuple:
        return (self.type, self.center_pos, self.orientation, self.bb_size)


class SOBJShape(StandardObject):
    struct = Struct("i4siiiiiifffiiiiii")
    type = 0x10000001
    signature = Signature("SOBJ")
    revision = 0
    name = ""
    user_data: DictInfo
    # flags are modified at runtime
    flags = 0
    # the bounding box is unused, it gets read but nothing is done with it
    oriented_bounding_box: OrientedBoundingBox
    position_offset: Vector3
    primitive_sets: List[PrimitiveSet]
    # base address is modified at runtime
    base_address = 0
    vertex_attributes: List[VertexAttribute]
    blend_shape = 0

    def __init__(self) -> None:
        super().__init__()
        self.user_data = DictInfo()
        self.position_offset = Vector3(0, 0, 0)
        self.primitive_sets = List()
        self.vertex_attributes = List()
        self.oriented_bounding_box = OrientedBoundingBox()

    def values(self) -> tuple:
        return (
            self.type,
            self.signature,
            self.revision,
            self.name,
            self.user_data,
            self.flags,
            self.oriented_bounding_box,
            self.position_offset,
            self.primitive_sets,
            self.base_address,
            self.vertex_attributes,
            self.blend_shape,
        )


class BoneFlag(IntFlag):
    IsIdentity = 1
    IsTranslateZero = 2
    IsRotateZero = 4
    IsScaleOne = 8
    IsUniformScale = 16
    IsSegmentScaleCompensate = 32
    IsNeedRendering = 64
    IsLocalMatrixCalculate = 128
    IsWorldMatrixCalculate = 256
    HasSkinningMatrix = 512


class Bone(StandardObject):
    struct = Struct("iiiiiiiifffffffff" + "f" * (12 * 3) + "ixxxxxxxx")
    name = ""
    flags = BoneFlag(0)
    joint_id = 0
    parent_id = -1
    parent = None
    child = None
    previous_sibling = None
    next_sibling = None
    scale: Vector3
    rotation: Vector3
    position: Vector3
    local: Matrix
    world: Matrix
    inverse_base: Matrix
    billboard_mode = BillboardMode.Off

    def __init__(self):
        self.scale = Vector3(1, 1, 1)
        self.rotation = Vector3(0, 0, 0)
        self.position = Vector3(0, 0, 0)
        self.local = Matrix(
            Vector4(1, 0, 0, 0), Vector4(0, 1, 0, 0), Vector4(0, 0, 1, 0)
        )
        self.world = Matrix(
            Vector4(1, 0, 0, 0), Vector4(0, 1, 0, 0), Vector4(0, 0, 1, 0)
        )
        self.inverse_base = Matrix(
            Vector4(1, 0, 0, 0), Vector4(0, 1, 0, 0), Vector4(0, 0, 1, 0)
        )

    def values(self):
        return (
            self.name,
            self.flags,
            self.joint_id,
            self.parent_id,
            Reference(self.parent),
            Reference(self.child),
            Reference(self.previous_sibling),
            Reference(self.next_sibling),
            self.scale,
            self.rotation,
            self.position,
            self.local,
            self.world,
            self.inverse_base,
            self.billboard_mode,
        )


class SkeletonFlag(IntFlag):
    IsModelCoordinate = 1
    IsTranslateAnimationEnabled = 2


class SOBJSkeleton(StandardObject):
    struct = Struct("i4siiiiiiiii")
    type = 0x2000000
    signature = Signature("SOBJ")
    revision = 0
    name = ""
    user_data: DictInfo
    bones: DictInfo[Bone]
    root_bone = None
    scaling_rule = SkeletonScalingRule.Standard
    flags = SkeletonFlag(0)

    def __init__(self):
        self.user_data = DictInfo()
        self.bones: DictInfo[Bone] = DictInfo()

    def values(self):
        return (
            self.type,
            self.signature,
            self.revision,
            self.name,
            self.user_data,
            self.bones,
            Reference(self.root_bone),
            self.scaling_rule,
            self.flags,
        )
