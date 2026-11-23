from .shared import StandardObject, Signature, Reference
from .dict import DictInfo
from struct import Struct
from enum import IntEnum


class TextureFormat(IntEnum):
    RGBA8 = 0  # Broken on 3DS home menu - do not use
    RGB8 = 1
    RGBA5551 = 2
    RGB565 = 3
    RGBA4 = 4
    LA8 = 5
    HILO8 = 6
    L8 = 7
    A8 = 8
    LA4 = 9
    L4 = 10
    A4 = 11
    ETC1 = 12
    ETC1A4 = 13

    def bytes_per_pixel(self):
        match self:
            case self.RGBA8:
                return 4
            case self.RGB8:
                return 3
            case f if f >= self.RGBA5551 and f <= self.HILO8:
                return 2
            case self.L8 | self.A8 | self.LA4:
                return 1
            case self.L4 | self.A4:
                return 0.5
            # TODO ETC1 / ETC1A4


class TXOB(StandardObject):
    struct = Struct("i4siiii")
    type: int
    signature = Signature("TXOB")
    revision = 0x5000000
    name = ""
    user_data: DictInfo

    def __init__(self):
        self.user_data = DictInfo()

    def values(self):
        return (self.type, self.signature, self.revision, self.name, self.user_data)


class ReferenceTexture(TXOB):
    struct = Struct(TXOB.struct.format + "ii")
    type = 0x20000004
    txob: TXOB

    def __init__(self, txob: TXOB):
        super().__init__()
        self.txob = txob

    def values(self):
        return (*super().values(), self.txob.name, Reference(self.txob))


class PixelBasedTexture(TXOB):
    # padding is written to at runtime
    struct = Struct(TXOB.struct.format + "iiiiixxxxii")
    height = 0
    width = 0
    gl_format = 0  # unused
    gl_type = 0  # unused
    mipmap_level_count = 0
    location_flag = 0
    hw_format = TextureFormat.RGBA4

    def values(self):
        return (
            *super().values(),
            self.height,
            self.width,
            self.gl_format,
            self.gl_type,
            self.mipmap_level_count,
            self.location_flag,
            self.hw_format,
        )


class PixelBasedImage(StandardObject):
    struct = Struct("iiiiiiii")
    height = 0
    width = 0
    data = b""
    dynamic_allocator = 0
    bits_per_pixel = 0  # unused
    location_address = 0
    memory_address = 0

    def values(self):
        return (
            self.height,
            self.width,
            self.data,
            self.dynamic_allocator,
            self.bits_per_pixel,
            self.location_address,
            self.memory_address,
        )


class ImageTexture(PixelBasedTexture):
    struct = Struct(PixelBasedTexture.struct.format + "i")
    type = 0x20000011
    pixel_based_image: PixelBasedImage

    def __init__(self):
        super().__init__()
        self.pixel_based_image = PixelBasedImage()

    def values(self):
        return (*super().values(), self.pixel_based_image)
