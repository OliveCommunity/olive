#include <gtest/gtest.h>

#include <QStringList>
#include <QVector3D>
#include <QVector4D>

#include "node/color/colormanager/colormanager.h"
#include "node/color/displaytransform/displaytransform.h"
#include "node/color/ociobase/ociobase.h"
#include "node/color/ociogradingtransformlinear/ociogradingtransformlinear.h"
#include "node/color/ociogradingtransformlog/ociogradingtransformlog.h"
#include "node/color/threewaycolor/threewaycolor.h"
#include "node/color/whitebalance/whitebalance.h"
#include "node/factory.h"
#include "node/project.h"
#include "node/project/serializer/serializer.h"
#include "render/diskmanager.h"
#include "render/job/colortransformjob.h"
#include "render/job/shaderjob.h"
#include "render/texture.h"

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

olive::NodeValueRow make_texture_row(const QString &input,
								   const olive::TexturePtr &tex)
{
	olive::NodeValueRow row;
	row.insert(input, olive::NodeValue(olive::NodeValue::k_texture, tex));
	return row;
}

olive::NodeValue vec4_value(const QVector4D &v)
{
	return olive::NodeValue(olive::NodeValue::k_vec4, v);
}

olive::NodeValue bool_value(bool b)
{
	return olive::NodeValue(olive::NodeValue::k_boolean, b);
}

} // namespace

// -----------------------------------------------------------------------------
// OCIOBaseNode passthrough (exercised through DisplayTransformNode, which is
// a concrete OCIOBaseNode). Without a Project the base never has a processor,
// so Value() must pass the input texture through unchanged.
// -----------------------------------------------------------------------------

TEST(OCIOBaseNode, PassesTextureThroughWhenProcessorMissing)
{
	olive::DisplayTransformNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::OCIOBaseNode::k_texture_input, tex);

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::NodeValue out = table.get(olive::NodeValue::k_texture);
	EXPECT_EQ(out.type(), olive::NodeValue::k_texture);
	EXPECT_EQ(out.to_texture(), tex);
}

