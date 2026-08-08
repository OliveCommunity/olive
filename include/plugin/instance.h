/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef OAK_EDITOR_PLUGIN_INSTANCE_H
#define OAK_EDITOR_PLUGIN_INSTANCE_H

#include <stdint.h>

#include "node/node.h"
#include "plugin/error.h"
#include "render/renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reference-counted handle to an OFX plugin instance
 *        (olive::plugin::OlivePluginInstance).
 *
 * Ownership/count semantics follow include/common/handle.h: create
 * returns count 1, addref/release adjust it, release destroys at zero.
 */
typedef struct OakPluginInstance {
	void *ctx;
	void (*addref)(void *ctx);
	void (*release)(void *ctx);
	uint32_t abi_version; /**< OAKPLUGIN_ABI_VERSION. */
} OakPluginInstance;

/**
 * @brief Create an instance of a discovered plugin (filter context).
 *        Returns an empty handle (ctx == NULL) for unknown ids/failure.
 */
OakPluginInstance oakplugin_instance_create(const char *plugin_id);

/** @brief Release one reference. NULL/empty no-op; clears ctx. */
void oakplugin_instance_free(OakPluginInstance *instance);

/**
 * @brief Set/get a parameter as an oaknode_value POD (type rules from
 *        node/node.h). String-typed params use
 *        oakplugin_instance_set_param_string()/get_param_string().
 */
int oakplugin_instance_set_param(OakPluginInstance instance,
								 const char *param_id,
								 const oaknode_value *value);
int oakplugin_instance_get_param(OakPluginInstance instance,
								 const char *param_id, oaknode_value *out);
int oakplugin_instance_set_param_string(OakPluginInstance instance,
										const char *param_id,
										const char *value);
int oakplugin_instance_get_param_string(OakPluginInstance instance,
										const char *param_id, char *buf,
										int buf_size);

/**
 * @brief Render one frame through the instance (renderAction).
 *
 * `src` may be an empty handle for generator plugins. Textures stay
 * owned by the caller (borrowed for the call).
 */
int oakplugin_instance_render(OakPluginInstance instance,
							  OakRenderTexture dst, OakRenderTexture src,
							  double time_seconds);

/**
 * @brief Progress callback for long renders (async return channel,
 *        01 §4 exception). Return non-zero to abort processing.
 */
typedef int (*oakplugin_progress_fn)(double progress, void *userdata);
int oakplugin_instance_set_progress_cb(OakPluginInstance instance,
									   oakplugin_progress_fn fn,
									   void *userdata);

/** @brief Cancel any in-progress render/progress reporting. */
int oakplugin_instance_cancel(OakPluginInstance instance);

/** @brief Alive-count for leak assertions in tests. */
int oakplugin_debug_alive_count(void);

/*
 * M11 §4（GL 路径 + render 驱动收编）新增声明。既有签名不变。
 *
 * oakrender 的 PluginJob 经本组入口把整帧渲染流程（RoI/RoD、
 * 多输入收集、isIdentity 短路、参数覆盖、CPU/GL 渲染与输出装配）
 * 委托给 oakplugin 的 render 驱动（Rust 侧 render_driver 模块，
 * 语义对照 src/render/src/plugin/pluginrenderer.cpp）。
 */

/** @brief 一帧渲染任务的参数覆盖条目（参数名 → oaknode_value POD；
 *         字符串参数走 oakplugin_instance_set_param_string）。 */
typedef struct oakplugin_job_value {
    const char *key;
    oaknode_value value;
} oakplugin_job_value;

/** @brief 一帧渲染任务的输入 clip 纹理条目。纹理为借用句柄
 *         （job 内有效）。 */
typedef struct oakplugin_job_texture {
    const char *clip;
    OakRenderTexture texture;
} oakplugin_job_texture;

/**
 * @brief beginSequenceRender 括号。oakrender 对同一实例的一批帧先
 *        begin 后 end，中间逐帧 oakplugin_instance_render_job
 *        （OFX：render action 由 begin/end sequence render 括号包围）。
 *        `interactive` 为信息性标记（Phase 2 不传入 action）。
 */
int oakplugin_instance_render_begin_sequence(OakPluginInstance instance,
                                             double start_time,
                                             double end_time,
                                             int interactive);

/** @brief endSequenceRender 括号（与 render_begin_sequence 配对）。 */
int oakplugin_instance_render_end_sequence(OakPluginInstance instance,
                                           double start_time,
                                           double end_time,
                                           int interactive);

/**
 * @brief 一帧渲染的单一 C ABI 调用（PluginJob 的载体）。
 *
 * @param dst  目标纹理（oakrender 创建）。GL 模式下调用方须先把
 *        dst 附着为渲染器输出目标并保持 GL 上下文 current
 *        （OFX "OpenGL Current Context" 规则；等价 C++
 *        PluginRenderer::attach_output_texture）。
 * @param src  主输入纹理（effect_input_id / SimpleSource；可空句柄）。
 * @param effect_input_id  job.src 落点的 clip 名（可 NULL）。
 * @param inputs / input_count  其余输入 clip 的纹理表。
 * @param values / value_count  参数覆盖表。
 * @param renderer  GL 渲染器（空句柄 → CPU 路径）。
 * @param clear_destination / interactive  信息性标记（Phase 2，
 *        render 驱动暂不处理；上层渲染器负责目标清空）。
 */
int oakplugin_instance_render_job(OakPluginInstance instance,
                                  OakRenderTexture dst,
                                  double time_seconds,
                                  int clear_destination,
                                  int interactive,
                                  const char *effect_input_id,
                                  OakRenderTexture src,
                                  const oakplugin_job_texture *inputs,
                                  int input_count,
                                  const oakplugin_job_value *values,
                                  int value_count,
                                  OakRenderRenderer renderer);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_PLUGIN_INSTANCE_H
