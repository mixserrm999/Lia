#!/usr/bin/env python3
import argparse
import json
import mimetypes
import posixpath
import re
import shutil
import sys
import tarfile
import tempfile
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import quote, unquote, urlparse


ARCHIVE_SUFFIXES = (".tar.gz", ".tgz", ".zip")


def semver_key(version):
    match = re.match(r"^v?(\d+)\.(\d+)\.(\d+)", version)
    if not match:
        return (-1, -1, -1, version)
    return tuple(int(part) for part in match.groups()) + (version,)


def is_safe_part(value):
    return bool(re.match(r"^[A-Za-z0-9._-]+$", value))


def find_archive(version_dir):
    for path in sorted(version_dir.iterdir()):
        if path.is_file() and any(path.name.endswith(suffix) for suffix in ARCHIVE_SUFFIXES):
            return path
    return None


def read_manifest_from_tar(path):
    with tarfile.open(path, "r:*") as archive:
        for member in archive.getmembers():
            member_name = member.name.strip("/")
            if not member.isfile() or not member_name.endswith("/lia.json") and member_name != "lia.json":
                continue
            file_obj = archive.extractfile(member)
            if file_obj is None:
                continue
            return json.loads(file_obj.read().decode("utf-8"))
    return None


def read_manifest_from_zip(path):
    with zipfile.ZipFile(path) as archive:
        for member_name in archive.namelist():
            normalized = member_name.strip("/")
            if normalized.endswith("/lia.json") or normalized == "lia.json":
                return json.loads(archive.read(member_name).decode("utf-8"))
    return None


def read_manifest_from_archive(path):
    if path.name.endswith(".zip"):
        return read_manifest_from_zip(path)
    return read_manifest_from_tar(path)


def validate_manifest(manifest):
    if not isinstance(manifest, dict):
        raise ValueError("manifest must be an object")

    name = manifest.get("name")
    version = manifest.get("version")
    main = manifest.get("main")
    dependencies = manifest.get("dependencies", {})
    dev_dependencies = manifest.get("devDependencies", {})
    scripts = manifest.get("scripts", {})
    bin_value = manifest.get("bin", {})

    if not isinstance(name, str) or not is_safe_part(name):
        raise ValueError("manifest name is invalid")
    if not isinstance(version, str) or not is_safe_part(version):
        raise ValueError("manifest version is invalid")
    if not isinstance(main, str) or not main.strip():
        raise ValueError("manifest main is invalid")
    if not isinstance(dependencies, dict) or not all(
        isinstance(key, str) and is_safe_part(key) and isinstance(value, str) and value.strip()
        for key, value in dependencies.items()
    ):
        raise ValueError("manifest dependencies must be an object of package string values")
    if not isinstance(dev_dependencies, dict) or not all(
        isinstance(key, str) and is_safe_part(key) and isinstance(value, str) and value.strip()
        for key, value in dev_dependencies.items()
    ):
        raise ValueError("manifest devDependencies must be an object of package string values")
    if not isinstance(scripts, dict) or not all(
        isinstance(key, str) and is_safe_part(key.replace(":", "-")) and isinstance(value, str) and value.strip()
        for key, value in scripts.items()
    ):
        raise ValueError("manifest scripts must be an object of string values")
    if isinstance(bin_value, str):
        if not bin_value.strip():
            raise ValueError("manifest bin must be a non-empty string")
    elif isinstance(bin_value, dict):
        if not all(
            isinstance(key, str) and is_safe_part(key) and isinstance(value, str) and value.strip()
            for key, value in bin_value.items()
        ):
            raise ValueError("manifest bin must be a string or an object of string values")
    else:
        raise ValueError("manifest bin must be a string or object")

    return name, version


