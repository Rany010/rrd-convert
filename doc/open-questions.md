# 未决项

实现语言、双中间表示、v1 的输入输出、内置消息、Rerun 版本、发布方式和测试框架已经写入 [product-definition.md](product-definition.md) 与 [architecture.md](architecture.md)。那些条目不再是待选项。

许可证已定为 MIT，见仓库根目录的 `LICENSE`。

实现中已经核对过的事项：

- 默认布局已写入同一个 `.rrd`。布局写失败只警告，Viewer 仍打开数据。
- 转换器已在 Ubuntu 20.04 x86_64（glibc 2.31、gcc 9）上编译并运行。Viewer 0.28.2 的核对是在 Ubuntu 24.04 Docker 镜像里做的，不是宿主机安装包。

仍需确认的事项：

- 发布流程如需新增 CI 配置，改动前先确认。
