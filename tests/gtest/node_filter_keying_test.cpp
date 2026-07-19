#include <gtest/gtest.h>

#include <QPointF>
#include <QStringList>
#include <QVector2D>
#include <QVector3D>

#include "node/color/colormanager/colormanager.h"
#include "node/effect/opacity/opacityeffect.h"
#include "node/filter/blur/blur.h"
#include "node/filter/dropshadow/dropshadowfilter.h"
#include "node/filter/mosaic/mosaicfilternode.h"
#include "node/filter/stroke/stroke.h"
#include "node/keying/chromakey/chromakey.h"
#include "node/keying/colordifferencekey/colordifferencekey.h"
#include "node/keying/despill/despill.h"
#include "node/project.h"
#include "olive/core/util/color.h"
#include "render/job/colortransformjob.h"
#include "render/job/shaderjob.h"
#include "render/texture.h"
#include "widget/slider/floatslider.h"

namespace
{

// A "dummy" texture has no renderer backend and is therefore safe to pass
// around in a headless, CPU-only test.
olive::TexturePtr make_dummy_texture()
{
	return std::make_shared<olive::Texture>(
		olive::VideoParams(16, 16, olive::core::PixelFormat::f32,
						   olive::VideoParams::k_rgba_channel_count));
}

olive::NodeValue texture_value(const olive::TexturePtr &tex)
{
	return olive::NodeValue(olive::NodeValue::k_texture, tex);
}

olive::NodeValue float_value(double d)
{
	return olive::NodeValue(olive::NodeValue::k_float, d);
}

olive::NodeValue bool_value(bool b)
{
	return olive::NodeValue(olive::NodeValue::k_boolean, b);
}

olive::NodeValue combo_value(int i)
{
	return olive::NodeValue(olive::NodeValue::k_combo, i);
}

olive::NodeValue vec2_value(const QVector2D &v)
{
	return olive::NodeValue(olive::NodeValue::k_vec2, v);
}

olive::NodeValueRow make_texture_row(const QString &input,
								   const olive::TexturePtr &tex)
{
	olive::NodeValueRow row;
	row.insert(input, texture_value(tex));
	return row;
}

} // namespace

// -----------------------------------------------------------------------------
// OpacityEffect
// -----------------------------------------------------------------------------

TEST(OpacityEffect, InputDefinitionsAndDefaults)
{
	olive::OpacityEffect node;

	EXPECT_TRUE(node.has_input_with_id(olive::OpacityEffect::k_texture_input));
	EXPECT_TRUE(node.has_input_with_id(olive::OpacityEffect::k_value_input));

	EXPECT_EQ(int(node.get_input_data_type(olive::OpacityEffect::k_texture_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_EQ(int(node.get_input_data_type(olive::OpacityEffect::k_value_input)),
			  int(olive::NodeValue::k_float));

	// The texture input is a static effect input: not keyframable.
	EXPECT_FALSE(node.is_input_keyframable(olive::OpacityEffect::k_texture_input));
	EXPECT_EQ(node.get_effect_input_id(), olive::OpacityEffect::k_texture_input);

	// Opacity is a 0-100% slider defaulting to fully opaque.
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::OpacityEffect::k_value_input).toDouble(),
		1.0);
	EXPECT_DOUBLE_EQ(node.get_input_property(olive::OpacityEffect::k_value_input,
										   QStringLiteral("min"))
						 .toDouble(),
					 0.0);
	EXPECT_DOUBLE_EQ(node.get_input_property(olive::OpacityEffect::k_value_input,
										   QStringLiteral("max"))
						 .toDouble(),
					 1.0);
	EXPECT_EQ(node.get_input_property(olive::OpacityEffect::k_value_input,
									QStringLiteral("view"))
				  .toInt(),
			  int(olive::FloatSlider::k_percentage));
}

TEST(OpacityEffect, Identity)
{
	olive::OpacityEffect node;

	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.opacity"));
	EXPECT_FALSE(node.name().isEmpty());
	EXPECT_FALSE(node.description().isEmpty());

	ASSERT_EQ(node.category().size(), 1);
	EXPECT_EQ(int(node.category().first()), int(olive::Node::k_category_filter));
}

TEST(OpacityEffect, RetranslateSetsInputNames)
{
	olive::OpacityEffect node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::OpacityEffect::k_texture_input),
			  QStringLiteral("Texture"));
	EXPECT_EQ(node.get_input_name(olive::OpacityEffect::k_value_input),
			  QStringLiteral("Opacity"));
}

TEST(OpacityEffect, ShaderCodeSelectsFragmentById)
{
	olive::OpacityEffect node;

	const olive::ShaderCode mult =
		node.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("rgbmult")));
	EXPECT_FALSE(mult.frag_code().isEmpty());
	EXPECT_TRUE(mult.vert_code().isEmpty());

	const olive::ShaderCode plain =
		node.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("other")));
	EXPECT_FALSE(plain.frag_code().isEmpty());
	EXPECT_TRUE(plain.vert_code().isEmpty());

	// The two requests resolve to different fragment shaders.
	EXPECT_NE(mult.frag_code(), plain.frag_code());
}

TEST(OpacityEffect, ValueWithoutTexturePushesNothing)
{
	olive::OpacityEffect node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(OpacityEffect, ValueWithFullOpacityPassesTextureThrough)
{
	olive::OpacityEffect node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::OpacityEffect::k_texture_input, tex);
	row.insert(olive::OpacityEffect::k_value_input, float_value(1.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	// 1.0 is a no-op: the input texture is pushed unchanged, not a job.
	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	EXPECT_EQ(out, tex);
	EXPECT_FALSE(out->is_job());
}

TEST(OpacityEffect, ValueWithFractionalOpacityPushesShaderJob)
{
	olive::OpacityEffect node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::OpacityEffect::k_texture_input, tex);
	row.insert(olive::OpacityEffect::k_value_input, float_value(0.5));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ShaderJob *>(out->job());
	ASSERT_NE(job, nullptr);

	// The default shader (no special ID) is used for a plain float multiply.
	EXPECT_TRUE(job->get_shader_id().isEmpty());

	const olive::NodeValueRow &values = job->get_values();
	ASSERT_TRUE(values.contains(olive::OpacityEffect::k_value_input));
	EXPECT_DOUBLE_EQ(values.value(olive::OpacityEffect::k_value_input).to_double(),
					 0.5);
	EXPECT_EQ(values.value(olive::OpacityEffect::k_texture_input).to_texture(),
			  tex);
}

TEST(OpacityEffect, ValueWithTextureOpacityPushesRgbMultJob)
{
	olive::OpacityEffect node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::TexturePtr opacity_tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::OpacityEffect::k_texture_input, tex);
	row.insert(olive::OpacityEffect::k_value_input, texture_value(opacity_tex));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ShaderJob *>(out->job());
	ASSERT_NE(job, nullptr);

	// A texture-valued opacity selects the rgbmult shader.
	EXPECT_EQ(job->get_shader_id(), QStringLiteral("rgbmult"));
	EXPECT_EQ(job->get_values()
				  .value(olive::OpacityEffect::k_value_input)
				  .to_texture(),
			  opacity_tex);
}

