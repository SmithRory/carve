Developer setup guide (VS Code + CMake Tools)
==============================================

This guide targets Ubuntu/Debian systems, including Ubuntu on WSL.

Prerequisites:
1. Install VS Code extensions: `clangd` and `CMake Tools`.
2. Install local tools: `clang`, `cmake` (3.24+), `uv`, and `conan` (2.x).
3. Install required C++ dependencies through Conan (includes CGAL) using the
   `conan install` commands in the next section.

One-time dependency setup (per build type):
1. In the VS Code integrated terminal, create the Conan profile:

```bash
CC=clang CXX=clang++ conan profile detect --name=clang --force
```

2. Install dependencies for each preset you plan to use:

```bash
# Release preset (carve-release)
CC=clang CXX=clang++ conan install . -of build/conan-release -pr:h=clang -pr:b=clang -s build_type=Release --build=missing -c tools.system.package_manager:mode=install -c tools.system.package_manager:sudo=True

# Debug preset (carve-debug / carve-debug-tidy)
CC=clang CXX=clang++ conan install . -of build/conan-debug -pr:h=clang -pr:b=clang -s build_type=Debug --build=missing -c tools.system.package_manager:mode=install -c tools.system.package_manager:sudo=True
```

Daily workflow in VS Code CMake Tools window:
1. Open the CMake Tools view (`View` -> `Open View...` -> `CMake`).
2. Select a configure preset in the CMake view:
   - `carve-release` for standard runs
   - `carve-debug` for debug symbols
   - `carve-debug-tidy` for debug + clang-tidy checks
3. Click `Configure` in the CMake view.
4. Click `Build` in the CMake view.
5. Set launch target to `carve` in CMake Tools.
6. Open Run and Debug, choose one of:
   - `Run carve (Conan env)`
   - `Debug carve (Conan env, WSL)`
7. Press `F5` (Start Debugging) to launch.

Switching run/debug behavior:
1. Change the active CMake preset in CMake Tools.
2. Re-run `Configure` and `Build`.
3. Press `F5` again with the desired Run and Debug configuration selected.

Notes:
- Use the CMake Tools window for configure/build as the primary workflow.
- The launch configurations apply the Conan runtime environment required by Qt plugins.
- `Debug carve (Conan env, WSL)` requires `gdb` at `/usr/bin/gdb`.
- Running the Qt app in WSL requires GUI support (WSLg on Windows 11, or an X server setup).

Optional: Shader compilation setup
----------------------------------

This is only needed if you use the `Shaders: Compile All` VS Code task.

1. Build bgfx shader tools locally (includes `shaderc`):

```bash
git clone https://github.com/bkaradzic/bx.git
git clone https://github.com/bkaradzic/bimg.git
git clone https://github.com/bkaradzic/bgfx.git

cd ~/bgfx
make tools
```

2. Point this repo to your local bgfx checkout:

```bash
export BGFX_DIR="$HOME/bgfx"
export SHADERC="$BGFX_DIR/tools/bin/linux/shaderc"
```

3. Verify `shaderc` is available:

```bash
test -x "${SHADERC}"
"${SHADERC}" --version
```

4. Compile shaders:

From this repository root:

```bash
cd /absolute/path/to/carve
./scripts/compile_shaders.sh
```

By default, the script compiles host-appropriate targets:
- Linux: `essl glsl spirv`
- macOS: `essl glsl metal spirv`

To override, set `SHADER_TARGETS` (space-separated), for example:

```bash
SHADER_TARGETS="spirv glsl" ./scripts/compile_shaders.sh
```

5. Compile shaders from VS Code:
Open `Terminal` -> `Run Task...`, select `Shaders: Compile All`, and wait for completion. Compiled outputs are written to `src/render/shaders/<backend>/`.
