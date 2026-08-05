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

#include "geometry.h"
#include "generator/shape/generatorwithmerge.h"
#include "gizmo/line.h"
#include "gizmo/path.h"
#include "gizmo/point.h"
#include "node.h"
#include "inputdragger.h"

namespace olive
{

class PolygonGenerator : public GeneratorWithMerge {
public:
	PolygonGenerator();

	NODE_DEFAULT_FUNCTIONS(PolygonGenerator)

	virtual std::string name() const override;
	virtual std::string id() const override;
	virtual std::vector<CategoryID> category() const override;
	virtual std::string description() const override;

	virtual void retranslate() override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual void generate_frame(FramePtr frame,
							   const GenerateJob &job) const override;

	virtual void update_gizmo_positions(const NodeValueRow &row,
									  const NodeGlobals &globals) override;

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;

	static const std::string k_points_input;
	static const std::string k_color_input;

protected:
	ShaderJob get_generate_job(const NodeValueRow &value,
							 const VideoParams &params) const;

	virtual void gizmo_drag_move(double x, double y, int modifiers) override;

private:
	static void add_point_to_path(PainterPath *path, const Bezier &before,
							   const Bezier &after);

	static PainterPath generate_path(const NodeValueArray &points, int size);

	template <typename T>
	void validate_gizmo_vector_size(std::vector<T *> &vec, int new_sz);

	template <typename T> NodeGizmo *create_appropriate_gizmo();

	PathGizmo *poly_gizmo_;
	std::vector<PointGizmo *> gizmo_position_handles_;
	std::vector<PointGizmo *> gizmo_bezier_handles_;
	std::vector<LineGizmo *> gizmo_bezier_lines_;
};

}

#endif // OAK_POLYGONGENERATOR_H