// -----------------------------------------------------------------------------
// BlurFilterNode
// -----------------------------------------------------------------------------

TEST(BlurFilterNode, InputDefinitionsAndDefaults)
{
	olive::BlurFilterNode node;

	EXPECT_EQ(int(node.get_input_data_type(olive::BlurFilterNode::k_texture_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_FALSE(node.is_input_keyframable(olive::BlurFilterNode::k_texture_input));
	EXPECT_EQ(node.get_effect_input_id(), olive::BlurFilterNode::k_texture_input);

	// Method is a static UI choice defaulting to Gaussian.
	EXPECT_EQ(int(node.get_input_data_type(olive::BlurFilterNode::k_method_input)),
			  int(olive::NodeValue::k_combo));
	EXPECT_FALSE(node.is_input_keyframable(olive::BlurFilterNode::k_method_input));
	EXPECT_FALSE(node.is_input_connectable(olive::BlurFilterNode::k_method_input));
	EXPECT_EQ(node.get_standard_value(olive::BlurFilterNode::k_method_input).toInt(),
			  int(olive::BlurFilterNode::k_gaussian));
	EXPECT_EQ(int(node.get_method()), int(olive::BlurFilterNode::k_gaussian));

	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::BlurFilterNode::k_radius_input).toDouble(),
		10.0);
	EXPECT_DOUBLE_EQ(node.get_input_property(olive::BlurFilterNode::k_radius_input,
										   QStringLiteral("min"))
						 .toDouble(),
					 0.0);

	EXPECT_TRUE(
		node.get_standard_value(olive::BlurFilterNode::k_horiz_input).toBool());
	EXPECT_TRUE(
		node.get_standard_value(olive::BlurFilterNode::k_vert_input).toBool());
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::BlurFilterNode::k_directional_degrees_input)
			.toDouble(),
		0.0);
	EXPECT_EQ(node.get_standard_value(olive::BlurFilterNode::k_radial_center_input)
				  .value<QVector2D>(),
			  QVector2D(0.0f, 0.0f));
	EXPECT_TRUE(node.get_standard_value(
					 olive::BlurFilterNode::k_repeat_edge_pixels_input)
					.toBool());
}

TEST(BlurFilterNode, Identity)
{
	olive::BlurFilterNode node;

	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.blur"));
	EXPECT_FALSE(node.name().isEmpty());
	EXPECT_FALSE(node.description().isEmpty());

	ASSERT_EQ(node.category().size(), 1);
	EXPECT_EQ(int(node.category().first()), int(olive::Node::k_category_filter));
}

TEST(BlurFilterNode, RetranslateSetsInputNamesAndComboStrings)
{
	olive::BlurFilterNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::BlurFilterNode::k_texture_input),
			  QStringLiteral("Input"));
	EXPECT_EQ(node.get_input_name(olive::BlurFilterNode::k_method_input),
			  QStringLiteral("Method"));
	EXPECT_EQ(node.get_combo_box_strings(olive::BlurFilterNode::k_method_input),
			  QStringList({ QStringLiteral("Box"), QStringLiteral("Gaussian"),
							QStringLiteral("Directional"),
							QStringLiteral("Radial") }));
	EXPECT_EQ(node.get_input_name(olive::BlurFilterNode::k_radius_input),
			  QStringLiteral("Radius"));
	EXPECT_EQ(node.get_input_name(olive::BlurFilterNode::k_horiz_input),
			  QStringLiteral("Horizontal"));
	EXPECT_EQ(node.get_input_name(olive::BlurFilterNode::k_vert_input),
			  QStringLiteral("Vertical"));
	EXPECT_EQ(
		node.get_input_name(olive::BlurFilterNode::k_repeat_edge_pixels_input),
		QStringLiteral("Repeat Edge Pixels"));
	EXPECT_EQ(
		node.get_input_name(olive::BlurFilterNode::k_directional_degrees_input),
		QStringLiteral("Direction"));
	EXPECT_EQ(node.get_input_name(olive::BlurFilterNode::k_radial_center_input),
			  QStringLiteral("Center"));
}

TEST(BlurFilterNode, MethodSwitchTogglesInputVisibility)
{
	olive::BlurFilterNode node;

	// Default method (Gaussian) shows the axis toggles only.
	EXPECT_FALSE(node.is_input_hidden(olive::BlurFilterNode::k_horiz_input));
	EXPECT_FALSE(node.is_input_hidden(olive::BlurFilterNode::k_vert_input));
	EXPECT_TRUE(
		node.is_input_hidden(olive::BlurFilterNode::k_directional_degrees_input));
	EXPECT_TRUE(node.is_input_hidden(olive::BlurFilterNode::k_radial_center_input));

	node.set_standard_value(olive::BlurFilterNode::k_method_input,
						  int(olive::BlurFilterNode::k_directional));
	EXPECT_TRUE(node.is_input_hidden(olive::BlurFilterNode::k_horiz_input));
	EXPECT_TRUE(node.is_input_hidden(olive::BlurFilterNode::k_vert_input));
	EXPECT_FALSE(
		node.is_input_hidden(olive::BlurFilterNode::k_directional_degrees_input));
	EXPECT_TRUE(node.is_input_hidden(olive::BlurFilterNode::k_radial_center_input));

	node.set_standard_value(olive::BlurFilterNode::k_method_input,
						  int(olive::BlurFilterNode::k_radial));
	EXPECT_TRUE(node.is_input_hidden(olive::BlurFilterNode::k_horiz_input));
	EXPECT_TRUE(node.is_input_hidden(olive::BlurFilterNode::k_vert_input));
	EXPECT_TRUE(
		node.is_input_hidden(olive::BlurFilterNode::k_directional_degrees_input));
	EXPECT_FALSE(node.is_input_hidden(olive::BlurFilterNode::k_radial_center_input));

	node.set_standard_value(olive::BlurFilterNode::k_method_input,
						  int(olive::BlurFilterNode::k_box));
	EXPECT_FALSE(node.is_input_hidden(olive::BlurFilterNode::k_horiz_input));
	EXPECT_FALSE(node.is_input_hidden(olive::BlurFilterNode::k_vert_input));
	EXPECT_TRUE(
		node.is_input_hidden(olive::BlurFilterNode::k_directional_degrees_input));
	EXPECT_TRUE(node.is_input_hidden(olive::BlurFilterNode::k_radial_center_input));
}

