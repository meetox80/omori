#!/usr/bin/env python3
import re
import struct
import sys
from pathlib import Path

VECTOR = {
    "add": 0, "mul": 1, "max": 2, "min": 3, "seq": 4, "sgt": 5, "sge": 6, "sne": 7, "frc": 8, "trunc": 9, "floor": 10, "mad": 11,
    "cndeq": 12, "cndge": 13, "cndgt": 14, "dp4": 15, "dp3": 16, "dp2add": 17, "max4": 19, "killeq": 24, "killgt": 25, "killge": 26,
    "killne": 27,
}
UNARY = {"frc", "trunc", "floor", "max4"}
TERNARY = {"mad", "cndeq", "cndge", "cndgt", "dp2add"}
RETAIN_PREV = 50
USAGE = {"position": 0, "normal": 3, "texcoord": 5, "color": 10}
FORMAT = {"position": 37, "normal": 57, "texcoord": 38, "color": 6}
ALLOC = {"position": 1, "interpolators": 2, "colors": 2}
COMPONENT = {"x": 0, "y": 1, "z": 2, "w": 3, "r": 0, "g": 1, "b": 2, "a": 3}
FETCH_SWIZZLE = {"x": 0, "y": 1, "z": 2, "w": 3, "0": 4, "1": 5, "_": 7}
EXEC_MAX = 6
EXPORT_POSITION = 62
PIXEL_INPUTS = 3


class AsmError(Exception):
    pass


def reg(text, kinds):
    m = re.fullmatch(r"([rco])(\d+)", text)
    if not m or m.group(1) not in kinds:
        raise AsmError(f"bad register {text}")
    n = int(m.group(2))
    if m.group(1) == "r" and n > 63 or m.group(1) == "c" and n > 255:
        raise AsmError(f"register out of range {text}")
    return m.group(1), n


def split_operand(text):
    neg = text.startswith("-")
    if neg:
        text = text[1:]
    name, _, swz = text.partition(".")
    return neg, name, swz


def write_mask(swz):
    if not swz:
        return 0xF
    mask = 0
    last = -1
    for ch in swz:
        if ch not in COMPONENT or COMPONENT[ch] <= last:
            raise AsmError(f"bad write mask .{swz}")
        last = COMPONENT[ch]
        mask |= 1 << last
    return mask


def components(swz):
    if not swz:
        return [0, 1, 2, 3]
    if len(swz) == 1:
        swz *= 4
    if len(swz) != 4 or any(ch not in COMPONENT for ch in swz):
        raise AsmError(f"bad swizzle .{swz}")
    return [COMPONENT[ch] for ch in swz]


def alu_swizzle(swz):
    return sum(((c - i) & 3) << (2 * i) for i, c in enumerate(components(swz)))


def alu_source(text):
    neg, name, swz = split_operand(text)
    kind, n = reg(name, "rc")
    return {"neg": int(neg), "temp": int(kind == "r"), "reg": n, "swz": alu_swizzle(swz)}


class Instr:
    def __init__(self, fetch, words, temps, patch=None):
        self.fetch = fetch
        self.words = words
        self.temps = temps
        self.patch = patch
        self.serialize = False


def parse_alu(op, args):
    if op not in VECTOR:
        raise AsmError(f"unknown instruction {op}")
    want = 1 if op in UNARY else 3 if op in TERNARY else 2
    if len(args) != want + 1:
        raise AsmError(f"{op} takes {want} sources")
    _, dname, dswz = split_operand(args[0])
    dkind, dreg = reg(dname, "ro")
    srcs = [alu_source(a) for a in args[1:]]
    if op in UNARY:
        srcs.append(dict(srcs[0]))
    if len(srcs) == 2:
        srcs.append({"neg": 0, "temp": 1, "reg": 0, "swz": 0})
    export = dkind == "o"
    d0 = dreg | (int(export) << 15) | (write_mask(dswz) << 16) | (RETAIN_PREV << 26)
    d1 = srcs[2]["swz"] | (srcs[1]["swz"] << 8) | (srcs[0]["swz"] << 16)
    d1 |= (srcs[2]["neg"] << 24) | (srcs[1]["neg"] << 25) | (srcs[0]["neg"] << 26)
    d2 = srcs[2]["reg"] | (srcs[1]["reg"] << 8) | (srcs[0]["reg"] << 16) | (VECTOR[op] << 24)
    d2 |= (srcs[2]["temp"] << 29) | (srcs[1]["temp"] << 30) | (srcs[0]["temp"] << 31)
    temps = [s["reg"] for s in srcs if s["temp"]] + ([] if export else [dreg])
    instr = Instr(False, [d0, d1, d2], temps)
    instr.export = dreg if export else None
    return instr


