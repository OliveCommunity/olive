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

#ifndef OAK_POLYGONGENERATOR_H
#define OAK_POLYGONGENERATOR_H

#include <QPainterPath>

#include "node/generator/shape/generatorwithmerge.h"
#include "node/gizmo/line.h"
#include "node/gizmo/path.h"
#include "node/gizmo/point.h"
#include "node/node.h"
#include "node/inputdragger.h"

namespace olive
{

class PolygonGenerator : public GeneratorWithMerge {
	Q_OBJECT
public:
	PolygonGenerator();

	NODE_DEFAULT_FUNCTIONS(PolygonGenerator)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	virtual void retranslate() override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual void generate_frame(FramePtr frame,
							   const GenerateJob &job) const override;

	virtual void update_gizmo_positions(const NodeValueRow &row,
									  const NodeGlobals &globals) override;

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;

	static const QString k_points_input;
	static const QString k_color_input;

protected:
	ShaderJob get_generate_job(const NodeValueRow &value,
							 const VideoParams &params) const;

protected slots:
	virtual void gizmo_drag_move(double x, double y,
							   const Qt::KeyboardModifiers &modifiers) override;

private:
	static void add_point_to_path(QPainterPath *path, const Bezier &before,
							   const Bezier &after);

	static QPainterPath generate_path(const NodeValueArray &points, int size);

	template <typename T>
	void validate_gizmo_vector_size(QVector<T *> &vec, int new_sz);

	template <typename T> NodeGizmo *create_appropriate_gizmo();

	PathGizmo *poly_gizmo_;
	QVector<PointGizmo *> gizmo_position_handles_;
	QVector<PointGizmo *> gizmo_bezier_handles_;
	QVector<LineGizmo *> gizmo_bezier_lines_;
};

}

#endif // OAK_POLYGONGENERATOR_H