TEST(BlurFilterNode, ShaderCodeLoadsFragmentResource)
{
	olive::BlurFilterNode node;

	const olive::ShaderCode code =
		node.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("test")));

	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.vert_code().isEmpty());
}

TEST(BlurFilterNode, ValueWithoutTexturePushesNothing)
{
	olive::BlurFilterNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(BlurFilterNode, ValueWithZeroRadiusPassesTextureThrough)
{
	olive::BlurFilterNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::BlurFilterNode::k_texture_input, tex);
	row.insert(olive::BlurFilterNode::k_method_input,
			   combo_value(int(olive::BlurFilterNode::k_gaussian)));
	row.insert(olive::BlurFilterNode::k_radius_input, float_value(0.0));
	row.insert(olive::BlurFilterNode::k_horiz_input, bool_value(true));
	row.insert(olive::BlurFilterNode::k_vert_input, bool_value(true));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	// No radius means no blur: the texture passes through unchanged.
	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	EXPECT_EQ(out, tex);
	EXPECT_FALSE(out->is_job());
}

TEST(BlurFilterNode, ValueWithBothAxesPushesTwoIterationJob)
{
	olive::BlurFilterNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::BlurFilterNode::k_texture_input, tex);
	row.insert(olive::BlurFilterNode::k_method_input,
			   combo_value(int(olive::BlurFilterNode::k_gaussian)));
	row.insert(olive::BlurFilterNode::k_radius_input, float_value(10.0));
	row.insert(olive::BlurFilterNode::k_horiz_input, bool_value(true));
	row.insert(olive::BlurFilterNode::k_vert_input, bool_value(true));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ShaderJob *>(out->job());
	ASSERT_NE(job, nullptr);

	// Blurring both axes runs the shader twice, feeding the texture input.
	EXPECT_EQ(job->get_iteration_count(), 2);
	EXPECT_EQ(job->get_iterative_input(), olive::BlurFilterNode::k_texture_input);

	const olive::NodeValueRow &values = job->get_values();
	ASSERT_TRUE(values.contains(QStringLiteral("resolution_in")));
	EXPECT_EQ(values.value(QStringLiteral("resolution_in")).to_vec2(),
			  QVector2D(16.0f, 16.0f));
	EXPECT_DOUBLE_EQ(
		values.value(olive::BlurFilterNode::k_radius_input).to_double(), 10.0);
}

TEST(BlurFilterNode, ValueWithSingleAxisPushesOneIterationJob)
{
	olive::BlurFilterNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::BlurFilterNode::k_texture_input, tex);
	row.insert(olive::BlurFilterNode::k_method_input,
			   combo_value(int(olive::BlurFilterNode::k_gaussian)));
	row.insert(olive::BlurFilterNode::k_radius_input, float_value(10.0));
	row.insert(olive::BlurFilterNode::k_horiz_input, bool_value(true));
	row.insert(olive::BlurFilterNode::k_vert_input, bool_value(false));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ShaderJob *>(out->job());
	ASSERT_NE(job, nullptr);
	EXPECT_EQ(job->get_iteration_count(), 1);
	EXPECT_EQ(job->get_iterative_input(), olive::BlurFilterNode::k_texture_input);
}

TEST(BlurFilterNode, ValueWithNoAxesPassesTextureThrough)
{
	olive::BlurFilterNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::BlurFilterNode::k_texture_input, tex);
	row.insert(olive::BlurFilterNode::k_method_input,
			   combo_value(int(olive::BlurFilterNode::k_gaussian)));
	row.insert(olive::BlurFilterNode::k_radius_input, float_value(10.0));
	row.insert(olive::BlurFilterNode::k_horiz_input, bool_value(false));
	row.insert(olive::BlurFilterNode::k_vert_input, bool_value(false));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	// Both axes unchecked disables the blur entirely.
	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	EXPECT_EQ(out, tex);
	EXPECT_FALSE(out->is_job());
}

