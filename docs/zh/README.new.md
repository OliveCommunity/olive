# Oak 视频编辑器

[![CI](https://github.com/OakVideoEditorCommunity/oak/actions/workflows/ci.yml/badge.svg)](https://github.com/OakVideoEditorCommunity/oak/actions/workflows/ci.yml)
[English](../README.new.md)

Oak 视频编辑器是面向 Windows、macOS 和 Linux 的**自由开源非线性视频编辑器**。

本项目是 Olive 视频编辑器的社区维护分支。

> **注意：Oak 目前处于 alpha 阶段，稳定性有限。欢迎试用并反馈，但请自行承担使用风险。**

<!-- 截图：主编辑界面（时间线 + 监视器） -->
![主界面截图](../images/screenshot-main.png)

## 功能特性

- 响应式时间线剪辑，配合智能磁盘/回放缓存
- 节点式合成与特效，内置 OpenFX（OFX）插件宿主
- 完整的色彩管理（OpenColorIO）：支持 `.cube`/`.3dl` LUT，可配置 display/view/look 变换
- 示波器：波形图、矢量图、直方图，以及音频表（LUFS/VU）
- 贝塞尔关键帧动画与曲线编辑器
- Multicam 多机位剪辑与基于波形的音频自动对齐
- 代理媒体工作流，流畅剪辑 4K/8K 素材
- 硬件加速与批量导出（H.264/H.265、图像序列、音频）
- 工程崩溃恢复与自动保存

<!-- 截图：节点编辑器 -->
![节点编辑器截图](../images/screenshot-node.png)

## 下载

Windows、macOS、Linux 预编译包见 [Releases](https://github.com/OakVideoEditorCommunity/oak/releases) 页面。

最新版本：[v0.4.2-alpha](https://github.com/OakVideoEditorCommunity/oak/releases/tag/v0.4.2-alpha)

## 架构

Oak 拆分为若干可独立测试的组件，组件之间以**纯 C ABI** 为边界：

| 组件 | 形态 | 作用 |
|---|---|---|
| `liboakcore` | 动态库 | 无 Qt 依赖的核心类型（有理数、时间码、贝塞尔、采样缓冲、音视频参数），纯 C ABI |
| `liboakengine` | 动态库 | 剪辑引擎（节点图、时间线、渲染、编解码、任务系统），仅通过 `oakengine_*` C ABI facade 暴露 |
| `oak-editor` | 应用程序 | Qt 图形界面，**只**经 C ABI 访问引擎 |
| `oak-render-worker` | 进程 | 无头渲染进程，在 GUI 线程之外渲染帧（NDJSON IPC） |
| `oak-cli` | 工具 | 引擎的命令行前端：媒体信息、探测、渲染、转码，无需 GUI |

这条 C ABI 边界让引擎可以被嵌入，也是后续将引擎按模块逐步用 Rust 重写的基础（见 [`riir.md`](plans/completed/riir.md)）。

<!-- 架构图：组件与 ABI 布局 -->
![架构图](../images/architecture.png)

## 命令行工具

`oak-cli` 是一个独立的、纯 C ABI 的引擎消费者：

```bash
oak-cli info <文件>                    # 媒体信息
oak-cli probe <文件>                   # 流/解码器探测
oak-cli render <project.ove> <输出>    # 渲染工程指定范围
oak-cli transcode <输入> <输出>        # 媒体转码
```

## 从源码构建

完整说明见 [`build.md`](build.md)（Windows/MSYS2、Linux Debian/Ubuntu/Fedora/Arch）和 [`build_macos.md`](build_macos.md)（macOS）。简要步骤：

```bash
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## 路线图

| 版本 | 主题 | 核心交付物 |
|:--|:--|:--|
| **0.3** | **插件架构** | OpenFX 宿主支持完整可用——"任意 OFX 插件加载不崩溃" |
| **0.4** | **调色、音频与性能** | `.cube`/`.3dl`、示波器、三向色轮、波形自动同步、BWF 时间码同步、音频表、代理媒体、硬件加速导出、批量渲染队列 |
| **0.5** | **动画、跟踪与协作** | 贝塞尔关键帧曲线编辑器、点跟踪、画面稳定器、完整 Multicam、OpenTimelineIO、EDL/XML 导入导出 |
| **0.6** | **稳定性** | 工程文件格式冻结（向后兼容）、崩溃恢复、自动保存、内存优化 |
| **1.0** | **生产就绪** | 文档完整、安装包、已知问题清单、社区支持渠道 |

## 参与贡献

欢迎贡献。请先阅读 [`../CONTRIBUTING.md`](../CONTRIBUTING.md)，其中约定：

- 代码风格（命名规则，含**结构体 typedef 使用帕斯卡命名法**），
- 所有测试必须使用 **Google Test** 编写，
- 面向引擎代码的 C ABI 边界契约。

更多项目文档：[中文文档目录](./)、[`facade-migration-roadmap.md`](plans/completed/facade-migration-roadmap.md)、[`riir.md`](plans/completed/riir.md)、[`gtest-migration-guide.md`](plans/completed/gtest-migration-guide.md)。

## 许可证

Oak 视频编辑器是采用 [GNU 通用公共许可证第 3 版](../LICENSE) 授权的自由软件。
