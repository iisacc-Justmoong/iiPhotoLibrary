"""Owner-thread Python access to the native LVUI photo model."""
from __future__ import annotations

import ctypes
import os
from pathlib import Path
import sys
import threading
import weakref

_NAMES = ("rows", "active", "busy", "can_share", "information_open", "empty",
          "motion_enabled", "compact_layout", "selected_video", "selected_bytes",
          "reveal_revision", "thumbnail_size", "viewport_width", "viewport_height",
          "access", "status", "selected_id", "selected_name", "selected_preview",
          "selected_created", "reveal_key", "row.gallery_key", "row.name", "row.preview",
          "row.preview_revision", "row.created", "row.bytes", "row.video", "row.selected", "row.id")
_IDS = {name: i for i, name in enumerate(_NAMES)}
_ACTIONS = ("connect", "pick_files", "refresh", "add_files", "open", "copy",
            "duplicate", "share", "trash", "prioritize", "preview_changed")
_COMMANDS = {name: i for i, name in enumerate(("connect", "pick_files", "refresh", "close_information",
              "open", "copy", "duplicate", "share", "trash", "hide", "show"))}
_TEXT = ("access", "status", "selected_id", "selected_name", "selected_preview", "selected_created", "reveal_key")
_BOOL = ("active", "busy", "can_share", "information_open", "empty", "motion_enabled", "compact_layout", "selected_video")
_UINT = ("selected_bytes", "reveal_revision")
_NUMBER = ("thumbnail_size", "viewport_width", "viewport_height")
_ROW_TEXT = ("id", "gallery_key", "name", "preview", "preview_revision", "created")
_OBSERVER = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p)
_ACTION = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_char_p), ctypes.c_size_t)
_LOAD_LOCK = threading.Lock()
_LIBRARIES = {}
_DLL_DIRECTORIES = {}


class _Entry(ctypes.Structure):
    _fields_ = [(name, ctypes.c_char_p) for name in _ROW_TEXT] + [("bytes", ctypes.c_uint64), ("video", ctypes.c_int)]


class _Flags(ctypes.Structure):
    _fields_ = [("active", ctypes.c_int), ("busy", ctypes.c_int), ("can_share", ctypes.c_int),
                ("access", ctypes.c_char_p), ("status", ctypes.c_char_p)]


def _utf8(value):
    if not isinstance(value, str):
        raise TypeError("photo text must be a string")
    if "\0" in value:
        raise ValueError("photo text cannot contain NUL")
    return value.encode("utf-8")


def _boolean(value):
    if not isinstance(value, bool):
        raise TypeError("photo flags must be Boolean")
    return int(value)


def _flags(value):
    unknown = set(value) - {"active", "busy", "can_share", "access", "status"}
    if unknown:
        raise ValueError("unknown photo flags: " + ", ".join(sorted(unknown)))
    return _Flags(*(_boolean(value.get(name, False)) for name in ("active", "busy", "can_share")),
                  _utf8(value.get("access", "")), _utf8(value.get("status", "")))


def _entry(value):
    unknown = set(value) - set(_ROW_TEXT) - {"bytes", "video"}
    if unknown:
        raise ValueError("unknown photo entry fields: " + ", ".join(sorted(unknown)))
    size = value.get("bytes", 0)
    if not isinstance(size, int) or isinstance(size, bool) or not 0 <= size <= 2**64 - 1:
        raise ValueError("photo byte count must be an unsigned 64-bit integer")
    return _Entry(*(_utf8(value.get(name, "")) for name in _ROW_TEXT), size, _boolean(value.get("video", False)))


def _strings(values):
    if isinstance(values, (str, bytes)):
        raise TypeError("photo values must be an iterable of strings")
    encoded = [_utf8(value) for value in values]
    return (ctypes.c_char_p * len(encoded))(*encoded)


def _library_path():
    configured = os.environ.get("IIPHOTOLIBRARY_PRESENTATION_LIBRARY")
    if configured:
        return configured
    prefix = Path(__file__).resolve().parents[3]
    names = (("iiPhotoLibraryPresentation.dll", "libiiPhotoLibraryPresentation.dll") if sys.platform == "win32" else
             ("libiiPhotoLibraryPresentation.dylib",) if sys.platform == "darwin" else
             ("libiiPhotoLibraryPresentation.so",))
    for directory in ("lib", "lib64", "bin"):
        for name in names:
            candidate = prefix / directory / name
            if candidate.is_file():
                return str(candidate)
    raise OSError("Cannot find the iiPhotoLibrary presentation shared library; provide library_path")