TEST(OCIOBaseNode, PushesNothingWhenTextureInputEmpty)
{
	olive::DisplayTransformNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

// -----------------------------------------------------------------------------
// ColorManager input colorspace auto-detection
// -----------------------------------------------------------------------------

TEST(ColorManager, DetectsColorspaceFromFfmpegTags)
{
	olive::ColorManager::set_up_default_config();
	olive::ProjectSerializer::initialize();
	olive::DiskManager::create_instance();

	olive::Project project;
	project.initialize();
	olive::ColorManager *cm = project.color_manager();
	ASSERT_NE(cm, nullptr);

	EXPECT_EQ(cm->get_colorspace_for_ffmpeg_tags(1, 1),
			  QStringLiteral("Rec.709 OETF"));
	EXPECT_EQ(cm->get_colorspace_for_ffmpeg_tags(1, 13),
			  QStringLiteral("sRGB OETF"));
	EXPECT_EQ(cm->get_colorspace_for_ffmpeg_tags(6, 6),
			  QStringLiteral("Rec.601 OETF (NTSC)"));
	EXPECT_EQ(cm->get_colorspace_for_ffmpeg_tags(5, 5),
			  QStringLiteral("Rec.601 OETF (PAL)"));

	// The embedded config has no PQ/HLG/Rec.2020 colorspace
	EXPECT_TRUE(cm->get_colorspace_for_ffmpeg_tags(9, 16).isEmpty());
	EXPECT_TRUE(cm->get_colorspace_for_ffmpeg_tags(9, 18).isEmpty());

	// Unknown tags never guess
	EXPECT_TRUE(cm->get_colorspace_for_ffmpeg_tags(0, 0).isEmpty());
	EXPECT_TRUE(cm->get_colorspace_for_ffmpeg_tags(2, 2).isEmpty());

	olive::DiskManager::destroy_instance();
	olive::ProjectSerializer::destroy();
}

// -----------------------------------------------------------------------------
// OCIOGradingTransformLogNode (lift/gamma/gain)
// -----------------------------------------------------------------------------

TEST(OCIOGradingTransformLogNode, InputIdsMatchOcioUniformNames)
{
	olive::OCIOGradingTransformLogNode node;

	// The input ids double as the OCIO GPU uniform names of the dynamic
	// GRADING_LOG transform (verified against OCIO's generated shader)
	EXPECT_EQ(olive::OCIOGradingTransformLogNode::k_lift_input,
			  QStringLiteral("ocio_grading_primary_brightness"));
	EXPECT_EQ(olive::OCIOGradingTransformLogNode::k_gain_input,
			  QStringLiteral("ocio_grading_primary_contrast"));
	EXPECT_EQ(olive::OCIOGradingTransformLogNode::k_gamma_input,
			  QStringLiteral("ocio_grading_primary_gamma"));

	EXPECT_TRUE(node.has_input_with_id(
		olive::OCIOGradingTransformLogNode::k_saturation_input));
	EXPECT_TRUE(
		node.has_input_with_id(olive::OCIOGradingTransformLogNode::k_pivot_input));

	// GRADING_LOG defaults (see ocio::GradingPrimary)
	EXPECT_EQ(node.get_standard_value(
				  olive::OCIOGradingTransformLogNode::k_lift_input),
			  QVariant::fromValue(QVector4D(0, 0, 0, 0)));
	EXPECT_EQ(node.get_standard_value(
				  olive::OCIOGradingTransformLogNode::k_gain_input),
			  QVariant::fromValue(QVector4D(1, 1, 1, 1)));
	EXPECT_EQ(node.get_standard_value(
				  olive::OCIOGradingTransformLogNode::k_gamma_input),
			  QVariant::fromValue(QVector4D(1, 1, 1, 1)));
	EXPECT_EQ(node.get_standard_value(
				  olive::OCIOGradingTransformLogNode::k_pivot_input),
			  QVariant::fromValue(-0.2));
}

TEST(OCIOGradingTransformLogNode, ValueMapsWheelsToOcioUniforms)
{
	olive::ColorManager::set_up_default_config();
	olive::ProjectSerializer::initialize();
	olive::DiskManager::create_instance();

	olive::Project project;
	project.initialize();

	auto *node = new olive::OCIOGradingTransformLogNode();
	node->setParent(&project);

	olive::NodeValueRow row;
	row.insert(olive::OCIOBaseNode::k_texture_input,
			   olive::NodeValue(olive::NodeValue::k_texture,
								make_dummy_texture()));
	row.insert(olive::OCIOGradingTransformLogNode::k_lift_input,
			   olive::NodeValue(olive::NodeValue::k_vec4,
								QVector4D(1.0, 0.25, 0.0, 0.0)));
	row.insert(olive::OCIOGradingTransformLogNode::k_gain_input,
			   olive::NodeValue(olive::NodeValue::k_vec4,
								QVector4D(2.0, 0.5, 1.0, 1.0)));
	row.insert(olive::OCIOGradingTransformLogNode::k_gamma_input,
			   olive::NodeValue(olive::NodeValue::k_vec4,
								QVector4D(2.0, 1.0, 0.5, 1.0)));

	olive::NodeValueTable table;
	node->value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	olive::TexturePtr tex = table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(tex);
	auto *job = dynamic_cast<olive::ColorTransformJob *>(tex->job());
	ASSERT_NE(job, nullptr);

	// Lift is additive: RGB += master
	const QVector3D lift =
		job->get(olive::OCIOGradingTransformLogNode::k_lift_input).to_vec3();
	EXPECT_FLOAT_EQ(lift.x(), 1.25f);
	EXPECT_FLOAT_EQ(lift.y(), 1.0f);
	EXPECT_FLOAT_EQ(lift.z(), 1.0f);

	// Gain multiplies: RGB *= master
	const QVector3D gain =
		job->get(olive::OCIOGradingTransformLogNode::k_gain_input).to_vec3();
	EXPECT_FLOAT_EQ(gain.x(), 1.0f);
	EXPECT_FLOAT_EQ(gain.y(), 2.0f);
	EXPECT_FLOAT_EQ(gain.z(), 2.0f);

	// Gamma multiplies: RGB *= master
	const QVector3D gamma =
		job->get(olive::OCIOGradingTransformLogNode::k_gamma_input).to_vec3();
	EXPECT_FLOAT_EQ(gamma.x(), 2.0f);
	EXPECT_FLOAT_EQ(gamma.y(), 1.0f);
	EXPECT_FLOAT_EQ(gamma.z(), 2.0f);

	olive::DiskManager::destroy_instance();
	olive::ProjectSerializer::destroy();
}

TEST(OCIOGradingTransformLogNode, FactoryCreatesNode)
{
	std::unique_ptr<olive::Node> node(
		olive::NodeFactory::create_from_factory_index(
			olive::NodeFactory::k_ocio_grading_transform_log));
	ASSERT_NE(node, nullptr);
	EXPECT_EQ(node->id(),
			  QStringLiteral("org.olivevideoeditor.Olive.ociogradingtransformlog"));
}

// -----------------------------------------------------------------------------
// WhiteBalanceNode
// -----------------------------------------------------------------------------

TEST(WhiteBalanceNode, NeutralTemperatureKeepsNearUnityGain)
{
	const QVector3D gain =
		olive::WhiteBalanceNode::get_gain_for_temperature(6500.0, 0.0);
	EXPECT_NEAR(gain.x(), 1.0f, 0.02f);
	EXPECT_FLOAT_EQ(gain.y(), 1.0f);
	EXPECT_NEAR(gain.z(), 1.0f, 0.02f);
}

TEST(WhiteBalanceNode, WarmAndCoolShiftAsExpected)
{
	const QVector3D warm =
		olive::WhiteBalanceNode::get_gain_for_temperature(3000.0, 0.0);
	EXPECT_GT(warm.x(), warm.z()); // warm light: red over blue
	EXPECT_FLOAT_EQ(warm.y(), 1.0f);

	const QVector3D cool =
		olive::WhiteBalanceNode::get_gain_for_temperature(10000.0, 0.0);
	EXPECT_GT(cool.z(), cool.x()); // cool light: blue over red
}

TEST(WhiteBalanceNode, TintMovesGreenMagentaAxis)
{
	const QVector3D magenta =
		olive::WhiteBalanceNode::get_gain_for_temperature(6500.0, -0.5);
	const QVector3D green =
		olive::WhiteBalanceNode::get_gain_for_temperature(6500.0, 0.5);
	EXPECT_LT(magenta.y(), 1.0f);
	EXPECT_GT(green.y(), 1.0f);
}

TEST(WhiteBalanceNode, FactoryCreatesNode)
{
	std::unique_ptr<olive::Node> node(
		olive::NodeFactory::create_from_factory_index(
			olive::NodeFactory::k_white_balance));
	ASSERT_NE(node, nullptr);
	EXPECT_EQ(node->id(),
			  QStringLiteral("org.olivevideoeditor.Olive.whitebalance"));
	EXPECT_TRUE(
		node->has_input_with_id(olive::WhiteBalanceNode::k_temperature_input));
	EXPECT_TRUE(node->has_input_with_id(olive::WhiteBalanceNode::k_tint_input));
}


// -----------------------------------------------------------------------------
// DisplayTransformNode
// -----------------------------------------------------------------------------

TEST(DisplayTransformNode, InputDefinitions)
{
	olive::DisplayTransformNode node;

	EXPECT_TRUE(node.has_input_with_id(olive::OCIOBaseNode::k_texture_input));
	EXPECT_TRUE(node.has_input_with_id(olive::DisplayTransformNode::k_display_input));
	EXPECT_TRUE(node.has_input_with_id(olive::DisplayTransformNode::k_view_input));
	EXPECT_TRUE(
		node.has_input_with_id(olive::DisplayTransformNode::k_direction_input));

	EXPECT_EQ(node.get_input_data_type(olive::DisplayTransformNode::k_display_input),
			  olive::NodeValue::k_combo);
	EXPECT_EQ(node.get_input_data_type(olive::DisplayTransformNode::k_view_input),
			  olive::NodeValue::k_combo);
	EXPECT_EQ(
		node.get_input_data_type(olive::DisplayTransformNode::k_direction_input),
		olive::NodeValue::k_combo);

	// Combo inputs are static UI choices: neither keyframable nor connectable.
	EXPECT_FALSE(
		node.is_input_keyframable(olive::DisplayTransformNode::k_display_input));
	EXPECT_FALSE(
		node.is_input_connectable(olive::DisplayTransformNode::k_display_input));
	EXPECT_FALSE(
		node.is_input_keyframable(olive::DisplayTransformNode::k_view_input));
	EXPECT_FALSE(
		node.is_input_connectable(olive::DisplayTransformNode::k_view_input));
	EXPECT_FALSE(
		node.is_input_keyframable(olive::DisplayTransformNode::k_direction_input));
	EXPECT_FALSE(
		node.is_input_connectable(olive::DisplayTransformNode::k_direction_input));

	EXPECT_EQ(node.get_standard_value(olive::DisplayTransformNode::k_display_input)
				  .toInt(),
			  0);
	EXPECT_EQ(node.get_standard_value(olive::DisplayTransformNode::k_view_input)
				  .toInt(),
			  0);
	EXPECT_EQ(node.get_standard_value(olive::DisplayTransformNode::k_direction_input)
				  .toInt(),
			  0);

	EXPECT_EQ(node.get_effect_input_id(), olive::OCIOBaseNode::k_texture_input);
}

TEST(DisplayTransformNode, Identity)
{
	olive::DisplayTransformNode node;

	EXPECT_EQ(node.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.displaytransform"));
	EXPECT_FALSE(node.name().isEmpty());
	EXPECT_FALSE(node.description().isEmpty());

	ASSERT_EQ(node.category().size(), 1);
	EXPECT_EQ(int(node.category().first()), int(olive::Node::k_category_color));
}

TEST(DisplayTransformNode, DisplayAndViewEmptyWithoutProject)
{
	olive::DisplayTransformNode node;

	// No ColorManager is attached, so display/view cannot be resolved.
	EXPECT_TRUE(node.get_display().isEmpty());
	EXPECT_TRUE(node.get_view().isEmpty());
	EXPECT_EQ(int(node.get_direction()), int(olive::ColorProcessor::k_normal));
}

TEST(DisplayTransformNode, ResolvesDisplayAndViewInProject)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	olive::ColorManager *manager = project.color_manager();
	ASSERT_NE(manager, nullptr);

	const QStringList displays = manager->list_available_displays();
	ASSERT_FALSE(displays.isEmpty());

	auto *node = new olive::DisplayTransformNode();
	node->setParent(&project);

	// Combo index 0 must resolve to the first available display/view.
	EXPECT_EQ(node->get_display(), displays.first());

	const QStringList views = manager->list_available_views(node->get_display());
	ASSERT_FALSE(views.isEmpty());
	EXPECT_EQ(node->get_view(), views.first());

	EXPECT_EQ(int(node->get_direction()), int(olive::ColorProcessor::k_normal));

	node->set_standard_value(olive::DisplayTransformNode::k_direction_input, 1);
	EXPECT_EQ(int(node->get_direction()), int(olive::ColorProcessor::k_inverse));
}

TEST(DisplayTransformNode, RetranslateSetsInputNames)
{
	olive::DisplayTransformNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::OCIOBaseNode::k_texture_input),
			  QStringLiteral("Input"));
	EXPECT_EQ(node.get_input_name(olive::DisplayTransformNode::k_display_input),
			  QStringLiteral("Display"));
	EXPECT_EQ(node.get_input_name(olive::DisplayTransformNode::k_view_input),
			  QStringLiteral("View"));
	EXPECT_EQ(node.get_input_name(olive::DisplayTransformNode::k_direction_input),
			  QStringLiteral("Direction"));
}

