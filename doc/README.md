# 交接文档

前九阶段已落地：CMake 包与 `examples/custom_decoder`、`core`、命令行、plain CDR、`.db3` 适配器、转换主循环、Rerun sink、Image、CompressedImage、CompressedVideo、PointCloud2、TFMessage、Marker、MarkerArray，以及通用 CDR。标准 ROS 2 bag 可以转换。自定义消息用 bag 内的 `ros2msg` 或 `--msg-path` 读取字段，仍要由解码器映射成图元。8 字节 CDR 字段从封装头之后的第一个字节对齐。不要另起一套流水线。

| 文档 | 用途 |
|------|------|
| [product-definition.md](product-definition.md) | 产品范围、非目标、成功标准和路线图 |
| [architecture.md](architecture.md) | 代码框架、执行流程、接口、v1 消息和实现顺序 |
| [open-questions.md](open-questions.md) | 未决项和实现中需要先验证的事项 |

公开简介在仓库根目录的 `README.md`。两边若有出入，以 `doc/` 为准。

许可证在第一次公开发布前再定。这不挡住第一条 `convert` 路径。
