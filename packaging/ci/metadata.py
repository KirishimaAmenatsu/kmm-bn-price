#!/usr/bin/env python3
"""One source of truth for target names, versions, and release asset allowlists."""
import argparse
import json
import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
TARGETS = [
    dict(distro="archlinux", arch="amd64", native="x86_64", machine="Advanced Micro Devices X86-64", image="archlinux:base-devel", runner="ubuntu-24.04", timeout=90, suffix="pkg.tar.zst"),
    *[dict(distro="debian-sid", arch=a, native=n, machine=m, image="debian:sid-slim", runner=r, timeout=t, suffix="deb") for a, n, m, r, t in [
        ("amd64", "amd64", "Advanced Micro Devices X86-64", "ubuntu-24.04", 120),
        ("aarch64", "arm64", "AArch64", "ubuntu-24.04-arm", 120),
        ("riscv64", "riscv64", "RISC-V", "ubuntu-24.04", 360)]],
    *[dict(distro="opensuse-tumbleweed", arch=a, native=n, machine=m, image="registry.opensuse.org/opensuse/tumbleweed:latest", runner=r, timeout=t, suffix="rpm") for a, n, m, r, t in [
        ("amd64", "x86_64", "Advanced Micro Devices X86-64", "ubuntu-24.04", 90),
        ("aarch64", "aarch64", "AArch64", "ubuntu-24.04-arm", 90),
        ("riscv64", "riscv64", "RISC-V", "ubuntu-24.04", 360)]],
]


def version(root=ROOT):
    text = (root / "CMakeLists.txt").read_text()
    match = re.search(r"project\(kmm_binance_price VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES CXX\)", text)
    if not match:
        raise ValueError("missing strict CMake project version")
    plugin_version = json.loads((root / "src/plugin.json").read_text())["KPlugin"]["Version"]
    if plugin_version != match[1]:
        raise ValueError("Qt plugin metadata version must match CMake")
    return match[1]


def validate_tag(tag, value):
    if not re.fullmatch(r"v(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", tag) or tag != "v" + value:
        raise ValueError("release tag must be vX.Y.Z and match CMake project VERSION")


def target(distro, arch):
    return next(x for x in TARGETS if x["distro"] == distro and x["arch"] == arch)


def asset(item, value):
    return f"kmm-binance-price_{value}-1_{item['distro']}_{item['arch']}.{item['suffix']}"


def validate_assets(directory, value):
    expected = {asset(x, value) for x in TARGETS}
    actual = {p.name for p in directory.iterdir()}
    if actual != expected or any(not p.is_file() or p.is_symlink() or p.stat().st_size == 0 for p in directory.iterdir()):
        raise ValueError(f"asset mismatch: missing={expected - actual}, unexpected={actual - expected}")
    return sorted(expected)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["prepare", "version", "asset", "check-assets"])
    parser.add_argument("args", nargs="*")
    args = parser.parse_args()
    value = version()
    if args.command == "prepare":
        tag = os.environ.get("GITHUB_REF", "").startswith("refs/tags/")
        if tag:
            validate_tag(os.environ["GITHUB_REF_NAME"], value)
        full = tag or os.environ.get("GITHUB_EVENT_NAME") == "workflow_dispatch"
        matrix = TARGETS if full else [x for x in TARGETS if x["arch"] == "amd64"]
        output = "matrix=" + json.dumps({"include": matrix}, separators=(",", ":")) + "\n"
        with open(os.environ["GITHUB_OUTPUT"], "a") as stream:
            stream.write(output)
    elif args.command == "version":
        print(value)
    elif args.command == "asset":
        print(asset(target(*args.args), value))
    else:
        print("\n".join(validate_assets(Path(args.args[0]), value)))


if __name__ == "__main__":
    main()