// -----------------------------------------------------------------------------
// ThreeWayColorNode (beyond the factory/default coverage in color_lut_test.cpp)
// -----------------------------------------------------------------------------

TEST(ThreeWayColorNode, ShaderCodeLoadsFragmentResource)
{
	olive::ThreeWayColorNode node;

	const olive::ShaderCode code =
		node.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("test")));

	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.vert_code().isEmpty());
}

TEST(ThreeWayColorNode, ValueWithoutTexturePushesNothing)
{
	olive::ThreeWayColorNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(ThreeWayColorNode, ValuePushesShaderJobWithDefaultLumaCoefficients)
{
	olive::ThreeWayColorNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::ThreeWayColorNode::k_texture_input, tex);

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
	EXPECT_TRUE(values.contains(olive::ThreeWayColorNode::k_texture_input));
	ASSERT_TRUE(values.contains(olive::ThreeWayColorNode::k_luma_coefficients_input));

	// Without a project the node falls back to Rec. 709 luma coefficients.
	const QVector3D coeffs =
		values.value(olive::ThreeWayColorNode::k_luma_coefficients_input).to_vec3();
	EXPECT_NEAR(coeffs.x(), 0.2126f, 0.0001f);
	EXPECT_NEAR(coeffs.y(), 0.7152f, 0.0001f);
	EXPECT_NEAR(coeffs.z(), 0.0722f, 0.0001f);
}

