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
from urllib.parse import parse_qs, quote, unquote, urlparse


ARCHIVE_SUFFIXES = (".tar.gz", ".tgz", ".zip")
METADATA_STRING_FIELDS = ("description", "license", "homepage", "repository")


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
    keywords = manifest.get("keywords", [])

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
    for field in METADATA_STRING_FIELDS:
        value = manifest.get(field, "")
        if value is not None and (not isinstance(value, str) or (value != "" and not value.strip())):
            raise ValueError(f"manifest {field} must be a string")
    if keywords is not None and (
        not isinstance(keywords, list)
        or not all(isinstance(value, str) and value.strip() for value in keywords)
    ):
        raise ValueError("manifest keywords must be an array of strings")

    return name, version


class Registry:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.packages_root = self.root / "packages"
        self.owners_path = self.root / "owners.json"
        self.tags_path = self.root / "tags.json"
        self.deprecations_path = self.root / "deprecations.json"

    def read_json_file(self, path, fallback):
        if not path.is_file():
            return fallback
        try:
            data = json.loads(path.read_text("utf-8"))
        except json.JSONDecodeError:
            return fallback
        return data if isinstance(data, type(fallback)) else fallback

    def write_json_file(self, path, data):
        self.root.mkdir(parents=True, exist_ok=True)
        temp_path = path.with_suffix(path.suffix + ".tmp")
        temp_path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", "utf-8")
        temp_path.replace(path)

    def read_owners(self):
        return self.read_json_file(self.owners_path, {})

    def write_owners(self, owners):
        self.write_json_file(self.owners_path, owners)

    def read_tags(self):
        return self.read_json_file(self.tags_path, {})

    def write_tags(self, tags):
        self.write_json_file(self.tags_path, tags)

    def read_deprecations(self):
        return self.read_json_file(self.deprecations_path, {})

    def write_deprecations(self, deprecations):
        self.write_json_file(self.deprecations_path, deprecations)

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

    def package_tags(self, name):
        tags = self.read_tags().get(name, {})
        tags = tags if isinstance(tags, dict) else {}
        versions = self.versions(name)
        if versions and "latest" not in tags:
            tags = {**tags, "latest": versions[-1]}
        return {
            key: value
            for key, value in tags.items()
            if is_safe_part(key) and isinstance(value, str) and value in versions
        }

    def resolve_version(self, name, version):
        versions = self.versions(name)
        if not versions:
            return None
        tags = self.package_tags(name)
        if version in tags:
            return tags[version]
        return version if version in versions else None

    def read_package_manifest(self, name, version):
        version_dir = self.package_dir(name) / version
        archive_path = find_archive(version_dir)
        if archive_path is None:
            return None, None

        manifest = read_manifest_from_archive(archive_path)
        return manifest, archive_path

    def metadata(self, name, version, base_url):
        resolved_version = self.resolve_version(name, version)
        if resolved_version is None:
            return None

        manifest, archive_path = self.read_package_manifest(name, resolved_version)
        if manifest is None:
            return None

        tarball = (
            f"{base_url}/tarballs/{quote(name)}/{quote(resolved_version)}/"
            f"{quote(archive_path.name)}"
        )

        deprecation = self.read_deprecations().get(name, {}).get(resolved_version)
        metadata = {
            "name": manifest.get("name", name),
            "version": manifest.get("version", resolved_version),
            "main": manifest.get("main"),
            "dependencies": manifest.get("dependencies", {}),
            "devDependencies": manifest.get("devDependencies", {}),
            "bin": manifest.get("bin", {}),
            "keywords": manifest.get("keywords", []),
            "dist-tags": self.package_tags(name),
            "tarball": tarball,
        }
        for field in METADATA_STRING_FIELDS:
            value = manifest.get(field)
            if isinstance(value, str) and value:
                metadata[field] = value
        if deprecation:
            metadata["deprecated"] = deprecation
        return metadata

    def search(self, query, base_url):
        query = query.lower()
        results = []
        if not self.packages_root.is_dir():
            return results
        for package_dir in sorted(self.packages_root.iterdir()):
            if not package_dir.is_dir() or not is_safe_part(package_dir.name):
                continue
            latest = self.resolve_version(package_dir.name, "latest")
            if latest is None:
                continue
            metadata = self.metadata(package_dir.name, latest, base_url)
            if metadata is None:
                continue
            haystack_parts = [
                metadata.get("name", ""),
                metadata.get("description", ""),
                metadata.get("license", ""),
                metadata.get("homepage", ""),
                metadata.get("repository", ""),
                " ".join(metadata.get("keywords", [])),
            ]
            haystack = " ".join(haystack_parts).lower()
            if query in haystack:
                results.append(metadata)
        return results

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

    def publish_archive(self, uploaded_path, base_url, token, tag):
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

        tags = self.read_tags()
        package_tags = tags.get(name, {})
        if not isinstance(package_tags, dict):
            package_tags = {}
        package_tags[tag] = version
        tags[name] = package_tags
        self.write_tags(tags)

        return {
            "ok": True,
            "name": name,
            "version": version,
            "tag": tag,
            "tarball": f"{base_url}/tarballs/{quote(name)}/{quote(version)}/{quote(archive_name)}",
        }

    def require_owner(self, name, token):
        owner = self.read_owners().get(name)
        if owner is None:
            raise FileNotFoundError(name)
        if owner != token:
            raise PermissionError("package is owned by another token")

    def deprecate(self, name, version, message, token):
        if not is_safe_part(name) or not is_safe_part(version):
            raise ValueError("invalid package or version")
        if self.resolve_version(name, version) != version:
            raise FileNotFoundError(f"{name}@{version}")
        self.require_owner(name, token)

        deprecations = self.read_deprecations()
        package_deprecations = deprecations.get(name, {})
        if not isinstance(package_deprecations, dict):
            package_deprecations = {}
        package_deprecations[version] = message
        deprecations[name] = package_deprecations
        self.write_deprecations(deprecations)


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

    def write_text(self, status, payload):
        body = payload.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
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

        if parts == ["search"]:
            query = parse_qs(parsed.query)
            term = query.get("q", [""])[0].strip()
            results = self.registry.search(term, self.base_url()) if term else []
            if query.get("format", ["json"])[0] == "text":
                lines = []
                for item in results:
                    description = item.get("description", "")
                    deprecated = f" deprecated={item['deprecated']}" if item.get("deprecated") else ""
                    line = f"{item['name']}@{item['version']}"
                    if description:
                        line += f" {description}"
                    line += deprecated
                    lines.append(line)
                self.write_text(200, "\n".join(lines) + ("\n" if lines else ""))
            else:
                self.write_json(200, {"results": results})
            return

        if len(parts) == 2 and parts[0] == "packages":
            name = parts[1]
            versions = self.registry.versions(name)
            if not versions:
                self.write_not_found()
                return
            tags = self.registry.package_tags(name)
            self.write_json(200, {"name": name, "versions": versions, "latest": tags.get("latest", versions[-1]), "dist-tags": tags})
            return

        if len(parts) == 3 and parts[0] == "packages" and parts[2] == "dist-tags":
            versions = self.registry.versions(parts[1])
            if not versions:
                self.write_not_found()
                return
            self.write_json(200, self.registry.package_tags(parts[1]))
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

        if parts == ["publish"]:
            self.handle_publish(parsed)
            return

        if len(parts) == 3 and parts[0] == "deprecate":
            self.handle_deprecate(parts[1], parts[2])
            return

        self.write_not_found()

    def handle_publish(self, parsed):
        query = parse_qs(parsed.query)
        tag = query.get("tag", ["latest"])[0]
        if not is_safe_part(tag):
            self.write_json(400, {"error": "invalid dist tag"})
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
            payload = self.registry.publish_archive(temp_path, self.base_url(), self.bearer_token(), tag)
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

    def handle_deprecate(self, name, version):
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

        message = self.rfile.read(length).decode("utf-8").strip()
        if not message:
            self.write_json(400, {"error": "deprecation message required"})
            return

        try:
            self.registry.deprecate(name, version, message, self.bearer_token())
        except FileNotFoundError:
            self.write_not_found()
            return
        except PermissionError as exc:
            self.write_json(403, {"error": str(exc)})
            return
        except ValueError as exc:
            self.write_json(400, {"error": str(exc)})
            return

        self.write_json(200, {"ok": True, "name": name, "version": version, "deprecated": message})

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