def parse_vfetch(args):
    if len(args) != 3:
        raise AsmError("vfetch takes a destination, an index register and an input")
    _, dname, dswz = split_operand(args[0])
    _, dreg = reg(dname, "r")
    _, sname, sswz = split_operand(args[1])
    _, sreg = reg(sname, "r")
    m = re.fullmatch(r"([a-z]+)(\d)", args[2])
    if not m or m.group(1) not in USAGE:
        raise AsmError(f"bad vertex input {args[2]}")
    usage, stream = m.group(1), int(m.group(2))
    dswz = dswz or "xyzw"
    if len(dswz) != 4 or any(ch not in FETCH_SWIZZLE for ch in dswz):
        raise AsmError(f"bad fetch swizzle .{dswz}")
    sswz = sswz or "x"
    if len(sswz) != 1 or sswz not in "xyzw":
        raise AsmError(f"bad index swizzle .{sswz}")
    d0 = (sreg << 5) | (dreg << 12) | (1 << 19) | (31 << 20) | (2 << 25) | (COMPONENT[sswz] << 30)
    d1 = sum(FETCH_SWIZZLE[ch] << (3 * i) for i, ch in enumerate(dswz)) | (FORMAT[usage] << 16)
    return Instr(True, [d0, d1, 0], [sreg, dreg], (USAGE[usage] << 12) | (stream << 16))


def parse_tfetch(args):
    if len(args) != 3:
        raise AsmError("tfetch takes a destination, coordinates and a texture")
    _, dname, dswz = split_operand(args[0])
    _, dreg = reg(dname, "r")
    if dswz and dswz != "xyzw":
        raise AsmError("tfetch writes all components")
    _, sname, sswz = split_operand(args[1])
    _, sreg = reg(sname, "r")
    sswz = sswz or "xy"
    if len(sswz) != 2:
        raise AsmError("tfetch reads two coordinates")
    m = re.fullmatch(r"tf(\d+)", args[2])
    if not m or int(m.group(1)) > 31:
        raise AsmError(f"bad texture {args[2]}")
    src = COMPONENT[sswz[0]] | (COMPONENT[sswz[1]] << 2)
    d0 = 1 | (sreg << 5) | (dreg << 12) | (1 << 19) | (int(m.group(1)) << 20) | (src << 26)
    d1 = 0x688 | 0x1F1FF000
    d2 = 1 << 14
    return Instr(True, [d0, d1, d2], [sreg, dreg])