TEST(ThreeWayColorNode, AmountInputsDefaultToFull)
{
	olive::ThreeWayColorNode node;

	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::ThreeWayColorNode::k_shadows_amount_input)
			.toDouble(),
		1.0);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::ThreeWayColorNode::k_midtones_amount_input)
			.toDouble(),
		1.0);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::ThreeWayColorNode::k_highlights_amount_input)
			.toDouble(),
		1.0);

	EXPECT_DOUBLE_EQ(
		node.get_input_property(olive::ThreeWayColorNode::k_shadows_amount_input,
							  QStringLiteral("min"))
			.toDouble(),
		0.0);
	EXPECT_DOUBLE_EQ(
		node.get_input_property(olive::ThreeWayColorNode::k_midtones_amount_input,
							  QStringLiteral("min"))
			.toDouble(),
		0.0);
	EXPECT_DOUBLE_EQ(
		node.get_input_property(olive::ThreeWayColorNode::k_highlights_amount_input,
							  QStringLiteral("min"))
			.toDouble(),
		0.0);
}

TEST(ThreeWayColorNode, RetranslateSetsInputNames)
{
	olive::ThreeWayColorNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::ThreeWayColorNode::k_texture_input),
			  QStringLiteral("Input"));
	EXPECT_EQ(node.get_input_name(olive::ThreeWayColorNode::k_shadows_color_input),
			  QStringLiteral("Shadows"));
	EXPECT_EQ(node.get_input_name(olive::ThreeWayColorNode::k_midtones_color_input),
			  QStringLiteral("Midtones"));
	EXPECT_EQ(
		node.get_input_name(olive::ThreeWayColorNode::k_highlights_color_input),
		QStringLiteral("Highlights"));
	EXPECT_EQ(
		node.get_input_name(olive::ThreeWayColorNode::k_shadows_amount_input),
		QStringLiteral("Shadows Amount"));
	EXPECT_EQ(
		node.get_input_name(olive::ThreeWayColorNode::k_midtones_amount_input),
		QStringLiteral("Midtones Amount"));
	EXPECT_EQ(
		node.get_input_name(olive::ThreeWayColorNode::k_highlights_amount_input),
		QStringLiteral("Highlights Amount"));
}

