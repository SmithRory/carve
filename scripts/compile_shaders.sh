#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${ROOT_DIR}/src/render/shaders_src"
OUT_DIR="${ROOT_DIR}/src/render/shaders"

find_shaderc() {
    if [[ -n "${SHADERC:-}" && -x "${SHADERC}" ]]; then
        printf '%s\n' "${SHADERC}"
        return 0
    fi

    if command -v shaderc >/dev/null 2>&1; then
        command -v shaderc
        return 0
    fi

    if [[ -n "${BGFX_DIR:-}" && -x "${BGFX_DIR}/tools/bin/linux/shaderc" ]]; then
        printf '%s\n' "${BGFX_DIR}/tools/bin/linux/shaderc"
        return 0
    fi

    if [[ -n "${BGFX_DIR:-}" && -d "${BGFX_DIR}/.build" ]]; then
        local candidate
        candidate="$(find "${BGFX_DIR}/.build" -type f \( -name shaderc -o -name shadercRelease \) 2>/dev/null | head -n 1 || true)"
        if [[ -n "${candidate}" && -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    fi

    if [[ -x "${HOME}/bgfx/tools/bin/linux/shaderc" ]]; then
        printf '%s\n' "${HOME}/bgfx/tools/bin/linux/shaderc"
        return 0
    fi

    if [[ -d "${HOME}/bgfx/.build" ]]; then
        local candidate
        candidate="$(find "${HOME}/bgfx/.build" -type f \( -name shaderc -o -name shadercRelease \) 2>/dev/null | head -n 1 || true)"
        if [[ -n "${candidate}" && -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    fi

    return 1
}

find_bgfx_shader_include() {
    if [[ -n "${BGFX_SHADER_INCLUDE:-}" && -f "${BGFX_SHADER_INCLUDE}/bgfx_shader.sh" ]]; then
        printf '%s\n' "${BGFX_SHADER_INCLUDE}"
        return 0
    fi

    if [[ -n "${BGFX_DIR:-}" ]]; then
        if [[ -f "${BGFX_DIR}/src/bgfx_shader.sh" ]]; then
            printf '%s\n' "${BGFX_DIR}/src"
            return 0
        fi
        if [[ -f "${BGFX_DIR}/include/bgfx/bgfx_shader.sh" ]]; then
            printf '%s\n' "${BGFX_DIR}/include/bgfx"
            return 0
        fi
    fi

    if [[ -f "${HOME}/bgfx/src/bgfx_shader.sh" ]]; then
        printf '%s\n' "${HOME}/bgfx/src"
        return 0
    fi

    local candidate
    candidate="$(find "${HOME}/.conan2" -type f -path '*/include/bgfx/bgfx_shader.sh' 2>/dev/null | head -n 1 || true)"
    if [[ -n "${candidate}" ]]; then
        dirname "${candidate}"
        return 0
    fi

    return 1
}

SHADERC_BIN="$(find_shaderc || true)"
if [[ -z "${SHADERC_BIN}" ]]; then
    cat <<'EOF'
Failed to locate bgfx shader compiler (shaderc).

Set one of:
  SHADERC=/absolute/path/to/shaderc
  BGFX_DIR=/path/to/bgfx (with tools/bin/linux/shaderc or .build output)

Then rerun this script.
EOF
    exit 1
fi

BGFX_SHADER_INCLUDE_DIR="$(find_bgfx_shader_include || true)"
if [[ -z "${BGFX_SHADER_INCLUDE_DIR}" ]]; then
    cat <<'EOF'
Failed to locate bgfx_shader.sh include directory.

Set one of:
  BGFX_SHADER_INCLUDE=/path/containing/bgfx_shader.sh
  BGFX_DIR=/path/to/bgfx (with src/bgfx_shader.sh or include/bgfx/bgfx_shader.sh)

Then rerun this script.
EOF
    exit 1
fi

if [[ ! -d "${SRC_DIR}" ]]; then
    echo "Shader source directory not found: ${SRC_DIR}" >&2
    exit 1
fi

if [[ ! -f "${SRC_DIR}/varying.def.sc" ]]; then
    echo "Shader varying definition file not found: ${SRC_DIR}/varying.def.sc" >&2
    exit 1
fi

mkdir -p "${OUT_DIR}"

if [[ -n "${SHADER_TARGETS:-}" ]]; then
    # Space-separated list, e.g. "spirv glsl".
    read -r -a targets <<<"${SHADER_TARGETS}"
else
    case "$(uname -s)" in
    Linux)
        targets=(essl glsl spirv)
        ;;
    Darwin)
        targets=(essl glsl metal spirv)
        ;;
    *)
        targets=(dxbc dxil essl glsl metal spirv)
        ;;
    esac
fi

platform_for_target() {
    case "$1" in
    dxbc) printf '%s\n' windows ;;
    dxil) printf '%s\n' windows ;;
    essl) printf '%s\n' android ;;
    glsl) printf '%s\n' linux ;;
    metal) printf '%s\n' osx ;;
    spirv) printf '%s\n' linux ;;
    *)
        return 1
        ;;
    esac
}

profile_for_target() {
    case "$1" in
    dxbc) printf '%s\n' s_5_0 ;;
    dxil) printf '%s\n' s_6_0 ;;
    essl) printf '%s\n' 100_es ;;
    glsl) printf '%s\n' 120 ;;
    metal) printf '%s\n' metal ;;
    spirv) printf '%s\n' spirv ;;
    *)
        return 1
        ;;
    esac
}

compile_shader_set() {
    local type="$1"
    local prefix="$2"
    shopt -s nullglob
    local files=("${SRC_DIR}/${prefix}"*.shader)
    shopt -u nullglob

    if [[ ${#files[@]} -eq 0 ]]; then
        return 0
    fi

    local target
    for target in "${targets[@]}"; do
        local out_target_dir="${OUT_DIR}/${target}"
        mkdir -p "${out_target_dir}"

        local platform
        platform="$(platform_for_target "${target}")"
        local profile
        profile="$(profile_for_target "${target}")"

        local shader_file
        for shader_file in "${files[@]}"; do
            local base
            base="$(basename "${shader_file}" .shader)"
            local out_file="${out_target_dir}/${base}.bin"

            echo "[${target}] ${base}.shader -> ${out_file}"
            "${SHADERC_BIN}" \
                --platform "${platform}" \
                -p "${profile}" \
                -O 3 \
                --type "${type}" \
                -i "${SRC_DIR}" \
                -i "${SRC_DIR}/common" \
                -i "${BGFX_SHADER_INCLUDE_DIR}" \
                --varyingdef "${SRC_DIR}/varying.def.sc" \
                --disasm \
                -f "${shader_file}" \
                -o "${out_file}"
        done
    done
}

compile_shader_set vertex "vs_"
compile_shader_set fragment "fs_"
compile_shader_set compute "cs_"

echo "Shader compilation completed."
