#!/usr/bin/env python3
"""One reviewed Mono pin for download, packaging and Wine compatibility checks.

Pin from wine-valve cd547f7a: dlls/appwiz.cpl/addons.c (MONO_VERSION,
MONO_SHA) and dlls/mscoree/mscoree_private.h (WINE_MONO_VERSION).
Changing Wine must fail closed until its Mono dependency is reviewed too.
"""
import argparse
import hashlib
from pathlib import Path
import re

VERSION = "10.4.1"
SHA256 = "071f4b2887e1c97a11d791ff3d65be9429eed6dec4c2708888bfd546ba358e23"
FILENAME = f"wine-mono-{VERSION}-x86.msi"
PAYLOAD_PATH = f"share/wine/mono/{FILENAME}"
URL = f"https://dl.winehq.org/wine/wine-mono/{VERSION}/{FILENAME}"


def check_source(source: Path) -> None:
    for relative, macro in (("dlls/appwiz.cpl/addons.c", "MONO_VERSION"),
                            ("dlls/mscoree/mscoree_private.h", "WINE_MONO_VERSION")):
        text = (source / relative).read_text()
        versions = re.findall(r'^\s*#define\s+' + macro + r'\s+"([^"]+)"', text, re.M)
        if versions != [VERSION]:
            raise ValueError(f"Wine Mono source mismatch in {relative}: {versions}, expected {VERSION}")
        if macro == "MONO_VERSION" and not re.search(
                r'^\s*#define\s+MONO_SHA\s+"' + SHA256 + r'"', text, re.M):
            raise ValueError("Wine Mono source checksum differs from the reviewed MSI")


def check_installer(data: bytes, name: str) -> None:
    # Inspect actual packaged PE bytes, not only the source/manifest: cached
    # Wine DLLs can belong to a different source revision. Both strings are
    # retained by appwiz's installer (also after strip).
    if not data.startswith(b"MZ") or FILENAME.encode("utf-16-le") not in data:
        raise ValueError(f"Wine Mono installer mismatch: {name} does not request {FILENAME}")
    if SHA256.encode() not in data and SHA256.encode("utf-16-le") not in data:
        raise ValueError(f"Wine Mono installer checksum mismatch: {name}")


def check_msi(data: bytes) -> None:
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError(f"Wine Mono MSI checksum mismatch: {FILENAME}")


def validate_payload(read, names, mono, pe_arch: str) -> None:
    if not isinstance(mono, dict):
        raise ValueError("component manifest has no mono object")
    if mono.get("version") != VERSION or mono.get("msi") != PAYLOAD_PATH:
        raise ValueError("Wine Mono manifest version/path differs from the reviewed Wine dependency")
    installers = [f"bin/{arch}/appwiz.cpl" for arch in (pe_arch, "i386-windows")]
    if mono.get("status") == "bundled":
        if mono.get("sha256") != SHA256:
            raise ValueError("Wine Mono manifest checksum mismatch")
        check_msi(read(PAYLOAD_PATH))
        for name in installers:
            check_installer(read(name), name)
    elif mono.get("status") == "disabled":
        if mono.get("sha256") != "" or any(name in names for name in installers) or any(
                name.startswith("share/wine/mono/") and name.endswith(".msi") for name in names):
            raise ValueError("Wine Mono payload is present although disabled")
    else:
        raise ValueError(f"invalid Wine Mono status: {mono.get('status')!r}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("field", nargs="?", choices=("version", "sha256", "filename", "url"))
    parser.add_argument("--source", type=Path)
    parser.add_argument("--installer", type=Path, action="append", default=[])
    parser.add_argument("--msi", type=Path)
    args = parser.parse_args()
    try:
        if args.source:
            check_source(args.source)
        for path in args.installer:
            check_installer(path.read_bytes(), str(path))
        if args.msi:
            check_msi(args.msi.read_bytes())
        if args.field:
            print({"version": VERSION, "sha256": SHA256, "filename": FILENAME, "url": URL}[args.field])
    except (OSError, ValueError) as error:
        parser.exit(1, f"Wine Mono validation failed: {error}\n")


if __name__ == "__main__":
    main()
