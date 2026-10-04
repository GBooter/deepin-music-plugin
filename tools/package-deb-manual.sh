#!/usr/bin/env bash
# 手动构建 .deb 包（debhelper 不可用时的替代方案）—— deepin-music 在线歌词增强版
#
# 背景：本机是 **deepin 25 磐石系统**，宿主与自建 sysroot 里都**没有 debhelper(dh)**，
# 因此 `dpkg-buildpackage`（debian/rules 里执行 `dh $@`）无法运行。本脚本改为：
#   1) 用 DESTDIR 暂存 `cmake --install` 的安装树（尊重项目自身的 install() 规则）
#   2) 手写 DEBIAN/control（满足 deepin 应用商店的必填字段要求）
#   3) 按 dpkg-shlibdeps 的等价逻辑**自动推导** Depends（见下方步骤 3）
#   4) `dpkg-deb --build --root-owner-group` 生成 .deb
#
# 用途：**上架 deepin 应用商店 / 分发**。磐石系统下deb 由 apt/dpkg 接管，
# 可正常安装到 /usr，无需关闭只读（本机自用请改用 tools/install-local.sh）。
#
# 产物结构与 dpkg-buildpackage 一致（/usr/bin, /usr/lib, /usr/share/...）。
# 注意：本脚本不产出 -dbgsym 符号包，也不做 lintian 检查。
#
# 用法：./package-deb-manual.sh <build-dir> <out-dir>
#   build-dir : 已完成 Release 构建的目录（含 CMakeCache.txt）
#   out-dir   : .deb 输出目录

set -euo pipefail

BUILD_DIR="${1:?用法: $0 <build-dir> <out-dir>}"
OUT_DIR="${2:?用法: $0 <build-dir> <out-dir>}"

PKG_NAME="deepin-music-online-lyric"
DISPLAY_NAME="Deepin音乐-V2"
# 上架 deepin 应用商店时 control 必填字段，不能留空（商店自动质检会拒包）
MAINTAINER="Deepin音乐-V2 <noreply@deepin.org>"
HOMEPAGE="https://github.com/linuxdeepin/deepin-music"
# 脚本位于 <project>/tools/，changelog 在 <project>/debian/changelog（上一级）
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CHANGELOG="$SCRIPT_DIR/../debian/changelog"
[ -f "$CHANGELOG" ] || { echo "ERROR: 找不到 $CHANGELOG"; exit 1; }
VERSION="$(sed -n '1s/.*(\([0-9.]*\)).*/\1/p' "$CHANGELOG")"
[ -n "$VERSION" ] || { echo "ERROR: 无法从 $CHANGELOG 解析版本号"; exit 1; }
# 交叉编译时目标架构由 DEB_TARGET_ARCH 指定（如 loong64）；否则用宿主架构。
TARGET_ARCH="${DEB_TARGET_ARCH:-$(dpkg --print-architecture 2>/dev/null || echo amd64)}"
ARCH="$TARGET_ARCH"
# 目标多架构 triple（定位库目录 / libswscale 等）：amd64→x86_64-linux-gnu，loong64→loongarch64-linux-gnu
MULTIARCH="$(dpkg-architecture -a "$TARGET_ARCH" -qDEB_HOST_MULTIARCH 2>/dev/null || echo "${TARGET_ARCH}-linux-gnu")"

# 暂存目录必须留有 >256M 空间：/tmp 在本机仅 10M tmpfs，装不下 38M 可执行 + 31M 共享库，
# 且 CMake 的 file(INSTALL) 在空间不足时不报错退出，只会留下一个截断的暂存树。
STAGE_PARENT="${TMPDIR:-}"
if [ -z "$STAGE_PARENT" ] || ! df -Pk "$STAGE_PARENT" 2>/dev/null \
        | awk 'NR==2 {exit ($4 > 262144) ? 0 : 1}'; then
    STAGE_PARENT="$HOME/.cache"
    mkdir -p "$STAGE_PARENT"
fi
STAGE="$(mktemp -d "$STAGE_PARENT/dm-plugin-deb.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT

echo "[1/6] 暂存安装树 → $STAGE (version=$VERSION arch=$ARCH)"
DESTDIR="$STAGE" cmake --install "$BUILD_DIR" >/dev/null

