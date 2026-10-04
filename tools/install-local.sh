#!/usr/bin/env bash
# 用户级安装 deepin-music 在线歌词增强版（无需 root，不触碰只读的 /usr）
#
# 背景：本机是 **deepin 25 磐石系统**，`/usr` 是只读 overlay。
# 本脚本用于**本机自用**：装到 ~/.local，完全不碰系统目录。
#
# 注意（曾经踩过的坑）：早前认为「dpkg 在磐石系统上不可靠、装到 /usr 的文件会被
# 还原删除」，这个结论**是错的** —— 磐石由 apt/dpkg 接管 deb 安装，可正常装到 /usr，
# 上架应用商店也走标准 deb。本脚本保留的价值是：
#   1) 免 root，不用改系统目录；
#   2) 便于多版本共存、随时卸载；
#   3) 在未激活磐石的环境（Immutable mode:false）下开发调试更方便。
# 上架 / 分发请用 tools/package-deb-manual.sh 生成标准 deb。
#
# 目录布局（lib 与 bin 同级，用 $ORIGIN RPATH 让动态链接器就地找到）：
#   ~/.local/bin/deepin-music-online-lyric
#   ~/.local/lib/libdmusic.so.1.0.0 (+ 两个 soname 链接)
#   ~/.local/share/applications/deepin-music-online-lyric.desktop
#   ~/.local/share/icons/hicolor/scalable/apps/deepin-music-online-lyric.svg
#   ~/.local/share/deepin-music-online-lyric/translations/*.qm
#   ~/.local/share/dsg/configs/org.deepin.music.online.lyric/   （dconfig schema）
#   ~/.local/share/deepin-manual/...                        （帮助文档）
#
# 用法：
#   ./install-local.sh <build-dir>        # build-dir 需是已完成 Release 配置的构建目录
#   ./install-local.sh <build-dir> --uninstall
#
# 环境要求：构建产物必须已包含在线歌词功能（libdmusic.so 里应有 LyricDownloader 符号）。

set -euo pipefail

APP_ID="deepin-music-online-lyric"
BIN_NAME="$APP_ID"

BUILD_DIR="${1:-}"
[ -n "$BUILD_DIR" ] || { echo "用法: $0 <build-dir> [--uninstall]" >&2; exit 1; }
BUILD_DIR="$(cd "$BUILD_DIR" 2>/dev/null && pwd)" || {
    echo "ERROR: 构建目录不存在: $1" >&2; exit 1; }

PREFIX="${HOME}/.local"
BIN_DIR="$PREFIX/bin"
LIB_DIR="$PREFIX/lib"
SHARE_DIR="$PREFIX/share"

