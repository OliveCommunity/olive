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

#ifndef OAK_CROPDISTORTNODE_H
#define OAK_CROPDISTORTNODE_H

#include <QVector2D>

#include "node/gizmo/point.h"
#include "node/gizmo/polygon.h"
#include "node/inputdragger.h"
#include "node/node.h"

namespace olive
{

class CropDistortNode : public Node {
	Q_OBJECT
public:
	CropDistortNode();

	NODE_DEFAULT_FUNCTIONS(CropDistortNode)

	virtual QString name() const override
	{
		return tr("Crop");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.olivevideoeditor.Olive.crop");
	}

	virtual QVector<CategoryID> category() const override
	{
		return { k_category_distort };
	}

	virtual QString description() const override
	{
		return tr("Crop the edges of an image.");
	}

	virtual void retranslate() override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;

	virtual void update_gizmo_positions(const NodeValueRow &row,
									  const NodeGlobals &globals) override;

	static const QString k_texture_input;
	static const QString k_left_input;
	static const QString k_top_input;
	static const QString k_right_input;
	static const QString k_bottom_input;
	static const QString k_feather_input;

protected slots:
	virtual void gizmo_drag_move(double delta_x, double delta_y,
							   const Qt::KeyboardModifiers &modifiers) override;

private:
	void create_crop_side_input(const QString &id);

	// Gizmo variables
	PointGizmo *point_gizmo_[k_gizmo_scale_count];
	PolygonGizmo *poly_gizmo_;
	QVector2D temp_resolution_;
};

}

#endif // OAK_CROPDISTORTNODE_H