echo "[2/6] 校验在线歌词功能确实在构建里"
# 装出一个没有在线歌词的空壳是最难排查的失败模式，在打包阶段就拦住。
# libdmusic 走 GNUInstallDirs，落在 lib/x86_64-linux-gnu/ 而非 lib/。
LIB_SUBDIR=""
for cand in "lib/$MULTIARCH" lib/x86_64-linux-gnu lib64 lib; do
    if [ -d "$STAGE/usr/$cand" ] && ls "$STAGE/usr/$cand"/libdmusic.so* >/dev/null 2>&1; then
        LIB_SUBDIR="$cand"
        break
    fi
done
if [ -n "$LIB_SUBDIR" ] && [ -f "$STAGE/usr/$LIB_SUBDIR/libdmusic.so.1.0.0" ]; then
    # 多重兜底：动态符号表(nm -D) 可能不导出内部类符号，再退到全符号表(nm) 与构建库本体，
    # 任一处查到即认定在线歌词已编入（避免对暂存副本 nm 的误报）。
    STAGED_LIB="$STAGE/usr/$LIB_SUBDIR/libdmusic.so.1.0.0"
    BUILD_LIB="$BUILD_DIR/src/libdmusic/libdmusic.so.1.0.0"
    if nm -D --defined-only "$STAGED_LIB" 2>/dev/null | grep -i "LyricDownloader" >/dev/null \
       || nm --defined-only "$STAGED_LIB" 2>/dev/null | grep -i "LyricDownloader" >/dev/null \
       || { [ -f "$BUILD_LIB" ] && nm -D --defined-only "$BUILD_LIB" 2>/dev/null | grep -i "LyricDownloader" >/dev/null; }; then
        echo "    OK: libdmusic.so 含 LyricDownloader 符号（在线歌词已编入）"
    else
        echo "    警告: 未查到 LyricDownloader 符号，该构建可能不含在线歌词功能" >&2
    fi
else
    echo "ERROR: 暂存树中缺少 libdmusic.so.1.0.0（探测了 lib/x86_64-linux-gnu、lib64、lib）" >&2
    exit 1
fi
[ -f "$STAGE/usr/bin/$PKG_NAME" ] || {
    echo "ERROR: 暂存树中缺少 /usr/bin/$PKG_NAME" >&2
    echo "       （产物名由 CMake 的 OUTPUT_NAME 决定，需重新 configure）" >&2
    exit 1; }

# 翻译资源校验：DTK 的 DGuiApplicationHelper::loadTranslator() 按 applicationName
# 找 <appName>_<locale>.qm。本工程 applicationName=deepin-music-online-lyric，
# 但 .qm 基名仍是 deepin-music_<locale>.qm（project() 名保持 deepin-music 未改，
# Qt 工具链按 translations/deepin-music_*.ts 生成）。两者基名不同，故把
# deepin-music_*.qm 复制为 deepin-music-online-lyric_*.qm 供 DTK 查找。
TR_DIR="$STAGE/usr/share/$PKG_NAME/translations"
if [ -d "$TR_DIR" ]; then
    QM_COUNT="$(ls "$TR_DIR"/deepin-music_*.qm 2>/dev/null | wc -l)"
    echo "    翻译: $QM_COUNT 个 deepin-music_*.qm（基名来自 project()）"
    [ "$QM_COUNT" -gt 0 ] || {
        echo "ERROR: 暂存树中缺少翻译文件，界面会退回英文" >&2
        exit 1; }
    # 生成 applicationName 对应的 qm 名：deepin-music_xx.qm → deepin-music-online-lyric_xx.qm
    for qm in "$TR_DIR"/deepin-music_*.qm; do
        [ -e "$qm" ] || continue
        cp -f "$qm" "${TR_DIR}/deepin-music-online-lyric_${qm##*/deepin-music_}"
    done
    echo "    已生成 $(ls "$TR_DIR"/deepin-music-online-lyric_*.qm 2>/dev/null | wc -l) 个 deepin-music-online-lyric_*.qm"
fi

# 应用图标校验：官方 deepin-music.svg 必须存在
ICON_FILE="$STAGE/usr/share/icons/hicolor/scalable/apps/$PKG_NAME.svg"
[ -f "$ICON_FILE" ] || {
    echo "ERROR: 暂存树中缺少应用图标 $ICON_FILE" >&2; exit 1; }
