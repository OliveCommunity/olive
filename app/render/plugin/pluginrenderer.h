/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

*/

#ifndef PLUGINRENDERER_H
#define PLUGINRENDERER_H
#include <QOpenGLExtraFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShader>
#include <QOpenGLVertexArrayObject>
#include <QThread>
#include <QOffscreenSurface>

#include "render/renderer.h"
#include "render/job/pluginjob.h"
#include "render/opengl/openglrenderer.h"
namespace olive
{
namespace plugin{
namespace detail {
// 作用：将字节行跨度转换为像素跨度，便于纹理读写。
// Purpose: Convert byte stride to pixel stride for texture I/O.
int BytesToPixels(int byte_linesize, const olive::VideoParams &params);
}
// 作用：OFX 插件渲染器，负责 CPU/GL 路径下的插件调用和纹理桥接。
// Purpose: OFX plugin renderer that drives CPU/GL render paths and texture bridging.
class PluginRenderer : public olive::OpenGLRenderer{
	Q_OBJECT
public:
	PluginRenderer(QObject *parent=nullptr):OpenGLRenderer(parent){};
	virtual ~PluginRenderer() override{};
	// 作用：将目标纹理绑定为插件输出。
	// Purpose: Attach destination texture as OFX output.
	void AttachOutputTexture(olive::TexturePtr texture);
	// 作用：解除目标纹理绑定。
	// Purpose: Detach destination texture binding.
	void DetachOutputTexture();
	// 作用：执行插件渲染流程（参数配置、输入/输出、调用渲染动作）。
	// Purpose: Execute plugin render flow (params, inputs/outputs, render actions).
	void RenderPlugin(TexturePtr src, olive::plugin::PluginJob& job,
					  olive::TexturePtr destination,
					  olive::VideoParams destination_params,
					  bool clear_destination, bool interactive);

};
}
}



#endif //PLUGINRENDERER_H
