### Deepin音乐-V2 在线歌词增强版（deepin-music-online-lyric）

deepin Music 的在线歌词增强版。接入 **酷狗 / 网易云 / LRCLIB** 三大歌词源，
播放时自动匹配并下载 `.lrc` 到歌曲所在目录，支持**逐字（Karaoke）歌词**，
并提供手动搜索歌词对话框（三源聚合 + 试听 + 一键应用）。

本工程以**独立应用**身份发布（包名 `deepin-music-online-lyric`、显示名 Deepin音乐-V2），
与系统自带的 deepin-music 使用不同的可执行文件名 / MPRIS ID / 单实例键 / dconfig schema，
二者可并存、互不冲突，只是额外自带在线歌词功能。

#### 安装方式一：生成 deb（上架应用商店 / 分发）

deepin 25 磐石系统由 apt/dpkg 接管 deb 安装，**可以正常安装到 `/usr`，无需关闭只读**，
因此上架 deepin 应用商店（lastore）走的就是标准 deb：

```bash
# 1. 构建（Release）
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make -C build-release -j8

# 2. 打包（自动推导 Depends、校验产物）
./tools/package-deb-manual.sh build-release dist
# 产物：dist/deepin-music-online-lyric_<版本>_amd64.deb
```

安装：`sudo apt install ./dist/deepin-music-online-lyric_7.0.68_amd64.deb`

打包脚本已按商店要求处理：control 必填字段齐全、包名全小写、
依赖由二进制 `DT_NEEDED` 自动推导（等价 `${shlibs:Depends}`）并显式补齐
QML 运行时包（翻译随 applicationName 天然匹配，无需软链）。

#### 安装方式二：用户级安装（本机自用，免root）

不想动系统目录、或需要多版本共存时，用用户级安装脚本：

```bash
./tools/install-local.sh build-release
~/.local/bin/deepin-music-online-lyric
```

安装完成后也可在应用菜单里搜索「Deepin音乐-V2」。
卸载：`./tools/install-local.sh build-release --uninstall`

#### 功能使用

1. **自动下载**：播放歌曲时若同目录没有同名 `.lrc`，会自动联网匹配并保存到该目录；
   若歌曲目录不可写，会退回缓存目录 `~/.cache/deepin/deepin-music-online-lyric/lyrics/`。
2. **手动搜索**：打开歌词窗口，点击右上角 **Search Lyrics**，
   在弹出的对话框中搜索关键词，切换 Kugou / NetEase / LRCLIB 结果并试听，
   点 **Apply** 直接写入当前歌曲目录。

#### 依赖

### 构建依赖

_The **master** branch is current development branch, build dependencies may changes without update README.md, refer to `./debian/control` for a working build depends list_

* cmake (>= 3.10)
* pkg-config
* libavutil-dev
* libavcodec-dev
* libavformat-dev
* libdtk6core-bin
* libdtk6gui-dev
* libicu-dev
* libssl-dev
* zlib1g-dev
* libmpris-qt6-dev
* libtag1-dev
* libxtst-dev
* libvlc-dev
* libvlccore-dev
* libsdl2-dev
* libsdl1.2debian
* Qt6 with modules:
  - qt6-svg-dev
  - qt6-multimedia-dev
  - qt6-tools-dev
  - qt6-tools-dev-tools
  - qt6-declarative-dev
  - qt6-5compat-dev
  - libqt6sql6-sqlite
  - qml6-module-qtquick-dialogs
* Deepin-tool-kit (>= 6.0) with modules:
  - libdtk6declarative-dev
  - libdtk6gui-dev
  - libdtk6core-bin

### Runtime dependencies

* libvlc5
* vlc-plugin-base
* libdtk6declarative
* qml6-module-qt-labs-platform
* libqt6sql6-sqlite
* gstreamer1.0-fluendo-mp3
* gstreamer1.0-libav
* gstreamer1.0-plugins-base
* gstreamer1.0-plugins-good
* gstreamer1.0-plugins-bad
* gstreamer1.0-plugins-ugly
* gstreamer1.0-pulseaudio
* gvfs-bin
* libuchardet0
* libmpris-qt6
* libsdl1.2debian

## 帮助

 - [Official Forum](https://bbs.deepin.org/)
 - [Developer Center](https://github.com/linuxdeepin/developer-center)
 - [Gitter](https://gitter.im/orgs/linuxdeepin/rooms)
 - [IRC Channel](https://webchat.freenode.net/?channels=deepin)
 - [Wiki](https://wiki.deepin.org/)

## 参与贡献

We encourage you to report issues and contribute changes

 - [Contribution guide for developers](https://github.com/linuxdeepin/developer-center/wiki/Contribution-Guidelines-for-Developers-en) (English)
 - [开发者代码贡献指南](https://github.com/linuxdeepin/developer-center/wiki/Contribution-Guidelines-for-Developers) (中文)

## 协议

deepin-music-online-lyric 根据 [GPL-3.0-or-later]（许可证）获得许可.