# ---------------------------------------------------------------- 卸载模式
if [ "${2:-}" = "--uninstall" ]; then
    echo "[卸载] 清理 $PREFIX 下的插件文件"
    # 卸载时的兜底 wrapper 也要清掉：上一轮若缺 patchelf，会把真二进制改名为
    # <name>.real 并生成同名 shell wrapper。只删 wrapper 会把 .real 留下来。
    rm -f  "$BIN_DIR/$BIN_NAME" \
           "$BIN_DIR/$BIN_NAME.real" \
           "$LIB_DIR"/libdmusic.so \
           "$LIB_DIR"/libdmusic.so.1.0 \
           "$LIB_DIR"/libdmusic.so.1.0.0
    rm -f  "$SHARE_DIR/applications/$APP_ID.desktop"
    rm -f  "$SHARE_DIR/icons/hicolor/scalable/apps/$APP_ID.svg"
    # 翻译目录里含大量 .qm 与按 locale 建的软链，rm -rf 一次可能被安全删除策略
    # 拦下（尤其软链与文件混合时）。逐项删除并在最后清理空目录。
    if [ -d "$SHARE_DIR/$APP_ID/translations" ]; then
        find "$SHARE_DIR/$APP_ID/translations" -maxdepth 1 -type l -delete 2>/dev/null || true
        find "$SHARE_DIR/$APP_ID/translations" -maxdepth 1 -type f -name '*.qm' -delete 2>/dev/null || true
        rmdir "$SHARE_DIR/$APP_ID/translations" 2>/dev/null || true
    fi
    rmdir "$SHARE_DIR/$APP_ID" 2>/dev/null || true
    rmdir "$SHARE_DIR/dsg/configs/$APP_ID" 2>/dev/null || true
    # 帮助文档：只删本插件装进去的那一层
    rmdir "$SHARE_DIR/deepin-manual/manual-assets/application/deepin-music-online-lyric/music/zh_CN/fig" 2>/dev/null || true
    rmdir "$SHARE_DIR/deepin-manual/manual-assets/application/deepin-music-online-lyric/music/zh_CN" 2>/dev/null || true
    rmdir "$SHARE_DIR/deepin-manual/manual-assets/application/deepin-music-online-lyric/music/en_US/fig" 2>/dev/null || true
    rmdir "$SHARE_DIR/deepin-manual/manual-assets/application/deepin-music-online-lyric/music/en_US" 2>/dev/null || true
    rmdir "$SHARE_DIR/deepin-manual/manual-assets/application/deepin-music-online-lyric/music/common" 2>/dev/null || true
    rmdir "$SHARE_DIR/deepin-manual/manual-assets/application/deepin-music-online-lyric/music" 2>/dev/null || true
    rmdir "$SHARE_DIR/deepin-manual/manual-assets/application/deepin-music-online-lyric" 2>/dev/null || true
    rmdir "$SHARE_DIR/deepin-manual/manual-assets/application" 2>/dev/null || true
    rmdir "$SHARE_DIR/icons/hicolor/scalable/apps" 2>/dev/null || true
    rmdir "$SHARE_DIR/icons/hicolor/scalable" 2>/dev/null || true
    rmdir "$SHARE_DIR/icons/hicolor" 2>/dev/null || true

    # 残留自检：把没清掉的东西明确列出来，而不是让用户以为卸载干净了
    LEFT=""
    [ -e "$BIN_DIR/$BIN_NAME" ] || [ -e "$BIN_DIR/$BIN_NAME.real" ] && LEFT="$LEFT $BIN_DIR/$BIN_NAME*"
    ls "$LIB_DIR"/libdmusic.so* >/dev/null 2>&1 && LEFT="$LEFT $LIB_DIR/libdmusic.so*"
    [ -e "$SHARE_DIR/applications/$APP_ID.desktop" ] && LEFT="$LEFT desktop"
    [ -d "$SHARE_DIR/$APP_ID" ] && LEFT="$LEFT $SHARE_DIR/$APP_ID"
    if [ -n "$LEFT" ]; then
        echo "[卸载] 完成，但以下残留需手动清理：$LEFT"
    else
        echo "[卸载] 完成（用户配置与歌词缓存在 ~/.cache/deepin/$APP_ID 与音乐目录，未删除）"
    fi
    exit 0
fi

# ------------------------------------------------------------ 安装前置检查
[ -f "$BUILD_DIR/CMakeCache.txt" ] || {
    echo "ERROR: $BUILD_DIR 不是 cmake 构建目录（缺 CMakeCache.txt）" >&2; exit 1; }

# 暂存目录必须放在有足够空间的地方。本机 /tmp 仅 10M tmpfs，而暂存树要放下
# 38M 可执行文件 + 31M 共享库，直接 mktemp 会 "No space left on device"
# （且 CMake 的 file(INSTALL) 在此处**不报错退出**，容易误判成构建问题）。
# 优先用 TMPDIR，其次退到项目同盘的临时目录。
STAGE_PARENT="${TMPDIR:-}"
if [ -z "$STAGE_PARENT" ] || ! df -Pk "$STAGE_PARENT" 2>/dev/null \
        | awk 'NR==2 {exit ($4 > 262144) ? 0 : 1}'; then
    STAGE_PARENT="$HOME/.cache"
    mkdir -p "$STAGE_PARENT"
fi
if ! df -Pk "$STAGE_PARENT" 2>/dev/null | awk 'NR==2 {exit ($4 > 262144) ? 0 : 1}'; then
    echo "ERROR: 找不到可用空间 >256M 的临时目录（TMPDIR=$STAGE_PARENT）" >&2
    echo "       请设置 TMPDIR 指向有空间的目录后重试。" >&2
    exit 1
fi
STAGE="$(mktemp -d "$STAGE_PARENT/dm-plugin-install.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
echo "[1/5] 从 $BUILD_DIR 暂存安装树到 $STAGE"
DESTDIR="$STAGE" cmake --install "$BUILD_DIR" >/dev/null

