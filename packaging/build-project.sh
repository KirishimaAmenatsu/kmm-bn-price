#!/usr/bin/env bash
set -euo pipefail
mode=$1
source_dir=$2
build_dir=$3
case "$mode" in
  configure)
    args=()
    if [[ -n ${KMM_SDK:-} ]]; then
      args+=("-DCMAKE_PREFIX_PATH=$KMM_SDK")
    fi
    cmake -S "$source_dir" -B "$build_dir" -DBUILD_TESTING=ON \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_SKIP_RPATH=ON -DKMM_PLUGIN_INSTALL_DIR="${KMM_PLUGIN_DIR:?}" "${args[@]}"
    ;;
  build) cmake --build "$build_dir" --parallel "${BUILD_JOBS:-2}" ;;
  test) QT_QPA_PLATFORM=offscreen ctest --test-dir "$build_dir" --output-on-failure ;;
  install) DESTDIR=${4:?} cmake --install "$build_dir" ;;
  *) exit 2 ;;
esac
