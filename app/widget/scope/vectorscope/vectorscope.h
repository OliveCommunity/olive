/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2026 mikesolar

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

#ifndef OAK_VECTORSCOPESCOPE_H
#define OAK_VECTORSCOPESCOPE_H

#include "widget/scope/scopebase/scopebase.h"

namespace olive
{

class VectorscopeScope : public ScopeBase {
	Q_OBJECT
public:
	VectorscopeScope(QWidget *parent = nullptr);

	MANAGEDDISPLAYWIDGET_DEFAULT_DESTRUCTOR(VectorscopeScope)

protected:
	virtual ScopeShaderCode generate_shader_code() override;

	virtual void draw_scope(void *managed_tex, void *pipeline) override;

	virtual void draw_scope_software(QPainter &p, const QImage &image) override;
};

}

#endif // OAK_VECTORSCOPESCOPE_H