TEST(BlurFilterNode, ValueWithDirectionalMethodPushesJob)
{
	olive::BlurFilterNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::BlurFilterNode::k_texture_input, tex);
	row.insert(olive::BlurFilterNode::k_method_input,
			   combo_value(int(olive::BlurFilterNode::k_directional)));
	row.insert(olive::BlurFilterNode::k_radius_input, float_value(10.0));
	row.insert(olive::BlurFilterNode::k_directional_degrees_input,
			   float_value(45.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	// Directional blur ignores the axis toggles and always runs once.
	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ShaderJob *>(out->job());
	ASSERT_NE(job, nullptr);
	EXPECT_EQ(job->get_iteration_count(), 1);
	EXPECT_DOUBLE_EQ(
		job->get_values()
			.value(olive::BlurFilterNode::k_directional_degrees_input)
			.to_double(),
		45.0);
}

TEST(BlurFilterNode, RadialGizmoFollowsCenterAndHalfResolution)
{
	olive::BlurFilterNode node;

	olive::TexturePtr tex = make_dummy_texture();

	ASSERT_EQ(node.get_gizmos().size(), 1);
	auto *gizmo = static_cast<olive::PointGizmo *>(node.get_gizmos().first());
	ASSERT_NE(gizmo, nullptr);

	olive::NodeValueRow row =
		make_texture_row(olive::BlurFilterNode::k_texture_input, tex);
	row.insert(olive::BlurFilterNode::k_method_input,
			   combo_value(int(olive::BlurFilterNode::k_radial)));
	row.insert(olive::BlurFilterNode::k_radial_center_input,
			   vec2_value(QVector2D(3.0f, -2.0f)));

	node.update_gizmo_positions(row, olive::NodeGlobals());

	// The gizmo sits at the center offset from half the texture resolution.
	EXPECT_TRUE(gizmo->is_visible());
	EXPECT_EQ(gizmo->get_point(), QPointF(11.0, 6.0));
	EXPECT_EQ(node.get_input_property(olive::BlurFilterNode::k_radial_center_input,
									QStringLiteral("offset"))
				  .value<QVector2D>(),
			  QVector2D(8.0f, 8.0f));

	// Any other method hides the gizmo again.
	row[olive::BlurFilterNode::k_method_input] =
		combo_value(int(olive::BlurFilterNode::k_gaussian));
	node.update_gizmo_positions(row, olive::NodeGlobals());
	EXPECT_FALSE(gizmo->is_visible());
}

// -----------------------------------------------------------------------------
// DropShadowFilter
// -----------------------------------------------------------------------------

TEST(DropShadowFilter, InputDefinitionsAndDefaults)
{
	olive::DropShadowFilter node;

	EXPECT_EQ(int(node.get_input_data_type(olive::DropShadowFilter::k_texture_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_FALSE(
		node.is_input_keyframable(olive::DropShadowFilter::k_texture_input));
	EXPECT_EQ(node.get_effect_input_id(), olive::DropShadowFilter::k_texture_input);

	// The default shadow is black.
	const olive::core::Color color =
		node.get_standard_value(olive::DropShadowFilter::k_color_input)
			.value<olive::core::Color>();
	EXPECT_FLOAT_EQ(color.red(), 0.0f);
	EXPECT_FLOAT_EQ(color.green(), 0.0f);
	EXPECT_FLOAT_EQ(color.blue(), 0.0f);
	EXPECT_FLOAT_EQ(color.alpha(), 1.0f);

	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::DropShadowFilter::k_distance_input)
			.toDouble(),
		10.0);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::DropShadowFilter::k_angle_input).toDouble(),
		135.0);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::DropShadowFilter::k_softness_input)
			.toDouble(),
		10.0);
	EXPECT_DOUBLE_EQ(
		node.get_input_property(olive::DropShadowFilter::k_softness_input,
							  QStringLiteral("min"))
			.toDouble(),
		0.0);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::DropShadowFilter::k_opacity_input)
			.toDouble(),
		1.0);
	EXPECT_DOUBLE_EQ(node.get_input_property(olive::DropShadowFilter::k_opacity_input,
										   QStringLiteral("min"))
						 .toDouble(),
					 0.0);
	EXPECT_EQ(node.get_input_property(olive::DropShadowFilter::k_opacity_input,
									QStringLiteral("view"))
				  .toInt(),
			  int(olive::FloatSlider::k_percentage));
	EXPECT_FALSE(
		node.get_standard_value(olive::DropShadowFilter::k_fast_input).toBool());
}

TEST(DropShadowFilter, Identity)
{
	olive::DropShadowFilter node;

	EXPECT_EQ(node.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.dropshadow"));
	EXPECT_FALSE(node.name().isEmpty());
	EXPECT_FALSE(node.description().isEmpty());

	ASSERT_EQ(node.category().size(), 1);
	EXPECT_EQ(int(node.category().first()), int(olive::Node::k_category_filter));
}

TEST(DropShadowFilter, RetranslateSetsInputNames)
{
	olive::DropShadowFilter node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::DropShadowFilter::k_texture_input),
			  QStringLiteral("Texture"));
	EXPECT_EQ(node.get_input_name(olive::DropShadowFilter::k_color_input),
			  QStringLiteral("Color"));
	EXPECT_EQ(node.get_input_name(olive::DropShadowFilter::k_distance_input),
			  QStringLiteral("Distance"));
	EXPECT_EQ(node.get_input_name(olive::DropShadowFilter::k_angle_input),
			  QStringLiteral("Angle"));
	EXPECT_EQ(node.get_input_name(olive::DropShadowFilter::k_softness_input),
			  QStringLiteral("Softness"));
	EXPECT_EQ(node.get_input_name(olive::DropShadowFilter::k_opacity_input),
			  QStringLiteral("Opacity"));
	EXPECT_EQ(node.get_input_name(olive::DropShadowFilter::k_fast_input),
			  QStringLiteral("Faster (Lower Quality)"));
}

TEST(DropShadowFilter, ShaderCodeLoadsFragmentResource)
{
	olive::DropShadowFilter node;

	const olive::ShaderCode code =
		node.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("test")));

	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.vert_code().isEmpty());
}

TEST(DropShadowFilter, ValueWithoutTexturePushesNothing)
{
	olive::DropShadowFilter node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(DropShadowFilter, ValueWithZeroSoftnessPushesSingleIterationJob)
{
	olive::DropShadowFilter node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::DropShadowFilter::k_texture_input, tex);
	row.insert(olive::DropShadowFilter::k_softness_input, float_value(0.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ShaderJob *>(out->job());
	ASSERT_NE(job, nullptr);

	// Zero softness skips the blur passes: a single shader iteration remains.
	EXPECT_EQ(job->get_iteration_count(), 1);

	const olive::NodeValueRow &values = job->get_values();
	EXPECT_EQ(values.value(QStringLiteral("resolution_in")).to_vec2(),
			  QVector2D(16.0f, 16.0f));
	// The previous-iteration input is always seeded with the source texture.
	EXPECT_EQ(values.value(QStringLiteral("previous_iteration_in"))
				  .to_texture(),
			  tex);
}

TEST(DropShadowFilter, ValueWithSoftnessPushesThreeIterationJob)
{
	olive::DropShadowFilter node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::DropShadowFilter::k_texture_input, tex);
	row.insert(olive::DropShadowFilter::k_softness_input, float_value(10.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ShaderJob *>(out->job());
	ASSERT_NE(job, nullptr);

	// Non-zero softness blurs iteratively over the previous pass.
	EXPECT_EQ(job->get_iteration_count(), 3);
	EXPECT_EQ(job->get_iterative_input(),
			  QStringLiteral("previous_iteration_in"));
}

// -----------------------------------------------------------------------------
// MosaicFilterNode
// -----------------------------------------------------------------------------

TEST(MosaicFilterNode, InputDefinitionsAndDefaults)
{
	olive::MosaicFilterNode node;

	EXPECT_EQ(
		int(node.get_input_data_type(olive::MosaicFilterNode::k_texture_input)),
		int(olive::NodeValue::k_texture));
	EXPECT_FALSE(
		node.is_input_keyframable(olive::MosaicFilterNode::k_texture_input));
	EXPECT_EQ(node.get_effect_input_id(), olive::MosaicFilterNode::k_texture_input);

	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::MosaicFilterNode::k_horiz_input).toDouble(),
		32.0);
	EXPECT_DOUBLE_EQ(node.get_input_property(olive::MosaicFilterNode::k_horiz_input,
										   QStringLiteral("min"))
						 .toDouble(),
					 1.0);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::MosaicFilterNode::k_vert_input).toDouble(),
		18.0);
	EXPECT_DOUBLE_EQ(node.get_input_property(olive::MosaicFilterNode::k_vert_input,
										   QStringLiteral("min"))
						 .toDouble(),
					 1.0);
}

