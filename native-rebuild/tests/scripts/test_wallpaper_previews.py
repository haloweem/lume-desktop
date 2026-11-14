"""Guard the preview pipeline's source paths, resume state and manifest edits."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys

sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location("previews", Path(__file__).parents[2] / "tools/wallpaper-previews.py")
previews = importlib.util.module_from_spec(spec)
spec.loader.exec_module(previews)


def sample():
    return {"id": "snow", "type": "video", "downloadUrl": "https://static.lumedesktop.com/lume/wallpapers/official/videos/snow.mp4",
            "sha256": "a" * 64, "sizeBytes": 123456, "title": "雪山", "width": 1920, "height": 1080}


def record(item):
    return {"status": "staged", "key": previews.preview_key(item), "sourceSha256": item["sha256"],
            "profile": previews.PROFILE_ID, "bytes": 800000}


class PreviewSafetyTests(unittest.TestCase):
    def test_candidate_changes_only_the_preview_url(self):
        before = {"wallpapers": [sample()], "carouselCoverOrder": ["snow"], "count": 1, "custom": {"keep": True}}
        snapshot = copy.deepcopy(before)
        candidate = previews.candidate_manifest(before, {"snow": record(before["wallpapers"][0])})
        self.assertEqual(before, snapshot)
        self.assertEqual(candidate["wallpapers"][0].pop("previewUrl"), previews.PUBLIC + previews.preview_key(sample()))
        self.assertEqual(candidate, snapshot)

    def test_unfinished_and_stale_outputs_cannot_be_referenced(self):
        for field, bad in (("status", "failed"), ("sourceSha256", "b" * 64), ("profile", "old"),
                           ("key", "lume/wallpapers/official/videos/snow.mp4"), ("bytes", 0)):
            output = record(sample())
            output[field] = bad
            with self.subTest(field=field), self.assertRaises(ValueError):
                previews.candidate_manifest({"wallpapers": [sample()]}, {"snow": output})

    def test_path_and_host_boundaries(self):
        for url in ("https://evil.example/lume/wallpapers/official/videos/a.mp4",
                    "https://static.lumedesktop.com/lume/wallpapers/official/videos/%2e%2e/secret",
                    "https://static.lumedesktop.com/lume/wallpapers/static/images/a.jpg",
                    "https://static.lumedesktop.com/lume/wallpapers/official/videos/a.mp4?token=x"):
            item = sample()
            item["downloadUrl"] = url
            with self.subTest(url=url), self.assertRaises(ValueError):
                previews.source_key(item)

    def test_profile_and_source_updates_get_new_keys(self):
        first = previews.preview_key(sample())
        item = sample()
        item["sha256"] = "b" * 64
        self.assertNotEqual(first, previews.preview_key(item))

    def test_duplicate_ids_are_rejected(self):
        with self.assertRaises(ValueError):
            previews.candidate_manifest({"wallpapers": [sample(), sample()]}, {})

    def test_partial_generation_preserves_existing_preview(self):
        item = sample()
        item["previewUrl"] = "https://static.lumedesktop.com/lume/old.mp4"
        self.assertEqual(previews.candidate_manifest({"wallpapers": [item]}, {}), {"wallpapers": [item]})

    def test_local_resume_detects_truncation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            store = previews.FolderStore(root, root / "manifest.json")
            key = previews.preview_key(sample())
            body = b"verified-content"
            store.put(key, body, "video/mp4", {"preview-source": "a" * 64,
                      "preview-profile": previews.PROFILE_ID, "preview-sha256": previews.digest(body)})
            self.assertEqual(int(store.head(key)["content-length"]), len(body))
            (root / "upload" / key).write_bytes(b"truncated")
            self.assertIsNone(store.head(key), "Corrupt staged files must be regenerated on resume")

    def test_local_stage_refuses_original_or_manifest_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            store = previews.FolderStore(Path(directory), Path(directory) / "manifest.json")
            for key in (previews.MANIFEST_KEY, "lume/wallpapers/official/videos/snow.mp4", "../outside.mp4"):
                with self.subTest(key=key), self.assertRaises(ValueError):
                    store.put(key, b"bad", "video/mp4", {})

    def test_error_output_redacts_signed_media_urls(self):
        message = previews.safe_error("failed https://host/path?q-ak=secret&q-signature=token reason")
        self.assertNotIn("secret", message)
        self.assertNotIn("token", message)

    def test_full_source_recovery_requires_exact_size_and_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data = b"verified-source"
            item = sample()
            item.update(sizeBytes=len(data), sha256=previews.digest(data))
            path = root / "source-media" / (item["sha256"] + ".media")
            path.parent.mkdir()
            path.write_bytes(data)
            self.assertEqual(previews.cached_source(root, item), path)
            path.write_bytes(b"same-size-wrong")
            self.assertIsNone(previews.cached_source(root, item))


if __name__ == "__main__":
    unittest.main()
