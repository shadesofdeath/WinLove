"""Minimal read-only offreg.dll wrapper (System32, no admin, no RegLoadKey) for the scan tools."""
import ctypes
from ctypes import wintypes

_dll = ctypes.WinDLL("offreg.dll")
_ORHKEY = ctypes.c_void_p

_dll.OROpenHive.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(_ORHKEY)]
_dll.ORCloseHive.argtypes = [_ORHKEY]
_dll.OROpenKey.argtypes = [_ORHKEY, wintypes.LPCWSTR, ctypes.POINTER(_ORHKEY)]
_dll.ORCloseKey.argtypes = [_ORHKEY]
_dll.OREnumKey.argtypes = [_ORHKEY, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD),
                           wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
_dll.OREnumValue.argtypes = [_ORHKEY, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD),
                             ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)]
_dll.ORQueryInfoKey.argtypes = [_ORHKEY, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD),
                                ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD),
                                ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD),
                                ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD),
                                ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]

REG_SZ, REG_EXPAND_SZ, REG_BINARY, REG_DWORD, REG_MULTI_SZ, REG_QWORD = 1, 2, 3, 4, 7, 11


class Key:
    def __init__(self, handle, hive=None):
        self.h = handle
        self._hive = hive  # keeps the hive alive

    def open(self, path: str) -> "Key | None":
        h = _ORHKEY()
        if _dll.OROpenKey(self.h, path, ctypes.byref(h)) != 0:
            return None
        return Key(h, self._hive or self)

    def subkeys(self):
        i = 0
        buf = ctypes.create_unicode_buffer(512)
        while True:
            n = wintypes.DWORD(512)
            r = _dll.OREnumKey(self.h, i, buf, ctypes.byref(n), None, None, None)
            if r != 0:
                return
            yield buf.value
            i += 1

    def values(self):
        """(name, type, data) — data decoded for strings / numbers, bytes otherwise."""
        i = 0
        name = ctypes.create_unicode_buffer(16384)
        while True:
            n = wintypes.DWORD(16384)
            t = wintypes.DWORD()
            size = wintypes.DWORD(0)
            r = _dll.OREnumValue(self.h, i, name, ctypes.byref(n), ctypes.byref(t), None, ctypes.byref(size))
            if r not in (0, 234):
                return
            data = (ctypes.c_ubyte * max(size.value, 1))()
            n = wintypes.DWORD(16384)
            r = _dll.OREnumValue(self.h, i, name, ctypes.byref(n), ctypes.byref(t), data, ctypes.byref(size))
            if r != 0:
                return
            raw = bytes(data[: size.value])
            yield name.value, t.value, decode(t.value, raw)
            i += 1

    def close(self):
        if self._hive is None:
            _dll.ORCloseHive(self.h)
        else:
            _dll.ORCloseKey(self.h)


def decode(t: int, raw: bytes):
    if t in (REG_SZ, REG_EXPAND_SZ):
        return raw.decode("utf-16-le", "replace").rstrip("\0")
    if t == REG_MULTI_SZ:
        return [s for s in raw.decode("utf-16-le", "replace").split("\0") if s]
    if t == REG_DWORD and len(raw) >= 4:
        return int.from_bytes(raw[:4], "little")
    if t == REG_QWORD and len(raw) >= 8:
        return int.from_bytes(raw[:8], "little")
    return raw


def open_hive(path: str) -> Key:
    h = _ORHKEY()
    r = _dll.OROpenHive(str(path), ctypes.byref(h))
    if r != 0:
        raise OSError(r, f"OROpenHive {path}")
    return Key(h)