TEST(MosaicFilterNode, Identity)
{
	olive::MosaicFilterNode node;

	EXPECT_EQ(node.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.mosaicfilter"));
	EXPECT_FALSE(node.name().isEmpty());
	EXPECT_FALSE(node.description().isEmpty());

	ASSERT_EQ(node.category().size(), 1);
	EXPECT_EQ(int(node.category().first()), int(olive::Node::k_category_filter));
}

TEST(MosaicFilterNode, RetranslateSetsInputNames)
{
	olive::MosaicFilterNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::MosaicFilterNode::k_texture_input),
			  QStringLiteral("Texture"));
	EXPECT_EQ(node.get_input_name(olive::MosaicFilterNode::k_horiz_input),
			  QStringLiteral("Horizontal"));
	EXPECT_EQ(node.get_input_name(olive::MosaicFilterNode::k_vert_input),
			  QStringLiteral("Vertical"));
}

TEST(MosaicFilterNode, ShaderCodeLoadsFragmentResource)
{
	olive::MosaicFilterNode node;

	const olive::ShaderCode code =
		node.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("test")));

	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.vert_code().isEmpty());
}

TEST(MosaicFilterNode, ValueWithoutTexturePushesNothing)
{
	olive::MosaicFilterNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(MosaicFilterNode, ValueWithMatchingResolutionPassesTextureThrough)
{
	olive::MosaicFilterNode node;

	// A mosaic block size equal to the texture size is a no-op.
	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::MosaicFilterNode::k_texture_input, tex);
	row.insert(olive::MosaicFilterNode::k_horiz_input, float_value(16.0));
	row.insert(olive::MosaicFilterNode::k_vert_input, float_value(16.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	EXPECT_EQ(out, tex);
	EXPECT_FALSE(out->is_job());
}

TEST(MosaicFilterNode, ValueWithSingleAxisMatchingResolutionRunsJob)
{
	olive::MosaicFilterNode node;

	// Only one axis matching the texture size still changes the image, so
	// the effect must run; passthrough requires BOTH axes to match.
	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::MosaicFilterNode::k_texture_input, tex);
	row.insert(olive::MosaicFilterNode::k_horiz_input, float_value(16.0));
	row.insert(olive::MosaicFilterNode::k_vert_input, float_value(8.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	EXPECT_TRUE(out->is_job());
}

TEST(MosaicFilterNode, ValuePushesJobWithLinearInterpolation)
{
	olive::MosaicFilterNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::MosaicFilterNode::k_texture_input, tex);
	row.insert(olive::MosaicFilterNode::k_horiz_input, float_value(32.0));
	row.insert(olive::MosaicFilterNode::k_vert_input, float_value(18.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ShaderJob *>(out->job());
	ASSERT_NE(job, nullptr);

	// Mipmapping would smear the blocks, so the mosaic forces bilinear lookup.
	EXPECT_EQ(int(job->get_interpolation(olive::MosaicFilterNode::k_texture_input)),
			  int(olive::Texture::k_linear));
}

// -----------------------------------------------------------------------------
// StrokeFilterNode
// -----------------------------------------------------------------------------

TEST(StrokeFilterNode, InputDefinitionsAndDefaults)
{
	olive::StrokeFilterNode node;

	EXPECT_EQ(int(node.get_input_data_type(olive::StrokeFilterNode::k_texture_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_FALSE(
		node.is_input_keyframable(olive::StrokeFilterNode::k_texture_input));
	EXPECT_EQ(node.get_effect_input_id(), olive::StrokeFilterNode::k_texture_input);

	// The default stroke is opaque white.
	const olive::core::Color color =
		node.get_standard_value(olive::StrokeFilterNode::k_color_input)
			.value<olive::core::Color>();
	EXPECT_FLOAT_EQ(color.red(), 1.0f);
	EXPECT_FLOAT_EQ(color.green(), 1.0f);
	EXPECT_FLOAT_EQ(color.blue(), 1.0f);
	EXPECT_FLOAT_EQ(color.alpha(), 1.0f);

	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::StrokeFilterNode::k_radius_input).toDouble(),
		10.0);
	EXPECT_DOUBLE_EQ(
		node.get_input_property(olive::StrokeFilterNode::k_radius_input,
							  QStringLiteral("min"))
			.toDouble(),
		0.0);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::StrokeFilterNode::k_opacity_input)
			.toDouble(),
		1.0);
	EXPECT_DOUBLE_EQ(
		node.get_input_property(olive::StrokeFilterNode::k_opacity_input,
							  QStringLiteral("min"))
			.toDouble(),
		0.0);
	EXPECT_DOUBLE_EQ(
		node.get_input_property(olive::StrokeFilterNode::k_opacity_input,
							  QStringLiteral("max"))
			.toDouble(),
		1.0);
	EXPECT_EQ(node.get_input_property(olive::StrokeFilterNode::k_opacity_input,
									QStringLiteral("view"))
				  .toInt(),
			  int(olive::FloatSlider::k_percentage));
	EXPECT_FALSE(
		node.get_standard_value(olive::StrokeFilterNode::k_inner_input).toBool());
}

TEST(StrokeFilterNode, Identity)
{
	olive::StrokeFilterNode node;

	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.stroke"));
	EXPECT_FALSE(node.name().isEmpty());
	EXPECT_FALSE(node.description().isEmpty());

	ASSERT_EQ(node.category().size(), 1);
	EXPECT_EQ(int(node.category().first()), int(olive::Node::k_category_filter));
}

TEST(StrokeFilterNode, RetranslateSetsInputNames)
{
	olive::StrokeFilterNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::StrokeFilterNode::k_texture_input),
			  QStringLiteral("Input"));
	EXPECT_EQ(node.get_input_name(olive::StrokeFilterNode::k_color_input),
			  QStringLiteral("Color"));
	EXPECT_EQ(node.get_input_name(olive::StrokeFilterNode::k_radius_input),
			  QStringLiteral("Radius"));
	EXPECT_EQ(node.get_input_name(olive::StrokeFilterNode::k_opacity_input),
			  QStringLiteral("Opacity"));
	EXPECT_EQ(node.get_input_name(olive::StrokeFilterNode::k_inner_input),
			  QStringLiteral("Inner"));
}

TEST(StrokeFilterNode, ShaderCodeLoadsFragmentResource)
{
	olive::StrokeFilterNode node;

	const olive::ShaderCode code =
		node.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("test")));

	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.vert_code().isEmpty());
}

