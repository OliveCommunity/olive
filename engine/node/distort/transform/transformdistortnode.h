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

#ifndef OAK_TRANSFORMDISTORTNODE_H
#define OAK_TRANSFORMDISTORTNODE_H

#include "node/generator/matrix/matrix.h"
#include "node/gizmo/point.h"
#include "node/gizmo/polygon.h"
#include "node/gizmo/screen.h"

namespace olive
{

class TransformDistortNode : public MatrixGenerator {
	Q_OBJECT
public:
	TransformDistortNode();

	NODE_DEFAULT_FUNCTIONS(TransformDistortNode)

	virtual QString name() const override
	{
		return tr("Transform");
	}

	virtual QString short_name() const override
	{
		// Override MatrixGenerator's short name "Ortho"
		return name();
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.olivevideoeditor.Olive.transform");
	}

	virtual QVector<CategoryID> category() const override
	{
		return { k_category_distort };
	}

	virtual QString description() const override
	{
		return tr(
			"Transform an image in 2D space. Equivalent to multiplying by an orthographic matrix.");
	}

	virtual void retranslate() override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;

	enum AutoScaleType {
		k_auto_scale_none,
		k_auto_scale_fit,
		k_auto_scale_fill,
		k_auto_scale_stretch
	};

	static QMatrix4x4 adjust_matrix_by_resolutions(
		const QMatrix4x4 &mat, const QVector2D &sequence_res,
		const QVector2D &texture_res, const QVector2D &offset,
		AutoScaleType autoscale_type = k_auto_scale_none);

	virtual void update_gizmo_positions(const NodeValueRow &row,
									  const NodeGlobals &globals) override;
	virtual QTransform
	gizmo_transformation(const NodeValueRow &row,
						const NodeGlobals &globals) const override;

	static const QString k_parent_input;
	static const QString k_texture_input;
	static const QString k_autoscale_input;
	static const QString k_interpolation_input;

protected slots:
	virtual void gizmo_drag_start(const olive::NodeValueRow &row, double x,
								double y, const olive::Rational &time) override;

	virtual void gizmo_drag_move(double x, double y,
							   const Qt::KeyboardModifiers &modifiers) override;

private:
	static QPointF create_scale_point(double x, double y, const QPointF &half_res,
									const QMatrix4x4 &mat);

	QMatrix4x4
	generate_auto_scaled_matrix(const QMatrix4x4 &generated_matrix,
							 const NodeValueRow &db, const NodeGlobals &globals,
							 const VideoParams &texture_params) const;

	bool is_a_scale_gizmo(NodeGizmo *g) const;

	// Gizmo variables
	double gizmo_start_angle_;
	QTransform gizmo_inverted_transform_;
	QPointF gizmo_anchor_pt_;
	bool gizmo_scale_uniform_;
	double gizmo_last_angle_;
	double gizmo_last_alt_angle_;
	int gizmo_rotate_wrap_;

	enum RotationDirection {
		k_direction_none,
		k_direction_positive, // Clockwise
		k_direction_negative // Counter-clockwise
	};

	static RotationDirection get_direction_from_angles(double last,
													double current);
	RotationDirection gizmo_rotate_last_dir_;
	RotationDirection gizmo_rotate_last_alt_dir_;

	enum GizmoScaleType { k_gizmo_scale_x_only, k_gizmo_scale_y_only, k_gizmo_scale_both };

	GizmoScaleType gizmo_scale_axes_;
	QVector2D gizmo_scale_anchor_;

	// Gizmo on screen object storage
	PointGizmo *point_gizmo_[k_gizmo_scale_count];
	PointGizmo *anchor_gizmo_;
	PolygonGizmo *poly_gizmo_;
	ScreenGizmo *rotation_gizmo_;
};

}

#endif // OAK_TRANSFORMDISTORTNODE_H