// -----------------------------------------------------------------------------
// OCIOGradingTransformLinearNode (beyond the clamp-invariant coverage in
// color_lut_test.cpp)
// -----------------------------------------------------------------------------

TEST(GradingTransformLinear, InputDefaults)
{
	olive::OCIOGradingTransformLinearNode node;

	const QVector4D contrast =
		node.get_standard_value(
				olive::OCIOGradingTransformLinearNode::k_contrast_input)
			.value<QVector4D>();
	EXPECT_FLOAT_EQ(contrast.x(), 1.0f);
	EXPECT_FLOAT_EQ(contrast.y(), 1.0f);
	EXPECT_FLOAT_EQ(contrast.z(), 1.0f);
	EXPECT_FLOAT_EQ(contrast.w(), 1.0f);

	const QVector4D offset =
		node.get_standard_value(olive::OCIOGradingTransformLinearNode::k_offset_input)
			.value<QVector4D>();
	EXPECT_FLOAT_EQ(offset.x(), 0.0f);
	EXPECT_FLOAT_EQ(offset.y(), 0.0f);
	EXPECT_FLOAT_EQ(offset.z(), 0.0f);
	EXPECT_FLOAT_EQ(offset.w(), 0.0f);

	const QVector4D exposure =
		node.get_standard_value(
				olive::OCIOGradingTransformLinearNode::k_exposure_input)
			.value<QVector4D>();
	EXPECT_FLOAT_EQ(exposure.x(), 0.0f);
	EXPECT_FLOAT_EQ(exposure.y(), 0.0f);
	EXPECT_FLOAT_EQ(exposure.z(), 0.0f);
	EXPECT_FLOAT_EQ(exposure.w(), 0.0f);

	EXPECT_DOUBLE_EQ(
		node.get_standard_value(
				olive::OCIOGradingTransformLinearNode::k_saturation_input)
			.toDouble(),
		1.0);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::OCIOGradingTransformLinearNode::k_pivot_input)
			.toDouble(),
		0.18);

	EXPECT_FALSE(
		node.get_standard_value(
				olive::OCIOGradingTransformLinearNode::k_clamp_black_enable_input)
			.toBool());
	EXPECT_FALSE(
		node.get_standard_value(
				olive::OCIOGradingTransformLinearNode::k_clamp_white_enable_input)
			.toBool());
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(
				olive::OCIOGradingTransformLinearNode::k_clamp_black_input)
			.toDouble(),
		0.0);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(
				olive::OCIOGradingTransformLinearNode::k_clamp_white_input)
			.toDouble(),
		1.0);

	// Clamp value inputs start out disabled, matching the enable toggles.
	EXPECT_FALSE(
		node.get_input_property(
				olive::OCIOGradingTransformLinearNode::k_clamp_black_input,
				QStringLiteral("enabled"))
			.toBool());
	EXPECT_FALSE(
		node.get_input_property(
				olive::OCIOGradingTransformLinearNode::k_clamp_white_input,
				QStringLiteral("enabled"))
			.toBool());
}