class Registry:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.packages_root = self.root / "packages"
        self.owners_path = self.root / "owners.json"

    def read_owners(self):
        if not self.owners_path.is_file():
            return {}
        try:
            owners = json.loads(self.owners_path.read_text("utf-8"))
        except json.JSONDecodeError:
            return {}
        return owners if isinstance(owners, dict) else {}

    def write_owners(self, owners):
        self.root.mkdir(parents=True, exist_ok=True)
        temp_path = self.owners_path.with_suffix(".json.tmp")
        temp_path.write_text(json.dumps(owners, indent=2, sort_keys=True) + "\n", "utf-8")
        temp_path.replace(self.owners_path)

    def package_dir(self, name):
        if not is_safe_part(name):
            return None
        return self.packages_root / name

    def versions(self, name):
        package_dir = self.package_dir(name)
        if package_dir is None or not package_dir.is_dir():
            return []
        return sorted(
            [path.name for path in package_dir.iterdir() if path.is_dir() and is_safe_part(path.name)],
            key=semver_key,
        )

    def resolve_version(self, name, version):
        versions = self.versions(name)
        if not versions:
            return None
        if version == "latest":
            return versions[-1]
        return version if version in versions else None

    def metadata(self, name, version, base_url):
        resolved_version = self.resolve_version(name, version)
        if resolved_version is None:
            return None

        version_dir = self.package_dir(name) / resolved_version
        archive_path = find_archive(version_dir)
        if archive_path is None:
            return None

        manifest = read_manifest_from_archive(archive_path)
        if manifest is None:
            return None

        tarball = (
            f"{base_url}/tarballs/{quote(name)}/{quote(resolved_version)}/"
            f"{quote(archive_path.name)}"
        )

        return {
            "name": manifest.get("name", name),
            "version": manifest.get("version", resolved_version),
            "main": manifest.get("main"),
            "dependencies": manifest.get("dependencies", {}),
            "devDependencies": manifest.get("devDependencies", {}),
            "bin": manifest.get("bin", {}),
            "tarball": tarball,
        }

    def archive_path(self, name, version, filename):
        resolved_version = self.resolve_version(name, version)
        if resolved_version is None or not is_safe_part(filename):
            return None

        package_dir = self.package_dir(name)
        if package_dir is None:
            return None

        archive_path = (package_dir / resolved_version / filename).resolve()
        try:
            archive_path.relative_to(self.root)
        except ValueError:
            return None

        if not archive_path.is_file() or not any(archive_path.name.endswith(suffix) for suffix in ARCHIVE_SUFFIXES):
            return None

        return archive_path

    def publish_archive(self, uploaded_path, base_url, token):
        manifest = read_manifest_from_archive(uploaded_path)
        if manifest is None:
            raise ValueError("archive must contain lia.json")

        name, version = validate_manifest(manifest)

        owners = self.read_owners()
        owner = owners.get(name)
        if owner is not None and owner != token:
            raise PermissionError("package is owned by another token")

        version_dir = self.packages_root / name / version
        version_dir.mkdir(parents=True, exist_ok=True)

        archive_name = f"{name}-{version}.tar.gz"
        final_path = version_dir / archive_name
        if final_path.exists() or find_archive(version_dir) is not None:
            raise FileExistsError(f"{name}@{version} already exists")

        shutil.copyfile(uploaded_path, final_path)
        owners[name] = token
        self.write_owners(owners)

        return {
            "ok": True,
            "name": name,
            "version": version,
            "tarball": f"{base_url}/tarballs/{quote(name)}/{quote(version)}/{quote(archive_name)}",
        }


