from enum import IntEnum
from .shared import StandardObject, List
from .dict import DictInfo
from struct import Struct


class AnimationGroupMemberType(IntEnum):
    MeshNodeVisibility = 0x00080000
    Mesh = 0x01000000
    TextureSampler = 0x02000000
    BlendOperation = 0x04000000
    MaterialColor = 0x08000000
    Model = 0x10000000
    TextureMapper = 0x20000000
    Bone = 0x40000000
    TextureCoordinator = 0x80000000


class AnimationGroupMember(StandardObject):
    # these names mainly apply to material members, not sure how others work
    # this is sort of accessing a hierarchy, so here's the naming convention:
    # object has fields, and those have values
    object_type = AnimationGroupMemberType.Mesh
    path: str = None
    member: str = None
    blend_operation_index: str = None
    value_offset = 0
    value_size = 0
    unknown = 0
    field_type = 0
    value_index = 0
    parent_name = ""
    field_index = 0
    parent_index = 0

    def refresh_struct(self):
        # padding is used at runtime
        fmt = "Iiiiiiiiixxxxi"
        if self.field_type <= 5:
            fmt += "i"
        self.struct = Struct(fmt)

    def values(self):
        return (
            self.object_type,
            self.path,
            self.member,
            self.blend_operation_index,
            self.value_offset,
            self.value_size,
            self.unknown,
            self.field_type,
            self.value_index,
        ) + (
            (self.parent_name, self.field_index)
            if self.field_type <= 5
            else (self.parent_index,)
        )


class GraphicsAnimationGroup(StandardObject):
    struct = Struct("Iiiiiiiii")
    type = 0x80000000
    flags = 0
    name = ""
    member_type = 0
    members: DictInfo[AnimationGroupMember]
    blend_operations: List[int]
    evalution_timing = 0

    def __init__(self):
        self.members = DictInfo()
        self.blend_operations = List()

    def values(self):
        return (
            self.type,
            self.flags,
            self.name,
            self.member_type,
            self.members,
            self.blend_operations,
            self.evalution_timing,
        )
