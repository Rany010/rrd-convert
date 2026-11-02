# 未决项

实现语言、双中间表示、v1 的输入输出、内置消息、Rerun 版本、发布方式和测试框架已经写入 [product-definition.md](product-definition.md) 与 [architecture.md](architecture.md)。那些条目不再是待选项。

仍未决定的只有许可证。第一次公开发布前选定即可，不挡住 `convert` 的实现。

实现中需要先验证、再定做法的事项：

- Rerun 0.28.2 的 C++ SDK 能否写默认布局（blueprint）。不能则保留 Viewer 的自动布局。
- 该版本 SDK 与 Viewer 在 Ubuntu 20.04（glibc 2.31、gcc 9）上的构建和运行。
- 发布流程如需新增 CI 配置，改动前先确认。
