#!/usr/bin/env python3
"""Fail closed on package metadata, payload layout, ELF, and dynamic linkage."""
from pathlib import Path
import re
import subprocess
import sys

from metadata import target, version


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def verify(distro, arch, package, plugin_dir, installed_root="/"):
    item = target(distro, arch)
    expected = plugin_dir + "/kmm_binance_price.so"
    value = version()
    if distro == "archlinux":
        info = run("bsdtar", "-xOf", package, ".PKGINFO")
        assert f"arch = {item['native']}" in info
        assert f"pkgver = {value}-1" in info
        assert f"depend = kmymoney={run('pacman', '-Q', 'kmymoney').split()[1]}" in info
        files = run("bsdtar", "-tf", package).splitlines()
    elif distro == "debian-sid":
        assert run("dpkg-deb", "-f", package, "Architecture") == item["native"]
        assert run("dpkg-deb", "-f", package, "Version") == value + "-1"
        deps = run("dpkg-deb", "-f", package, "Depends")
        assert f"kmymoney (= {run('dpkg-query', '-W', '-f=${Version}', 'kmymoney')})" in deps
        assert "libc6" in deps and "libqt6core6" in deps, "missing dpkg-shlibdeps dependencies"
        # fsys-tarfile is the real package payload, not a filename heuristic.
        with subprocess.Popen(["dpkg-deb", "--fsys-tarfile", package], stdout=subprocess.PIPE) as producer:
            listing = subprocess.run(["tar", "-tf", "-"], stdin=producer.stdout, capture_output=True, text=True, check=True)
            producer.stdout.close()
            assert producer.wait() == 0
        files = listing.stdout.splitlines()
    else:
        assert run("rpm", "-qp", "--qf", "%{ARCH}", package) == item["native"]
        assert run("rpm", "-qp", "--qf", "%{VERSION}-%{RELEASE}", package) == value + "-1"
        deps = run("rpm", "-qpR", package)
        assert "kmymoney = " + run("rpm", "-q", "--qf", "%{VERSION}-%{RELEASE}", "kmymoney") in deps
        assert "libQt6Core.so.6" in deps and "libkmm_mymoney.so.5" in deps
        files = run("rpm", "-qpl", package).splitlines()
    files = ["/" + f.removeprefix("./").lstrip("/") for f in files]
    assert files.count(expected) == 1, (expected, files)
    # The payload may contain only the plugin, native package metadata, and
    # license/docs/directories. No SDK headers, test executables, or host libs.
    allowed_metadata = {"/.PKGINFO", "/.BUILDINFO", "/.MTREE"}
    for name in files:
        assert name == expected or name in allowed_metadata or name.endswith("/") or name.startswith("/usr/share/doc/") or name.startswith("/usr/share/licenses/"), name
    plugin_file = str(Path(installed_root) / expected.lstrip("/"))
    header = run("readelf", "-h", plugin_file)
    assert re.search(r"Machine:\s+" + re.escape(item["machine"]) + r"\s*$", header, re.M), header
    linked = run("ldd", plugin_file)
    assert "not found" not in linked, linked
    assert "libkmm_plugin.so.5" in linked and "libkmm_mymoney.so.5" in linked
    dynamic = run("readelf", "-d", plugin_file)
    assert "RPATH" not in dynamic and "RUNPATH" not in dynamic, dynamic
    print(f"Verified {distro}/{arch}: native metadata, exact KMyMoney dependency, payload, ELF and linkage under {installed_root}")


if __name__ == "__main__":
    verify(*sys.argv[1:])
