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

#ifndef OAK_HISTOGRAMSCOPE_H
#define OAK_HISTOGRAMSCOPE_H

#include "widget/scope/scopebase/scopebase.h"

namespace olive
{

class HistogramScope : public ScopeBase {
	Q_OBJECT
public:
	HistogramScope(QWidget *parent = nullptr);

	MANAGEDDISPLAYWIDGET_DEFAULT_DESTRUCTOR(HistogramScope)

protected slots:
	virtual void on_init() override;

	virtual void on_destroy() override;

protected:
	virtual ScopeShaderCode generate_shader_code() override;

	virtual void draw_scope(void *managed_tex, void *pipeline) override;

	virtual void draw_scope_software(QPainter &p, const QImage &image) override;

private:
	void *pipeline_secondary_;
	void *texture_row_sums_;
};

}

#endif // OAK_HISTOGRAMSCOPE_H