def _load_library(path, core_directory):
    def load(value, globally=False):
        mode = getattr(ctypes, "RTLD_GLOBAL", 0) if globally else getattr(ctypes, "RTLD_LOCAL", 0)
        key = (str(value), mode)
        if key not in _LIBRARIES:
            _LIBRARIES[key] = ctypes.CDLL(str(value), mode=mode)
        return _LIBRARIES[key]
    with _LOAD_LOCK:
        if core_directory:
            directory = Path(core_directory).expanduser().resolve(strict=True)
            if not directory.is_dir():
                raise NotADirectoryError(str(directory))
            if sys.platform == "win32" and str(directory) not in _DLL_DIRECTORIES:
                _DLL_DIRECTORIES[str(directory)] = os.add_dll_directory(str(directory))
            for name in ("iiSDKCoreObservation", "iiSDKCore"):
                names = ((name + ".dll", "lib" + name + ".dll") if sys.platform == "win32" else
                         ("lib" + name + ".dylib",) if sys.platform == "darwin" else ("lib" + name + ".so",))
                candidate = next((directory / filename for filename in names if (directory / filename).is_file()), None)
                if candidate:
                    load(candidate.resolve(), globally=True)
                elif name == "iiSDKCore":
                    raise OSError("No shared iiSDKCore library in " + str(directory))
        candidate = Path(path).expanduser()
        if candidate.is_file():
            candidate = candidate.resolve()
            if sys.platform == "win32" and str(candidate.parent) not in _DLL_DIRECTORIES:
                _DLL_DIRECTORIES[str(candidate.parent)] = os.add_dll_directory(str(candidate.parent))
            return load(candidate)
        return load(path)


def _configure(library):
    v, i, s, z, u, d = ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_uint64, ctypes.c_double
    array = ctypes.POINTER(s)
    signatures = {
        "last_error": ([], s), "create": ([], v), "destroy": ([v], i),
        "observe": ([v, v, v], i), "watch_rows": ([v, array, z], i), "set_actions": ([v, v, v], i),
        "update": ([v, ctypes.POINTER(_Flags), ctypes.POINTER(_Entry), z, ctypes.POINTER(i)], i),
        "update_flags": ([v, ctypes.POINTER(_Flags), ctypes.POINTER(i)], i),
        "change": ([v, s, ctypes.POINTER(_Entry), ctypes.POINTER(i)], i),
        "reorder": ([v, array, z, ctypes.POINTER(i)], i),
        "visible_ids": ([v, array, z], i), "drop_files": ([v, array, z, ctypes.POINTER(i)], i),
        "select": ([v, s, i], i), "command": ([v, i], i), "resize": ([v, d, d], i),
        "pinch": ([v, d, ctypes.POINTER(i)], i), "count": ([v, ctypes.POINTER(z)], i),
        "text": ([v, i, v, z, ctypes.POINTER(z)], i), "boolean": ([v, i, ctypes.POINTER(i)], i),
        "unsigned": ([v, i, ctypes.POINTER(u)], i), "number": ([v, i, ctypes.POINTER(d)], i),
        "row_text": ([v, z, i, v, z, ctypes.POINTER(z)], i),
        "row_boolean": ([v, z, i, ctypes.POINTER(i)], i), "row_unsigned": ([v, z, i, ctypes.POINTER(u)], i),
        "acquire_model": ([v, ctypes.POINTER(v)], i), "lease_model": ([v, ctypes.POINTER(v)], i),
        "release_model": ([v], i),
    }
    for name, (arguments, result) in signatures.items():
        function = getattr(library, "iiphoto_" + name)
        function.argtypes, function.restype = arguments, result