# cmake 的 install 规则已带 OUTPUT_NAME=deepin-music-online-lyric，落地文件名即 $BIN_NAME。
# 若用的是旧的产物名为 deepin-music 的构建目录，这里兼容之。
[ -f "$STAGE/usr/bin/$BIN_NAME" ] || {
    if [ -f "$STAGE/usr/bin/deepin-music" ]; then
        mv "$STAGE/usr/bin/deepin-music" "$STAGE/usr/bin/$BIN_NAME"
        echo "    （旧构建目录：已把可执行名改为 $BIN_NAME）"
    else
        echo "ERROR: 暂存树中找不到可执行文件，期望 $STAGE/usr/bin/$BIN_NAME" >&2
        echo "       请先重新构建（cmake 的 OUTPUT_NAME 改动需要重新 configure）。" >&2
        exit 1
    fi
}

# 共享库：libdmusic 走 GNUInstallDirs → lib/x86_64-linux-gnu/，
# 而非 lib/。这里两处都探测，避免因发行版 multilib 布局差异而漏掉。
LIB_SUBDIR=""
for cand in lib/x86_64-linux-gnu lib64 lib; do
    if [ -d "$STAGE/usr/$cand" ] && ls "$STAGE/usr/$cand"/libdmusic.so* >/dev/null 2>&1; then
        LIB_SUBDIR="$cand"
        break
    fi
done

echo "[2/5] 校验在线歌词功能确实在构建里"
# 缺 libdmusic.so 就无法工作；这里额外确认核心符号存在，避免装出一个空壳。
if [ -n "$LIB_SUBDIR" ] && [ -f "$STAGE/usr/$LIB_SUBDIR/libdmusic.so.1.0.0" ]; then
    if nm -D --defined-only "$STAGE/usr/$LIB_SUBDIR/libdmusic.so.1.0.0" 2>/dev/null \
        | grep -q "LyricDownloader"; then
        echo "    OK: libdmusic.so 含 LyricDownloader 符号（在线歌词已编入）"
    else
        echo "    警告: 未查到 LyricDownloader 符号，该构建可能不含在线歌词功能" >&2
    fi
else
    echo "ERROR: 暂存树中缺少 libdmusic.so.1.0.0（探测了 lib/x86_64-linux-gnu、lib64、lib）" >&2
    exit 1
fi

echo "[3/5] 安装到 $PREFIX（不碰 /usr，无需 root）"
mkdir -p "$BIN_DIR" "$LIB_DIR" "$SHARE_DIR/applications" \
         "$SHARE_DIR/icons/hicolor/scalable/apps"

install -m 755 "$STAGE/usr/bin/$BIN_NAME" "$BIN_DIR/$BIN_NAME"
# 库与 soname 链接
install -m 755 "$STAGE/usr/$LIB_SUBDIR"/libdmusic.so.* "$LIB_DIR/"
( cd "$LIB_DIR" && ln -sf libdmusic.so.1.0.0 libdmusic.so.1.0 \
                  && ln -sf libdmusic.so.1.0   libdmusic.so )

# desktop / 图标：安装到用户级目录后需放开可执行位才能被菜单识别
install -m 644 "$STAGE/usr/share/applications/$APP_ID.desktop" \
                "$SHARE_DIR/applications/$APP_ID.desktop"
install -m 644 "$STAGE/usr/share/icons/hicolor/scalable/apps/$APP_ID.svg" \
                "$SHARE_DIR/icons/hicolor/scalable/apps/$APP_ID.svg"

# 翻译与文档
# DTK 按 applicationName=deepin-music-online-lyric 找 <appName>_<locale>.qm，
# 但 .qm 基名仍是 deepin-music_<locale>.qm（project() 名未改）。拷贝后额外生成
# deepin-music-online-lyric_*.qm 软匹配名，确保界面中文等翻译能正常加载。
if [ -d "$STAGE/usr/share/$APP_ID/translations" ]; then
    mkdir -p "$SHARE_DIR/$APP_ID/translations"
    cp "$STAGE/usr/share/$APP_ID/translations/"*.qm "$SHARE_DIR/$APP_ID/translations/" 2>/dev/null || true
    for qm in "$SHARE_DIR/$APP_ID/translations/"deepin-music_*.qm; do
        [ -e "$qm" ] || continue
        cp -f "$qm" "${SHARE_DIR/$APP_ID/translations/deepin-music-online-lyric_${qm##*/deepin-music_}}"
    done
    echo "    翻译: $(ls "$SHARE_DIR/$APP_ID/translations/"*.qm 2>/dev/null | wc -l) 个 .qm"
fi
[ -d "$STAGE/usr/share/dsg/configs/$APP_ID" ] && \
    mkdir -p "$SHARE_DIR/dsg/configs" && \
    cp -r "$STAGE/usr/share/dsg/configs/$APP_ID" "$SHARE_DIR/dsg/configs/" || true