TEST(GradingTransformLinear, Identity)
{
	olive::OCIOGradingTransformLinearNode node;

	EXPECT_EQ(node.id(),
			  QStringLiteral(
				  "org.olivevideoeditor.Olive.ociogradingtransformlinear"));
	EXPECT_FALSE(node.name().isEmpty());
	EXPECT_FALSE(node.description().isEmpty());

	ASSERT_EQ(node.category().size(), 1);
	EXPECT_EQ(int(node.category().first()), int(olive::Node::k_category_color));
}

TEST(GradingTransformLinear, ClampEnableTogglesEnabledProperty)
{
	olive::OCIOGradingTransformLinearNode node;

	node.set_standard_value(
		olive::OCIOGradingTransformLinearNode::k_clamp_white_enable_input, true);
	EXPECT_TRUE(
		node.get_input_property(
				olive::OCIOGradingTransformLinearNode::k_clamp_white_input,
				QStringLiteral("enabled"))
			.toBool());

	node.set_standard_value(
		olive::OCIOGradingTransformLinearNode::k_clamp_black_enable_input, true);
	EXPECT_TRUE(
		node.get_input_property(
				olive::OCIOGradingTransformLinearNode::k_clamp_black_input,
				QStringLiteral("enabled"))
			.toBool());

	node.set_standard_value(
		olive::OCIOGradingTransformLinearNode::k_clamp_white_enable_input, false);
	EXPECT_FALSE(
		node.get_input_property(
				olive::OCIOGradingTransformLinearNode::k_clamp_white_input,
				QStringLiteral("enabled"))
			.toBool());
}

TEST(GradingTransformLinear, WhiteClampMinimumFollowsStaticBlackClamp)
{
	olive::OCIOGradingTransformLinearNode node;

	// Constructor seeds the white clamp minimum just above the black clamp.
	EXPECT_DOUBLE_EQ(
		node.get_input_property(
				olive::OCIOGradingTransformLinearNode::k_clamp_white_input,
				QStringLiteral("min"))
			.toDouble(),
		0.000001);

	node.set_standard_value(
		olive::OCIOGradingTransformLinearNode::k_clamp_black_input, 0.5);
	EXPECT_DOUBLE_EQ(
		node.get_input_property(
				olive::OCIOGradingTransformLinearNode::k_clamp_white_input,
				QStringLiteral("min"))
			.toDouble(),
		0.5 + 0.000001);
}