echo "    图标: $PKG_NAME.svg（官方图标）"

echo "[3/6] 解析依赖（dpkg-shlibdeps 等价实现）"
# 商店规范要求 Depends 由 ${shlibs:Depends} 自动推导共享库依赖，而不是手写维护
# （手写必然遗漏，也会随系统升级失效）。本机没有 debhelper/dpkg-shlibdeps，
# 这里按 dpkg-shlibdeps 的等价逻辑自行推导：
#   1) 扫描所有 ELF 文件的 DT_NEEDED
#   2) 过滤掉由本包自身提供、以及 libc/libstdc++/libgcc/libm 等基础库
#   3) 通过 dpkg -S 反查 .so 对应的包名；查不到的（本地自建 sysroot 装的）
#      回退到按命名规则推断，避免依赖列表为空被商店判为非法。
SHLIB_DEPS=""
add_dep() {
    local d="$1"
    [ -n "$d" ] || return
    # ⚠️ 去重必须先把已有列表规范化成「前后都带逗号」的形式再匹配。
    # 原写法 `case ",$SHLIB_DEPS," in *",$d,"*)` 有两个 bug：
    #   1) 首次调用时 SHLIB_DEPS 为空 → 实际是 ",," ，能匹配，正常；
    #   2) 但当列表是 "a, b" 时，",a, b," 里对 "b" 匹配 ",b," 会失败
    #      （b 前面是空格不是逗号）→ b 被重复添加。
    # 结果是 Depends 里同一个包出现多次（实测 libqt6core6 出现 3 次）。
    # dpkg 能容忍，但商店自动质检可能判为格式异常。
    local padded=",${SHLIB_DEPS# },"
    case "$padded" in
        *",$d,"*) return ;;
    esac
    SHLIB_DEPS="${SHLIB_DEPS:+$SHLIB_DEPS, }$d"
}

# 基础库：不需要写 Depends（由 libc6 等基础包保证）
# 注意所有可能返回非零的命令都要显式兜底 —— 脚本开头有 set -e，
# 未处理的非零退出会让整个打包流程静默中止（本机 dpkg 数据库为空，
# `dpkg -S` 对任何 .so 都返回 1，正是踩这个坑）。
is_base_lib() {
    case "$1" in
        libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|\
        libstdc++.so.*|libgcc_s.so.*|libresolv.so.*|libutil.so.*|\
        ld-linux*|libBrokenLocale.so.*) return 0 ;;
        # 本包自带的私有库，不能自依赖
        libdmusic.so*) return 0 ;;
    esac
    return 1
}

# libfoo.so.1.2.3 → libfoo1（Debian 命名惯例）
# ⚠️ 有两类例外，不能机械拼接，否则商店装机时依赖解析失败：
#  1) Qt6 包名里**也带 soversion**：libQt6Core.so.6 → libqt6core6
#     （来源：packages.debian.org 的 libqt6core6/libqt6dbus6/libqt6sql6 等）
#  2) 基础 C 库有独立命名：libz.so.1 → **zlib1g**（不是 libz1）
#     libicu*.so.NN → libicuNN；libcrypto.so.3 → libcrypto3 / libssl3
#     这些逐一核实自 packages.debian.org，不能靠猜。
so_to_pkg() {
    local soname="$1"
    local base="${soname%%.so*}"          # libQt6Core / libz
    local rest="${soname#*.so.}"          # 6.8.0 / 1.2.11 / 74.2
    local major="${rest%%.*}"             # 6 / 1 / 74

    # --- 基础 C 库特例 ---
    case "$base" in
        libz)        echo "zlib1g"; return ;;
        # deepin 25 仓库里没有名为 libcrypto3 的包（已核对 dists/crimson 三个组件
        # 的 Packages 索引）；libcrypto.so.3 与 libssl.so.3 同属 **libssl3** 包。
        # 按 Debian 惯例写 libcrypto3 会导致安装器报「依赖关系不满足」。
        libcrypto)   echo "libssl3"; return ;;
        libssl)      echo "libssl3"; return ;;
        # libicui18n.so.74 → libicu74（ICU 的 so 名带 i18n/n，但包名只取主版本）
        libicui18n)  echo "libicu${major}"; return ;;
        libicu*)     echo "libicu${major}"; return ;;
        libstdc++)   echo "libstdc++6"; return ;;
        libgcc_s)    echo "libgcc-s1"; return ;;
        # deepin 系库的包名**不带 ABI 数字后缀**（已核对 deepin 25 crimson 仓库）：
        #   libdtk6core.so.6 → libdtk6core（不是 libdtk6core6）
        #   libdtk6gui.so.6   → libdtk6gui（不是 libdtk6gui6）
        #   libmpris-qt6.so.1 → libmpris-qt6（不是 libmpris-qt61）
        # 按 Debian 惯例拼出带后缀的名字在 deepin 25 源里不存在，装机必失败。
        libdtk6*)    echo "$base"; return ;;
        libmpris-qt*) echo "$base"; return ;;
    esac

    case "$base" in
        # Qt6：libQt6Core.so.6 → libqt6core6（module 名小写 + soversion）
        libQt6*) echo "libqt6$(echo "${base#libQt6}" | tr 'A-Z' 'a-z')$major" ;;
        # 其余按 libfoo.so.N → libfooN
        *) echo "${base}${major}" ;;
    esac
}

