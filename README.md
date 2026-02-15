Carve
============

A simple-to-use 3D modeling tool aimed at replacing parts of the Hammer editor.

Current scaffold:
- Qt6 desktop application shell
- bgfx renderer initialization inside a native Qt widget
- Clear-color 3D viewer loop (no scene yet)

Build requirements:
- CMake 3.24+
- C++23 compiler (MSVC 2022+, clang 16+, or gcc 13+)
- `uv` (used to install and run Conan 2.x)
- Internet access during first Conan install (to fetch dependencies)
- `gdb` (optional, only needed for source-level debugging in VS Code)

Dependencies are resolved from Conan Center:
- `qt/6.10.1`
- `bgfx/1.129.8930-495`
- `cgal/6.1.1`

Developer setup guide: `DEVELOPER_SETUP.md`

Shader workflow:
- Source shaders are in `src/render/shaders_src` (text `.shader` files).
- Compiled backend binaries are emitted to `src/render/shaders/<backend>/*.bin`.
- VS Code task: `Shaders: Compile All` (runs `scripts/compile_shaders.sh`).
- If auto-detection fails, set either `SHADERC=/path/to/shaderc` or `BGFX_DIR=/path/to/bgfx`.
