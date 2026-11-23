from .shared import InlineObject, Signature, StandardObject, StringTable
from typing import TypeVar, Generic
from struct import Struct
from . import patricia

T = TypeVar("T", bound=StandardObject)


class Node(InlineObject, Generic[T]):
    struct = Struct("ihhii")
    refbit: int
    left_index: int
    right_index: int
    name: str
    content: T

    def __init__(self, name: str, content: T) -> None:
        super().__init__()
        self.name = name
        self.content = content
        self.refbit = -1
        self.left_index = 0
        self.right_index = 0

    def values(self) -> tuple:
        return (self.refbit, self.left_index, self.right_index, self.name, self.content)

    def get_name(self) -> str:
        return self.name or ""


class DICT(StandardObject, Generic[T]):
    signature = Signature("DICT")
    nodes: list[Node]

    def __init__(self) -> None:
        super().__init__()
        self.nodes = [Node(None, None)]

    def refresh_struct(self):
        self.struct = Struct("4sii" + Node.struct.format * len(self.nodes))

    def values(self) -> tuple:
        self.refresh_struct()
        return (self.signature, self.struct.size, self.len()) + tuple(self.nodes)

    def len(self):
        return len(self.nodes) - 1

    def __getitem__(self, name: str) -> T:
        if isinstance(name, int):
            return self.nodes[name + 1].content
        # could be smarter but eh
        for n in self.nodes:
            if n.name == name:
                return n.content
        return None

    def __iter__(self):
        for n in self.nodes[1:]:
            yield n.name

    def get_index(self, name: str) -> int:
        for i in range(len(self.nodes)):
            if self.nodes[i].name == name:
                return i - 1

    def add(self, name: str, data: T):
        self.nodes.append(Node(name, data))
        self.regenerate()

    def regenerate(self):
        tree = patricia.generate(
            [n.get_name() for n in self.nodes if n != self.nodes[0]]
        )
        tree.root.idx_entry = -1
        for n in self.nodes:
            p = tree[n.get_name()]
            assert p.name == n.get_name().ljust(
                tree.string_length, "\0"
            ), f"{p.name}, {n.get_name()}"
            if n != self.nodes[0]:
                n.refbit = p.refbit
            n.left_index = p.left.idx_entry + 1
            n.right_index = p.right.idx_entry + 1


class DictInfo(InlineObject, Generic[T]):
    struct = Struct("ii")
    dict: DICT

    def __init__(self) -> None:
        super().__init__()
        self.dict = DICT()

    def values(self) -> tuple:
        return (self.dict.len(), self.dict if self.dict.len() else None)

    def add(self, name: str, data: T):
        self.dict.add(name, data)

    def __getitem__(self, name: str) -> T:
        return self.dict[name]

    def len(self) -> int:
        return self.dict.len()

    def __iter__(self):
        return iter(self.dict)

    def get_index(self, name: str) -> int:
        return self.dict.get_index(name)