TEST(StrokeFilterNode, ValueWithoutTexturePushesNothing)
{
	olive::StrokeFilterNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(StrokeFilterNode, ValueWithRadiusAndOpacityPushesJob)
{
	olive::StrokeFilterNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::StrokeFilterNode::k_texture_input, tex);
	row.insert(olive::StrokeFilterNode::k_radius_input, float_value(10.0));
	row.insert(olive::StrokeFilterNode::k_opacity_input, float_value(1.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ShaderJob *>(out->job());
	ASSERT_NE(job, nullptr);
	EXPECT_EQ(job->get_values()
				  .value(QStringLiteral("resolution_in"))
				  .to_vec2(),
			  QVector2D(16.0f, 16.0f));
}

TEST(StrokeFilterNode, ValueWithZeroRadiusPassesTextureThrough)
{
	olive::StrokeFilterNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::StrokeFilterNode::k_texture_input, tex);
	row.insert(olive::StrokeFilterNode::k_radius_input, float_value(0.0));
	row.insert(olive::StrokeFilterNode::k_opacity_input, float_value(1.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	EXPECT_EQ(out, tex);
	EXPECT_FALSE(out->is_job());
}

TEST(StrokeFilterNode, ValueWithZeroOpacityPassesTextureThrough)
{
	olive::StrokeFilterNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::StrokeFilterNode::k_texture_input, tex);
	row.insert(olive::StrokeFilterNode::k_radius_input, float_value(10.0));
	row.insert(olive::StrokeFilterNode::k_opacity_input, float_value(0.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	EXPECT_EQ(out, tex);
	EXPECT_FALSE(out->is_job());
}

// -----------------------------------------------------------------------------
// ChromaKeyNode
// -----------------------------------------------------------------------------

TEST(ChromaKeyNode, InputDefinitionsAndDefaults)
{
	olive::ChromaKeyNode node;

	// The texture input comes from OCIOBaseNode and is the effect input.
	EXPECT_TRUE(node.has_input_with_id(olive::OCIOBaseNode::k_texture_input));
	EXPECT_EQ(node.get_effect_input_id(), olive::OCIOBaseNode::k_texture_input);

	// The default key color is pure green.
	const olive::core::Color color =
		node.get_standard_value(olive::ChromaKeyNode::k_color_input)
			.value<olive::core::Color>();
	EXPECT_FLOAT_EQ(color.red(), 0.0f);
	EXPECT_FLOAT_EQ(color.green(), 1.0f);
	EXPECT_FLOAT_EQ(color.blue(), 0.0f);
	EXPECT_FLOAT_EQ(color.alpha(), 1.0f);

	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::ChromaKeyNode::k_lower_tolerance_input)
			.toDouble(),
		5.0);
	EXPECT_DOUBLE_EQ(
		node.get_input_property(olive::ChromaKeyNode::k_lower_tolerance_input,
							  QStringLiteral("min"))
			.toDouble(),
		0.0);
	EXPECT_DOUBLE_EQ(
		node.get_input_property(olive::ChromaKeyNode::k_lower_tolerance_input,
							  QStringLiteral("base"))
			.toDouble(),
		0.1);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::ChromaKeyNode::k_upper_tolerance_input)
			.toDouble(),
		25.0);
	EXPECT_DOUBLE_EQ(
		node.get_input_property(olive::ChromaKeyNode::k_upper_tolerance_input,
							  QStringLiteral("base"))
			.toDouble(),
		0.1);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::ChromaKeyNode::k_highlights_input)
			.toDouble(),
		100.0);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::ChromaKeyNode::k_shadows_input).toDouble(),
		100.0);

	EXPECT_EQ(int(node.get_input_data_type(
					  olive::ChromaKeyNode::k_garbage_matte_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_FALSE(
		node.is_input_keyframable(olive::ChromaKeyNode::k_garbage_matte_input));
	EXPECT_EQ(
		int(node.get_input_data_type(olive::ChromaKeyNode::k_core_matte_input)),
		int(olive::NodeValue::k_texture));
	EXPECT_FALSE(
		node.is_input_keyframable(olive::ChromaKeyNode::k_core_matte_input));

	EXPECT_FALSE(
		node.get_standard_value(olive::ChromaKeyNode::k_invert_input).toBool());
	EXPECT_FALSE(
		node.get_standard_value(olive::ChromaKeyNode::k_mask_only_input).toBool());
}

TEST(ChromaKeyNode, Identity)
{
	olive::ChromaKeyNode node;

	EXPECT_EQ(node.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.chromakey"));
	EXPECT_FALSE(node.name().isEmpty());
	EXPECT_FALSE(node.description().isEmpty());

	ASSERT_EQ(node.category().size(), 1);
	EXPECT_EQ(int(node.category().first()), int(olive::Node::k_category_keying));
}

TEST(ChromaKeyNode, RetranslateSetsInputNames)
{
	olive::ChromaKeyNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::OCIOBaseNode::k_texture_input),
			  QStringLiteral("Input"));
	EXPECT_EQ(node.get_input_name(olive::ChromaKeyNode::k_garbage_matte_input),
			  QStringLiteral("Garbage Matte"));
	EXPECT_EQ(node.get_input_name(olive::ChromaKeyNode::k_core_matte_input),
			  QStringLiteral("Core Matte"));
	EXPECT_EQ(node.get_input_name(olive::ChromaKeyNode::k_color_input),
			  QStringLiteral("Key Color"));
	EXPECT_EQ(node.get_input_name(olive::ChromaKeyNode::k_shadows_input),
			  QStringLiteral("Shadows"));
	EXPECT_EQ(node.get_input_name(olive::ChromaKeyNode::k_highlights_input),
			  QStringLiteral("Highlights"));
	EXPECT_EQ(node.get_input_name(olive::ChromaKeyNode::k_upper_tolerance_input),
			  QStringLiteral("Upper Tolerance"));
	EXPECT_EQ(node.get_input_name(olive::ChromaKeyNode::k_lower_tolerance_input),
			  QStringLiteral("Lower Tolerance"));
	EXPECT_EQ(node.get_input_name(olive::ChromaKeyNode::k_invert_input),
			  QStringLiteral("Invert Mask"));
	EXPECT_EQ(node.get_input_name(olive::ChromaKeyNode::k_mask_only_input),
			  QStringLiteral("Show Mask Only"));
}

TEST(ChromaKeyNode, ShaderCodeSubstitutesStub)
{
	olive::ChromaKeyNode node;

	// The fragment shader contains a %1 placeholder for OCIO-generated code,
	// which GetShaderCode fills with the request's stub.
	const olive::ShaderCode with_stub = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("test"),
								   QStringLiteral("OAK_TEST_STUB")));
	EXPECT_TRUE(with_stub.frag_code().contains(QStringLiteral("OAK_TEST_STUB")));

	const olive::ShaderCode no_stub =
		node.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("test")));
	EXPECT_FALSE(no_stub.frag_code().isEmpty());
	EXPECT_FALSE(no_stub.frag_code().contains(QStringLiteral("%1")));
}