class PhotoModel:
    """Event-driven native model; close it on its creating thread."""
    def __init__(self, library_path=None, *, core_library_dir=None):
        core_library_dir = core_library_dir or os.environ.get("IIPHOTOLIBRARY_CORE_LIBRARY_DIR")
        self._library = _load_library(str(library_path or _library_path()), core_library_dir)
        _configure(self._library)
        self._owner = threading.get_ident()
        self._depth = self._callback_depth = 0
        self._closed = self._pending_close = False
        self._callback_errors = []
        self._observer = self._action = None
        self._handle = self._library.iiphoto_create()
        if not self._handle:
            raise RuntimeError(self._error())
        weak = weakref.ref(self)

        def on_observe(_context, property_id, row_id):
            model = weak()
            if model is None or model._closed or model._observer is None:
                return
            model._callback_depth += 1
            try:
                model._observer(_NAMES[property_id], row_id.decode("utf-8") if row_id else None)
            except BaseException as exc:
                model._callback_errors.append(exc)
            finally:
                model._callback_depth -= 1

        def on_action(_context, kind, values, count):
            model = weak()
            if model is None or model._closed or model._action is None:
                return
            model._callback_depth += 1
            try:
                model._action(_ACTIONS[kind], [values[index].decode("utf-8") for index in range(count)])
            except BaseException as exc:
                model._callback_errors.append(exc)
            finally:
                model._callback_depth -= 1
        self._observer_callback = _OBSERVER(on_observe)
        self._action_callback = _ACTION(on_action)

    def _error(self):
        return (self._library.iiphoto_last_error() or b"native photo model error").decode("utf-8", "replace")

    def _check_owner(self):
        if threading.get_ident() != self._owner:
            raise RuntimeError("photo model requires its owner thread")

    def _check_open(self):
        self._check_owner()
        if self._closed:
            raise RuntimeError("photo model is closed")

    def _destroy(self):
        if self._handle:
            if self._library.iiphoto_destroy(self._handle):
                raise RuntimeError(self._error())
            self._handle = None
        self._pending_close = False
        self._observer = self._action = None

    def _call(self, function, *args):
        self._check_open()
        self._depth += 1
        try:
            if function(self._handle, *args):
                raise RuntimeError(self._error())
        finally:
            self._depth -= 1
            if not self._depth and self._pending_close:
                self._destroy()
        if not self._depth and not self._callback_depth:
            self.raise_callback_errors()

    def raise_callback_errors(self):
        self._check_owner()
        if self._callback_errors:
            raise self._callback_errors.pop(0)

    def close(self):
        self._check_owner()
        if self._closed:
            return
        self._closed = True
        if self._depth:
            self._pending_close = True
        else:
            self._destroy()

    def __enter__(self):
        self._check_open()
        return self

    def __exit__(self, *_):
        self.close()

    def __del__(self):
        if (getattr(self, "_handle", None) and not getattr(self, "_closed", True)
                and threading.get_ident() == getattr(self, "_owner", None)):
            try:
                self.close()
            except Exception:
                pass

    def observe(self, callback):
        if callback is not None and not callable(callback):
            raise TypeError("observer must be callable or None")
        self._check_open()
        self._observer = callback
        self._call(self._library.iiphoto_observe,
                   ctypes.cast(self._observer_callback, ctypes.c_void_p) if callback else None, None)

    def set_actions(self, callback):
        if callback is not None and not callable(callback):
            raise TypeError("action handler must be callable or None")
        self._check_open()
        self._action = callback
        self._call(self._library.iiphoto_set_actions,
                   ctypes.cast(self._action_callback, ctypes.c_void_p) if callback else None, None)

    def watch_rows(self, identities):
        """Replace the explicitly watched row IDs; other row properties stay unsubscribed."""
        values = _strings(identities)
        self._call(self._library.iiphoto_watch_rows, values, len(values))

    def update(self, flags, entries):
        state = _flags(flags)
        values = [_entry(value) for value in entries]
        array = (_Entry * len(values))(*values)
        result = ctypes.c_int()
        self._call(self._library.iiphoto_update, ctypes.byref(state), array, len(array), ctypes.byref(result))
        return bool(result.value)

    def update_flags(self, flags):
        state, result = _flags(flags), ctypes.c_int()
        self._call(self._library.iiphoto_update_flags, ctypes.byref(state), ctypes.byref(result))
        return bool(result.value)

    def change(self, value):
        item, result = _entry(value), ctypes.c_int()
        self._call(self._library.iiphoto_change, item.id, ctypes.byref(item), ctypes.byref(result))
        return bool(result.value)

    def remove(self, identity):
        result = ctypes.c_int()
        self._call(self._library.iiphoto_change, _utf8(identity), None, ctypes.byref(result))
        return bool(result.value)

    def reorder(self, identities):
        values, result = _strings(identities), ctypes.c_int()
        self._call(self._library.iiphoto_reorder, values, len(values), ctypes.byref(result))
        return bool(result.value)

    def set_visible_ids(self, identities):
        values = _strings(identities)
        self._call(self._library.iiphoto_visible_ids, values, len(values))

    def drop_files(self, files):
        values, result = _strings(files), ctypes.c_int()
        self._call(self._library.iiphoto_drop_files, values, len(values), ctypes.byref(result))
        return bool(result.value)

    def select(self, identity, *, information=True):
        self._call(self._library.iiphoto_select, _utf8(identity), _boolean(information))

    def command(self, name):
        if name not in _COMMANDS:
            raise ValueError("unknown photo command: " + str(name))
        self._call(self._library.iiphoto_command, _COMMANDS[name])

    def resize(self, width, height):
        self._call(self._library.iiphoto_resize, float(width), float(height))

    def pinch(self, scale):
        result = ctypes.c_int()
        self._call(self._library.iiphoto_pinch, float(scale), ctypes.byref(result))
        return bool(result.value)

    def _text(self, function, *prefix):
        length = ctypes.c_size_t()
        self._call(function, *prefix, None, 0, ctypes.byref(length))
        buffer = ctypes.create_string_buffer(length.value + 1)
        self._call(function, *prefix, buffer, len(buffer), ctypes.byref(length))
        return buffer.value.decode("utf-8")

    def _scalar(self, function, value_type, *prefix):
        value = value_type()
        self._call(function, *prefix, ctypes.byref(value))
        return value.value

    def snapshot(self):
        """Read current values on demand; callbacks may observe intermediate updates."""
        lib = self._library
        result = {name: self._text(lib.iiphoto_text, _IDS[name]) for name in _TEXT}
        result.update((name, bool(self._scalar(lib.iiphoto_boolean, ctypes.c_int, _IDS[name]))) for name in _BOOL)
        result.update((name, self._scalar(lib.iiphoto_unsigned, ctypes.c_uint64, _IDS[name])) for name in _UINT)
        result.update((name, self._scalar(lib.iiphoto_number, ctypes.c_double, _IDS[name])) for name in _NUMBER)
        count = self._scalar(lib.iiphoto_count, ctypes.c_size_t)
        rows = []
        for index in range(count):
            row = {name: self._text(lib.iiphoto_row_text, index, _IDS["row." + name]) for name in _ROW_TEXT}
            row["bytes"] = self._scalar(lib.iiphoto_row_unsigned, ctypes.c_uint64, index, _IDS["row.bytes"])
            for name in ("video", "selected"):
                row[name] = bool(self._scalar(lib.iiphoto_row_boolean, ctypes.c_int, index, _IDS["row." + name]))
            rows.append(row)
        result["rows"] = rows
        return result

    def lease_model(self):
        self._check_open()
        if not self._depth and not self._callback_depth:
            self.raise_callback_errors()
        lease = ctypes.c_void_p()
        try:
            self._call(self._library.iiphoto_acquire_model, ctypes.byref(lease))
            return ModelLease(self, lease.value)
        except BaseException:
            if lease.value:
                self._library.iiphoto_release_model(lease.value)
            raise


class ModelLease:
    """Retain the native model until the view has detached and queries have returned."""
    def __init__(self, model, handle):
        self._model, self._handle = model, handle

    def _call(self, function, *args):
        self._model._check_owner()
        if not self._handle:
            raise RuntimeError("photo model lease is closed")
        if function(self._handle, *args):
            raise RuntimeError(self._model._error())

    @property
    def pointer(self):
        result = ctypes.c_void_p()
        self._call(self._model._library.iiphoto_lease_model, ctypes.byref(result))
        return result.value

    def close(self):
        self._model._check_owner()
        if self._handle:
            self._call(self._model._library.iiphoto_release_model)
            self._handle = None

    def __enter__(self):
        self._model._check_owner()
        if not self._handle:
            raise RuntimeError("photo model lease is closed")
        return self

    def __exit__(self, *_):
        self.close()

    def __del__(self):
        model = getattr(self, "_model", None)
        if (getattr(self, "_handle", None) and model is not None
                and threading.get_ident() == getattr(model, "_owner", None)):
            try:
                self.close()
            except Exception:
                pass
