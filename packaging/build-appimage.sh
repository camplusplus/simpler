#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${project_dir}/build"
appdir="${project_dir}/AppDir"
release_dir="${project_dir}/dist"
linuxdeploy="${LINUXDEPLOY:-${project_dir}/packaging/linuxdeploy}"

case "$(uname -m)" in
    x86_64) linuxdeploy_arch="x86_64" ;;
    aarch64) linuxdeploy_arch="aarch64" ;;
    armv7l|armv6l) linuxdeploy_arch="armhf" ;;
    *)
        echo "Unsupported host architecture: $(uname -m)" >&2
        exit 1
        ;;
esac

if [[ ! -x "${linuxdeploy}" ]]; then
    if [[ -n "${LINUXDEPLOY:-}" ]]; then
        echo "LINUXDEPLOY does not point to an executable: ${linuxdeploy}" >&2
        exit 1
    fi
    mkdir -p "$(dirname "${linuxdeploy}")"
    curl --fail --location --retry 3 \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${linuxdeploy_arch}.AppImage" \
        --output "${linuxdeploy}"
    chmod +x "${linuxdeploy}"
fi

cmake -S "${project_dir}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}" --parallel

rm -rf "${appdir}"
mkdir -p "${appdir}/usr/bin" "${appdir}/usr/share/applications"
install -m 0755 "${build_dir}/simpler_sampler" "${appdir}/usr/bin/simpler_sampler"
install -m 0644 "${project_dir}/packaging/simpler-sampler.desktop" \
    "${appdir}/usr/share/applications/simpler-sampler.desktop"
install -m 0644 "${project_dir}/packaging/simpler-sampler.svg" \
    "${appdir}/simpler-sampler.svg"

"${linuxdeploy}" \
    --appdir "${appdir}" \
    --desktop-file "${appdir}/usr/share/applications/simpler-sampler.desktop" \
    --icon-file "${appdir}/simpler-sampler.svg" \
    --output appimage

mkdir -p "${release_dir}"
find "${project_dir}" -maxdepth 1 -type f -name 'Simpler-Sampler-*.AppImage' \
    -exec mv -f {} "${release_dir}/" \;
echo "AppImage written to ${release_dir}/"