TEST(ChromaKeyNode, ValueWithoutProcessorPushesNothing)
{
	// Without a project no color manager is attached, so no processor is ever
	// generated and Value() must push nothing even with a valid texture.
	olive::ChromaKeyNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::OCIOBaseNode::k_texture_input, tex);

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(ChromaKeyNode, ValueInProjectPushesColorTransformJob)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	auto *node = new olive::ChromaKeyNode();
	node->setParent(&project);

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::OCIOBaseNode::k_texture_input, tex);

	olive::NodeValueTable table;
	node->value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ColorTransformJob *>(out->job());
	ASSERT_NE(job, nullptr);

	// Adding the node to the project generates its XYZ processor.
	EXPECT_NE(job->get_color_processor(), nullptr);
	EXPECT_EQ(job->get_function_name(),
			  QStringLiteral("SceneLinearToCIEXYZ_d65"));
	EXPECT_EQ(job->custom_shader_source(), node);
	EXPECT_EQ(job->get_input_texture().to_texture(), tex);
}

// -----------------------------------------------------------------------------
// ColorDifferenceKeyNode
// -----------------------------------------------------------------------------

TEST(ColorDifferenceKeyNode, InputDefinitionsAndDefaults)
{
	olive::ColorDifferenceKeyNode node;

	EXPECT_EQ(int(node.get_input_data_type(
					  olive::ColorDifferenceKeyNode::k_texture_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_FALSE(node.is_input_keyframable(
		olive::ColorDifferenceKeyNode::k_texture_input));
	EXPECT_EQ(node.get_effect_input_id(),
			  olive::ColorDifferenceKeyNode::k_texture_input);

	EXPECT_EQ(int(node.get_input_data_type(
					  olive::ColorDifferenceKeyNode::k_garbage_matte_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_EQ(int(node.get_input_data_type(
					  olive::ColorDifferenceKeyNode::k_core_matte_input)),
			  int(olive::NodeValue::k_texture));

	// Key color is a static combo defaulting to the first entry (green).
	EXPECT_EQ(int(node.get_input_data_type(
					  olive::ColorDifferenceKeyNode::k_color_input)),
			  int(olive::NodeValue::k_combo));
	EXPECT_EQ(node.get_standard_value(olive::ColorDifferenceKeyNode::k_color_input)
				  .toInt(),
			  0);

	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::ColorDifferenceKeyNode::k_highlights_input)
			.toDouble(),
		1.0);
	EXPECT_DOUBLE_EQ(
		node.get_input_property(olive::ColorDifferenceKeyNode::k_highlights_input,
							  QStringLiteral("min"))
			.toDouble(),
		0.0);
	EXPECT_DOUBLE_EQ(
		node.get_input_property(olive::ColorDifferenceKeyNode::k_highlights_input,
							  QStringLiteral("base"))
			.toDouble(),
		0.01);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::ColorDifferenceKeyNode::k_shadows_input)
			.toDouble(),
		1.0);
	EXPECT_DOUBLE_EQ(
		node.get_input_property(olive::ColorDifferenceKeyNode::k_shadows_input,
							  QStringLiteral("min"))
			.toDouble(),
		0.0);
	EXPECT_DOUBLE_EQ(
		node.get_input_property(olive::ColorDifferenceKeyNode::k_shadows_input,
							  QStringLiteral("base"))
			.toDouble(),
		0.01);

	EXPECT_FALSE(node.get_standard_value(
					  olive::ColorDifferenceKeyNode::k_mask_only_input)
					 .toBool());
}

TEST(ColorDifferenceKeyNode, Identity)
{
	olive::ColorDifferenceKeyNode node;

	EXPECT_EQ(node.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.colordifferencekey"));
	EXPECT_FALSE(node.name().isEmpty());
	EXPECT_FALSE(node.description().isEmpty());

	ASSERT_EQ(node.category().size(), 1);
	EXPECT_EQ(int(node.category().first()), int(olive::Node::k_category_keying));
}

TEST(ColorDifferenceKeyNode, RetranslateSetsInputNamesAndComboStrings)
{
	olive::ColorDifferenceKeyNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::ColorDifferenceKeyNode::k_texture_input),
			  QStringLiteral("Input"));
	EXPECT_EQ(
		node.get_input_name(olive::ColorDifferenceKeyNode::k_garbage_matte_input),
		QStringLiteral("Garbage Matte"));
	EXPECT_EQ(node.get_input_name(olive::ColorDifferenceKeyNode::k_core_matte_input),
			  QStringLiteral("Core Matte"));
	EXPECT_EQ(node.get_input_name(olive::ColorDifferenceKeyNode::k_color_input),
			  QStringLiteral("Key Color"));
	EXPECT_EQ(node.get_combo_box_strings(
				  olive::ColorDifferenceKeyNode::k_color_input),
			  QStringList(
				  { QStringLiteral("Green"), QStringLiteral("Blue") }));
	EXPECT_EQ(node.get_input_name(olive::ColorDifferenceKeyNode::k_shadows_input),
			  QStringLiteral("Shadows"));
	EXPECT_EQ(
		node.get_input_name(olive::ColorDifferenceKeyNode::k_highlights_input),
		QStringLiteral("Highlights"));
	EXPECT_EQ(node.get_input_name(olive::ColorDifferenceKeyNode::k_mask_only_input),
			  QStringLiteral("Show Mask Only"));
}

TEST(ColorDifferenceKeyNode, ShaderCodeLoadsFragmentResource)
{
	olive::ColorDifferenceKeyNode node;

	const olive::ShaderCode code =
		node.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("test")));

	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.vert_code().isEmpty());
}

