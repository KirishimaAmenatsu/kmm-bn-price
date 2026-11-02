#!/usr/bin/env bash
# Runs only inside a disposable build container, never on the user's host.
set -euo pipefail
[[ ${CI_CONTAINER:-} == 1 ]] || { echo 'Refusing host package installation' >&2; exit 2; }
export LC_ALL=C.UTF-8
distro=${1:?}
arch=${2:?}
case "$distro/$arch" in
  archlinux/amd64|debian-sid/amd64|debian-sid/aarch64|debian-sid/riscv64|opensuse-tumbleweed/amd64|opensuse-tumbleweed/aarch64|opensuse-tumbleweed/riscv64) ;;
  *) exit 2 ;;
esac
export BUILD_JOBS=${BUILD_JOBS:-2}
mkdir -p /work/source /out
tar -C /input --exclude=.git --exclude='./build*' --exclude=./stage --exclude=./dist -cf - . | tar -C /work/source -xf -
cd /work/source
case "$distro" in
  archlinux)
    pacman -Syu --noconfirm --needed cmake gcc make python qt6-base kcoreaddons kconfig kxmlgui libalkimia gmp kmymoney
    ;;
  debian-sid)
    export DEBIAN_FRONTEND=noninteractive
    sed -i 's/Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources
    apt-get update
    apt-get install -y --no-install-recommends ca-certificates build-essential cmake debhelper python3 qt6-base-dev libkf6coreaddons-dev libkf6config-dev libkf6xmlgui-dev libalkimia6-dev libgmp-dev kmymoney
    bash packaging/ci/debian-sdk.sh
    export KMM_SDK=/work/kmymoney-sdk
    ;;
  opensuse-tumbleweed)
    zypper --non-interactive refresh
    zypper --non-interactive install --no-recommends cmake gcc-c++ make python3 rpm-build kmymoney kmymoney-devel 'cmake(Qt6Test)' 'cmake(KF6CoreAddons)' 'cmake(KF6Config)' 'cmake(KF6XmlGui)' 'cmake(LibAlkimia6)' gmp-devel
    ;;
esac
# Tests and compatibility headers currently target the upstream 5.2.2 ABI.
case "$distro" in
  archlinux) [[ $(pacman -Q kmymoney | cut -d ' ' -f2) == 5.2.2-* ]] ;;
  debian-sid) [[ $(dpkg-query -W -f='${source:Version}' kmymoney) == 5.2.2-* ]] ;;
  opensuse-tumbleweed) [[ $(rpm -q --qf '%{VERSION}' kmymoney) == 5.2.2 ]] ;;
esac
# Query the actual Qt6 installation, including Debian multiarch and RPM lib64.
qtpaths=
for candidate in qtpaths6 qtpaths-qt6 /usr/lib/qt6/bin/qtpaths /usr/lib64/qt6/bin/qtpaths qtpaths; do
  if tool=$(command -v "$candidate") && [[ $("$tool" --query QT_VERSION) == 6.* ]]; then
    qtpaths=$tool
    break
  fi
done
[[ -n $qtpaths ]]
export KMM_PLUGIN_DIR
KMM_PLUGIN_DIR="$("$qtpaths" --query QT_INSTALL_PLUGINS)/kmymoney_plugins"
[[ $KMM_PLUGIN_DIR == /usr/*/kmymoney_plugins ]]
version=$(python3 packaging/ci/metadata.py version)
asset=$(python3 packaging/ci/metadata.py asset "$distro" "$arch")
python3 - "$version" <<'PY'
import re, sys
from pathlib import Path
recipe = Path('packaging/arch/PKGBUILD').read_text()
assert re.search(r'^pkgver=' + re.escape(sys.argv[1]) + '$', recipe, re.M), 'PKGBUILD version must match CMake'
PY
tar --exclude=./build --exclude=./obj-ci --exclude=./debian -czf /work/source.tar.gz -C /work source
case "$distro" in
  archlinux)
    useradd -m builder
    mkdir -p /work/arch
    cp packaging/arch/PKGBUILD /work/source.tar.gz /work/arch/
    chown -R builder:builder /work
    runuser -u builder -- env KMM_PLUGIN_DIR="$KMM_PLUGIN_DIR" BUILD_JOBS="$BUILD_JOBS" bash -c 'cd /work/arch; makepkg --noconfirm'
    packages=(/work/arch/*.pkg.tar.zst)
    [[ ${#packages[@]} == 1 ]]
    cp "${packages[0]}" "/out/$asset"
    pacman -U --noconfirm "/out/$asset"
    test_binary=/work/arch/src/build/test_plugin
    ;;
  debian-sid)
    cp -r packaging/debian debian
    chmod +x debian/rules
    printf 'kmm-binance-price (%s-1) unstable; urgency=medium\n\n  * CI source build.\n\n -- KMyMoney Binance Plugin contributors <noreply@example.invalid>  Thu, 01 Jan 2026 00:00:00 +0000\n' "$version" > debian/changelog
    dpkg-buildpackage --build=binary --no-sign
    packages=(/work/kmm-binance-price_*.deb)
    [[ ${#packages[@]} == 1 ]]
    cp "${packages[0]}" "/out/$asset"
    apt-get install -y "/out/$asset"
    test_binary=/work/source/obj-ci/test_plugin
    ;;
  opensuse-tumbleweed)
    mkdir -p /work/rpm/{SOURCES,SPECS,BUILD,BUILDROOT,RPMS,SRPMS}
    cp /work/source.tar.gz /work/rpm/SOURCES/
    kmm_version=$(rpm -q --qf '%{VERSION}-%{RELEASE}' kmymoney)
    # RPM 4.20+ otherwise removes the build tree before the installed-package
    # discovery check can reuse the offline test executable.
    rpmbuild -ba --noclean --define '_topdir /work/rpm' --define "plugin_version $version" --define "kmm_version $kmm_version" --define "plugin_dir $KMM_PLUGIN_DIR" packaging/rpm/kmm-binance-price.spec
    mapfile -t packages < <(find /work/rpm/RPMS -name 'kmm-binance-price-*.rpm' ! -name '*debuginfo*' ! -name '*debugsource*')
    [[ ${#packages[@]} == 1 ]]
    cp "${packages[0]}" "/out/$asset"
    zypper --non-interactive --no-gpg-checks install "/out/$asset"
    test_binary=$(find /work/rpm/BUILD -path '*/build/test_plugin' -type f)
    [[ -n $test_binary && $test_binary != *$'\n'* ]]
    ;;
esac
python3 packaging/ci/verify-package.py "$distro" "$arch" "/out/$asset" "$KMM_PLUGIN_DIR"
QT_QPA_PLATFORM=offscreen "$test_binary" --discover-root "${KMM_PLUGIN_DIR%/kmymoney_plugins}"
