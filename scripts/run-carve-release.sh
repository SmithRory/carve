#!/usr/bin/env bash
set -eo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <target-path> [args...]" >&2
    exit 2
fi

target_path="$1"
shift

target_dir="$(cd "$(dirname "$target_path")" && pwd)"
target_name="$(basename "$target_path")"
target_exe="${target_dir}/${target_name}"
conan_run="${target_dir}/conanrun.sh"

if [[ ! -f "$conan_run" ]]; then
    echo "Missing Conan runtime env script: $conan_run" >&2
    exit 1
fi

# shellcheck source=/dev/null
source "$conan_run"
export FONTCONFIG_PATH="${FONTCONFIG_PATH:-/etc/fonts}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"

# bgfx's Vulkan path can crash under WSL/X11; force fallback when needed.
if [[ -n "${WSL_DISTRO_NAME:-}" || -n "${WSL_INTEROP:-}" ]]; then
    export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/nonexistent}"
fi

exec "$target_exe" "$@"