# 注意：这里刻意用管道而非 `while read; do ... done < <(...)`。
# 本机 /bin/sh 是 dash，且部分环境下 /dev/fd 不可用，进程替换会报
# "/dev/fd/63: 没有那个文件或目录" 并连带 awk Broken pipe。
for elf in "$STAGE/usr/bin/$PKG_NAME" "$STAGE/usr/$LIB_SUBDIR"/libdmusic.so.*; do
    [ -f "$elf" ] || continue
    NEEDED_LIST="$(objdump -p "$elf" 2>/dev/null | awk '/NEEDED/ {print $2}')"
    for needed in $NEEDED_LIST; do
        is_base_lib "$needed" && continue || true
        # 本机 dpkg 数据库为空（ostree 管理），dpkg -S 必然查不到 → 返回 1。
        # 必须 `|| true` 兜住，否则 set -e 直接中止打包。
        pkg="$(dpkg -S "$needed" 2>/dev/null | head -1 | cut -d: -f1)" || true
        if [ -z "$pkg" ]; then
            pkg="$(so_to_pkg "$needed")" || true
        fi
        add_dep "$pkg" || true
    done
done
echo "$SHLIB_DEPS" | tr ',' '\n' | sed '/^$/d' | sort -u | sed 's/^/    /'

# libdmusic.so 依赖 FFmpeg（libavcodec/libavformat），但那两 个 .so 只在
# **运行时**由 libdmusic 通过 dlopen/动态链接使用，且部分构建下不体现在
# 主程序的 DT_NEEDED 里 → 上面的自动推导会漏掉。逐个核实后显式补上。
# ⚠️ 这里必须用**空格**分隔，不能用逗号。
# `for p in $FFMPEG_DEPS` 按 IFS（空格/换行）分词，若变量里存的是
# "a, b, c"，迭代出的是 "a," / "b," / "c," —— 每个元素尾部多一个逗号，
# 最终 Depends 里出现空项（",, "），dpkg-deb 直接报
#   Depends 字段中缺少软件包名，或者填写的软件包名无效
# add_dep 内的去重逻辑按 ",$d," 匹配，带逗号也匹配不上，会静默产生重复/空项。
# libswscale 的二进制包名随 FFmpeg 大版本变化（.5=FFmpeg5, .7=FFmpeg6.x, .8=FFmpeg7.x），
# **不能硬编码**：deepin 25 实际装的是 libswscale.so.7，硬编码 libswscale5 导致
# 安装器报「依赖关系不满足： libswscale5」（包名已核实存在于 packages.ubuntu.com/noble）。
# 这里从本机（= 目标系统同源）实际 soname 推导包名，并做存在性守卫。
SWSCALE_MAJOR="$(basename "$(find /usr/lib/x86_64-linux-gnu -maxdepth 1 -name 'libswscale.so.*' 2>/dev/null | sort -V | tail -n1)" 2>/dev/null | sed -n 's/^libswscale\.so\.\([0-9]*\).*/\1/p')"
if [ -z "$SWSCALE_MAJOR" ]; then
    echo "ERROR: 本机找不到 libswscale.so.*，无法推导 ffmpeg swscale 依赖包名" >&2
    exit 1
