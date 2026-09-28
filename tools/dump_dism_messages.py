# Dump RT_MESSAGETABLE entries (id -> text) from DISM / WIM DLLs, English and Turkish when present.
import ctypes
import glob
import os
import struct
import sys
from ctypes import wintypes

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.LoadLibraryExW.restype = wintypes.HMODULE
k32.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
k32.FindResourceExW.restype = wintypes.HRSRC
k32.FindResourceExW.argtypes = [wintypes.HMODULE, wintypes.LPVOID, wintypes.LPVOID, wintypes.WORD]
k32.LoadResource.restype = wintypes.HGLOBAL
k32.LoadResource.argtypes = [wintypes.HMODULE, wintypes.HRSRC]
k32.LockResource.restype = ctypes.c_void_p
k32.LockResource.argtypes = [wintypes.HGLOBAL]
k32.SizeofResource.restype = wintypes.DWORD
k32.SizeofResource.argtypes = [wintypes.HMODULE, wintypes.HRSRC]

LOAD_LIBRARY_AS_DATAFILE = 0x2
LOAD_LIBRARY_AS_IMAGE_RESOURCE = 0x20
RT_MESSAGETABLE = 11


def dump(path, lang=0x409):
    h = k32.LoadLibraryExW(path, None, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)
    if not h:
        return {}
    out = {}
    r = k32.FindResourceExW(h, ctypes.c_void_p(RT_MESSAGETABLE), ctypes.c_void_p(1), lang)
    if not r:
        return {}
    size = k32.SizeofResource(h, r)
    ptr = k32.LockResource(k32.LoadResource(h, r))
    data = ctypes.string_at(ptr, size)
    (blocks,) = struct.unpack_from("<I", data, 0)
    for b in range(blocks):
        low, high, off = struct.unpack_from("<III", data, 4 + b * 12)
        pos = off
        for mid in range(low, high + 1):
            length, flags = struct.unpack_from("<HH", data, pos)
            raw = data[pos + 4: pos + length]
            text = raw.decode("utf-16-le" if flags & 1 else "mbcs", errors="replace").rstrip("\x00").strip()
            out[mid] = text
            pos += length
    return out


targets = [r"C:\Windows\System32\wimgapi.dll", r"C:\Windows\System32\dismapi.dll"]
targets += glob.glob(r"C:\Windows\System32\Dism\*.dll")
want = sys.argv[1] if len(sys.argv) > 1 else "all"
for t in targets:
    for lang, tag in ((0x409, "en"),):
        msgs = dump(t, lang)
        # MUI: English strings live in en-US\<dll>.mui
        if not msgs:
            mui = os.path.join(os.path.dirname(t), "en-US", os.path.basename(t) + ".mui")
            if os.path.exists(mui):
                msgs = dump(mui, lang)
        if not msgs:
            continue
        for mid, text in sorted(msgs.items()):
            if want == "all" or (mid & 0xFFFF0000) in (0xC1420000, 0xC1510000, 0x80070000, 0xC0040000, 0x800F0000):
                print(f"{os.path.basename(t)}\t0x{mid:08X}\t{text[:220]!r}")
