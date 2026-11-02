# rrd-convert · 产品定义

记录日期：2026-10-30。更新：2026-11-02。

副标题：go from any bag to Rerun — 多格式适配进 `.rrd`：快、简单、可扩展，开箱就能看见效果。

## 一句话定位

rrd-convert 给智能驾驶和机器人工具链做离线转换。一条命令把存量记录变成可打开的可视化结果。输入格式可插拔，v1 先支持标准 ROS 2 bag（SQLite 存储）。可视化引擎也可插拔，v1 先写 Rerun `.rrd`。标准消息开箱可用，自定义消息和自定义引擎都通过注册接入。

## 差异化

Rerun Viewer 已能直接打开 MCAP，并自动映射常见的 ROS 2 标准消息；`.db3` 也可以先用 `mcap convert` 转成 MCAP，不需要安装 ROS。所以「不装 ROS 把 `.db3` 转成 `.rrd`」本身不是卖点。rrd-convert 的价值在四件事上：

1. Marker：`visualization_msgs` 不在 Rerun 的自动映射范围内。
2. 自研或厂商消息的语义映射：按消息定义通用解析，插件只写「字段到图元」的映射，结果是点、线、框、文字，而不只是字段表。
3. 双中间表示：同一批解码结果可以换到其它可视化引擎。
4. CI 批量转换：有明确的退出码、可解析的汇总和不会留下半截文件的输出。

标准传感器消息做到「能正确显示」即可，主要工作量放在以上四项。

## 产品原则

1. 快而简单，多格式兼容
   - 默认路径：安装预编译包 → 一条命令 → `rerun out.rrd`
   - 输入走 Format Adapter。v1 实现 ROS 2 SQLite 存储，接口预留 `.bag`、MCAP 和其它存储
2. 两端都能二次开发
   - 前面用输入中间表示收齐各种存储
   - 后面用可视化中间表示接各种引擎
   - 解码按消息类型注册，不在主循环里按话题名分支
   - 自研或厂商消息、Foxglove、socket 流、自定义格式都以插件或独立包接入
   - 文档带最小插件示例
3. 容易快速看到效果
   - 样例 bag 使用仓库内生成的合成 bag
   - 内置常见 `sensor_msgs`、`tf2_msgs`、`visualization_msgs`
   - 成功标准：新人 10 分钟内在 Rerun 里看到画面
   - 坐标系和根坐标系在第一条转换路径里就正确处理；默认布局放在最后，不挡住第一条路径

## 目标用户与场景

| 项 | 范围 |
|----|------|
| 主用户 | 智驾 / 机器人工具链、评测、调试；有存量 bag，想快速进 Rerun |
| 核心场景 | 离线转换 ROS 2 bag，查看传感器数据、Marker 和自定义消息的语义结果 |
| 扩展场景 | 插件接入自研或厂商消息；CI 批量转换；日后更换存储或可视化引擎 |
| 非场景 | 完整业务可视化套件、仿真器、回灌评测引擎、实车上回放控制 |

## 用户旅程

1. 安装。从 GitHub Releases 下载预编译包（v1 先提供 Ubuntu 20.04 x86_64）。Viewer 用 `pip install rerun-sdk==0.28.2`。
2. 转换。`rrd-convert convert <bag 目录或 .db3> -o run.rrd`，需要时加 `--topics` 过滤。
3. 打开。`rerun run.rrd`。默认可见图像、点云和坐标系。
4. 扩展。安装导出的 CMake 包，写自己的可执行文件，注册 Decoder、Format Adapter 或 Sink，仍用同一套命令。

一次命令选择一个 sink。v1 的 sink 是 Rerun 的 save 模式。

## 架构

```text
多种输入存储
  → Format Adapter
  → 输入中间表示       话题、类型、记录时间、序列化、原始字节
  → Decoder            按消息类型注册
  → 可视化中间表示     图像、视频帧、点、线、形状、文字、坐标变换、清除……
  → Sink               按引擎注册
  → .rrd / Foxglove / socket 流 / 自定义格式
```

- 输入中间表示是格式适配器的唯一出口。解码器不关心数据来自哪一种存储。
- 可视化中间表示是解码器的唯一出口。它按几何和数据种类定义，不含 ROS 类型，也不含某个引擎的概念（例如实体路径）。Marker 由解码器拆成这些图元。
- v1 只实现 ROS 2 SQLite 适配器和 Rerun `.rrd` sink。其它适配器和 sink 只留注册接口。

代码框架、执行流程、接口和实现顺序见 [architecture.md](architecture.md)。

## 扩展模型

1. 新输入格式：实现 Format Adapter，产出输入中间表示并注册。
2. 新消息：实现 Decoder，按类型名注册。自定义消息可直接用通用 CDR 读取得到的字段，只写字段到图元的映射。
3. 新可视化引擎：实现 Sink，只消费可视化图元。可以是 Rerun `.rrd`、Foxglove 格式、socket 流或自定义格式。
4. 新图元种类：通过扩展图元携带，不修改内置图元集合。
5. 深度业务可视化：独立插件或私有包，不进入本仓库的默认依赖。

