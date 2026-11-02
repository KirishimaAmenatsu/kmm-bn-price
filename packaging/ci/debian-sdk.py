#!/usr/bin/env python3
"""Extract public, configured headers; never compile/ship substitute KMM libs."""
from pathlib import Path
import re
import shutil
import subprocess
import sys


def create(configured, sdk):
    copied = set()
    for script in configured.rglob("cmake_install.cmake"):
        for block in re.findall(r"file\(INSTALL\s+DESTINATION\s+.*?\)", script.read_text(), re.S):
            match = re.match(r'file\(INSTALL\s+DESTINATION\s+"\$\{CMAKE_INSTALL_PREFIX\}/(include/kmymoney[^"\n]*)"\s+TYPE FILE FILES\s+(.*)\)', block, re.S)
            if not match:
                continue
            destination = sdk / match[1]
            destination.mkdir(parents=True, exist_ok=True)
            for name in re.findall(r'"([^"\n]+)"', match[2]):
                source = Path(name)
                # This settings header requires code generation, is not used
                # by the plugin/tests, and is deliberately NOT stubbed.
                if source.name == "kmymoneysettings.h":
                    continue
                if source.suffix != ".h" or not source.is_file():
                    raise ValueError(f"unexpected/missing public header: {source}")
                shutil.copy2(source, destination / source.name)
                copied.add(source.name)
    if not {"kmymoneyplugin.h", "mymoneyfile.h", "mymoneymoney.h", "kmm_mymoney_export.h", "kmm_plugin_export.h"} <= copied:
        raise ValueError("upstream public-header install layout changed")
    paths = subprocess.check_output(["dpkg-query", "-L", "kmymoney"], text=True).splitlines()
    (sdk / "lib").mkdir(parents=True, exist_ok=True)
    for library in ("kmm_plugin", "kmm_mymoney"):
        candidates = [Path(p) for p in paths if re.fullmatch(rf"/usr/lib/[^\n]*/lib{library}\.so\.5", p)]
        if len(candidates) != 1 or not candidates[0].is_file():
            raise ValueError(f"missing installed KMyMoney runtime library: {library}")
        (sdk / "lib" / f"lib{library}.so").symlink_to(candidates[0])
    print(f"SDK: {len(copied)} public headers and two links to installed runtime libraries")


if __name__ == "__main__":
    create(Path(sys.argv[1]), Path(sys.argv[2]))
