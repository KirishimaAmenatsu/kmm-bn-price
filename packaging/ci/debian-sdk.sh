#!/usr/bin/env bash
set -euo pipefail
[[ ${CI_CONTAINER:-} == 1 ]]
source_version=$(dpkg-query -W -f='${source:Version}' kmymoney)
[[ $source_version == 5.2.2-* ]] || { echo 'KMyMoney ABI changed; review SDK and plugin before rebuilding' >&2; exit 1; }
apt-get build-dep -y --no-install-recommends "kmymoney=$source_version"
mkdir -p /work/sdk-source
cd /work/sdk-source
apt-get source "kmymoney=$source_version"
sources=(/work/sdk-source/kmymoney-*)
[[ ${#sources[@]} == 1 && -d ${sources[0]} ]]
cmake -S "${sources[0]}" -B /work/sdk-configured \
  -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_SKIP_RPATH=ON -DBUILD_TESTING=OFF \
  -DENABLE_WEBENGINE=ON -DBUILD_WITH_QT6=ON
# Debian removes all development headers and unversioned .so links. Do not
# build/repackage a different host: take only the configured public headers
# listed by upstream's install rules, and link to the installed runtime.
python3 /work/source/packaging/ci/debian-sdk.py /work/sdk-configured /work/kmymoney-sdk
