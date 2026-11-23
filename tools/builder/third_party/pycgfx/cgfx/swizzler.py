from PIL.Image import Image
from .txob import PixelBasedImage, TXOB, ImageTexture, TextureFormat


def swizzle(im: Image, format: TextureFormat) -> bytes:
    swizzle_map = [
        0x00,
        0x01,
        0x04,
        0x05,
        0x10,
        0x11,
        0x14,
        0x15,
        0x02,
        0x03,
        0x06,
        0x07,
        0x12,
        0x13,
        0x16,
        0x17,
        0x08,
        0x09,
        0x0C,
        0x0D,
        0x18,
        0x19,
        0x1C,
        0x1D,
        0x0A,
        0x0B,
        0x0E,
        0x0F,
        0x1A,
        0x1B,
        0x1E,
        0x1F,
        0x20,
        0x21,
        0x24,
        0x25,
        0x30,
        0x31,
        0x34,
        0x35,
        0x22,
        0x23,
        0x26,
        0x27,
        0x32,
        0x33,
        0x36,
        0x37,
        0x28,
        0x29,
        0x2C,
        0x2D,
        0x38,
        0x39,
        0x3C,
        0x3D,
        0x2A,
        0x2B,
        0x2E,
        0x2F,
        0x3A,
        0x3B,
        0x3E,
        0x3F,
    ]

    bpp_input = 4

    input = im.tobytes()
    output = b""

    for ty in range(im.height // 8):
        for tx in range(im.width // 8):
            tile = bytearray(8 * 8 * format.bytes_per_pixel())
            for y in range(8):
                for x in range(8):
                    input_pixel_offset = (
                        (ty * 8 + y) * im.width + (tx * 8 + x)
                    ) * bpp_input
                    pixel = input[input_pixel_offset : input_pixel_offset + bpp_input][
                        ::-1
                    ]
                    match format:
                        case TextureFormat.RGB8:
                            pixel = pixel[1:]
                        case TextureFormat.RGBA5551:
                            pixel = (
                                (pixel[0] >> 7)
                                | ((pixel[1] & 0xF8) >> 2)
                                | ((pixel[2] & 0xF8) << 3)
                                | ((pixel[3] & 0xF8) << 8)
                            ).to_bytes(2, "little")
                        case TextureFormat.RGB565:
                            pixel = (
                                (pixel[1] >> 3)
                                | ((pixel[2] & 0xFC) << 3)
                                | ((pixel[3] & 0xF8) << 8)
                            ).to_bytes(2, "little")
                        case TextureFormat.RGBA4:
                            pixel = bytes(
                                [
                                    (pixel[0] >> 4) | (pixel[1] & 0xF0),
                                    (pixel[2] >> 4) | (pixel[3] & 0xF0),
                                ]
                            )
                        case _:
                            raise RuntimeError(
                                f"Unsupported pixel format {format.name}"
                            )
                    output_pixel_offset = swizzle_map[y * 8 + x] * len(pixel)
                    tile[output_pixel_offset : output_pixel_offset + len(pixel)] = pixel
            output += tile
    return output


def to_txob(
    im: Image, format: TextureFormat = TextureFormat.RGBA4, mipmaps=1
) -> ImageTexture:
    txob = ImageTexture()
    txob.width = txob.pixel_based_image.width = im.width
    txob.height = txob.pixel_based_image.height = im.height
    im = im.convert("RGBA")
    txob.hw_format = format
    txob.pixel_based_image.data = swizzle(im, format)
    txob.mipmap_level_count = 1
    return txob
