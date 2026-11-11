"""Synthetic PE image builder for tests.

Produces small but structurally complete PE32 / PE32+ images with imports,
delay imports, exports (incl. forwarders and ordinal-only entries), a resource
tree with VS_VERSIONINFO, a CodeView debug entry, an optional Rich header and
an optional overlay. Everything the builder writes is known, so tests can
assert exact values without relying on any third-party file.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

FILE_ALIGN = 0x200
SECT_ALIGN = 0x1000
HEADERS_SIZE = 0x400
RICH_DANS = 0x536E6144


def align(n: int, a: int) -> int:
    return (n + a - 1) // a * a


def _rol32(v: int, s: int) -> int:
    s %= 32
    v &= 0xFFFFFFFF
    return ((v << s) | (v >> (32 - s))) & 0xFFFFFFFF


class _Section:
    def __init__(self, name: str, rva: int, chars: int):
        self.name, self.rva, self.chars = name, rva, chars
        self.data = bytearray()

    def add(self, blob: bytes, align_to: int = 4) -> int:
        self.data += b"\0" * ((-len(self.data)) % align_to)
        rva = self.rva + len(self.data)
        self.data += blob
        return rva

    def patch(self, rva: int, fmt: str, *vals) -> None:
        struct.pack_into(fmt, self.data, rva - self.rva, *vals)


def _vblock(key: str, value: bytes = b"", vtype: int = 0, children=()) -> bytes:
    body = bytearray(struct.pack("<HHH", 0, 0, vtype)) + key.encode("utf-16-le") + b"\0\0"
    body += b"\0" * ((-len(body)) % 4)
    body += value
    for child in children:
        body += b"\0" * ((-len(body)) % 4)
        body += child
    value_len = len(value) // 2 if vtype == 1 else len(value)
    struct.pack_into("<HH", body, 0, len(body), value_len)
    return bytes(body)


def version_blob(file_version=(1, 2, 3, 4), product_version=(5, 6, 7, 8),
                 strings=None, table="040904b0") -> bytes:
    strings = strings if strings is not None else {"ProductName": "Test Product",
                                                   "FileVersion": "1, 2, 3, 4"}
    fv, pv = file_version, product_version
    fixed = struct.pack("<13I", 0xFEEF04BD, 0x10000,
                        (fv[0] << 16) | fv[1], (fv[2] << 16) | fv[3],
                        (pv[0] << 16) | pv[1], (pv[2] << 16) | pv[3],
                        0x3F, 0, 0x40004, 1, 0, 0, 0)
    str_blocks = [_vblock(k, (v + "\0").encode("utf-16-le"), 1) for k, v in strings.items()]
    sfi = _vblock("StringFileInfo", children=[_vblock(table, children=str_blocks, vtype=1)], vtype=1)
    vfi = _vblock("VarFileInfo", children=[_vblock("Translation", struct.pack("<HH", 0x409, 0x4B0))],
                  vtype=1)
    return _vblock("VS_VERSION_INFO", fixed, 0, [sfi, vfi])


def build_rsrc(base_rva: int, items) -> bytes:
    """items: iterable of (type, name, lang, data); type/name may be int or str."""
    tree: dict = {}
    for t, n, lang, blob in items:
        tree.setdefault(t, {}).setdefault(n, {})[lang] = blob
    buf = bytearray()
    strings_fix, data_fix = [], []

    def write_dir(mapping, level):
        named = sorted(k for k in mapping if isinstance(k, str))
        ids = sorted(k for k in mapping if isinstance(k, int))
        off = len(buf)
        buf.extend(struct.pack("<IIHHHH", 0, 0, 0, 0, len(named), len(ids)))
        entries = []
        for k in named + ids:
            entries.append((len(buf), k))
            buf.extend(b"\0" * 8)
        for eo, k in entries:
            if isinstance(k, str):
                strings_fix.append((eo, k))
            else:
                struct.pack_into("<I", buf, eo, k)
            child = mapping[k]
            if level < 2:
                struct.pack_into("<I", buf, eo + 4, 0x80000000 | write_dir(child, level + 1))
            else:
                de = len(buf)
                buf.extend(b"\0" * 16)
                struct.pack_into("<I", buf, eo + 4, de)
                data_fix.append((de, child))
        return off

    write_dir(tree, 0)
    for eo, s in strings_fix:
        so = len(buf)
        buf.extend(struct.pack("<H", len(s)) + s.encode("utf-16-le"))
        struct.pack_into("<I", buf, eo, 0x80000000 | so)
    for de, blob in data_fix:
        buf.extend(b"\0" * ((-len(buf)) % 4))
        do = len(buf)
        buf.extend(blob)
        struct.pack_into("<II", buf, de, base_rva + do, len(blob))
    return bytes(buf)


@dataclass
class BuiltPE:
    data: bytes
    image_base: int
    expected: dict = field(default_factory=dict)


def build_pe(*, pe32plus: bool = False, dll: bool = True, imports=None, delay_imports=None,
             exports=None, forwarders=None, ordinal_only_exports: int = 0,
             export_ordinal_base: int = 1, export_name: str = "test.dll",
             version: bytes | None = b"default", extra_resources=(), pdb_path: str | None = None,
             rich_entries=None, overlay: bytes = b"", timestamp: int = 0x3EF859D5) -> BuiltPE:
    image_base = 0x180000000 if pe32plus else (0x10000000 if dll else 0x400000)
    thunk_fmt, tsize = ("<Q", 8) if pe32plus else ("<I", 4)
    ord_flag = (1 << 63) if pe32plus else (1 << 31)
    expected: dict = {"imports": {}, "delay_imports": {}, "iat_va": {}}
    dirs = [(0, 0)] * 16

    text = _Section(".text", 0x1000, 0x60000020)
    text.add(b"\xC3" * 0x40)

    rd = _Section(".rdata", 0x2000, 0x40000040)

    def thunk_list(dll_name, funcs, store):
        values, exp = [], []
        for hint, f in enumerate(funcs):
            if isinstance(f, int):
                values.append(ord_flag | f)
                exp.append(("ordinal", f))
            else:
                values.append(rd.add(struct.pack("<H", hint) + f.encode() + b"\0", 2))
                exp.append(("name", f, hint))
        store[dll_name] = exp
        return b"".join(struct.pack(thunk_fmt, v) for v in values) + struct.pack(thunk_fmt, 0)

    if imports:
        descs = []
        for dll_name, funcs in imports.items():
            name_rva = rd.add(dll_name.encode() + b"\0", 2)
            thunks = thunk_list(dll_name, funcs, expected["imports"])
            ilt = rd.add(thunks, tsize)
            iat = rd.add(thunks, tsize)
            expected["iat_va"][dll_name] = [image_base + iat + tsize * i for i in range(len(funcs))]
            descs.append(struct.pack("<IIIII", ilt, 0, 0, name_rva, iat))
        blob = b"".join(descs) + b"\0" * 20
        dirs[1] = (rd.add(blob), len(blob))

    if delay_imports:
        descs = []
        for dll_name, funcs in delay_imports.items():
            name_rva = rd.add(dll_name.encode() + b"\0", 2)
            thunks = thunk_list(dll_name, funcs, expected["delay_imports"])
            int_rva = rd.add(thunks, tsize)
            iat = rd.add(thunks, tsize)
            hmod = rd.add(b"\0" * tsize, tsize)
            descs.append(struct.pack("<8I", 1, name_rva, hmod, iat, int_rva, 0, 0, 0))
        blob = b"".join(descs) + b"\0" * 32
        dirs[13] = (rd.add(blob), len(blob))

    if exports is not None or forwarders or ordinal_only_exports:
        named = list(exports or [])
        fwd = dict(forwarders or {})
        # function table: named code exports, forwarders, then unnamed code exports
        funcs = [(n, None) for n in named] + [(n, t) for n, t in fwd.items()] + \
                [(None, None)] * ordinal_only_exports
        names_sorted = sorted((n, i) for i, (n, _t) in enumerate(funcs) if n)
        edir = rd.add(b"\0" * 40)
        aof = rd.add(b"\0" * 4 * len(funcs))
        aon = rd.add(b"\0" * 4 * len(names_sorted))
        aono = rd.add(b"\0" * 2 * len(names_sorted), 2)
        dll_name_rva = rd.add(export_name.encode() + b"\0", 1)
        for k, (n, idx) in enumerate(names_sorted):
            rd.patch(aon + 4 * k, "<I", rd.add(n.encode() + b"\0", 1))
            rd.patch(aono + 2 * k, "<H", idx)
        exp_funcs = {}
        for idx, (n, target) in enumerate(funcs):
            if target is None:
                frva = 0x1000 + 0x10 * idx
            else:
                frva = rd.add(target.encode() + b"\0", 1)
            rd.patch(aof + 4 * idx, "<I", frva)
            exp_funcs[export_ordinal_base + idx] = {"name": n, "rva": frva, "forwarder": target}
        size = rd.rva + len(rd.data) - edir
        rd.patch(edir, "<IIHHIIIIIII", 0, timestamp, 0, 0, dll_name_rva, export_ordinal_base,
                 len(funcs), len(names_sorted), aof, aon, aono)
        dirs[0] = (edir, size)
        expected["exports"] = exp_funcs

    debug_entry_rva = None
    rsds_rva = None
    if pdb_path is not None:
        guid = bytes(range(16))
        rsds = b"RSDS" + guid + struct.pack("<I", 3) + pdb_path.encode() + b"\0"
        rsds_rva = rd.add(rsds)
        debug_entry_rva = rd.add(struct.pack("<IIHHIIII", 0, timestamp, 0, 0, 2, len(rsds), rsds_rva, 0))
        dirs[6] = (debug_entry_rva, 28)

    if not rd.data:
        rd.add(b"\0" * 4)

    sections = [text, rd]
    res_items = list(extra_resources)
    if version == b"default":
        version = version_blob()
    if version:
        res_items.append((16, 1, 0x409, version))
    if res_items:
        rsrc_rva = rd.rva + align(len(rd.data), SECT_ALIGN)
        rs = _Section(".rsrc", rsrc_rva, 0x40000040)
        rs.data += build_rsrc(rsrc_rva, res_items)
        sections.append(rs)
        dirs[2] = (rsrc_rva, len(rs.data))

    # Raw layout
    raw = HEADERS_SIZE
    layout = []
    for s in sections:
        rsize = align(len(s.data), FILE_ALIGN)
        layout.append((s, raw, rsize))
        raw += rsize
    if debug_entry_rva is not None:
        rd_raw = next(r for s, r, _ in layout if s is rd)
        rd.patch(debug_entry_rva + 24, "<I", rd_raw + (rsds_rva - rd.rva))

    # DOS header + stub + optional Rich header
    hdr = bytearray(HEADERS_SIZE)
    hdr[0:2] = b"MZ"
    stub = b"\x0e\x1f\xba\x0e\x00\xb4\x09\xcd\x21\xb8\x01\x4c\xcd\x21This program cannot be run in DOS mode.\r\r\n$"
    hdr[0x40:0x40 + len(stub)] = stub
    dans = 0x80
    if rich_entries:
        csum = dans
        for i in range(dans):
            if 0x3C <= i < 0x40:
                continue
            csum = (csum + _rol32(hdr[i], i)) & 0xFFFFFFFF
        for prod, build, count in rich_entries:
            csum = (csum + _rol32((prod << 16) | build, count)) & 0xFFFFFFFF
        key = csum
        rich = struct.pack("<4I", RICH_DANS ^ key, key, key, key)
        for prod, build, count in rich_entries:
            rich += struct.pack("<II", ((prod << 16) | build) ^ key, count ^ key)
        rich += b"Rich" + struct.pack("<I", key)
        hdr[dans:dans + len(rich)] = rich
        e_lfanew = align(dans + len(rich), 8)
        expected["rich_key"] = key
    else:
        e_lfanew = dans
    struct.pack_into("<I", hdr, 0x3C, e_lfanew)

    opt_size = (112 if pe32plus else 96) + 16 * 8
    if pe32plus:
        chars = 0x0022 | (0x2000 if dll else 0)
    else:
        chars = 0x0102 | (0x2000 if dll else 0)
    machine = 0x8664 if pe32plus else 0x14C
    p = e_lfanew
    hdr[p:p + 4] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", hdr, p + 4, machine, len(sections), timestamp, 0, 0, opt_size, chars)
    last = sections[-1]
    size_of_image = align(last.rva + len(last.data), SECT_ALIGN)
    opt = p + 24
    common = (0x1000, 0x1000)  # entry point, base of code
    if pe32plus:
        struct.pack_into("<HBBIIIIIQIIHHHHHHIIIIHHQQQQII", hdr, opt,
                         0x20B, 14, 0, 0x200, 0x400, 0, *common, image_base,
                         SECT_ALIGN, FILE_ALIGN, 6, 0, 0, 0, 6, 0, 0, size_of_image,
                         HEADERS_SIZE, 0, 2, 0x0160, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
        dd = opt + 112
    else:
        struct.pack_into("<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII", hdr, opt,
                         0x10B, 6, 0, 0x200, 0x400, 0, *common, 0x2000, image_base,
                         SECT_ALIGN, FILE_ALIGN, 4, 0, 0, 0, 4, 0, 0, size_of_image,
                         HEADERS_SIZE, 0, 2, 0, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
        dd = opt + 96
    for i, (rva, size) in enumerate(dirs):
        struct.pack_into("<II", hdr, dd + 8 * i, rva, size)
    st = opt + opt_size
    for i, (s, rptr, rsize) in enumerate(layout):
        struct.pack_into("<8sIIIIIIHHI", hdr, st + 40 * i, s.name.encode(), len(s.data), s.rva,
                         rsize, rptr, 0, 0, 0, 0, s.chars)

    out = bytearray(hdr)
    for s, rptr, rsize in layout:
        out += bytes(s.data) + b"\0" * (rsize - len(s.data))
    out += overlay
    expected.update({
        "e_lfanew": e_lfanew, "size_of_image": size_of_image,
        "sections": [(s.name, s.rva, rptr, rsize) for s, rptr, rsize in layout],
        "overlay_offset": raw if overlay else None,
    })
    return BuiltPE(bytes(out), image_base, expected)
