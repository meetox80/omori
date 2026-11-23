from .dict import DictInfo
from .shared import Signature, StandardObject, Vector3, Vector4, Matrix, List
from .sobj import SOBJMesh, SOBJShape, SOBJSkeleton
from struct import Struct
from .mtob import MTOB
from enum import IntEnum
from .animation import GraphicsAnimationGroup


class CMDL(StandardObject):
    struct = Struct("i4siiiiiiixxxxiifffffffff" + "f" * 12 * 2 + "i" * 11)
    type = 0x40000012
    signature = Signature("CMDL")
    revision = 0x7000000
    name = ""
    user_data: DictInfo
    flags = 1  # looks transform-related, specific meaning unknown
    branch_visible = False  # unused
    nr_children = 0
    animation_group_descriptions: DictInfo[GraphicsAnimationGroup]
    scale: Vector3
    rotation: Vector3
    translation: Vector3
    local: Matrix
    world: Matrix
    meshes: List[SOBJMesh]
    materials: DictInfo[MTOB]
    shapes: List[SOBJShape]
    mesh_nodes: DictInfo
    visible = True
    cull_mode = 0
    layer_id = 0

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
        self.meshes = List()
        self.materials = DictInfo()
        self.shapes = List()
        self.mesh_nodes = DictInfo()

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
            self.meshes,
            self.materials,
            self.shapes,
            self.mesh_nodes,
            self.visible,
            self.cull_mode,
            self.layer_id,
        )


class CMDLWithSkeleton(CMDL):
    struct = Struct(CMDL.struct.format + "i")
    type = CMDL.type | 0x80
    skeleton: SOBJSkeleton = None

    def values(self) -> tuple:
        return super().values() + (self.skeleton,)
