# 架构与实现约定

状态：交接基线，2026-10-30。代码尚未开始。按本文实现，不要改成按话题名分发，也不要让解码器直接调用某个可视化 SDK。

## 流水线

```text
Format Adapter      存储 → Input Record
    → Decoder       Input Record → Visual Primitive，键是消息类型
    → Sink          Visual Primitive → 一个引擎，键是图元种类
```

跨层只有这两种记录。

Input Record 对每种存储都一样：话题名、消息类型、时间戳、序列化名称、载荷字节。载荷保持序列化后的原样。`.db3` 适配器和以后的自定义适配器都停在这条记录上。

Visual Primitive 与引擎无关：图像、压缩图像、点云、坐标变换、标记。Rerun sink 以及以后的 Foxglove、socket、自定义 sink 都从这里开始。它们不看见 bag 字节，也不看见 ROS 类型。

主循环只做三次查找：

1. Format Adapter 产出 Input Record，用其中的类型字符串选择 Decoder。
2. Decoder 返回零个或多个 Visual Primitive。
3. 本次选中的 Sink 按图元种类接收。

话题名只决定实体路径，不决定走哪段代码。没有对应解码器的类型跳过并计数，不中断转换。

## v1 实现范围

- 一个 Format Adapter：标准 ROS 2 SQLite `.db3`。
- 一个 Sink：Rerun `.rrd`，save 模式。
- 内置 Decoder：
  - `sensor_msgs/msg/Image`
  - `sensor_msgs/msg/CompressedImage`，只接受 JPEG 和 PNG
  - `sensor_msgs/msg/PointCloud2`
  - `tf2_msgs/msg/TFMessage`
  - `visualization_msgs/msg/Marker`
  - `visualization_msgs/msg/MarkerArray`
- Marker 只做这些形状：arrow、cube、sphere、cylinder、line strip、points、text。其它形状跳过并计数。
- 不做 H.264 / H.265，不做厂商原始包。

Foxglove、socket 流和自定义引擎在 v1 只有接口，没有第二个 sink 实现。

## 接口

```text
FormatAdapter
  open(paths) -> status
  topics() -> topic name, type, serialization format
  next() -> Input Record

Input Record
  topic, type name, timestamp, serialization, bytes

Decoder
  type_name() -> string
  decode(Input Record) -> list of Visual Primitive, or skip

Visual Primitive
  kind, entity path, timestamp, payload for that kind

Sink
  name() -> string
  open(output) -> status
  write(Visual Primitive) -> status
  close() -> status

Registry
  add_adapter(format id, adapter)
  add_decoder(type name, decoder)
  add_sink(name, sink)
```

内置项由 `main` 调用的一个注册函数登记。插件再调用一次注册，不修改读取循环。v1 的插件是同一进程里的静态注册。动态库等到确实要在仓库外单独发布插件时再做。

内置图元可以用 `std::variant`。新增一种图元时，同时注册能产出它的 Decoder 和能写出它的 Sink，不把这个 variant 改成所有插件的公共必改点。Sink 忽略自己不认识的图元种类。

## 读包

使用系统的 `libsqlite3`，读取 ROS 2 bag 的 `topics` 和 `messages` 表。CDR 封装和上述内置消息的字段在本仓库内解码。

不链接 `rclcpp`、`rosbag2_cpp`，也不链接由发行版生成的消息包。

时间：消息有 `std_msgs/Header.stamp` 时用它，否则用 bag 记录时间。Rerun 时间线名称是 `capture_time`。

实体路径默认等于话题名。v1 不要求配置文件。配置文件只作为以后覆盖实体路径的可选项。

打开 bag 或打开输出失败时用异常，带上路径和失败原因。单条消息失败时返回状态，记下类型和话题，然后继续。

## 构建

- C++17，CMake，可执行文件名 `rrd-convert`
- 子命令：`rrd-convert convert <input.db3> -o <output.rrd>`
- Rerun C++ SDK：本机已有则 `find_package`，否则用 CMake `FetchContent` 固定到一个发行版本
- `libsqlite3` 用系统库
- 命名和格式按 Google C++ Style：类型 `CamelCase`，函数和变量 `snake_case`，私有成员尾部下划线，常量 `kCamelCase`，缩进 2 空格
- 注释、日志和错误信息用英文

建议目录，实现时可以微调文件名，不要打乱三层边界：

```text
CMakeLists.txt
src/main.cpp
src/format/          Format Adapter 与 ROS 2 .db3 读取
src/record/          Input Record 与 Visual Primitive
src/decode/          Decoder 与注册表
src/sink/            Sink 接口与 Rerun save
src/app/             convert 主循环
```

## 明确不做

- 转换前必须提供话题清单
- 按话题名选择解码器
- 在主循环里写一串 `can_handle`
- 视频转码、厂商雷达 SDK、投影叠加、仪表盘
- v1 内建第二个 sink
- 把原始 bag 字节旁路写到另一个文件
- v1 做 `inspect`、话题过滤或时间裁剪
- `ament`、已 source 的 ROS 工作区、私有消息包

## 曾比较过的做法

| 做法 | 不采用的原因 |
|------|----------------|
| 链接 `rosbag2_cpp` 和生成的 `sensor_msgs` | 解码最短，但核心路径会依赖某个 ROS 2 发行版 |
| 按话题名注册解码器，并在主循环里手写分支 | 每加一种消息都要改主循环 |
| 从第一天起所有图元都用 `std::any` | 插件可以不改 variant，但内置集合是闭合的，测试和阅读成本更高。内置用 variant，新种类用成对注册 |

## 实现顺序

按这个顺序做，做完一步再进入下一步。

1. CMake 与空的 `convert` 命令，能解析输入路径和 `-o`。
2. `.db3` Format Adapter，把每条消息读成 Input Record。未知表结构要报出文件路径。
3. 注册表和主循环。没有解码器时跳过并打印计数。
4. 六个内置 Decoder，产出 Visual Primitive。先做 Image、PointCloud2、TFMessage，再做压缩图像和 Marker。
5. Rerun Sink，save 出一个能用 `rerun` 打开的 `.rrd`。
6. 合成 `.db3` 夹具，以及一条检查实体路径的测试。夹具只含上述内置类型。
7. 一个不改主循环的静态注册示例，作为最小插件文档的代码。
8. 最小 blueprint：图像、3D、基础实体。这一步失败不回退前面的转换路径。

许可证、Foxglove、socket、MCAP 输入都不在这个顺序里。