[ -d "$STAGE/usr/share/deepin-manual" ] && \
    cp -r "$STAGE/usr/share/deepin-manual" "$SHARE_DIR/" || true

echo "[4/5] 修补 RPATH：让可执行文件用 \$ORIGIN 找到同级的 libdmusic.so"
# 不可变系统上无法写 /etc/ld.so.conf.d，故用 $ORIGIN 相对 RPATH。
# 已有 RPATH 先剥掉再追加，避免系统残留的 RUNPATH 抢先解析。
RP_NEW='$ORIGIN/../lib'
if command -v patchelf >/dev/null 2>&1; then
    patchelf --remove-rpath "$BIN_DIR/$BIN_NAME" 2>/dev/null || true
    patchelf --set-rpath "$RP_NEW" "$BIN_DIR/$BIN_NAME"
    echo "    OK: patchelf 已设置 RPATH=$RP_NEW"
else
    echo "    警告: 未安装 patchelf，改用 wrapper 启动脚本兜底" >&2
    # 无 patchelf 时的兜底：生成一个 shell wrapper，显式设置 LD_LIBRARY_PATH。
    mv "$BIN_DIR/$BIN_NAME" "$BIN_DIR/$BIN_NAME.real"
    cat > "$BIN_DIR/$BIN_NAME" <<WRAP
#!/usr/bin/env bash
# 由 install-local.sh 生成的兜底启动脚本（因为缺少 patchelf 无法写 RPATH）
export LD_LIBRARY_PATH="$LIB_DIR\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
exec "$BIN_DIR/$BIN_NAME.real" "\$@"
WRAP
    chmod 755 "$BIN_DIR/$BIN_NAME"
    echo "    OK: 已生成 wrapper $BIN_DIR/$BIN_NAME"
fi

echo "[5/5] 注册 desktop 文件并做冒烟验证"
command -v update-desktop-database >/dev/null 2>&1 && \
    update-desktop-database "$SHARE_DIR/applications" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && \
    gtk-update-icon-cache -f -t "$SHARE_DIR/icons/hicolor" 2>/dev/null || true

# 冒烟：确认动态链接完整 + 离屏能起来
echo "--- 冒烟检查 ---"
if ldd "$BIN_DIR/$BIN_NAME" 2>/dev/null | grep -q "not found"; then
    echo "    警告: 存在未解析的依赖库："
    ldd "$BIN_DIR/$BIN_NAME" | grep "not found" | sed 's/^/      /'
else
    echo "    OK: 动态依赖全部可解析"
fi

if [ -x "$BIN_DIR/$BIN_NAME.real" ] || [ -x "$BIN_DIR/$BIN_NAME" ]; then
    RUNTIME_DIR="$(mktemp -d "$STAGE_PARENT/dm-plugin-xdg.XXXXXX")"
    chmod 700 "$RUNTIME_DIR"
    LOG="$STAGE_PARENT/dm-plugin-smoke.log"
    QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR="$RUNTIME_DIR" \
        timeout 6 "$BIN_DIR/$BIN_NAME" >"$LOG" 2>&1
    rc=$?
    # 124 = 被timeout 杀掉 = 存活到超时即视为启动成功（无崩溃）
    if [ "$rc" = "124" ] || [ "$rc" = "0" ]; then
        echo "    OK: 离屏启动成功（exit $rc）"
    else
        echo "    警告: 离屏启动异常退出（exit $rc），详见 $LOG"
    fi
    # 翻译加载验证：QM 找不到时 DTK 会刷一条 warning，界面会退回英文。
    if grep -q "can not find qm files" "$LOG" 2>/dev/null; then
        echo "    警告: 翻译 .qm 未被找到，界面将显示英文（检查 translations 软链）"
    else
        echo "    OK: 翻译 .qm 加载正常"
    fi
    # 在线歌词相关日志抽样，便于确认功能模块已加载
    if grep -qiE "lyric" "$LOG" 2>/dev/null; then
        echo "    OK: 日志中出现歌词模块相关记录"
    fi
    rm -rf "$RUNTIME_DIR"
fi

echo
echo "==================================================================="
echo "安装完成: $BIN_DIR/$BIN_NAME"
echo
echo "启动方式："
echo "  1) 菜单里搜索「Deepin音乐-V2」"
echo "  2) 或直接在终端运行: $BIN_DIR/$BIN_NAME"
echo
echo "若菜单里没出现，退出登录后重新登录一次即可刷新。"
echo "卸载：$0 $BUILD_DIR --uninstall"
echo "==================================================================="