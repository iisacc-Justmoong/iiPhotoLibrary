"""Headless Python/C boundary checks for the native photo presentation model."""
import importlib.util
import os
import pathlib
import sys
import threading
import unittest
from unittest import mock

SOURCE = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("iiphotolibrary_presentation",
    os.environ.get("IIPHOTOLIBRARY_TEST_PYTHON_MODULE", SOURCE / "src/python/iiphotolibrary_presentation.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
LIBRARY = sys.argv.pop(1) if len(sys.argv) > 1 else ""
if LIBRARY == "-":
    LIBRARY = None  # Exercise prefix-relative library discovery from the installed module.
CORE = sys.argv.pop(1) if len(sys.argv) > 1 else None


def entry(identity, name, key=None, revision="1"):
    return {"id": identity, "gallery_key": key or "key-" + identity,
            "name": name, "preview": "asset:" + identity,
            "preview_revision": revision, "created": "2026-01-01T00:00:00Z",
            "bytes": 42, "video": False}


class PhotoPresentationTest(unittest.TestCase):
    def model(self):
        model = module.PhotoModel(LIBRARY, core_library_dir=CORE)
        self.addCleanup(model.close)
        return model

    def test_snapshot_partial_changes_and_commands(self):
        model = self.model()
        events, actions = [], []
        model.observe(lambda property_name, row_id: events.append((property_name, row_id)))
        model.watch_rows(["a"])
        model.set_actions(lambda name, values: actions.append((name, values)))
        self.assertTrue(model.update({"active": True, "busy": False, "can_share": True,
                                     "access": "ready", "status": "connected"},
                                    [entry("a", "Alpha"), entry("b", "Beta")]))
        self.assertEqual([r["id"] for r in model.snapshot()["rows"]], ["a", "b"])
        events.clear()
        self.assertTrue(model.change(entry("a", "New alpha", revision="2")))
        self.assertIn(("row.name", "a"), events)
        self.assertNotIn(("rows", None), events)
        events.clear()
        self.assertTrue(model.change(entry("b", "Unwatched beta")))
        self.assertFalse(any(row_id == "b" for _, row_id in events))
        self.assertIn(("preview_changed", ["asset:a"]), actions)
        model.select("a")
        self.assertEqual(model.snapshot()["selected_id"], "a")
        self.assertTrue(model.snapshot()["information_open"])
        model.command("share")
        self.assertIn(("share", ["a"]), actions)
        model.set_visible_ids(["a"])
        self.assertIn(("prioritize", ["key-a"]), actions)
        self.assertTrue(model.reorder(["b", "a"]))
        self.assertTrue(model.remove("a"))
        self.assertEqual(model.snapshot()["selected_id"], "")
        self.assertEqual([r["id"] for r in model.snapshot()["rows"]], ["b"])
        self.assertTrue(model.change(entry("a", "Returned")))
        events.clear()
        self.assertTrue(model.change(entry("a", "Returned and changed")))
        self.assertIn(("row.name", "a"), events)

    def test_validation_thread_lifetime_and_reentrant_close(self):
        model = self.model()
        self.assertTrue(model.update({"active": True}, [entry("a", "Alpha")]))
        with self.assertRaises(ValueError):
            model.change(entry("b", "bad\0name"))
        self.assertFalse(model.change(entry("b", "duplicate", "key-a")))
        errors = []
        def foreign():
            try:
                model.snapshot()
            except RuntimeError as error:
                errors.append(str(error))
        thread = threading.Thread(target=foreign)
        thread.start(); thread.join()
        self.assertEqual(len(errors), 1)
        self.assertIn("owner thread", errors[0])
        lease = model.lease_model()
        self.assertTrue(lease.pointer)
        model.observe(lambda name, row: model.close() if name == "busy" else None)
        model.update_flags({"active": True, "busy": True})
        with self.assertRaises(RuntimeError):
            model.snapshot()
        self.assertTrue(lease.pointer)
        lease.close()
        with self.assertRaises(RuntimeError):
            _ = lease.pointer

    def test_callback_reconfiguration_and_lease_boundary(self):
        model = self.model()
        model.update({"active": True}, [entry("a", "Alpha")])
        lease = model.lease_model()
        seen, failures = [], []
        def observe(name, row):
            seen.append((name, row))
            if name == "row.name":
                try:
                    lease.close()
                except RuntimeError as error:
                    failures.append(str(error))
                model.observe(None)
        model.observe(observe)
        model.watch_rows(["a"])
        self.assertTrue(model.change(entry("a", "Changed")))
        self.assertEqual(seen, [("row.name", "a")])
        self.assertIn("after callbacks return", failures[0])
        self.assertTrue(lease.pointer)
        lease.close()
        model.change(entry("a", "Changed again"))
        self.assertEqual(len(seen), 1)

    def test_c_boundary_rejects_foreign_thread(self):
        model = self.model()
        results = []
        def foreign():
            status = model._library.iiphoto_command(model._handle, 2)
            results.append((status, model._library.iiphoto_last_error().decode()))
        thread = threading.Thread(target=foreign)
        thread.start(); thread.join()
        self.assertNotEqual(results[0][0], 0)
        self.assertIn("owner thread", results[0][1])

    def test_windows_mingw_library_name_is_discoverable(self):
        with mock.patch.object(module.sys, "platform", "win32"), \
             mock.patch.dict(os.environ, {"IIPHOTOLIBRARY_PRESENTATION_LIBRARY": ""}), \
             mock.patch.object(pathlib.Path, "is_file", lambda path: path.name == "libiiPhotoLibraryPresentation.dll"):
            self.assertEqual(pathlib.Path(module._library_path()).name, "libiiPhotoLibraryPresentation.dll")

    def test_action_error_and_no_action_after_close(self):
        model = self.model()
        model.update({"active": True}, [entry("a", "Alpha")])
        def failed(name, values):
            raise ValueError("action failed")
        model.set_actions(failed)
        with self.assertRaisesRegex(ValueError, "action failed"):
            model.command("refresh")
        model.set_actions(None)
        self.assertFalse(model.drop_files(["/tmp/example.jpg"]))
        model.close()
        with self.assertRaises(RuntimeError):
            model.command("refresh")


if __name__ == "__main__":
    unittest.main()
