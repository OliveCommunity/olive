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

//! GL 互操作桥（阶段 6a spike 结论；实现未落地）。
//!
//! ## 背景
//!
//! OFX 的 `OpenGLRender` 扩展要求宿主把 clip 纹理以 **GL 纹理名**
//! （`kOfxImageEffectPropOpenGLTextureIndex`）递给插件，插件直接画
//! 进输出纹理。oak 的渲染后端是 wgpu，[`crate::render::texture_id`]
//! 因此是恒 0 的桩：render_driver 的 `use_opengl` 决策据它恒回退
//! CPU 路径（GL 插件经 CPU render action 仍可工作）。本模块记录把
//! 桩替换为真实 GL 互操作的评估。
//!
//! ## 方案 A：wgpu-hal GL 互操作 —— 不可行（macOS）
//!
//! wgpu-hal 的 GL 互操作面（`hal::api::Gles` 的 adapter/texture
//! 互转）只在实例本身就是 GLES 后端时存在。oak 在 macOS 上的 wgpu
//! 实例是 **Metal**（wgpu 支持矩阵中 macOS/iOS 仅 Metal 为一等后
//! 端；OpenGL 需 ANGLE 转译层且仅 GLES 3.0，见
//! <https://github.com/gfx-rs/wgpu> 的 Supported Platforms 表与
//! CHANGELOG #4185"GLES backend optional on macOS"）。Metal 后端与
//! GL 纹理名之间没有共享命名空间，wgpu 也不暴露跨后端纹理导入。
//! 即便为插件强行把整条管线切到 wgpu GLES 后端，也是以全局渲染性
//! 能换单一插件路径，方向错误。**结论：放弃。**
//!
//! ## 方案 B：独立离屏 GL 上下文 + 回读 + wgpu 上传 —— 技术可行，
//! 暂缓
//!
//! 路径：宿主自建原生 GL 上下文（macOS 为 CGL；OpenGL 自 10.14 起
//! 弃用但仍可用），与插件共享纹理命名空间（CGL share group）→ 插件
//! render 画进 FBO 附着纹理 → `glReadPixels`/PBO 回读为 CPU 帧 →
//! 经 [`oakrender::backend::GpuContextLike::upload`] 上传成 wgpu
//! 纹理。接线点已就位：
//!
//! 1. [`crate::render::texture_id`] 返回真实 GL 名（当前恒 0）；
//! 2. [`crate::render_driver::render_frame`] 的 `use_opengl` 分支
//!    （`plugin_supports_opengl && depth_ok && dst_id != 0`）随之
//!    生效，走 [`crate::instance::Instance::render_gl`]；
//! 3. GL suite（[`crate::suites::gl_render`]）的纹理索引属性写出真
//!    值。
//!
//! 暂缓理由：
//! - 需引入新依赖（`glow` + CGL 绑定）与上下文生命周期/线程模型
//!    管理（OFX 插件可在自起线程回调 suite）；
//! - 每帧同步回读是一次 GPU stall，性能上只在"插件本来就是 GL 加
//!    速"时划算，而当前无任何真实 GL OFX 插件可验证（测试 `.gl`
//!    变体只验证 suite 调用面）；
//! - CPU 回退路径完整可用，GL 插件经 render action 正确出帧。
//!
//! ## TODO(phase-GL)
//!
//! 实现方案 B：CGL 上下文工厂 + share group、`texture_id` 真值化、
//! 回读→上传流水线，以及一个真实 GL 插件的端到端验证（golden 帧比
//! 较）。在此之前，[`crate::render::texture_id`] 保持恒 0 桩。
