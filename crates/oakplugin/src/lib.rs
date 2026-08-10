// Oak Video Editor - Non-Linear Video Editor
// Copyright (C) 2026 Oak Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

//! # oakplugin — Oak 的插件模块（自研 OFX 宿主）
//!
//! 本 crate 是 oakplugin 模块的全部实现（M11）：
//!
//! - **OFX 宿主**：扫描 bundle、加载插件、实现八张 suite
//!   （property/memory/image_effect/param/message/progress/timeline/
//!   multithread），驱动 describe/createInstance/render 等 action。
//! - **桥**：把 OFX 实例的参数接到 oaknode（[`bridge::node`]）、把
//!   clip 的输入输出接到 oakrender 纹理（[`bridge::render`]）、把
//!   参数修改包成 undo 命令（[`bridge::undo`]）。
//! - **C ABI 出口**（[`ffi`]）：逐字实现 `include/plugin/*.h`。
//!
//! ## FFI 纪律（全 crate 最高优先级约定）
//!
//! 1. 每个 `extern "C"` 导出函数体必须包
//!    [`handle::guard`]/[`handle::guard_ptr`]（catch_unwind + 错误码
//!    映射）。panic 越过 FFI 边界是 release 阻断级缺陷。
//! 2. 句柄一律 [`handle::RefBox`]；`ctx` 是不透明指针，含义只在本
//!    crate 内解释。
//! 3. 插件可在任意自起线程回调 suite（multithread suite 存活期
//!    内）；一切共享状态走 `Mutex`，句柄注册表见 [`handle::Registry`]。
//! 4. OFX 语义以 openfx HostSupport 为参照系；协商与时序实现点必须
//!    注释对应 HostSupport 文件与行号（格式：`// HS: ofxhImageEffect.cpp:2776`）。
//!
//! ## 第 1 期范围
//!
//! filter/generator/transition 上下文；CPU 渲染路径。GL 纹理 suite
//! 与 ofxColour 为第 2 期（[`clip`]/[`image`] 内以 `// [P2]` 标记
//! 预留点）。

#![deny(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

pub mod bridge;
pub mod clip;
pub mod descriptor;
pub mod error;
pub mod ffi;
pub mod handle;
pub mod host;
pub mod image;
pub mod instance;
pub mod param;
pub mod progress;
pub mod property;
pub mod render_driver;
pub mod suites;