class RegistryHandler(BaseHTTPRequestHandler):
    registry = None
    token = None

    def log_message(self, fmt, *args):
        sys.stderr.write("registry: " + fmt % args + "\n")

    def base_url(self):
        host = self.headers.get("Host", f"{self.server.server_address[0]}:{self.server.server_address[1]}")
        return f"http://{host}"

    def write_json(self, status, payload):
        body = json.dumps(payload, sort_keys=True).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def write_not_found(self):
        self.write_json(404, {"error": "not found"})

    def authorized(self):
        expected = f"Bearer {self.token}"
        return self.headers.get("Authorization") == expected

    def bearer_token(self):
        value = self.headers.get("Authorization", "")
        prefix = "Bearer "
        if not value.startswith(prefix):
            return None
        return value[len(prefix):]

    def write_unauthorized(self):
        self.write_json(401, {"error": "unauthorized"})

    def do_GET(self):
        parsed = urlparse(self.path)
        parts = [unquote(part) for part in parsed.path.split("/") if part]

        if parts == ["health"]:
            self.write_json(200, {"ok": True})
            return

        if parts == ["auth", "check"]:
            if not self.authorized():
                self.write_unauthorized()
                return
            self.write_json(200, {"ok": True})
            return

        if len(parts) == 2 and parts[0] == "packages":
            name = parts[1]
            versions = self.registry.versions(name)
            if not versions:
                self.write_not_found()
                return
            self.write_json(200, {"name": name, "versions": versions, "latest": versions[-1]})
            return

        if len(parts) == 3 and parts[0] == "packages":
            metadata = self.registry.metadata(parts[1], parts[2], self.base_url())
            if metadata is None:
                self.write_not_found()
                return
            self.write_json(200, metadata)
            return

        if len(parts) == 4 and parts[0] == "tarballs":
            archive_path = self.registry.archive_path(parts[1], parts[2], posixpath.basename(parts[3]))
            if archive_path is None:
                self.write_not_found()
                return
            content_type = mimetypes.guess_type(archive_path.name)[0] or "application/octet-stream"
            data = archive_path.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return

        self.write_not_found()

    def do_PUT(self):
        parsed = urlparse(self.path)
        parts = [unquote(part) for part in parsed.path.split("/") if part]

        if parts != ["publish"]:
            self.write_not_found()
            return

        if not self.authorized():
            self.write_unauthorized()
            return

        content_length = self.headers.get("Content-Length")
        if content_length is None:
            self.write_json(411, {"error": "content length required"})
            return

        try:
            length = int(content_length)
        except ValueError:
            self.write_json(400, {"error": "invalid content length"})
            return

        with tempfile.NamedTemporaryFile(dir=self.registry.root, suffix=".upload", delete=False) as file_obj:
            temp_path = Path(file_obj.name)
            remaining = length
            while remaining > 0:
                chunk = self.rfile.read(min(remaining, 1024 * 64))
                if not chunk:
                    break
                file_obj.write(chunk)
                remaining -= len(chunk)

        if remaining != 0:
            temp_path.unlink(missing_ok=True)
            self.write_json(400, {"error": "incomplete upload"})
            return

        try:
            payload = self.registry.publish_archive(temp_path, self.base_url(), self.bearer_token())
        except PermissionError as exc:
            temp_path.unlink(missing_ok=True)
            self.write_json(403, {"error": str(exc)})
            return
        except FileExistsError as exc:
            temp_path.unlink(missing_ok=True)
            self.write_json(409, {"error": str(exc)})
            return
        except ValueError as exc:
            temp_path.unlink(missing_ok=True)
            self.write_json(400, {"error": str(exc)})
            return
        except Exception as exc:
            temp_path.unlink(missing_ok=True)
            self.write_json(500, {"error": str(exc)})
            return

        temp_path.unlink(missing_ok=True)
        self.write_json(201, payload)


def main():
    parser = argparse.ArgumentParser(description="Run a local Lia package registry")
    parser.add_argument("--root", default="registry", help="Registry storage root")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7788)
    parser.add_argument("--token", default="dev-token", help="Bearer token required for auth/publish")
    args = parser.parse_args()

    registry = Registry(args.root)
    registry.packages_root.mkdir(parents=True, exist_ok=True)
    RegistryHandler.registry = registry
    RegistryHandler.token = args.token

    server = ThreadingHTTPServer((args.host, args.port), RegistryHandler)
    print(f"registry listening on http://{args.host}:{args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