TEST(GradingTransformLinear, WhiteClampMinimumNotUpdatedWhenBlackKeyframed)
{
	olive::OCIOGradingTransformLinearNode node;

	// With the black clamp keyframing, the static UI minimum can no longer
	// follow it; the invariant is enforced per frame in Value() instead.
	node.set_input_is_keyframing(
		olive::OCIOGradingTransformLinearNode::k_clamp_black_input, true);
	node.set_standard_value(
		olive::OCIOGradingTransformLinearNode::k_clamp_black_input, 0.5);

	EXPECT_DOUBLE_EQ(
		node.get_input_property(
				olive::OCIOGradingTransformLinearNode::k_clamp_white_input,
				QStringLiteral("min"))
			.toDouble(),
		0.000001);
}

TEST(GradingTransformLinear, ValueWithoutProcessorPushesNothing)
{
	// Without a project no color manager is attached, so no processor is ever
	// generated and Value() must push nothing even with a valid texture.
	olive::OCIOGradingTransformLinearNode node;

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::OCIOBaseNode::k_texture_input, tex);

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(GradingTransformLinear, ValueInProjectPushesColorTransformJob)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	auto *node = new olive::OCIOGradingTransformLinearNode();
	node->setParent(&project);

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::OCIOBaseNode::k_texture_input, tex);
	row.insert(olive::OCIOGradingTransformLinearNode::k_offset_input,
			   vec4_value(QVector4D(0.0f, 0.0f, 0.0f, 0.0f)));
	row.insert(olive::OCIOGradingTransformLinearNode::k_exposure_input,
			   vec4_value(QVector4D(0.0f, 0.0f, 0.0f, 0.0f)));
	row.insert(olive::OCIOGradingTransformLinearNode::k_contrast_input,
			   vec4_value(QVector4D(1.0f, 1.0f, 1.0f, 1.0f)));
	row.insert(olive::OCIOGradingTransformLinearNode::k_clamp_black_enable_input,
			   bool_value(false));
	row.insert(olive::OCIOGradingTransformLinearNode::k_clamp_white_enable_input,
			   bool_value(false));

	olive::NodeValueTable table;
	node->value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ColorTransformJob *>(out->job());
	ASSERT_NE(job, nullptr);
	EXPECT_NE(job->get_color_processor(), nullptr);

	const olive::NodeValueRow &values = job->get_values();

	// Defaults convert to neutral vec3s for OCIO.
	const QVector3D offset =
		values.value(olive::OCIOGradingTransformLinearNode::k_offset_input)
			.to_vec3();
	EXPECT_FLOAT_EQ(offset.x(), 0.0f);
	EXPECT_FLOAT_EQ(offset.y(), 0.0f);
	EXPECT_FLOAT_EQ(offset.z(), 0.0f);

	const QVector3D exposure =
		values.value(olive::OCIOGradingTransformLinearNode::k_exposure_input)
			.to_vec3();
	EXPECT_FLOAT_EQ(exposure.x(), 1.0f);
	EXPECT_FLOAT_EQ(exposure.y(), 1.0f);
	EXPECT_FLOAT_EQ(exposure.z(), 1.0f);

	const QVector3D contrast =
		values.value(olive::OCIOGradingTransformLinearNode::k_contrast_input)
			.to_vec3();
	EXPECT_FLOAT_EQ(contrast.x(), 1.0f);
	EXPECT_FLOAT_EQ(contrast.y(), 1.0f);
	EXPECT_FLOAT_EQ(contrast.z(), 1.0f);

	// Disabled clamps are replaced with OCIO's "no clamp" sentinels.
	ASSERT_TRUE(
		values.contains(olive::OCIOGradingTransformLinearNode::k_clamp_black_input));
	EXPECT_LT(values
				  .value(olive::OCIOGradingTransformLinearNode::k_clamp_black_input)
				  .to_double(),
			  -1e300);
	ASSERT_TRUE(
		values.contains(olive::OCIOGradingTransformLinearNode::k_clamp_white_input));
	EXPECT_GT(values
				  .value(olive::OCIOGradingTransformLinearNode::k_clamp_white_input)
				  .to_double(),
			  1e300);
}