fi
echo "  [deps] libswscale soname 推导 → libswscale$SWSCALE_MAJOR"
FFMPEG_DEPS="libavcodec60 libavformat60 libavutil58 libswscale$SWSCALE_MAJOR libvlc5 vlc-plugin-base"
for p in $FFMPEG_DEPS; do add_dep "$p" || true; done

# QML 模块不会被 shlibdeps 推导出来，必须显式列出（商店装机后缺一个就白屏）。
# 清单来自 `grep -rhoE "^import ..."` 对 src/music-player/**/*.qml 的实际扫描结果，
# 不要凭记忆手写。依据：官方 dtk-development skill「gotchas 第 10 章 debian 打包」
# 明确指出「QML 运行时包不会被 ${shlibs:Depends} 自动推导」。
# ⚠️ 包名必须是仓库里真实存在的：Qt6 的 QML 模块包名已去掉 Qt5 时代的 "2" 后缀
# （正确：qml6-module-qtquick-templates；qml6-module-qtquick-templates2 不存在，
#  写错会导致安装器报「依赖关系不满足」。已逐一对照 packages.debian.org 核实）。
QML_DEPS="qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-window, qml6-module-qtquick-dialogs, qml6-module-qtquick-shapes, qml6-module-qtqml, qml6-module-qtqml-models, qml6-module-qtquick-templates, qml6-module-qt-labs-platform, qml6-module-qt5compat-graphicaleffects, qml6-module-qtquick-controls2-styles-chameleon, libdtk6declarative"
# gstreamer 解码插件与运行时工具：Qt Multimedia 的后端，装机后缺了播不出声音
RUNTIME_DEPS="gstreamer1.0-libav, gstreamer1.0-plugins-base, gstreamer1.0-plugins-good, gstreamer1.0-plugins-bad, gstreamer1.0-plugins-ugly, gstreamer1.0-fluendo-mp3, gstreamer1.0-pulseaudio, libuchardet0, libsdl1.2debian, gvfs-bin"

if [ -z "$SHLIB_DEPS" ]; then
    echo "ERROR: 未能推导出任何共享库依赖，Depends 会为空（商店判为非法包）" >&2
    exit 1
fi

# Depends 合法性自检：dpkg-deb 对空项/带逗号元素会直接拒包，但它的报错
# 只说"第 N 行附近"，很难定位。这里提前拦下并指出具体是哪个值。
# 同时**统一去重并排序** —— 上游多处add_dep 难免有重叠，
# 直接输出会出现同名包重复（dpkg 能容忍，商店质检可能判异常）。
ALL_DEPS="$(echo "$SHLIB_DEPS, $QML_DEPS, $RUNTIME_DEPS" \
    | tr ',' '\n' | sed '/^[[:space:]]*$/d' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' \
    | sort -u | tr '\n' ',' | sed 's/,$//')"

BAD_DEPS=""
for d in $(echo "$ALL_DEPS" | tr ',' ' '); do
    case "$d" in
        ""|*[!a-z0-9.+-]*) BAD_DEPS="$BAD_DEPS [$d]" ;;
    esac
done
if [ -n "$BAD_DEPS" ]; then
    echo "ERROR: Depends 含非法包名（空项或非法字符）:$BAD_DEPS" >&2
    echo "       常见原因：变量用逗号分隔却按空格迭代（for p in \$VAR）" >&2
    exit 1
fi
DEP_COUNT="$(echo "$ALL_DEPS" | tr ',' '\n' | wc -l)"
echo "    依赖去重后共 $DEP_COUNT 项"