## v1 范围

- 语言：C++17，CMake，一个可执行文件 `rrd-convert`
- 输入：单个 `.db3`，或含 `metadata.yaml` 和多个分片的 bag 目录。压缩的 bag 明确报错
- 输出：Rerun 0.28.x / 0.29.x Viewer 可打开的 `.rrd`（save）
- CLI：`convert`，含话题过滤、根坐标系、`.msg` 目录、JSON 汇总
- 内置消息：图像、压缩图像（含 H.264 / H.265 视频帧）、压缩视频、点云、TF、Marker、MarkerArray，清单见架构文档
- 通用 CDR：按 bag 内嵌消息定义或用户提供的 `.msg` 解析自定义消息
- 扩展点：Format Adapter、Decoder、Sink 三套静态注册；导出 CMake 包
- CI 友好：退出码、可解析汇总、先写临时文件再改名
- 发布：GitHub Releases 预编译包，先覆盖 Ubuntu 20.04 x86_64
- 测试：GoogleTest；合成样例 bag，不收录私有录包
- 文档：5 分钟上手 + 最小插件示例。上手文档随第一条可用转换路径一起写

`inspect`、时间裁剪和一次转换多个 bag 的批处理命令放到 v1.1。

## Viewer 端前提

- Viewer 版本：0.28.x 或 0.29.x。更旧的 Viewer 打不开这个版本写出的 `.rrd`。
- 视频播放：Viewer 需要 FFmpeg 5.1 或更高版本；码流不能含 B 帧。Ubuntu 20.04 软件源里的 FFmpeg 是 4.2，需要另装。

## 非目标

v1 明确不做：

- 覆盖全部自研或厂商私有消息
- 厂商雷达 SDK、视频转码、相机投影叠加、业务仪表
- 读取压缩的 bag
- 仿真器、回灌评测引擎、实车上的回放控制
- 替代官方 MCAP 工具链，或实时 ROS 2 bridge
- 绑定某个 ROS 2 发行版才能运行核心路径
- 同一次运行里同时写多个引擎
- 把原始 bag 字节另存成文件。以后的 Foxglove sink 写的是可视化图元，和原样拷贝字节不是同一件事

## 与现有方案怎么比

| 方案 | 关系 | rrd-convert 差异 |
|------|------|------------------|
| `mcap convert` 后用 Viewer 打开 MCAP | 官方路径，不需要 ROS，标准消息覆盖好 | 显示 Marker；自定义消息映射成几何而不只是字段；可换引擎；面向 CI 的退出码和汇总 |
| rosbag2_storage_rrd | 官方实验录制插件 | 转换已经录好的包，不绑发行版的录制栈 |
| bag2rrd | 形态接近，偏 ROS 1 | 输入和可视化引擎都可替换；v1 先做 ROS 2 和 Rerun |

## 成功标准

| 标准 | 对应 |
|------|------|
| 新人用样例 bag，10 分钟内在 Rerun 看到可视化 | 原则 3 |
| 标准 sensor 类型和 Marker 无需写代码即可转换 | 开箱 |
| 新消息类型只写字段映射并注册 Decoder 后完成一轮转换，不改主循环 | 可扩展 |
| 新引擎按文档注册 Sink 后消费同一批图元，不改解码器 | 可扩展 |
| CI 能凭退出码和 JSON 汇总判断转换结果 | CI |
| README 首页只讲一条命令，不要求先学 MCAP | 简单 |

## 路线图

| 阶段 | 内容 |
|------|------|
| v1 | ROS 2 SQLite 适配器（含分片和话题过滤）、内置消息、通用 CDR、Rerun save sink、静态注册与导出 CMake 包、合成样例、Ubuntu 20.04 预编译包 |
| v1.1 | `inspect`、时间裁剪、批处理命令；可选 `connect` |
| v2 | ROS 1 `.bag` 和 MCAP 输入适配器；Foxglove 或其它 sink；压缩 bag |
| 之后 | 更多适配器、图元模板；评测或回灌流水线的可选钩子 |

## 已定

- 名称 `rrd-convert`，副标题 go from any bag to Rerun
- 仓库 https://github.com/Rany010/rrd-convert
- C++17，核心路径不链接 ROS 2 发行版
- 两条中间表示，三套注册，一次命令一个 sink
- v1 的实现组合是 ROS 2 SQLite 存储加 Rerun `.rrd`
- Rerun C++ SDK 固定 0.28.2
- 视频帧原样写入，不转码
- 测试用 GoogleTest
- 实现开始后用真实录包本地调试

## 未决

许可证在第一次公开发布前确定。不挡住开发。
