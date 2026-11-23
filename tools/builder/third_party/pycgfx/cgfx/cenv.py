from .shared import StandardObject, Signature, List
from .dict import DictInfo
from struct import Struct


class CENVCamera(StandardObject):
    struct = Struct("iii")
    unk1 = 0
    name = ""
    unk2 = 0

    def values(self):
        return (self.unk1, self.name, self.unk2)


class CENVLight(StandardObject):
    struct = Struct("iii")
    unk1 = 0
    name = ""
    unk2 = 0

    def values(self):
        return (self.unk1, self.name, self.unk2)


class CENVLightSet(StandardObject):
    struct = Struct("iii")
    unk = 0
    lights: List[CENVLight]

    def __init__(self):
        self.lights = List()

    def values(self):
        return (self.unk, self.lights)


class CENV(StandardObject):
    struct = Struct("i4siiiiiiiiii")
    type = 0x800000
    signature = Signature("CENV")
    revision = 0x6000000
    name = ""
    user_data: DictInfo
    cameras: List[CENVCamera]
    light_sets: List[CENVLightSet]
    other_list: List

    def __init__(self):
        self.user_data = DictInfo()
        self.cameras = List()
        self.light_sets = List()
        self.other_list = List()

    def values(self):
        return (
            self.type,
            self.signature,
            self.revision,
            self.name,
            self.user_data,
            self.cameras,
            self.light_sets,
            self.other_list,
        )
