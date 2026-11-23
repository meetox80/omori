from abc import ABC, abstractmethod
import struct
from collections import OrderedDict
from typing import Generic, TypeVar

T = TypeVar("T")


class Signature:
    data: str

    def __init__(self, data) -> None:
        self.data = data


class StringTable:
    table: OrderedDict[bytes, int]
    total = 0
    padding: int
    offset: int

    def __init__(self) -> None:
        self.table = OrderedDict()

    @staticmethod
    def correct(s: bytes | str) -> bytes:
        if isinstance(s, str):
            return s.encode() + b"\0"
        else:
            # textures are aligned to 16 bytes
            # vertex buffers aren't but aligning everything is easier
            return s + b"\0" * (-len(s) % 16)
        return s

    def add(self, s: bytes | str):
        s = self.correct(s)
        if s not in self.table:
            self.table[s] = self.total
            self.total += len(s)

    def prepare(self, offset: int) -> int:
        self.offset = offset
        self.padding = -(offset + self.total) % 16
        self.padding ^= 8  # align content to 16, aka header to halfway through
        self.total += self.padding
        return offset + self.total

    def size(self) -> int:
        return self.total

    def empty(self) -> bool:
        return len(self.table) == 0

    def get(self, s: str) -> int:
        return self.offset + self.table[self.correct(s)]

    def write(self) -> bytes:
        return b"".join(self.table.keys()) + b"\0" * self.padding


class BaseObject(ABC):
    struct: struct.Struct
    offset: int
    inline = False

    def refresh_struct(self):
        pass

    @abstractmethod
    def values(self) -> tuple:
        pass

    def flat_values(self):
        self.refresh_struct()
        for v in self.values():
            if isinstance(v, InlineObject):
                # v.refresh_struct()
                # vals = list(v.flat_values())
                # bufs = []
                # for i in range(len(vals)):
                #     if vals[i] is None: vals[i] = 0
                #     if isinstance(vals[i], bytes): bufs.append(i)
                #     if isinstance(vals[i], Signature): vals[i] = vals[i].data.encode()
                #     if isinstance(vals[i], StandardObject): vals[i] = 0
                #     if isinstance(vals[i], str): vals[i] = 0
                # for i in bufs[::-1]:
                #     vals[i:i+1] = [0, 0]
                # v.struct.pack(*vals)
                yield from v.flat_values()
            else:
                yield v

    def real_values(self, strings, imag) -> tuple:
        values = []
        fmt_pos = 0
        offset = self.offset
        for v in self.flat_values():
            # add values
            if isinstance(v, StandardObject):
                values.append(v.offset - offset)
            elif isinstance(v, InlineObject):
                values += list(v.real_values(strings, imag))
            elif isinstance(v, Reference):
                if v.obj is None:
                    values.append(0)
                else:
                    values.append(v.obj.offset - offset)
            elif isinstance(v, Signature):
                values.append(v.data.encode())
            elif isinstance(v, str):
                # string
                values.append(strings.get(v) - offset)
            elif isinstance(v, bytes):
                # data
                values.append(len(v))
                offset += 4
                fmt_pos += 1
                values.append(imag.get(v) - offset if v else 0)
            elif v is None:
                # null
                values.append(0)
            else:
                values.append(v)
            # update offset
            if isinstance(v, InlineObject):
                fmt_pos += v.size()
            else:
                fmt_pos += 1
                while fmt_pos < len(self.struct.format):
                    if self.struct.format[fmt_pos] == "x":
                        fmt_pos += 1
                        continue
                    try:
                        offset = self.offset + struct.calcsize(
                            self.struct.format[:fmt_pos]
                        )
                        break
                    except struct.error:
                        if self.struct.format[fmt_pos - 1 : fmt_pos + 1] != "4s":
                            raise RuntimeError(
                                f"can't use numbers other than 4s (found {self.struct.format[fmt_pos:fmt_pos+2]})"
                            )
                        fmt_pos += 1
                        continue
        return values

    def prepare(self, offset: int, strings: StringTable, imag: StringTable) -> int:
        """offset is current offset, returns new offset"""
        self.refresh_struct()
        self.offset = offset
        values = self.values()
        old_len = 0
        while old_len != len(values):
            old_len = len(values)
            values = [
                vv
                for v in values
                for vv in (v.values() if isinstance(v, InlineObject) else [v])
            ]
        offset = self.offset + self.size()
        for v in values:
            if isinstance(v, StandardObject):
                offset = v.prepare(offset, strings, imag)
            elif isinstance(v, str):
                # string (not signature)
                strings.add(v)
            elif isinstance(v, bytes):
                imag.add(v)
        return offset

    def write(self, strings: StringTable, imag: StringTable) -> bytes:
        values = self.real_values(strings, imag)
        data = self.struct.pack(*values)
        for v in self.flat_values():
            if isinstance(v, StandardObject):
                data += v.write(strings, imag)
        return data

    def size(self) -> int:
        self.refresh_struct()
        return self.struct.size

    def __eq__(self, other) -> bool:
        return isinstance(other, type(self)) and self.values() == other.values()


class StandardObject(BaseObject):
    pass


class InlineObject(BaseObject):
    pass


class Reference:
    def __init__(self, obj: StandardObject):
        self.obj = obj


class ListData(StandardObject, Generic[T]):
    contents: list[T]

    def __init__(self, list=None) -> None:
        super().__init__()
        self.contents = list if list else []

    def refresh_struct(self):
        self.struct = struct.Struct("i" * len(self.contents))

    def values(self) -> tuple:
        return tuple(self.contents)

    def __len__(self):
        return len(self.contents)

    def add(self, value: T):
        self.contents.append(value)


class List(InlineObject, Generic[T]):
    data: ListData

    def __init__(self, list=None) -> None:
        super().__init__()
        self.data = ListData(list)

    def refresh_struct(self):
        self.struct = struct.Struct("ii")

    def values(self) -> tuple:
        return (len(self.data), self.data if len(self.data) else None)

    def add(self, value: T):
        self.data.add(value)

    def __len__(self):
        return len(self.data)


class Vector3(InlineObject):
    struct = struct.Struct("fff")
    x: float
    y: float
    z: float

    def __init__(self, x, y, z):
        self.x = x
        self.y = y
        self.z = z

    def values(self) -> tuple:
        return (self.x, self.y, self.z)


class Vector4(Vector3):
    struct = struct.Struct("ffff")
    w: float

    def __init__(self, x, y, z, w):
        super().__init__(x, y, z)
        self.w = w

    def values(self):
        return (self.x, self.y, self.z, self.w)


class Matrix(InlineObject):
    struct = struct.Struct("f" * 12)
    columns: list[Vector4]

    def __init__(self, col1, col2, col3):
        self.columns = [col1, col2, col3]

    def values(self) -> tuple:
        return tuple(self.columns)


class OrientationMatrix(InlineObject):
    struct = struct.Struct("f" * 9)
    columns: list[Vector3]

    def __init__(self, col1, col2, col3):
        self.columns = [col1, col2, col3]

    def values(self) -> tuple:
        return tuple(self.columns)


class Color(InlineObject):
    def __init__(self, r, g, b, a):
        self.r = r
        self.g = g
        self.b = b
        self.a = a

    def values(self):
        return (self.r, self.g, self.b, self.a)


class ColorByte(Color):
    struct = struct.Struct("BBBB")


class ColorFloat(Color):
    struct = struct.Struct("ffff")

    def as_byte(self) -> ColorByte:
        return ColorByte(
            int(self.r * 255), int(self.g * 255), int(self.b * 255), int(self.a * 255)
        )
