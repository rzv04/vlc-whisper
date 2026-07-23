# Building

cmake -S worker -B build/windows-x64 \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/windows-x64-mingw.cmake"

cmake --build build/windows-x64