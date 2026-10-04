### Deepin Music with Online Lyrics (deepin-music-online-lyric)

A deepin Music build with **online lyrics**. It queries **Kugou / NetEase / LRCLIB**,
automatically downloads a matching `.lrc` next to the song on playback, supports
**word-by-word (karaoke) lyrics**, and ships a manual lyrics-search dialog
(three sources aggregated, with preview and one-click apply).

It is published as a **standalone app** (package `deepin-music-online-lyric`,
display name "Deepin音乐-V2"). It uses a different binary name / MPRIS ID /
single-instance key / dconfig schema from the system deepin-music, so the two
coexist without conflict — only with online lyrics added.

#### Option 1: build a .deb (for app store submission / distribution)

On deepin 25 (immutable/"磐石"), deb packages are handled by apt/dpkg and
**install into `/usr` normally — no need to disable read-only mode**. The
deepin app store (lastore) accepts standard debs:

```bash
# 1. Build (Release)
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make -C build-release -j8

# 2. Package (auto-derives Depends, validates output)
./tools/package-deb-manual.sh build-release dist
# output: dist/deepin-music-online-lyric_<version>_amd64.deb
```

Install with: `sudo apt install ./dist/deepin-music-online-lyric_7.0.68_amd64.deb`

The script already handles app-store requirements: all mandatory control
fields, lowercase package name, dependencies auto-derived from the binary's
`DT_NEEDED` (equivalent to `${shlibs:Depends}`), explicit QML runtime
packages. Translations match `applicationName` automatically (no symlinks needed).

#### Option 2: user-level install (local use, no root)

```bash
./tools/install-local.sh build-release
~/.local/bin/deepin-music-online-lyric
```

After installing you can also find "Deepin音乐-V2" in the application menu.
Uninstall: `./tools/install-local.sh build-release --uninstall`

> A `.deb` can still be produced for distribution via
> `./tools/package-deb-manual.sh build-release dist`, but on immutable target
> machines the user-level install above remains the reliable path.

### Dependencies

### Build dependencies

_The **master** branch is current development branch, build dependencies may changes without update README.md, refer to `./debian/control` for a working build depends list_

* cmake (>= 3.10)
* pkg-config
* libavutil-dev
* libavcodec-dev
* libavformat-dev
* libdtk6core-bin
* libdtk6gui-dev
* libicu-dev
* libmpris-qt6-dev
* libtag1-dev
* libxtst-dev
* libvlc-dev
* libvlccore-dev
* libsdl2-dev
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

## Installation

### Build from source code

1. Make sure you have installed all dependencies.

_Package name may be different between distros, if deepin-music is available from your distro, check the packaging script delivered from your distro is a better idea._

Assume you are using [Deepin](https://distrowatch.com/table.php?distribution=deepin) or other debian-based distro which got deepin-music delivered:

``` shell
$ apt build-dep deepin-music
```

2. Build:

```
$ cd deepin-music
$ mkdir Build
$ cd Build
$ cmake ..
$ make
```

3. Install:

```
$ sudo make install
```

The executable binary file could be found at `/usr/bin/deepin-music`

## Usage

Execute `deepin-music`

## Getting help

 - [Official Forum](https://bbs.deepin.org/)
 - [Developer Center](https://github.com/linuxdeepin/developer-center)
 - [Gitter](https://gitter.im/orgs/linuxdeepin/rooms)
 - [IRC Channel](https://webchat.freenode.net/?channels=deepin)
 - [Wiki](https://wiki.deepin.org/)

## Getting involved

We encourage you to report issues and contribute changes

 - [Contribution guide for developers](https://github.com/linuxdeepin/developer-center/wiki/Contribution-Guidelines-for-Developers-en) (English)
 - [开发者代码贡献指南](https://github.com/linuxdeepin/developer-center/wiki/Contribution-Guidelines-for-Developers) (中文)

## License

deepin-music is licensed under [GPL-3.0-or-later](LICENSE).
