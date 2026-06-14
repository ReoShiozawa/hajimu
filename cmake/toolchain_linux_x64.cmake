# CMake Toolchain: Linux x86_64 (musl-cross)
# 使用方法: cmake -DCMAKE_TOOLCHAIN_FILE=/path/to/this/file ...
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   x86_64-linux-musl-gcc)
set(CMAKE_CXX_COMPILER x86_64-linux-musl-g++)

set(MUSL_SYSROOT /opt/homebrew/Cellar/musl-cross/0.9.11/libexec/x86_64-linux-musl)
set(CMAKE_FIND_ROOT_PATH ${MUSL_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
