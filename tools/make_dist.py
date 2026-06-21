#!/usr/bin/env python3
import argparse
import hashlib
import shutil
import tarfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as file_obj:
        for chunk in iter(lambda: file_obj.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_if_exists(source, destination):
    if source.is_dir():
        shutil.copytree(source, destination)
    elif source.is_file():
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def stage_release(binary, version, platform, out_dir):
    package_name = f"lia-{version}-{platform}"
    stage_dir = out_dir / package_name
    if stage_dir.exists():
        shutil.rmtree(stage_dir)

    (stage_dir / "bin").mkdir(parents=True)
    binary_name = "lia.exe" if binary.name.endswith(".exe") else "lia"
    shutil.copy2(binary, stage_dir / "bin" / binary_name)

    for name in ["README.md", "ROADMAP.md"]:
        copy_if_exists(ROOT / name, stage_dir / name)
    for name in ["examples", "scripts"]:
        copy_if_exists(ROOT / name, stage_dir / name)

    return package_name, stage_dir


def make_tar_gz(stage_dir, archive_path):
    with tarfile.open(archive_path, "w:gz") as archive:
        archive.add(stage_dir, arcname=stage_dir.name)


def make_zip(stage_dir, archive_path):
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(stage_dir.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(stage_dir.parent))


def main():
    parser = argparse.ArgumentParser(description="Create Lia release archives")
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--out", default="dist")
    args = parser.parse_args()

    binary = (ROOT / args.binary).resolve()
    if not binary.is_file():
        raise SystemExit(f"binary not found: {binary}")

    out_dir = (ROOT / args.out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    package_name, stage_dir = stage_release(binary, args.version, args.platform, out_dir)

    tar_path = out_dir / f"{package_name}.tar.gz"
    zip_path = out_dir / f"{package_name}.zip"
    make_tar_gz(stage_dir, tar_path)
    make_zip(stage_dir, zip_path)

    checksum_path = out_dir / "SHA256SUMS"
    artifacts = [tar_path, zip_path]
    checksum_path.write_text(
        "".join(f"{sha256(path)}  {path.name}\n" for path in artifacts),
        encoding="utf-8",
    )

    print(f"Created {tar_path}")
    print(f"Created {zip_path}")
    print(f"Created {checksum_path}")


if __name__ == "__main__":
    main()
