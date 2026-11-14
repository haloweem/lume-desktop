#!/usr/bin/env python3
"""Generate resumable previews with existing FFmpeg and stage a COS upload folder.

Reads only CDN media fragments needed for each preview; no cloud keys required.
Only validated clips are referenced. Publication is through the logged-in console.
"""
import argparse
import copy
import hashlib
import json
import logging
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from urllib.parse import quote, unquote, urlsplit
import urllib.request

BUCKET = "lume-12345-1444884137"
REGION = "ap-nanjing"
ORIGIN = BUCKET + ".cos." + REGION + ".myqcloud.com"
MANIFEST_KEY = "lume/wallpapers/official/manifest.json"
PREFIX = "lume/wallpapers/official/previews/"
PUBLIC = "https://static.lumedesktop.com/"
PROFILE = {"version": 1, "seconds": 8, "width": 1280, "height": 720,
           "fps": 24, "crf": 28, "maxrate": "800k", "preset": "veryfast"}
PROFILE_ID = hashlib.sha256(json.dumps(PROFILE, sort_keys=True).encode()).hexdigest()[:12]


def json_bytes(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_name(path.name + ".pending")
    pending.write_bytes(json_bytes(value))
    os.replace(pending, path)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_error(error):
    text = re.sub(r"https?://[^\s\"']+", "<media-url>", str(error))
    text = re.sub(r"(?i)(secret[_-]?(?:key|id)|authorization|q-signature|q-ak)[=:][^\s,]+",
                  r"\1=<redacted>", text)
    return text[-1400:]


def source_key(item):
    ident = item.get("id", "")
    if not re.fullmatch(r"[A-Za-z0-9_.-]{1,96}", ident) or ident in (".", ".."):
        raise ValueError("Invalid wallpaper id")
    url = urlsplit(item.get("downloadUrl", ""))
    if url.hostname not in ("static.lumedesktop.com", ORIGIN) or url.query or url.fragment:
        raise ValueError("Source must be an official, unsigned wallpaper URL")
    if url.scheme not in ("https", "http") or url.username or url.password:
        raise ValueError("Invalid source URL")
    key = unquote(url.path).lstrip("/")
    if not key.startswith("lume/wallpapers/official/videos/"):
        raise ValueError("Source must be inside official/videos/")
    if any(part in ("", ".", "..") for part in key.split("/")) or "\\" in key:
        raise ValueError("Invalid source key")
    if not re.fullmatch(r"[0-9a-fA-F]{64}", item.get("sha256", "")):
        raise ValueError("Source has no valid SHA-256")
    return key


def preview_key(item):
    identity = digest(json_bytes([source_key(item), item["sha256"].lower(), PROFILE]))[:20]
    return PREFIX + item["id"] + "-" + identity + ".mp4"


def candidate_manifest(before, records):
    candidate = copy.deepcopy(before)
    ids = set()
    for item in candidate.get("wallpapers", []):
        ident = item["id"]
        if ident.lower() in ids:
            raise ValueError("Duplicate wallpaper id")
        ids.add(ident.lower())
        if ident not in records:
            continue
        record = records[ident]
        if (record.get("status") not in ("uploaded", "staged", "reused") or
                record.get("key") != preview_key(item) or
                record.get("sourceSha256") != item["sha256"].lower() or
                record.get("profile") != PROFILE_ID or record.get("bytes", 0) <= 0):
            raise ValueError("Cannot reference an unverified or stale preview: " + ident)
        item["previewUrl"] = PUBLIC + record["key"]
    return candidate


class FolderStore:
    """No cloud credentials: read CDN fragments and stage a browser-upload folder."""
    def __init__(self, root, manifest):
        self.root = root
        self.manifest = manifest

    def read(self, key):
        data = self.manifest.read_bytes()
        return data, digest(data)

    def head(self, key):
        path = self.root / "upload" / key
        meta = self.root / "state" / (digest(key.encode()) + ".json")
        if not path.is_file() or not meta.is_file():
            return None
        record = json.loads(meta.read_text("utf-8"))
        if record.get("x-cos-meta-preview-sha256") != digest(path.read_bytes()):
            return None  # Rebuild the staged copy from the validated local clip.
        record["content-length"] = str(path.stat().st_size)
        return record

    def put(self, key, data, content_type, metadata=None, **conditions):
        if not key.startswith(PREFIX) or not key.endswith(".mp4"):
            raise ValueError("Offline stage can write only preview MP4s")
        path = self.root / "upload" / key
        path.parent.mkdir(parents=True, exist_ok=True)
        pending = path.with_suffix(".pending")
        pending.write_bytes(data)
        os.replace(pending, path)
        write_json(self.root / "state" / (digest(key.encode()) + ".json"),
                   {"x-cos-meta-" + k: str(v) for k, v in metadata.items()})

    def signed_input(self, key):
        return PUBLIC + quote(key, safe="/-_.~")


def mp4_faststart(path):
    atoms = []
    with path.open("rb") as stream:
        while len(atoms) < 40:
            header = stream.read(8)
            if len(header) != 8:
                break
            size, kind = struct.unpack(">I4s", header)
            if size == 1:
                size = struct.unpack(">Q", stream.read(8))[0]
                header_size = 16
            else:
                header_size = 8
            if size < header_size:
                break
            atoms.append(kind)
            stream.seek(size - header_size, 1)
    return b"moov" in atoms and b"mdat" in atoms and atoms.index(b"moov") < atoms.index(b"mdat")


def verify_clip(ffmpeg, path):
    if not 2048 <= path.stat().st_size <= 2 * 1024 * 1024 or not mp4_faststart(path):
        raise ValueError("Preview size/MP4 faststart validation failed")
    result = subprocess.run([str(ffmpeg), "-nostdin", "-hide_banner", "-v", "error", "-threads", "1",
                             "-i", str(path), "-map", "0:v:0", "-an", "-f", "null", "-",
                             "-progress", "pipe:1", "-nostats"],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
    progress = result.stdout.decode("utf-8", "replace")
    frames = [int(v) for v in re.findall(r"(?m)^frame=(\d+)$", progress)]
    times = [int(v) / 1000000 for v in re.findall(r"(?m)^out_time_us=(\d+)$", progress)]
    if result.returncode or not frames or max(frames) < 1 or not times or not 0 < max(times) <= 8.25:
        raise ValueError("Preview decode/duration validation failed: " + safe_error(result.stderr.decode("utf-8", "replace")))
    return {"frames": max(frames), "seconds": max(times)}


class SourceDecodeError(ValueError):
    pass


def cached_source(root, item):
    path = root / "source-media" / (item["sha256"].lower() + ".media")
    if path.is_file() and path.stat().st_size == item["sizeBytes"] and digest(path.read_bytes()) == item["sha256"].lower():
        return path
    return None


def cache_original(store, root, item):
    """Only for broken HTTP seeking: verify a complete source before retrying."""
    if cached_source(root, item):
        return
    if not 0 < item.get("sizeBytes", 0) <= 1024 * 1024 * 1024:
        raise ValueError("Source is too large for the bounded full-download recovery")
    path = root / "source-media" / (item["sha256"].lower() + ".media")
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_suffix(".download")
    request = urllib.request.Request(store.signed_input(source_key(item)), headers={"Accept-Encoding": "identity"})
    sha256, total = hashlib.sha256(), 0
    with urllib.request.urlopen(request, timeout=25) as response, pending.open("wb") as out:
        while chunk := response.read(1024 * 1024):
            total += len(chunk)
            if total > item["sizeBytes"]:
                raise ValueError("Recovery source exceeds its manifest size")
            out.write(chunk)
            sha256.update(chunk)
    if total != item["sizeBytes"] or sha256.hexdigest() != item["sha256"].lower():
        raise ValueError("Recovery source does not match the original manifest SHA-256/size")
    os.replace(pending, path)


def produce(store, item, ffmpeg, root, encoder="libx264"):
    key = preview_key(item)
    base = {"id": item["id"], "key": key, "sourceSha256": item["sha256"].lower(), "profile": PROFILE_ID}
    head = store.head(key)
    if head is not None:
        if (head.get("x-cos-meta-preview-source") != base["sourceSha256"] or
                head.get("x-cos-meta-preview-profile") != PROFILE_ID or
                not re.fullmatch("[0-9a-f]{64}", head.get("x-cos-meta-preview-sha256", ""))):
            raise ValueError("Existing preview metadata conflict; object was preserved")
        return {**base, "status": "reused", "bytes": int(head["content-length"]),
                "sha256": head["x-cos-meta-preview-sha256"]}
    path = root / "clips" / Path(key).name
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_suffix(".pending.mp4")
    if path.exists():
        details = verify_clip(ffmpeg, path)
    else:
        source = cached_source(root, item)
        encoding = (["-c:v", "h264_nvenc", "-preset", "p4", "-rc", "vbr", "-cq", "30", "-b:v", "700k"]
                    if encoder == "nvenc" else ["-c:v", "libx264", "-preset", PROFILE["preset"],
                                                "-threads", "2", "-crf", str(PROFILE["crf"])])
        args = [str(ffmpeg), "-nostdin", "-hide_banner", "-v", "error", "-y",
                "-rw_timeout", "20000000", "-threads", "2", "-t", str(PROFILE["seconds"]),
                "-i", str(source) if source else store.signed_input(source_key(item)), "-map", "0:v:0", "-an", "-sn", "-dn",
                "-filter_threads", "1", "-vf",
                "scale=w='min(1280,iw)':h='min(720,ih)':force_original_aspect_ratio=decrease:force_divisible_by=2,fps=24,setsar=1",
                *encoding, "-maxrate", PROFILE["maxrate"], "-bufsize", "1600k", "-pix_fmt", "yuv420p",
                "-profile:v", "main", "-level:v", "3.1", "-g", "48", "-movflags", "+faststart",
                "-t", str(PROFILE["seconds"]), "-f", "mp4", str(pending)]
        result = subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=240)
        if result.returncode:
            raise SourceDecodeError("FFmpeg failed: " + safe_error(result.stderr.decode("utf-8", "replace")))
        details = verify_clip(ffmpeg, pending)
        os.replace(pending, path)
    content = path.read_bytes()
    sha256 = digest(content)
    store.put(key, content, "video/mp4", {"preview-source": base["sourceSha256"],
              "preview-profile": PROFILE_ID, "preview-sha256": sha256}, IfNoneMatch="*")
    head = store.head(key)
    if not head or int(head["content-length"]) != len(content) or head.get("x-cos-meta-preview-sha256") != sha256:
        raise ValueError("Uploaded preview failed metadata/size verification")
    return {**base, "status": "staged", "bytes": len(content), "sha256": sha256, **details}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd() / "wallpaper-previews")
    parser.add_argument("--ffmpeg", type=Path)
    parser.add_argument("--offline", action="store_true", help="Generate a local batch-upload folder using CDN input")
    parser.add_argument("--manifest", type=Path, required=True, help="Official manifest snapshot")
    parser.add_argument("--encoder", choices=("libx264", "nvenc"), default="libx264")
    parser.add_argument("--workers", type=int, choices=(1, 2, 3), default=1)
    parser.add_argument("--limit", type=int, default=0, help="Pilot only; zero processes the full catalog")
    parser.add_argument("--ids", default="", help="Comma-separated pilot wallpaper ids")
    parser.add_argument("--run", action="store_true", help="Encode and stage previews; otherwise only print a plan")
    args = parser.parse_args()
    logging.basicConfig(level=logging.ERROR)
    for logger in ("qcloud_cos", "urllib3", "requests"):
        logging.getLogger(logger).setLevel(logging.CRITICAL)
    args.root.mkdir(parents=True, exist_ok=True)
    lock = (args.root / "worker.lock").open("a+b")
    if os.name == "nt":
        import msvcrt
        lock.write(b"\0")
        lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
    else:
        import fcntl
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    store = FolderStore(args.root, args.manifest)
    raw, etag = store.read(MANIFEST_KEY)
    before = json.loads(raw)
    videos = [item for item in before["wallpapers"] if item.get("type") == "video"]
    if not videos or len(videos) != len(before["wallpapers"]):
        raise ValueError("Expected a nonempty all-video official catalog")
    for item in videos:
        source_key(item)
    selected = videos
    if args.ids:
        wanted = set(args.ids.split(","))
        selected = [item for item in videos if item["id"] in wanted]
        if {item["id"] for item in selected} != wanted:
            raise ValueError("Pilot id was not found")
    if args.limit:
        selected = selected[:args.limit]
    plan = {"event": "plan", "videos": len(videos), "selected": len(selected), "profile": PROFILE,
            "sourceBytes": sum(item.get("sizeBytes", 0) for item in selected), "internalCos": False,
            "encoder": args.encoder, "workers": args.workers}
    print(json.dumps(plan), flush=True)
    if not args.run:
        return 0
    ffmpeg = args.ffmpeg or Path(shutil.which("ffmpeg") or "ffmpeg-not-found")
    if not ffmpeg.is_file():
        raise ValueError("FFmpeg was not found; pass --ffmpeg with the existing executable path")
    run_dir = args.root / "runs" / time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    run_dir.mkdir(parents=True, exist_ok=False)
    (run_dir / "manifest-before.json").write_bytes(raw)
    started = time.monotonic()
    records, failures = {}, []
    def run_one(item):
        started_one = time.monotonic()
        for attempt in range(2):
            try:
                result = produce(store, item, ffmpeg, args.root, args.encoder)
                result["elapsed"] = round(time.monotonic() - started_one, 2)
                return result
            except Exception as error:
                if attempt:
                    raise
                if isinstance(error, SourceDecodeError):
                    cache_original(store, args.root, item)
    executor = ThreadPoolExecutor(max_workers=args.workers)
    futures = {executor.submit(run_one, item): item for item in selected}
    for index, future in enumerate(as_completed(futures), 1):
        item = futures[future]
        try:
            record = future.result()
            records[item["id"]] = record
        except Exception as error:
            record = {"id": item["id"], "status": "failed", "error": safe_error(error)}
            failures.append(record)
        record.update(index=index, total=len(selected))
        with (run_dir / "records.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(record, ensure_ascii=False) + "\n")
        print(json.dumps(record, ensure_ascii=False), flush=True)
        if index % 10 == 0 or index == len(selected):
            write_json(run_dir / "progress.json", {"done": index, "total": len(selected),
                       "failed": len(failures), "seconds": round(time.monotonic() - started, 2)})
    executor.shutdown()
    candidate = candidate_manifest(before, records)
    write_json(run_dir / "manifest-candidate.json", candidate)
    report = {"profile": PROFILE, "profileId": PROFILE_ID, "selected": len(selected), "verified": len(records),
              "failed": failures, "previewBytes": sum(rec["bytes"] for rec in records.values()),
              "originalBytes": sum(item.get("sizeBytes", 0) for item in selected),
              "seconds": round(time.monotonic() - started, 2), "runDirectory": str(run_dir),
              "publication": {"status": "not-requested"}}
    write_json(run_dir / "report.json", report)
    staged = args.root / "upload" / MANIFEST_KEY
    if not failures and not args.limit and not args.ids and len(records) == len(videos):
        write_json(staged, candidate)
        report["publication"] = {"status": "staged-for-console-upload", "manifest": str(staged)}
        write_json(run_dir / "report.json", report)
    write_json(args.root / "latest-report.json", report)
    print(json.dumps({"event": "complete", **report}, ensure_ascii=False), flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print("ERROR: " + safe_error(error), file=sys.stderr, flush=True)
        sys.exit(1)
