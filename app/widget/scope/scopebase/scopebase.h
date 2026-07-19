/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#ifndef OAK_SCOPEBASE_H
#define OAK_SCOPEBASE_H

#include "codec/frame.h"
#include "render/colorprocessor.h"
#include "widget/manageddisplay/manageddisplay.h"

namespace olive
{

class ScopeBase : public ManagedDisplayWidget {
public:
	ScopeBase(QWidget *parent = nullptr);

	MANAGEDDISPLAYWIDGET_DEFAULT_DESTRUCTOR(ScopeBase)

public slots:
	void set_buffer(TexturePtr frame);

protected slots:
	virtual void on_init() override;

	virtual void on_paint() override;

	virtual void on_destroy() override;

protected:
	virtual void showEvent(QShowEvent *e) override;

	virtual ShaderCode generate_shader_code() = 0;

	/**
   * @brief GPU-accelerated draw function used on OpenGL backends.
   *
   * Override this if your sub-class scope needs extra drawing.
   */
	virtual void draw_scope(TexturePtr managed_tex, QVariant pipeline);

	/**
   * @brief Software draw function used on backend-neutral paths (e.g. Vulkan).
   *
   * Implementations receive an 8-bit sRGB/display-ready image and should draw
   * the scope visualization with QPainter.
   */
	virtual void draw_scope_software(QPainter &p, const QImage &image) = 0;

private:
	void update_software_image();

	QVariant pipeline_;

	TexturePtr texture_;

	TexturePtr managed_tex_;

	bool managed_tex_up_to_date_;

	TexturePtr software_tex_;
	QByteArray software_buffer_;
	QImage software_image_;
	bool software_image_up_to_date_;

	TexturePtr local_texture_;
};

}

#endif // OAK_SCOPEBASE_H
