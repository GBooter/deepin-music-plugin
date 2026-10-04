# toolchain-loong64.cmake — 交叉编译工具链
# 宿主机(x86_64) → 目标 loongarch64 (loong64)
#
# 前提（在构建机上）：
#   1) 安装交叉编译器：gcc-loongarch64-linux-gnu g++-loongarch64-linux-gnu binutils-loongarch64-linux-gnu
#   2) 用 Debian 多架构装目标依赖：dpkg --add-architecture loong64 后 apt install <pkg>:loong64
#      （Qt6 / DTK6 / ffmpeg / icu / ssl / sdl 等 dev 包，详见 README 构建依赖）
#   3) 宿主工具（qmlcachegen/qmake6/lrelease 等）必须是 amd64 版：qt6-tools-dev-tools:amd64
#      （交叉构建时工具跑在宿主机上，装成 :loong64 会无法执行）
#
# 用法：
#   cmake -B build-loong64 -S . \
#     -DCMAKE_TOOLCHAIN_FILE=$PWD/toolchain-loong64.cmake \
#     -DCMAKE_BUILD_TYPE=Release -DAPP_VERSION=7.0.68 -DVERSION=7.0.68
#   cmake --build build-loong64 -j$(nproc)
#   DEB_TARGET_ARCH=loong64 ./tools/package-deb-manual.sh build-loong64 dist

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR loongarch64)

# 交叉编译器（gcc-loongarch64-linux-gnu / g++-loongarch64-linux-gnu 提供）
set(CMAKE_C_COMPILER   loongarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER loongarch64-linux-gnu-g++)
set(CMAKE_AR           loongarch64-linux-gnu-ar)
set(CMAKE_RANLIB       loongarch64-linux-gnu-ranlib)
set(CMAKE_STRIP        loongarch64-linux-gnu-strip)
set(CMAKE_LINKER       loongarch64-linux-gnu-ld)

# Debian 多架构：交叉 gcc 已内置目标库/头搜索路径
#   /usr/lib/loongarch64-linux-gnu 、 /usr/include/loongarch64-linux-gnu 、 /usr/include
# 编译与链接交给 gcc 驱动即可。这里只为 CMake 的 find_package（Qt6 / DTK6 的 *Config.cmake）
# 与 pkg-config 指明目标架构路径。
set(CMAKE_PREFIX_PATH
    /usr/lib/loongarch64-linux-gnu/cmake
    /usr/lib/loongarch64-linux-gnu)

set(ENV{PKG_CONFIG_PATH} "/usr/lib/loongarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")

# Qt6 的宿主工具（moc / rcc / qmlcachegen / qmltyperegistrar / qmake6）必须跑在宿主机(amd64)上。
# 交叉构建时由 QT_HOST_PATH 指向宿主 Qt6，否则 qmlcachegen 因架构不匹配而无法运行。
set(QT_HOST_PATH /usr)
set(QT_HOST_PATH_CMAKE_DIR /usr/lib/x86_64-linux-gnu/cmake)
