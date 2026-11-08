#!/bin/sh
# Cross-compiles the Windows VST3 (x86_64) from Linux/macOS with llvm-mingw.
#   ./build-vst-win-mingw.sh <path to llvm-mingw> [vst3sdk dir]
# On Windows itself, MSVC is simpler:
#   cmake -S plugin -B build-vst -A x64 && cmake --build build-vst --config Release
set -e
MINGW=${1:?usage: $0 <llvm-mingw dir> [vst3sdk dir]}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$HERE/build-vst-win
mkdir -p "$WORK/bin"
SDK=${2:-$WORK/vst3sdk}
if [ ! -d "$SDK" ]; then
  git clone --depth 1 --branch v3.8.0_build_66 --recurse-submodules --shallow-submodules \
    https://github.com/steinbergmedia/vst3sdk.git "$SDK"
fi
python3 "$HERE/tools/patch_vst3sdk_mingw.py" "$SDK"
printf '#!/bin/sh\nexit 0\n' > "$WORK/bin/attrib"; chmod +x "$WORK/bin/attrib"
cat > "$WORK/toolchain.cmake" <<T
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $MINGW/bin/x86_64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER $MINGW/bin/x86_64-w64-mingw32-clang++)
set(CMAKE_RC_COMPILER $MINGW/bin/x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH $MINGW/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
T
cmake -S "$HERE/plugin" -B "$WORK/build" -DCMAKE_TOOLCHAIN_FILE="$WORK/toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DVST3_SDK_ROOT="$SDK" \
  -DSMTG_RUN_VST_VALIDATOR=OFF -DSMTG_CREATE_MODULE_INFO=OFF -DSMTG_USE_STATIC_CRT=ON \
  -DCMAKE_CXX_FLAGS="-DVSTGUI_WIN32_CUSTOMFONT_SUPPORT=0 -D__maybenull= -D__out= -D__in= -D__inout= -D__in_opt= -D__out_opt= -D__notnull= -DWS_EX_NOREDIRECTIONBITMAP=0x00200000L -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00"
PATH="$WORK/bin:$PATH" cmake --build "$WORK/build" -j4 --target EPS16
echo "VST3: $WORK/build/VST3/Release/EPS16.vst3"