def parse(path):
    clauses = []
    for number, raw in enumerate(Path(path).read_text().splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        try:
            op, _, rest = line.partition(" ")
            args = [a.strip() for a in rest.split(",")] if rest.strip() else []
            if op == "exec":
                clauses.append(("exec", []))
            elif op == "alloc":
                if len(args) != 1 or args[0] not in ALLOC:
                    raise AsmError("alloc takes position, interpolators or colors")
                clauses.append(("alloc", ALLOC[args[0]]))
            else:
                if not clauses or clauses[-1][0] != "exec":
                    raise AsmError("instruction outside an exec clause")
                if op == "vfetch":
                    instr = parse_vfetch(args)
                elif op == "tfetch":
                    instr = parse_tfetch(args)
                else:
                    instr = parse_alu(op, args)
                clauses[-1][1].append(instr)
        except AsmError as e:
            raise AsmError(f"{path}:{number}: {e}") from None
    return clauses


def assemble(path, vertex):
    clauses = parse(path)
    cf = []
    body = []
    last_fetch = None
    for kind, value in clauses:
        if kind == "alloc":
            cf.append(("alloc", value))
            continue
        if not value:
            raise AsmError(f"{path}: empty exec clause")
        for at in range(0, len(value), EXEC_MAX):
            group = value[at:at + EXEC_MAX]
            for instr in group:
                if last_fetch is not None and instr.fetch != last_fetch:
                    instr.serialize = True
                last_fetch = instr.fetch
            cf.append(("exec", (len(body), group)))
            body.extend(group)
    execs = [i for i, c in enumerate(cf) if c[0] == "exec"]
    if not execs:
        raise AsmError(f"{path}: no exec clause")
    if len(cf) % 2:
        cf.append(("nop", None))
    base = len(cf) // 2
    words = []
    for i, (kind, value) in enumerate(cf):
        if kind == "exec":
            first, group = value
            seq = 0
            vc = 0
            for k, instr in enumerate(group):
                seq |= (int(instr.fetch) | (int(instr.serialize) << 1)) << (2 * k)
                if instr.fetch and instr.patch is not None:
                    vc |= 1 << k
            opcode = 2 if i == execs[-1] else 1
            w0 = (base + first) | (len(group) << 12) | (seq << 16) | ((vc & 0xF) << 28)
            w1 = ((vc >> 4) & 3) | (1 << 9) | (opcode << 12)
        elif kind == "alloc":
            w0 = 0
            w1 = (value << 9) | (12 << 12)
        else:
            w0 = w1 = 0
        words.append((w0, w1))
    code = []
    for a, b in zip(words[0::2], words[1::2]):
        code += [a[0], (a[1] & 0xFFFF) | ((b[0] & 0xFFFF) << 16), (b[0] >> 16) | (b[1] << 16)]
    patches = []
    for k, instr in enumerate(body):
        code += instr.words
        if instr.patch is not None:
            patches.append(instr.patch | (base + k))
    temps = max(t for instr in body for t in instr.temps)
    exports = [instr.export for instr in body if getattr(instr, "export", None) is not None]
    if vertex:
        interps = [e for e in exports if e < EXPORT_POSITION]
        if EXPORT_POSITION not in exports or not interps:
            raise AsmError(f"{path}: a vertex shader exports a position and interpolators")
        program_control = temps | (1 << 16) | (max(interps) << 20)
        context_misc = 0
    else:
        if exports != [0] * len(exports) or not exports:
            raise AsmError(f"{path}: a pixel shader exports color 0")
        program_control = (max(temps, PIXEL_INPUTS - 1) << 8) | (1 << 28)
        context_misc = 4
    sh_size = len(code) * 4
    off_shader = 9 * 4
    data = [0, sh_size, program_control, context_misc, 0, 0, 0, len(patches) if vertex else 0] + (patches if vertex else [])
    offset = off_shader + len(data) * 4
    header = [0x102A1101 if vertex else 0x102A1100, offset, sh_size, 0, 0x24, 0, off_shader, 0, 0]
    out = header + data + code
    return struct.pack(f">{len(out)}I", *out)


def main(argv):
    root = Path(__file__).resolve().parent.parent
    src = Path(argv[1]) if len(argv) > 1 else root / "engine/src/platform/xenon/shaders"
    done = 0
    for path in sorted(src.iterdir()):
        if path.suffix not in (".xvs", ".xps"):
            continue
        vertex = path.suffix == ".xvs"
        blob = assemble(path, vertex)
        out = path.with_suffix(".vsu" if vertex else ".psu")
        out.write_bytes(blob)
        print(f"{out.name} {len(blob)} bytes")
        done += 1
    if not done:
        raise AsmError(f"no shaders in {src}")


if __name__ == "__main__":
    try:
        main(sys.argv)
    except AsmError as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(1)
