# 在线歌词运行时联网验证工具

驱动**生产类** `LyricDownloader`（内部使用真实 `KugouApi` / `NetEaseApi` / `HttpClient`）
走**真实网络**，验证这条链路在服务端可用：

```
Kugou 搜索 → KRC 解密(LyricCrypt) → KRC→标准 LRC(LyricFormat) → 落盘 .lrc
```

## 与单元测试的分工

| | 单元测试 `tests/libdmusic-test` | 本工具 |
|---|---|---|
| 网络 | 全 mock，离线 | **真实联网** |
| 覆盖 | 回退顺序、无歌词 vs 网络错误、保存/缓存回退 | 「服务端是否真的能用」 |
| 复现 | 稳定可重复 | 受网络/服务端影响 |
| 桌面 | 不需要 | 不需要（`QCoreApplication`） |

单测证明**逻辑正确**，本工具证明**真实可用**。改完歌词模块建议两者都跑。

## 构建

默认主构建**不包含**本工具（避免影响打包）。需显式开启：

```bash
cmake -B build -S . -DBUILD_LYRIC_RUNTIME_CHECK=ON ...
cmake --build build --target lyric_runtime_check
```

产物：`build/tools/lyric-runtime-check/lyric_runtime_check`

> 若在只读根 / 自建 sysroot 环境下编译，Qt host 工具与本工具运行都需要
> `LD_LIBRARY_PATH` 指向 sysroot 库目录（见项目 MEMORY.md「环境注意」）。

## 运行

```bash
# 内置样例（中文 + 英文，均为逐字歌词）
./lyric_runtime_check

# 指定歌曲
./lyric_runtime_check 晴天 周杰伦

# 列出内置样例
./lyric_runtime_check --list
```

运行需要**网络**，不需要显示服务。歌词落到 `$TMPDIR/lyric_runtime_out/*.lrc`。

## 退出码

- `0` — 至少一首歌成功取到歌词
- `1` — 全部失败（网络不通 / 服务端协议变更 / 匹配失败）
- `2` — 超过 180s 兜底超时被强制终止

## 读日志要点

- `Kugou device register failed, error_code 20010` 是**非致命**的：设备注册失败后仍以匿名方式
  取到歌词，**不是 bug**，不要误判。
- `netError: true` 且 `LYRICS_FAIL` 才代表网络/服务端真的有问题。
- 逐字歌词的判定：输出里的 `wordByWordLines > 0`，且行内出现多个 `[mm:ss.xx]` 时间戳。