# [3.5] Depends 存在性校验：对照 deepin 25 官方仓库索引，任何在源里查不到的
# 包名都意味着装机时「依赖关系不满足」（安装器一次只报一个，逐个试错代价极高，
# 曾因此连踩 libswscale5 / libdtk6core6 / libmpris-qt61 等多个坑）。
# 下载失败（离线构建）则跳过校验，不阻塞打包。
REPO_CACHE="${TMPDIR:-/tmp}/dm-plugin-repo-index"
check_repo_deps() {
    local repo_base="https://community-packages.deepin.com/beige"
    local comps="main commercial community"
    mkdir -p "$REPO_CACHE" 2>/dev/null || return 0
    local have_index=0
    for c in $comps; do
        local f="$REPO_CACHE/$c.gz"
        # 索引缓存 7 天，过期自动重拉
        if [ ! -f "$f" ] || [ -n "$(find "$f" -mtime +7 2>/dev/null)" ]; then
            curl -sL --max-time 120 -o "$f" "$repo_base/dists/crimson/$c/binary-amd64/Packages.gz" 2>/dev/null || continue
        fi
        [ -s "$f" ] && have_index=1
    done
    [ "$have_index" = "1" ] || { echo "    [repo-check] 索引不可用，跳过校验"; return 0; }
    # ⚠️ 不能直接 `zcat f | grep -q`：脚本开 pipefail，grep -q 命中后提前退出，
    # zcat 收 SIGPIPE（141）→ 命中反而被误判为不存在（已实测踩坑）。
    # 先把包名一次性展开成普通文件，再对文件做完整 grep。
    : > "$REPO_CACHE/all-pkgs.txt"
    for c in $comps; do
        zcat "$REPO_CACHE/$c.gz" 2>/dev/null | grep '^Package: ' | sed 's/^Package: //' >> "$REPO_CACHE/all-pkgs.txt" || true
    done
    local missing=""
    for d in $(echo "$ALL_DEPS" | tr ',' ' '); do
        grep -qxF "$d" "$REPO_CACHE/all-pkgs.txt" || missing="$missing $d"
    done
    if [ -n "$missing" ]; then
        echo "ERROR: 以下 Depends 在 deepin 25 仓库(crimson)中不存在，装机必然报「依赖关系不满足」:$missing" >&2
        echo "       修正 so_to_pkg/显式清单里的包名（用 zgrep '^Package: ' 核对真实名称）" >&2
        exit 1
    fi
    echo "    [repo-check] $DEP_COUNT 项依赖全部存在于 deepin 25 仓库"
}
check_repo_deps

echo "[4/6] 写入 DEBIAN/control"
mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $VERSION
Section: audio
Priority: optional
Architecture: $ARCH
Maintainer: $MAINTAINER
Homepage: $HOMEPAGE
Installed-Size: $(du -ks "$STAGE" | cut -f1)
Depends: $ALL_DEPS
Description: $DISPLAY_NAME
 基于 Kugou / 网易云音乐 / LRCLIB 三大音源的在线歌词音乐播放器。
 支持播放时自动匹配并下载 .lrc 歌词到歌曲所在目录，提供逐字（卡拉OK）歌词，
 内置三源聚合的歌词搜索对话框，可试听并一键应用。
 .
 本包是 deepin-music 的「在线歌词增强版」，以**独立应用**身份发布（包名
 deepin-music-online-lyric、显示名 Deepin音乐-V2），与系统自带 deepin-music
 使用不同的可执行文件名 / MPRIS ID / 单实例键 / dconfig schema，二者可并存不冲突。
EOF

echo "[5/6] 规范化权限（目录 755 / 可执行 755 / 普通文件 644）"
find "$STAGE" -type d -exec chmod 755 {} +
find "$STAGE" -type f -exec chmod 644 {} +
# 恢复可执行位：主程序与共享库
[ -f "$STAGE/usr/bin/$PKG_NAME" ] && chmod 755 "$STAGE/usr/bin/$PKG_NAME"
find "$STAGE/usr/lib" "$STAGE/usr/lib64" -name 'libdmusic.so*' -exec chmod 755 {} + 2>/dev/null || true

echo "[6/6] dpkg-deb --build → $OUT_DIR"
mkdir -p "$OUT_DIR"
DEB_PATH="$OUT_DIR/${PKG_NAME}_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$STAGE" "$DEB_PATH"
echo "完成: $DEB_PATH"
echo
echo "校验："
echo "  desktop: desktop-file-validate 可离线跑（需 desktop-file-utils）"
echo "  Depends: 已由二进制 DT_NEEDED 自动推导 + 显式 QML/编解码依赖"
echo "  布局:   标准 deb（/usr/bin, /usr/lib, /usr/share），deepin 25 磐石系统下"
echo "          由 apt/dpkg 接管安装，无需关闭只读；商店上架走标准 deb 即可。"
echo
echo "本机自用（可选，不走商店）："
echo "  ./tools/install-local.sh <build-dir>    # 装到 ~/.local，免 root"