TEST(ColorDifferenceKeyNode, ValueWithoutTexturePushesNothing)
{
	olive::ColorDifferenceKeyNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(ColorDifferenceKeyNode, ValuePushesShaderJobWithRowValues)
{
	olive::ColorDifferenceKeyNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::ColorDifferenceKeyNode::k_texture_input, tex);
	row.insert(olive::ColorDifferenceKeyNode::k_color_input, combo_value(1));
	row.insert(olive::ColorDifferenceKeyNode::k_mask_only_input, bool_value(true));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ShaderJob *>(out->job());
	ASSERT_NE(job, nullptr);

	// The whole input row is forwarded into the job.
	const olive::NodeValueRow &values = job->get_values();
	EXPECT_EQ(values.value(olive::ColorDifferenceKeyNode::k_texture_input)
				  .to_texture(),
			  tex);
	EXPECT_EQ(values.value(olive::ColorDifferenceKeyNode::k_color_input).to_int(),
			  1);
	EXPECT_TRUE(values.value(olive::ColorDifferenceKeyNode::k_mask_only_input)
					.to_bool());
}

// -----------------------------------------------------------------------------
// DespillNode
// -----------------------------------------------------------------------------

TEST(DespillNode, InputDefinitionsAndDefaults)
{
	olive::DespillNode node;

	EXPECT_EQ(int(node.get_input_data_type(olive::DespillNode::k_texture_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_FALSE(node.is_input_keyframable(olive::DespillNode::k_texture_input));
	EXPECT_EQ(node.get_effect_input_id(), olive::DespillNode::k_texture_input);

	EXPECT_EQ(int(node.get_input_data_type(olive::DespillNode::k_color_input)),
			  int(olive::NodeValue::k_combo));
	EXPECT_EQ(node.get_standard_value(olive::DespillNode::k_color_input).toInt(),
			  0);
	EXPECT_EQ(int(node.get_input_data_type(olive::DespillNode::k_method_input)),
			  int(olive::NodeValue::k_combo));
	EXPECT_EQ(node.get_standard_value(olive::DespillNode::k_method_input).toInt(),
			  0);
	EXPECT_FALSE(node.get_standard_value(
					  olive::DespillNode::k_preserve_luminance_input)
					 .toBool());
}

TEST(DespillNode, Identity)
{
	olive::DespillNode node;

	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.despill"));
	EXPECT_FALSE(node.name().isEmpty());
	EXPECT_FALSE(node.description().isEmpty());

	ASSERT_EQ(node.category().size(), 1);
	EXPECT_EQ(int(node.category().first()), int(olive::Node::k_category_keying));
}

TEST(DespillNode, RetranslateSetsInputNamesAndComboStrings)
{
	olive::DespillNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::DespillNode::k_texture_input),
			  QStringLiteral("Input"));
	EXPECT_EQ(node.get_input_name(olive::DespillNode::k_color_input),
			  QStringLiteral("Key Color"));
	EXPECT_EQ(node.get_combo_box_strings(olive::DespillNode::k_color_input),
			  QStringList(
				  { QStringLiteral("Green"), QStringLiteral("Blue") }));
	EXPECT_EQ(node.get_input_name(olive::DespillNode::k_method_input),
			  QStringLiteral("Method"));
	EXPECT_EQ(node.get_combo_box_strings(olive::DespillNode::k_method_input),
			  QStringList({ QStringLiteral("Average"),
							QStringLiteral("Double Red Average"),
							QStringLiteral("Double Average"),
							QStringLiteral("Limit") }));
	EXPECT_EQ(node.get_input_name(olive::DespillNode::k_preserve_luminance_input),
			  QStringLiteral("Preserve Luminance"));
}

TEST(DespillNode, ShaderCodeLoadsFragmentResource)
{
	olive::DespillNode node;

	const olive::ShaderCode code =
		node.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("test")));

	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.vert_code().isEmpty());
}

TEST(DespillNode, ValueInProjectWithoutTexturePushesNothing)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	auto *node = new olive::DespillNode();
	node->setParent(&project);

	olive::NodeValueTable table;
	node->value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(DespillNode, ValueInProjectPushesJobWithLumaCoefficients)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	auto *node = new olive::DespillNode();
	node->setParent(&project);

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::DespillNode::k_texture_input, tex);

	olive::NodeValueTable table;
	node->value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ShaderJob *>(out->job());
	ASSERT_NE(job, nullptr);

	// The job carries the color manager's default luma coefficients.
	double expected[3] = { 0.0, 0.0, 0.0 };
	project.color_manager()->get_default_luma_coefs(expected);

	const olive::NodeValueRow &values = job->get_values();
	ASSERT_TRUE(values.contains(QStringLiteral("luma_coeffs")));
	const QVector3D coeffs =
		values.value(QStringLiteral("luma_coeffs")).to_vec3();
	EXPECT_FLOAT_EQ(coeffs.x(), float(expected[0]));
	EXPECT_FLOAT_EQ(coeffs.y(), float(expected[1]));
	EXPECT_FLOAT_EQ(coeffs.z(), float(expected[2]));

	EXPECT_EQ(values.value(olive::DespillNode::k_texture_input).to_texture(),
			  tex);
}

TEST(DespillNode, ValueWithoutProjectUsesRec709LumaFallback)
{
	// A graph-less node has no project color manager; it must fall back to
	// Rec. 709 luma coefficients instead of crashing.
	olive::DespillNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::DespillNode::k_texture_input, tex);

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ShaderJob *>(out->job());
	ASSERT_NE(job, nullptr);

	const olive::NodeValueRow &values = job->get_values();
	ASSERT_TRUE(values.contains(QStringLiteral("luma_coeffs")));
	const QVector3D coeffs =
		values.value(QStringLiteral("luma_coeffs")).to_vec3();
	EXPECT_NEAR(coeffs.x(), 0.2126f, 0.0001f);
	EXPECT_NEAR(coeffs.y(), 0.7152f, 0.0001f);
	EXPECT_NEAR(coeffs.z(), 0.0722f, 0.0001f);
}
