# M11：oakplugin Rust 重写（含自研 OFX Host）

> 前置：M9（oakplugin 拆分）已完成，模块边界已是纯 C ABI
> （`include/plugin/*.h`，引用计数句柄）。本计划把 oakplugin 的
> **整个实现**换成 Rust：自研 OFX 宿主取代 openfx HostSupport
> （third_party/openfx/HostSupport，vendored 1.3 万行 C++），六个
> C++ 类（4485 行）语义级移植；pluginrenderer（oakrender 内，
> 1857 行）在第 2 期收编。
>
> 这是项目全量 Rust 化的滩头模块：第一张 Rust crate 模板
> （构建、测试、FFI 纪律、泄漏断言）在此确立。
>
> 本文档覆盖已立项的 0-2 期；OpenCL（3 期）与 UI 类 suite
> （Interact/Dialog/Draw，4 期）届时评审再立项。

## 0. 形态决策（2026-08，已定）

- **单一 Rust crate 收编整个模块**，不自起独立 host 库。crate
  内部 host 核心与实例/参数/纹理桥之间是普通 Rust 模块调用
  （safe，编译器查所有权），只有两条 FFI 边界：
  1. 对 OFX 插件：fetchSuite/action，规范定义的 C ABI；
  2. 对 oak 其余模块：`include/plugin/*.h`，M9 已冻结。
- 上游零感知：oaknode 的 PluginNode、facade、oakrender 都只认
  C ABI，不关心实现语言。
- HostSupport 删除后，liboaknode/liboakrender 对 liboakplugin 的
  C++ 符号引用（PluginCache、Property::Set 等 30+ 个）归零。
- unsafe 收敛在两层：suite trampoline（插件调入）与 C ABI 导出
  （oak 调入）。其余全部 safe Rust。
- 每个 FFI 入口必须 `catch_unwind` 兜底并映射为负错误码（01 §0），
  构建保持 `panic = "unwind"`。

## 1. 保留与消耗的资产

- `include/plugin/{error,host,instance}.h`：公共契约，不变。
- 六个 C++ 类的语义（olivehost/oliveplugininstance/oliveclip/
  paraminstance/image/pluginprogressreporter）：移植，不重新设计。
- pluginrenderer.cpp 的渲染流程语义：2 期收编时逐段对照。
- HostSupport：仅作行为参照系，不进构建。

## 2. 第 0 期：Golden master 与测试基建（约 1 周）

不改实现，产出验收门槛。

### 2.1 插件内省 C ABI（include/plugin/instance.h 新增，约 15 函数）

参数枚举（名称/OFX 类型/标签/hint/父组/坐标系/secret/
display min-max/choice labels-values-order/按类型默认值）、clip
枚举（名称/标签/可选性/分量/位深）、描述符（标识/label/描述/
上下文集）。PluginNode 同期改走该 C ABI，node→plugin C++ 引用
归零。行为必须逐行不变。

### 2.2 描述符快照

系统三 bundle（CImg/Misc/Shadertoy）全量 dump 成 JSON 入库
（tests/ofx/snapshots/）：标识、上下文、参数矩阵（默认值与
choice 排序结果）、clip 描述、**getClipPreferences 协商后结果**
（分量/位深/像素比——隐式行为主要来源）。后续每期 diff 必须为空。

### 2.3 渲染 golden master

代表性效果集（CImg invert/blur、一个 2D 变换、一个 Shadertoy、
一个 generator）：固定输入帧，CPU 与 GL 两路径，全链路
**F32 + ACEScg**，存 EXR + SHA256。CPU 路径要求 bit 级一致，
GL 允许 1e-4 容差；GL 用例仅本机，CI 跳过。

### 2.4 最小测试插件

~300 行 C 测试插件（filter 上下文，覆盖常用参数类型与双 clip），
供 suite round-trip 单测与 1 期首测。

## 3. 第 1 期：Rust crate 落地（约 4-5 周）

### 3.1 范围

- host 核心：Property/Memory/ImageEffect v1/Parameter（12 种类型）/
  Message v1v2/Progress v1v2/TimeLine v1/MultiThread v1 suite；
  filter/generator/transition 上下文；describe/describeInContext/
  createInstance/destroyInstance/getClipPreferences/getRoD/getRoI/
  isIdentity/render（CPU）/begin-endSequenceRender 动作。
- 六类移植：host 单例（扫描/缓存）、instance 包装、clip↔纹理桥
  （经 oakrender C ABI）、param↔node 桥（经 oaknode/oakundo C
  ABI）、image 包装、progress reporter。
- 语义重灾区（逐条对照 HostSupport 并注释）：clip 分量/位深/
  像素比协商顺序；RoD/RoI；isIdentity 短路；field 透传。

### 3.2 crate 结构（src/plugin/rust/）

声明底稿已入库，见该目录。模块划分：ffi（C ABI 导出）、handle
（句柄脚手架）、host（扫描/缓存/suite 注册）、descriptor、
instance、clip、image、param、property、suites/*（八张函数表）、
bridge/{node,render,undo}（oak C ABI 导入）。

### 3.3 构建

crate 出 staticlib；CMake 经 corrosion（或自定义 target）链成
liboakplugin；standalone 树接线不变；CI 加 rust toolchain（版本
pin 在 rust-toolchain.toml）。

### 3.4 明确不做

GL 路径、ofxColour（均 2 期）；OpenCL（3 期评审）；
Interact/Dialog/Draw（4 期）；Parametric（按需）。公共 C ABI 不变。

### 3.5 验收

- 0 期 golden master 全绿（描述符 diff 空、CPU 渲染 bit 一致）。
- 测试插件 round-trip + CImg 全量插件 describe/协商冒烟。
- 生命周期：createInstance/destroyInstance 配对、重复 scan、
  render 中途 cancel；alive 计数无泄漏。
- nm：liboaknode/liboakrender→liboakplugin C++ 符号为 0；
  HostSupport 移出构建。
- cargo test（host 内部单测）+ ctest（C ABI 层）双绿。

## 4. 第 2 期：GL 路径 + ofxColour + pluginrenderer 收编（约 2-3 周）

- OpenGLRender suite v1（clipLoadTexture/clipFreeTexture/
  flushResources）与像素深度协商；纹理桥沿用 M9 的
  wrap_native/readback 机制。
- ofxColour：属性族 + GetOutputColourspace action；ACEScg 工作
  空间经 OCIO config 属性告知插件。
- pluginrenderer.cpp 语义收编进 crate（render driver 模块），
  oakrender 的 PluginJob 退化为一次 C ABI 调用；oakrender 彻底
  不碰 OFX。
- 验收：GL golden master 本机绿；ofxColour 属性 round-trip 单测 +
  支持插件端到端用例；F32+ACEScg 链路保持绿。

## 5. 依赖与顺序

- 开始前：③ 剩余机械部分（render→node、→common、transition
  清理）收尾。
- 2.1 完成后 ③ 的 node→plugin 依赖清零；render→plugin 的 12 个
  符号随 1 期 HostSupport 删除消失。
- 每期结束独立评审再进下一期。

## 6. 风险

- **语义边角**：HostSupport 隐式行为无法全覆盖；缓解：快照含
  协商后结果 + CImg 全量协商冒烟。
- **FFI 纪律**：panic 越界/回调线程模型是新坑；缓解：catch_unwind
  全入口覆盖、共享状态全部 Mutex、回调线程注册表。
- **GL 本机依赖**：GL 验收需人工本机确认。
- **首个 Rust 模块**：构建模板（corrosion、双测试层、alive 计数）
  要为后续模块立好，宁慢勿滥。