TEST(GradingTransformLinear, ValueAppliesMasterChannelMath)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	auto *node = new olive::OCIOGradingTransformLinearNode();
	node->setParent(&project);

	olive::TexturePtr tex = make_dummy_texture();
	olive::NodeValueRow row =
		make_texture_row(olive::OCIOBaseNode::k_texture_input, tex);
	// Layout is {master, red, green, blue}.
	row.insert(olive::OCIOGradingTransformLinearNode::k_offset_input,
			   vec4_value(QVector4D(0.1f, 0.2f, 0.3f, 0.4f)));
	row.insert(olive::OCIOGradingTransformLinearNode::k_exposure_input,
			   vec4_value(QVector4D(1.0f, 0.0f, 0.0f, 0.0f)));
	row.insert(olive::OCIOGradingTransformLinearNode::k_contrast_input,
			   vec4_value(QVector4D(2.0f, 0.5f, 1.0f, 1.0f)));
	row.insert(olive::OCIOGradingTransformLinearNode::k_clamp_black_enable_input,
			   bool_value(false));
	row.insert(olive::OCIOGradingTransformLinearNode::k_clamp_white_enable_input,
			   bool_value(false));

	olive::NodeValueTable table;
	node->value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = static_cast<olive::ColorTransformJob *>(out->job());
	ASSERT_NE(job, nullptr);

	const olive::NodeValueRow &values = job->get_values();

	// Offset: master is added to each channel.
	const QVector3D offset =
		values.value(olive::OCIOGradingTransformLinearNode::k_offset_input)
			.to_vec3();
	EXPECT_NEAR(offset.x(), 0.3f, 0.0001f);
	EXPECT_NEAR(offset.y(), 0.4f, 0.0001f);
	EXPECT_NEAR(offset.z(), 0.5f, 0.0001f);

	// Exposure: channels become 2^(master + channel) gain values.
	const QVector3D exposure =
		values.value(olive::OCIOGradingTransformLinearNode::k_exposure_input)
			.to_vec3();
	EXPECT_NEAR(exposure.x(), 2.0f, 0.0001f);
	EXPECT_NEAR(exposure.y(), 2.0f, 0.0001f);
	EXPECT_NEAR(exposure.z(), 2.0f, 0.0001f);

	// Contrast: master multiplies each channel.
	const QVector3D contrast =
		values.value(olive::OCIOGradingTransformLinearNode::k_contrast_input)
			.to_vec3();
	EXPECT_NEAR(contrast.x(), 1.0f, 0.0001f);
	EXPECT_NEAR(contrast.y(), 2.0f, 0.0001f);
	EXPECT_NEAR(contrast.z(), 2.0f, 0.0001f);
}

TEST(GradingTransformLinear, RetranslateSetsInputNames)
{
	olive::OCIOGradingTransformLinearNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::OCIOBaseNode::k_texture_input),
			  QStringLiteral("Input"));
	EXPECT_EQ(
		node.get_input_name(olive::OCIOGradingTransformLinearNode::k_contrast_input),
		QStringLiteral("Contrast"));
	EXPECT_EQ(
		node.get_input_name(olive::OCIOGradingTransformLinearNode::k_offset_input),
		QStringLiteral("Offset"));
	EXPECT_EQ(
		node.get_input_name(olive::OCIOGradingTransformLinearNode::k_exposure_input),
		QStringLiteral("Exposure"));
	EXPECT_EQ(
		node.get_input_name(
			olive::OCIOGradingTransformLinearNode::k_saturation_input),
		QStringLiteral("Saturation"));
	EXPECT_EQ(
		node.get_input_name(olive::OCIOGradingTransformLinearNode::k_pivot_input),
		QStringLiteral("Pivot"));
	EXPECT_EQ(
		node.get_input_name(
			olive::OCIOGradingTransformLinearNode::k_clamp_black_enable_input),
		QStringLiteral("Enable Black Clamp"));
	EXPECT_EQ(
		node.get_input_name(
			olive::OCIOGradingTransformLinearNode::k_clamp_black_input),
		QStringLiteral("Black Clamp"));
	EXPECT_EQ(
		node.get_input_name(
			olive::OCIOGradingTransformLinearNode::k_clamp_white_enable_input),
		QStringLiteral("Enable White Clamp"));
	EXPECT_EQ(
		node.get_input_name(
			olive::OCIOGradingTransformLinearNode::k_clamp_white_input),
		QStringLiteral("White Clamp"));
}
