#include <gtest/gtest.h>

#include <QMatrix4x4>
#include <QPointF>
#include <QStringList>
#include <QVector2D>
#include <QVector3D>

#include "node/color/colormanager/colormanager.h"
#include "node/distort/cornerpin/cornerpindistortnode.h"
#include "node/distort/crop/cropdistortnode.h"
#include "node/distort/flip/flipdistortnode.h"
#include "node/distort/mask/mask.h"
#include "node/distort/ripple/rippledistortnode.h"
#include "node/distort/swirl/swirldistortnode.h"
#include "node/distort/tile/tiledistortnode.h"
#include "node/distort/transform/transformdistortnode.h"
#include "node/distort/wave/wavedistortnode.h"
#include "node/filter/blur/blur.h"
#include "node/generator/polygon/polygon.h"
#include "node/generator/shape/generatorwithmerge.h"
#include "node/globals.h"
#include "node/project.h"
#include "node/traverser.h"
#include "olive/core/util/color.h"
#include "render/job/generatejob.h"
#include "render/job/shaderjob.h"
#include "render/loopmode.h"
#include "render/texture.h"
#include "render/videoparams.h"
#include "widget/slider/floatslider.h"

namespace
{

// Node that pushes a fixed dummy texture, used to feed the texture input of
// distort nodes without any renderer (same pattern as node_generator_test).
class ConstantTextureNode : public olive::Node {
public:
	ConstantTextureNode() = default;

	NODE_DEFAULT_FUNCTIONS(ConstantTextureNode)

	virtual QString name() const override
	{
		return QStringLiteral("Test Texture");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.oak.test.distort_texture");
	}

	virtual QVector<CategoryID> category() const override
	{
		return { k_category_generator };
	}

	void set_texture(const olive::TexturePtr &texture)
	{
		texture_ = texture;
	}

	virtual void value(const olive::NodeValueRow &value,
					   const olive::NodeGlobals &globals,
					   olive::NodeValueTable *table) const override
	{
		Q_UNUSED(value)
		Q_UNUSED(globals)

		table->push(olive::NodeValue(olive::NodeValue::k_texture, texture_, this));
	}

private:
	olive::TexturePtr texture_;
};

template <typename T> T *add_node(olive::Project *project)
{
	T *node = new T();
	node->setParent(project);
	return node;
}

olive::TimeRange first_frame()
{
	return olive::TimeRange(olive::Rational(0), olive::Rational(1, 30));
}

// A fresh traverser per call: NodeTraverser caches tables per node/range, so
// reusing one would return stale results after changing standard values.
olive::NodeValueTable generate_table(const olive::Node *node,
									const olive::VideoParams &vparams)
{
	olive::NodeTraverser traverser;
	traverser.set_cache_video_params(vparams);
	return traverser.generate_table(node, first_frame());
}

// A "dummy" texture has no renderer backend and is therefore safe to pass
// around in a headless, CPU-only test.
olive::TexturePtr make_dummy_texture(int width, int height)
{
	return std::make_shared<olive::Texture>(
		olive::VideoParams(width, height, olive::core::PixelFormat::u8,
						   olive::VideoParams::k_rgba_channel_count));
}

olive::VideoParams sequence_params(int width, int height)
{
	return olive::VideoParams(width, height, olive::core::PixelFormat::f32,
							  olive::VideoParams::k_rgba_channel_count);
}

olive::NodeValue texture_value(const olive::TexturePtr &texture)
{
	return olive::NodeValue(olive::NodeValue::k_texture, texture);
}

olive::NodeValue float_value(double v)
{
	return olive::NodeValue(olive::NodeValue::k_float, v);
}

olive::NodeValue bool_value(bool b)
{
	return olive::NodeValue(olive::NodeValue::k_boolean, b);
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

olive::TexturePtr get_output_texture(const olive::NodeValueTable &table)
{
	return table.get(olive::NodeValue::k_texture).to_texture();
}

} // namespace

// -----------------------------------------------------------------------------
// TransformDistortNode
// -----------------------------------------------------------------------------

TEST(TransformDistortNode, MetadataIsCorrect)
{
	olive::TransformDistortNode node;
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.transform"));
	EXPECT_EQ(node.name(), QStringLiteral("Transform"));
	// ShortName() overrides MatrixGenerator's "Ortho"
	EXPECT_EQ(node.short_name(), QStringLiteral("Transform"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_distort));

	EXPECT_TRUE(node.get_flags() & olive::Node::k_video_effect);
	EXPECT_EQ(node.get_effect_input_id(), olive::TransformDistortNode::k_texture_input);
}

TEST(TransformDistortNode, InputDefinitionsAndDefaults)
{
	olive::TransformDistortNode node;

	// Texture is prepended, so it is the primary effect input and cannot be
	// keyframed
	ASSERT_TRUE(node.has_input_with_id(olive::TransformDistortNode::k_texture_input));
	EXPECT_EQ(int(node.get_input_data_type(olive::TransformDistortNode::k_texture_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_FALSE(
		node.is_input_keyframable(olive::TransformDistortNode::k_texture_input));

	ASSERT_TRUE(node.has_input_with_id(olive::TransformDistortNode::k_parent_input));
	EXPECT_EQ(int(node.get_input_data_type(olive::TransformDistortNode::k_parent_input)),
			  int(olive::NodeValue::k_matrix));

	ASSERT_TRUE(node.has_input_with_id(olive::TransformDistortNode::k_autoscale_input));
	EXPECT_EQ(
		int(node.get_input_data_type(olive::TransformDistortNode::k_autoscale_input)),
		int(olive::NodeValue::k_combo));
	EXPECT_EQ(node.get_standard_value(olive::TransformDistortNode::k_autoscale_input)
				  .toInt(),
			  int(olive::TransformDistortNode::k_auto_scale_none));

	ASSERT_TRUE(
		node.has_input_with_id(olive::TransformDistortNode::k_interpolation_input));
	EXPECT_EQ(int(node.get_input_data_type(
				  olive::TransformDistortNode::k_interpolation_input)),
			  int(olive::NodeValue::k_combo));
	// 2 = mipmapped bilinear
	EXPECT_EQ(node.get_standard_value(
				  olive::TransformDistortNode::k_interpolation_input)
				  .toInt(),
			  int(olive::Texture::k_mipmapped_linear));

	// MatrixGenerator inputs are inherited
	EXPECT_TRUE(node.has_input_with_id(olive::MatrixGenerator::k_position_input));
	EXPECT_TRUE(node.has_input_with_id(olive::MatrixGenerator::k_rotation_input));
	EXPECT_TRUE(node.has_input_with_id(olive::MatrixGenerator::k_scale_input));
	EXPECT_TRUE(node.has_input_with_id(olive::MatrixGenerator::k_uniform_scale_input));
	EXPECT_TRUE(node.has_input_with_id(olive::MatrixGenerator::k_anchor_input));
}

TEST(TransformDistortNode, RetranslateSetsNamesAndComboStrings)
{
	olive::TransformDistortNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::TransformDistortNode::k_texture_input),
			  QStringLiteral("Texture"));
	EXPECT_EQ(node.get_input_name(olive::TransformDistortNode::k_parent_input),
			  QStringLiteral("Parent"));
	EXPECT_EQ(node.get_input_name(olive::TransformDistortNode::k_autoscale_input),
			  QStringLiteral("Auto-Scale"));
	EXPECT_EQ(node.get_input_name(olive::TransformDistortNode::k_interpolation_input),
			  QStringLiteral("Interpolation"));

	// Inherited names from MatrixGenerator
	EXPECT_EQ(node.get_input_name(olive::MatrixGenerator::k_position_input),
			  QStringLiteral("Position"));
	EXPECT_EQ(node.get_input_name(olive::MatrixGenerator::k_anchor_input),
			  QStringLiteral("Anchor Point"));

	const QStringList autoscale = node.get_combo_box_strings(
		olive::TransformDistortNode::k_autoscale_input);
	ASSERT_EQ(autoscale.size(), 4);
	EXPECT_EQ(autoscale.at(int(olive::TransformDistortNode::k_auto_scale_none)),
			  QStringLiteral("None"));
	EXPECT_EQ(autoscale.at(int(olive::TransformDistortNode::k_auto_scale_fit)),
			  QStringLiteral("Fit"));
	EXPECT_EQ(autoscale.at(int(olive::TransformDistortNode::k_auto_scale_fill)),
			  QStringLiteral("Fill"));
	EXPECT_EQ(autoscale.at(int(olive::TransformDistortNode::k_auto_scale_stretch)),
			  QStringLiteral("Stretch"));

	const QStringList interpolation = node.get_combo_box_strings(
		olive::TransformDistortNode::k_interpolation_input);
	ASSERT_EQ(interpolation.size(), 3);
	EXPECT_EQ(interpolation.at(int(olive::Texture::k_nearest)),
			  QStringLiteral("Nearest Neighbor"));
	EXPECT_EQ(interpolation.at(int(olive::Texture::k_linear)),
			  QStringLiteral("Bilinear"));
	EXPECT_EQ(interpolation.at(int(olive::Texture::k_mipmapped_linear)),
			  QStringLiteral("Mipmapped Bilinear"));
}

TEST(TransformDistortNode, GetShaderCodeReturnsEmptyCode)
{
	olive::TransformDistortNode node;

	// The transform is applied through the ove_mvpmat uniform of the default
	// shader, so the node provides no shader code of its own
	const olive::ShaderCode code = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("anything")));
	EXPECT_TRUE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.vert_code().isEmpty());
}

TEST(TransformDistortNode, AdjustMatrixByResolutionsIdentityWhenMatching)
{
	// With identical sequence and texture resolutions, no offset and an
	// identity input matrix, the adjusted matrix must remain identity (the
	// scale to clip space and back cancels out)
	const QMatrix4x4 adjusted = olive::TransformDistortNode::adjust_matrix_by_resolutions(
		QMatrix4x4(), QVector2D(1024.0f, 1024.0f), QVector2D(1024.0f, 1024.0f),
		QVector2D(0.0f, 0.0f), olive::TransformDistortNode::k_auto_scale_none);
	EXPECT_TRUE(adjusted.isIdentity());
}

TEST(TransformDistortNode, AdjustMatrixByResolutionsAppliesOffset)
{
	// The offset lives in texture pixel space: with matching 1024x1024
	// resolutions, offsetting by (128, 256) maps the origin to clip space
	// (128*2/1024, 256*2/1024) = (0.25, 0.5)
	const QMatrix4x4 adjusted = olive::TransformDistortNode::adjust_matrix_by_resolutions(
		QMatrix4x4(), QVector2D(1024.0f, 1024.0f), QVector2D(1024.0f, 1024.0f),
		QVector2D(128.0f, 256.0f), olive::TransformDistortNode::k_auto_scale_none);

	const QVector3D mapped = adjusted.map(QVector3D(0.0f, 0.0f, 0.0f));
	EXPECT_FLOAT_EQ(mapped.x(), 0.25f);
	EXPECT_FLOAT_EQ(mapped.y(), 0.5f);
	EXPECT_FLOAT_EQ(mapped.z(), 0.0f);
}

TEST(TransformDistortNode, AdjustMatrixByResolutionsScalesToClipSpace)
{
	// Without auto-scale a 512x256 texture in a 1024x512 sequence covers only
	// half the frame in each axis
	const QMatrix4x4 adjusted = olive::TransformDistortNode::adjust_matrix_by_resolutions(
		QMatrix4x4(), QVector2D(1024.0f, 512.0f), QVector2D(512.0f, 256.0f),
		QVector2D(0.0f, 0.0f), olive::TransformDistortNode::k_auto_scale_none);

	const QVector3D corner = adjusted.map(QVector3D(1.0f, 1.0f, 0.0f));
	EXPECT_FLOAT_EQ(corner.x(), 0.5f);
	EXPECT_FLOAT_EQ(corner.y(), 0.5f);
}

TEST(TransformDistortNode, AdjustMatrixByResolutionsStretchFillsSequence)
{
	// Stretch distorts the texture to the sequence aspect ratio exactly
	const QMatrix4x4 adjusted = olive::TransformDistortNode::adjust_matrix_by_resolutions(
		QMatrix4x4(), QVector2D(1024.0f, 512.0f), QVector2D(512.0f, 256.0f),
		QVector2D(0.0f, 0.0f), olive::TransformDistortNode::k_auto_scale_stretch);

	const QVector3D corner = adjusted.map(QVector3D(1.0f, 1.0f, 0.0f));
	EXPECT_FLOAT_EQ(corner.x(), 1.0f);
	EXPECT_FLOAT_EQ(corner.y(), 1.0f);
}

TEST(TransformDistortNode, AdjustMatrixByResolutionsFitWideFootage)
{
	// Footage wider than the sequence (AR 4.0 in AR 2.0) is scaled by width,
	// leaving letterbox bars: the vertical clip extent shrinks to 0.5
	const QMatrix4x4 fit = olive::TransformDistortNode::adjust_matrix_by_resolutions(
		QMatrix4x4(), QVector2D(1024.0f, 512.0f), QVector2D(1024.0f, 256.0f),
		QVector2D(0.0f, 0.0f), olive::TransformDistortNode::k_auto_scale_fit);

	const QVector3D corner = fit.map(QVector3D(1.0f, 1.0f, 0.0f));
	EXPECT_FLOAT_EQ(corner.x(), 1.0f);
	EXPECT_FLOAT_EQ(corner.y(), 0.5f);
}

TEST(TransformDistortNode, AdjustMatrixByResolutionsFillWideFootage)
{
	// Fill scales the same footage by height instead, cropping the sides
	const QMatrix4x4 fill = olive::TransformDistortNode::adjust_matrix_by_resolutions(
		QMatrix4x4(), QVector2D(1024.0f, 512.0f), QVector2D(1024.0f, 256.0f),
		QVector2D(0.0f, 0.0f), olive::TransformDistortNode::k_auto_scale_fill);

	const QVector3D corner = fill.map(QVector3D(1.0f, 1.0f, 0.0f));
	EXPECT_FLOAT_EQ(corner.x(), 2.0f);
	EXPECT_FLOAT_EQ(corner.y(), 1.0f);
}

TEST(TransformDistortNode, AdjustMatrixByResolutionsFitTallFootage)
{
	// Footage narrower than the sequence (AR 0.5 in AR 2.0) is scaled by
	// height, leaving pillarbox bars
	const QMatrix4x4 fit = olive::TransformDistortNode::adjust_matrix_by_resolutions(
		QMatrix4x4(), QVector2D(1024.0f, 512.0f), QVector2D(256.0f, 512.0f),
		QVector2D(0.0f, 0.0f), olive::TransformDistortNode::k_auto_scale_fit);

	const QVector3D corner = fit.map(QVector3D(1.0f, 1.0f, 0.0f));
	EXPECT_FLOAT_EQ(corner.x(), 0.25f);
	EXPECT_FLOAT_EQ(corner.y(), 1.0f);
}

TEST(TransformDistortNode, ValueWithoutTexturePushesMatrixOnly)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TransformDistortNode>(&project);

	olive::NodeValueTable table = generate_table(node, sequence_params(1920, 1080));

	// The generated matrix is always pushed; with no texture connected the
	// re-pushed texture value is a null texture
	const olive::NodeValue matrix = table.get(olive::NodeValue::k_matrix);
	ASSERT_EQ(int(matrix.type()), int(olive::NodeValue::k_matrix));
	EXPECT_TRUE(matrix.to_matrix().isIdentity());

	EXPECT_TRUE(table.get(olive::NodeValue::k_texture).to_texture() == nullptr);
}

TEST(TransformDistortNode, ValueWithIdentityTransformPassesTextureThrough)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TransformDistortNode>(&project);
	auto *constant = add_node<ConstantTextureNode>(&project);

	// Texture matching the sequence resolution with default transform values
	// produces an identity adjusted matrix, which the node treats as a no-op
	const olive::TexturePtr base = make_dummy_texture(1024, 1024);
	constant->set_texture(base);
	olive::Node::connect_edge(
		constant,
		olive::NodeInput(node, olive::TransformDistortNode::k_texture_input));

	olive::NodeValueTable table = generate_table(node, sequence_params(1024, 1024));

	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	EXPECT_EQ(out, base);
	EXPECT_FALSE(out->is_job());

	EXPECT_TRUE(table.get(olive::NodeValue::k_matrix).to_matrix().isIdentity());
}

TEST(TransformDistortNode, ValueWithTexturePushesMatrixJob)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TransformDistortNode>(&project);
	auto *constant = add_node<ConstantTextureNode>(&project);

	// A 64x48 texture in a 1920x1080 sequence yields a non-identity matrix
	const olive::TexturePtr base = make_dummy_texture(64, 48);
	constant->set_texture(base);
	olive::Node::connect_edge(
		constant,
		olive::NodeInput(node, olive::TransformDistortNode::k_texture_input));

	olive::NodeValueTable table = generate_table(node, sequence_params(1920, 1080));

	olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	// The job adopts the sequence resolution, not the texture's, since the
	// transform may change the apparent size
	EXPECT_EQ(out->params().width(), 1920);
	EXPECT_EQ(out->params().height(), 1080);

	auto *job = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(job);

	// The original texture is fed in as ove_maintex
	EXPECT_EQ(job->get(QStringLiteral("ove_maintex")).to_texture(), base);

	// The mvp matrix scales the texture into sequence clip space:
	// 64/1920 on X and 48/1080 on Y
	const QMatrix4x4 mvp = job->get(QStringLiteral("ove_mvpmat")).to_matrix();
	EXPECT_NEAR(mvp(0, 0), 64.0 / 1920.0, 1e-6);
	EXPECT_NEAR(mvp(1, 1), 48.0 / 1080.0, 1e-6);
	EXPECT_FLOAT_EQ(mvp(2, 2), 1.0f);
	EXPECT_FLOAT_EQ(mvp(3, 3), 1.0f);

	// The raw generated matrix (identity here) is pushed alongside the job
	EXPECT_TRUE(table.get(olive::NodeValue::k_matrix).to_matrix().isIdentity());

	// Interpolation defaults to mipmapped bilinear and follows the input
	EXPECT_EQ(int(job->get_interpolation(QStringLiteral("ove_maintex"))),
			  int(olive::Texture::k_mipmapped_linear));

	node->set_standard_value(olive::TransformDistortNode::k_interpolation_input,
						   int(olive::Texture::k_nearest));
	table = generate_table(node, sequence_params(1920, 1080));
	out = get_output_texture(table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());
	job = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(job);
	EXPECT_EQ(int(job->get_interpolation(QStringLiteral("ove_maintex"))),
			  int(olive::Texture::k_nearest));
}

TEST(TransformDistortNode, ValueBakesPositionIntoJobMatrix)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TransformDistortNode>(&project);
	node->set_standard_value(olive::MatrixGenerator::k_position_input,
						   QVector2D(100.0f, 50.0f));

	auto *constant = add_node<ConstantTextureNode>(&project);
	constant->set_texture(make_dummy_texture(64, 48));
	olive::Node::connect_edge(
		constant,
		olive::NodeInput(node, olive::TransformDistortNode::k_texture_input));

	olive::NodeValueTable table = generate_table(node, sequence_params(1920, 1080));

	// The table matrix is the pure transform: a 100x50 pixel translation
	const QMatrix4x4 generated = table.get(olive::NodeValue::k_matrix).to_matrix();
	const QVector3D raw = generated.map(QVector3D(0.0f, 0.0f, 0.0f));
	EXPECT_FLOAT_EQ(raw.x(), 100.0f);
	EXPECT_FLOAT_EQ(raw.y(), 50.0f);

	// The job matrix expresses the same translation in clip space
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());
	auto *job = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(job);

	const QVector3D clip = job->get(QStringLiteral("ove_mvpmat"))
							   .to_matrix()
							   .map(QVector3D(0.0f, 0.0f, 0.0f));
	EXPECT_NEAR(clip.x(), 100.0 * 2.0 / 1920.0, 1e-6);
	EXPECT_NEAR(clip.y(), 50.0 * 2.0 / 1080.0, 1e-6);
}

// -----------------------------------------------------------------------------
// CropDistortNode
// -----------------------------------------------------------------------------

TEST(CropDistortNode, MetadataIsCorrect)
{
	olive::CropDistortNode node;
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.crop"));
	EXPECT_EQ(node.name(), QStringLiteral("Crop"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_distort));

	EXPECT_TRUE(node.get_flags() & olive::Node::k_video_effect);
	EXPECT_EQ(node.get_effect_input_id(), olive::CropDistortNode::k_texture_input);
}

TEST(CropDistortNode, InputDefinitionsAndDefaults)
{
	olive::CropDistortNode node;

	EXPECT_EQ(int(node.get_input_data_type(olive::CropDistortNode::k_texture_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_FALSE(node.is_input_keyframable(olive::CropDistortNode::k_texture_input));

	// All four sides are 0..1 percentage sliders defaulting to zero
	const QString sides[] = { olive::CropDistortNode::k_left_input,
							  olive::CropDistortNode::k_top_input,
							  olive::CropDistortNode::k_right_input,
							  olive::CropDistortNode::k_bottom_input };
	for (const QString &side : sides) {
		EXPECT_EQ(int(node.get_input_data_type(side)), int(olive::NodeValue::k_float));
		EXPECT_DOUBLE_EQ(node.get_standard_value(side).toDouble(), 0.0);
		EXPECT_DOUBLE_EQ(
			node.get_input_property(side, QStringLiteral("min")).toDouble(), 0.0);
		EXPECT_DOUBLE_EQ(
			node.get_input_property(side, QStringLiteral("max")).toDouble(), 1.0);
		EXPECT_EQ(node.get_input_property(side, QStringLiteral("view")).toInt(),
				  int(olive::slider::k_percentage));
	}

	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::CropDistortNode::k_feather_input).toDouble(),
		0.0);
	EXPECT_DOUBLE_EQ(node.get_input_property(olive::CropDistortNode::k_feather_input,
										   QStringLiteral("min"))
						 .toDouble(),
					 0.0);
}

TEST(CropDistortNode, RetranslateSetsInputNames)
{
	olive::CropDistortNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::CropDistortNode::k_texture_input),
			  QStringLiteral("Texture"));
	EXPECT_EQ(node.get_input_name(olive::CropDistortNode::k_left_input),
			  QStringLiteral("Left"));
	EXPECT_EQ(node.get_input_name(olive::CropDistortNode::k_top_input),
			  QStringLiteral("Top"));
	EXPECT_EQ(node.get_input_name(olive::CropDistortNode::k_right_input),
			  QStringLiteral("Right"));
	EXPECT_EQ(node.get_input_name(olive::CropDistortNode::k_bottom_input),
			  QStringLiteral("Bottom"));
	EXPECT_EQ(node.get_input_name(olive::CropDistortNode::k_feather_input),
			  QStringLiteral("Feather"));
}

TEST(CropDistortNode, GetShaderCodeLoadsCropShader)
{
	olive::CropDistortNode node;

	const olive::ShaderCode code = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("anything")));
	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("left_in")));
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("feather_in")));
	EXPECT_TRUE(code.vert_code().isEmpty());
}

TEST(CropDistortNode, ValueWithoutTexturePushesNothing)
{
	olive::CropDistortNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(CropDistortNode, ValueWithZeroCropPassesTextureThrough)
{
	olive::CropDistortNode node;

	const olive::TexturePtr tex = make_dummy_texture(120, 80);
	olive::NodeValueTable table;
	node.value(make_texture_row(olive::CropDistortNode::k_texture_input, tex),
			   olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	EXPECT_EQ(out, tex);
	EXPECT_FALSE(out->is_job());
}

TEST(CropDistortNode, ValueWithOnlyFeatherPassesTextureThrough)
{
	olive::CropDistortNode node;

	// NOTE: the node only checks the four crop sides when deciding to run the
	// shader; a feather without any crop is silently ignored
	const olive::TexturePtr tex = make_dummy_texture(120, 80);
	olive::NodeValueRow row =
		make_texture_row(olive::CropDistortNode::k_texture_input, tex);
	row.insert(olive::CropDistortNode::k_feather_input, float_value(5.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	EXPECT_EQ(out, tex);
	EXPECT_FALSE(out->is_job());
}

TEST(CropDistortNode, ValueWithCropPushesShaderJob)
{
	olive::CropDistortNode node;

	const olive::TexturePtr tex = make_dummy_texture(120, 80);
	olive::NodeValueRow row =
		make_texture_row(olive::CropDistortNode::k_texture_input, tex);
	row.insert(olive::CropDistortNode::k_left_input, float_value(0.25));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	// The job reuses the input texture's params
	EXPECT_EQ(out->params().width(), tex->params().width());
	EXPECT_EQ(out->params().height(), tex->params().height());

	auto *job = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(job);
	EXPECT_DOUBLE_EQ(job->get(olive::CropDistortNode::k_left_input).to_double(),
					 0.25);
	EXPECT_EQ(job->get(QStringLiteral("resolution_in")).to_vec2(),
			  QVector2D(120.0f, 80.0f));
}

// -----------------------------------------------------------------------------
// FlipDistortNode
// -----------------------------------------------------------------------------

TEST(FlipDistortNode, MetadataIsCorrect)
{
	olive::FlipDistortNode node;
	// NOTE: unlike most Olive nodes ("org.olivevideoeditor.Olive.*"), the
	// domain (inconsistent ID, documented here as a suspected bug)
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.flip"));
	EXPECT_EQ(node.name(), QStringLiteral("Flip"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_distort));

	EXPECT_TRUE(node.get_flags() & olive::Node::k_video_effect);
	EXPECT_EQ(node.get_effect_input_id(), olive::FlipDistortNode::k_texture_input);
}

TEST(FlipDistortNode, InputDefaults)
{
	olive::FlipDistortNode node;

	EXPECT_EQ(int(node.get_input_data_type(olive::FlipDistortNode::k_texture_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_FALSE(node.is_input_keyframable(olive::FlipDistortNode::k_texture_input));

	EXPECT_EQ(
		int(node.get_input_data_type(olive::FlipDistortNode::k_horizontal_input)),
		int(olive::NodeValue::k_boolean));
	EXPECT_FALSE(node.get_standard_value(olive::FlipDistortNode::k_horizontal_input)
					 .toBool());
	EXPECT_EQ(int(node.get_input_data_type(olive::FlipDistortNode::k_vertical_input)),
			  int(olive::NodeValue::k_boolean));
	EXPECT_FALSE(node.get_standard_value(olive::FlipDistortNode::k_vertical_input)
					 .toBool());
}

TEST(FlipDistortNode, RetranslateSetsInputNames)
{
	olive::FlipDistortNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::FlipDistortNode::k_texture_input),
			  QStringLiteral("Input"));
	EXPECT_EQ(node.get_input_name(olive::FlipDistortNode::k_horizontal_input),
			  QStringLiteral("Horizontal"));
	EXPECT_EQ(node.get_input_name(olive::FlipDistortNode::k_vertical_input),
			  QStringLiteral("Vertical"));
}

TEST(FlipDistortNode, GetShaderCodeLoadsFlipShader)
{
	olive::FlipDistortNode node;

	const olive::ShaderCode code = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("anything")));
	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("horiz_in")));
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("vert_in")));
}

TEST(FlipDistortNode, ValueWithoutTexturePushesNothing)
{
	olive::FlipDistortNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(FlipDistortNode, ValueWithNoFlipPassesTextureThrough)
{
	olive::FlipDistortNode node;

	const olive::TexturePtr tex = make_dummy_texture(64, 48);
	olive::NodeValueTable table;
	node.value(make_texture_row(olive::FlipDistortNode::k_texture_input, tex),
			   olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	EXPECT_EQ(out, tex);
	EXPECT_FALSE(out->is_job());
}

TEST(FlipDistortNode, ValueWithFlipPushesShaderJob)
{
	olive::FlipDistortNode node;

	const olive::TexturePtr tex = make_dummy_texture(64, 48);
	olive::NodeValueRow row =
		make_texture_row(olive::FlipDistortNode::k_texture_input, tex);
	row.insert(olive::FlipDistortNode::k_horizontal_input, bool_value(true));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());
	EXPECT_EQ(out->params().width(), tex->params().width());

	auto *job = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(job);
	EXPECT_TRUE(job->get(olive::FlipDistortNode::k_horizontal_input).to_bool());
	EXPECT_FALSE(job->get(olive::FlipDistortNode::k_vertical_input).to_bool());

	// Vertical flip on its own also triggers the shader
	row.insert(olive::FlipDistortNode::k_horizontal_input, bool_value(false));
	row.insert(olive::FlipDistortNode::k_vertical_input, bool_value(true));

	olive::NodeValueTable vertical_table;
	node.value(row, olive::NodeGlobals(), &vertical_table);

	ASSERT_EQ(vertical_table.count(), 1);
	out = get_output_texture(vertical_table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());
	job = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(job);
	EXPECT_FALSE(job->get(olive::FlipDistortNode::k_horizontal_input).to_bool());
	EXPECT_TRUE(job->get(olive::FlipDistortNode::k_vertical_input).to_bool());
}

// -----------------------------------------------------------------------------
// CornerPinDistortNode
// -----------------------------------------------------------------------------

TEST(CornerPinDistortNode, MetadataIsCorrect)
{
	olive::CornerPinDistortNode node;
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.cornerpin"));
	EXPECT_EQ(node.name(), QStringLiteral("Corner Pin"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_distort));

	EXPECT_TRUE(node.get_flags() & olive::Node::k_video_effect);
	EXPECT_EQ(node.get_effect_input_id(), olive::CornerPinDistortNode::k_texture_input);
}

TEST(CornerPinDistortNode, InputDefaults)
{
	olive::CornerPinDistortNode node;

	EXPECT_EQ(
		int(node.get_input_data_type(olive::CornerPinDistortNode::k_texture_input)),
		int(olive::NodeValue::k_texture));
	EXPECT_FALSE(
		node.is_input_keyframable(olive::CornerPinDistortNode::k_texture_input));

	EXPECT_EQ(int(node.get_input_data_type(
				  olive::CornerPinDistortNode::k_perspective_input)),
			  int(olive::NodeValue::k_boolean));
	EXPECT_TRUE(node.get_standard_value(
					olive::CornerPinDistortNode::k_perspective_input)
					.toBool());

	// All four corners are pixel offsets relative to their respective image
	// corner and default to no offset
	const QString corners[] = { olive::CornerPinDistortNode::k_top_left_input,
								olive::CornerPinDistortNode::k_top_right_input,
								olive::CornerPinDistortNode::k_bottom_right_input,
								olive::CornerPinDistortNode::k_bottom_left_input };
	for (const QString &corner : corners) {
		EXPECT_EQ(int(node.get_input_data_type(corner)),
				  int(olive::NodeValue::k_vec2));
		EXPECT_EQ(node.get_standard_value(corner).value<QVector2D>(),
				  QVector2D(0.0f, 0.0f));
	}
}

TEST(CornerPinDistortNode, RetranslateSetsInputNames)
{
	olive::CornerPinDistortNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::CornerPinDistortNode::k_texture_input),
			  QStringLiteral("Texture"));
	EXPECT_EQ(node.get_input_name(olive::CornerPinDistortNode::k_perspective_input),
			  QStringLiteral("Perspective"));
	EXPECT_EQ(node.get_input_name(olive::CornerPinDistortNode::k_top_left_input),
			  QStringLiteral("Top Left"));
	EXPECT_EQ(node.get_input_name(olive::CornerPinDistortNode::k_top_right_input),
			  QStringLiteral("Top Right"));
	EXPECT_EQ(node.get_input_name(olive::CornerPinDistortNode::k_bottom_right_input),
			  QStringLiteral("Bottom Right"));
	EXPECT_EQ(node.get_input_name(olive::CornerPinDistortNode::k_bottom_left_input),
			  QStringLiteral("Bottom Left"));
}

TEST(CornerPinDistortNode, GetShaderCodeLoadsFragAndVertShaders)
{
	olive::CornerPinDistortNode node;

	const olive::ShaderCode code = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("anything")));
	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("perspective_in")));
	// Corner Pin is one of the few nodes with a custom vertex shader
	EXPECT_FALSE(code.vert_code().isEmpty());
	EXPECT_TRUE(code.vert_code().contains(QStringLiteral("top_left_in")));
}

TEST(CornerPinDistortNode, ValueToPixelConvertsOffsetsToPixels)
{
	olive::CornerPinDistortNode node;

	olive::NodeValueRow row;
	row.insert(olive::CornerPinDistortNode::k_top_left_input,
			   vec2_value(QVector2D(10.0f, 20.0f)));
	row.insert(olive::CornerPinDistortNode::k_top_right_input,
			   vec2_value(QVector2D(-30.0f, 40.0f)));
	row.insert(olive::CornerPinDistortNode::k_bottom_right_input,
			   vec2_value(QVector2D(-50.0f, -60.0f)));
	row.insert(olive::CornerPinDistortNode::k_bottom_left_input,
			   vec2_value(QVector2D(70.0f, -80.0f)));

	const QVector2D resolution(200.0f, 100.0f);

	// Top-left offsets are relative to (0, 0)
	const QPointF top_left = node.value_to_pixel(0, row, resolution);
	EXPECT_DOUBLE_EQ(top_left.x(), 10.0);
	EXPECT_DOUBLE_EQ(top_left.y(), 20.0);

	// Top-right offsets are relative to (width, 0)
	const QPointF top_right = node.value_to_pixel(1, row, resolution);
	EXPECT_DOUBLE_EQ(top_right.x(), 170.0);
	EXPECT_DOUBLE_EQ(top_right.y(), 40.0);

	// Bottom-right offsets are relative to (width, height)
	const QPointF bottom_right = node.value_to_pixel(2, row, resolution);
	EXPECT_DOUBLE_EQ(bottom_right.x(), 150.0);
	EXPECT_DOUBLE_EQ(bottom_right.y(), 40.0);

	// Bottom-left offsets are relative to (0, height)
	const QPointF bottom_left = node.value_to_pixel(3, row, resolution);
	EXPECT_DOUBLE_EQ(bottom_left.x(), 70.0);
	EXPECT_DOUBLE_EQ(bottom_left.y(), 20.0);
}

TEST(CornerPinDistortNode, ValueWithoutTexturePushesNothing)
{
	olive::CornerPinDistortNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(CornerPinDistortNode, ValueWithDefaultCornersPassesTextureThrough)
{
	olive::CornerPinDistortNode node;

	const olive::TexturePtr tex = make_dummy_texture(100, 100);
	olive::NodeValueTable table;
	node.value(make_texture_row(olive::CornerPinDistortNode::k_texture_input, tex),
			   olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	EXPECT_EQ(out, tex);
	EXPECT_FALSE(out->is_job());
}

TEST(CornerPinDistortNode, ValueWithMovedCornerPushesVertexCoordinates)
{
	olive::CornerPinDistortNode node;

	const olive::TexturePtr tex = make_dummy_texture(100, 100);
	olive::NodeValueRow row =
		make_texture_row(olive::CornerPinDistortNode::k_texture_input, tex);
	row.insert(olive::CornerPinDistortNode::k_top_left_input,
			   vec2_value(QVector2D(10.0f, 20.0f)));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());
	EXPECT_EQ(out->params().width(), tex->params().width());

	auto *job = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(job);
	EXPECT_EQ(job->get(QStringLiteral("resolution_in")).to_vec2(),
			  QVector2D(100.0f, 100.0f));

	// Slider offsets are converted to pixel positions and then to clip space
	// (-1..1): top-left (10, 20) -> (-0.8, -0.6), the untouched corners land
	// on the default quad
	const QVector<float> &vertices = job->get_vertex_coordinates();
	ASSERT_EQ(int(vertices.size()), 18);

	// First triangle
	EXPECT_FLOAT_EQ(vertices.at(0), -0.8f);
	EXPECT_FLOAT_EQ(vertices.at(1), -0.6f);
	EXPECT_FLOAT_EQ(vertices.at(2), 0.0f);
	EXPECT_FLOAT_EQ(vertices.at(3), 1.0f);
	EXPECT_FLOAT_EQ(vertices.at(4), -1.0f);
	EXPECT_FLOAT_EQ(vertices.at(5), 0.0f);
	EXPECT_FLOAT_EQ(vertices.at(6), 1.0f);
	EXPECT_FLOAT_EQ(vertices.at(7), 1.0f);
	EXPECT_FLOAT_EQ(vertices.at(8), 0.0f);

	// Second triangle
	EXPECT_FLOAT_EQ(vertices.at(9), -0.8f);
	EXPECT_FLOAT_EQ(vertices.at(10), -0.6f);
	EXPECT_FLOAT_EQ(vertices.at(12), -1.0f);
	EXPECT_FLOAT_EQ(vertices.at(13), 1.0f);
	EXPECT_FLOAT_EQ(vertices.at(15), 1.0f);
	EXPECT_FLOAT_EQ(vertices.at(16), 1.0f);
}

// -----------------------------------------------------------------------------
// MaskDistortNode
// -----------------------------------------------------------------------------

TEST(MaskDistortNode, MetadataIsCorrect)
{
	olive::MaskDistortNode node;
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.mask"));
	EXPECT_EQ(node.name(), QStringLiteral("Mask"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_distort));

	// From GeneratorWithMerge: the base texture is the effect input
	EXPECT_TRUE(node.get_flags() & olive::Node::k_video_effect);
	EXPECT_EQ(node.get_effect_input_id(), olive::GeneratorWithMerge::k_base_input);
}

TEST(MaskDistortNode, InputDefinitionsAndDefaults)
{
	olive::MaskDistortNode node;

	EXPECT_EQ(
		int(node.get_input_data_type(olive::MaskDistortNode::k_invert_input)),
		int(olive::NodeValue::k_boolean));
	EXPECT_FALSE(node.get_standard_value(olive::MaskDistortNode::k_invert_input)
					 .toBool());

	EXPECT_EQ(
		int(node.get_input_data_type(olive::MaskDistortNode::k_feather_input)),
		int(olive::NodeValue::k_float));
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::MaskDistortNode::k_feather_input).toDouble(),
		0.0);
	EXPECT_DOUBLE_EQ(node.get_input_property(olive::MaskDistortNode::k_feather_input,
										   QStringLiteral("min"))
						 .toDouble(),
					 0.0);

	// From PolygonGenerator: the color is hidden because the mask must stay
	// white for the multiply to work, and the shape defaults to a pentagon
	EXPECT_TRUE(node.is_input_hidden(olive::PolygonGenerator::k_color_input));
	EXPECT_TRUE(node.input_is_array(olive::PolygonGenerator::k_points_input));
	EXPECT_EQ(node.input_array_size(olive::PolygonGenerator::k_points_input), 5);
}

TEST(MaskDistortNode, RetranslateSetsInputNames)
{
	olive::MaskDistortNode node;
	node.retranslate();

	// The base input is renamed from GeneratorWithMerge's "Base"
	EXPECT_EQ(node.get_input_name(olive::GeneratorWithMerge::k_base_input),
			  QStringLiteral("Texture"));
	EXPECT_EQ(node.get_input_name(olive::MaskDistortNode::k_invert_input),
			  QStringLiteral("Invert"));
	EXPECT_EQ(node.get_input_name(olive::MaskDistortNode::k_feather_input),
			  QStringLiteral("Feather"));

	// Inherited names from PolygonGenerator
	EXPECT_EQ(node.get_input_name(olive::PolygonGenerator::k_points_input),
			  QStringLiteral("Points"));
	EXPECT_EQ(node.get_input_name(olive::PolygonGenerator::k_color_input),
			  QStringLiteral("Color"));
}

TEST(MaskDistortNode, GetShaderCodeSelectsShaderByRequestId)
{
	olive::MaskDistortNode node;

	const olive::ShaderCode merge = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("mrg")));
	EXPECT_FALSE(merge.frag_code().isEmpty());
	EXPECT_TRUE(merge.frag_code().contains(QStringLiteral("tex_a")));

	const olive::ShaderCode feather = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("feather")));
	EXPECT_FALSE(feather.frag_code().isEmpty());
	EXPECT_TRUE(feather.frag_code().contains(QStringLiteral("radius_in")));

	const olive::ShaderCode invert = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("invert")));
	EXPECT_FALSE(invert.frag_code().isEmpty());
	EXPECT_TRUE(invert.frag_code().contains(QStringLiteral("tex_in")));

	// Unknown ids fall through to PolygonGenerator, which serves "rgb"
	const olive::ShaderCode rgb = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("rgb")));
	EXPECT_FALSE(rgb.frag_code().isEmpty());
	EXPECT_TRUE(rgb.frag_code().contains(QStringLiteral("texture_in")));

	const olive::ShaderCode unknown = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("bogus")));
	EXPECT_TRUE(unknown.frag_code().isEmpty());
	EXPECT_TRUE(unknown.vert_code().isEmpty());
}

TEST(MaskDistortNode, ValueWithoutTexturePushesGeneratePipeline)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::MaskDistortNode>(&project);

	const olive::VideoParams vparams = sequence_params(320, 240);
	olive::NodeValueTable table = generate_table(node, vparams);

	// Without a base the mask still generates its polygon, wrapped in the
	// "rgb" conversion job
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());
	EXPECT_EQ(out->params().width(), vparams.width());
	EXPECT_EQ(out->params().height(), vparams.height());

	auto *rgb = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(rgb);
	EXPECT_EQ(rgb->get_shader_id(), QStringLiteral("rgb"));

	// The polygon color is forced to white
	const olive::core::Color color =
		rgb->get(QStringLiteral("color_in")).to_color();
	EXPECT_FLOAT_EQ(color.red(), 1.0f);
	EXPECT_FLOAT_EQ(color.green(), 1.0f);
	EXPECT_FLOAT_EQ(color.blue(), 1.0f);
	EXPECT_FLOAT_EQ(color.alpha(), 1.0f);

	// The nested generation job renders to an 8-bit buffer
	const olive::TexturePtr generate =
		rgb->get(QStringLiteral("texture_in")).to_texture();
	ASSERT_TRUE(generate);
	ASSERT_TRUE(generate->is_job());
	EXPECT_EQ(int(generate->params().format()), int(olive::core::PixelFormat::u8));
	EXPECT_TRUE(dynamic_cast<olive::GenerateJob *>(generate->job()));
}

TEST(MaskDistortNode, ValueWithInvertWrapsGenerationInInvertJob)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::MaskDistortNode>(&project);
	node->set_standard_value(olive::MaskDistortNode::k_invert_input, true);

	olive::NodeValueTable table = generate_table(node, sequence_params(320, 240));

	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *invert = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(invert);
	EXPECT_EQ(invert->get_shader_id(), QStringLiteral("invert"));

	// The inverted texture is the usual rgb generation pipeline
	const olive::TexturePtr rgb_tex =
		invert->get(QStringLiteral("tex_in")).to_texture();
	ASSERT_TRUE(rgb_tex);
	ASSERT_TRUE(rgb_tex->is_job());
	auto *rgb = dynamic_cast<olive::ShaderJob *>(rgb_tex->job());
	ASSERT_TRUE(rgb);
	EXPECT_EQ(rgb->get_shader_id(), QStringLiteral("rgb"));
}

TEST(MaskDistortNode, ValueWithTexturePushesMergeJob)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::MaskDistortNode>(&project);
	auto *constant = add_node<ConstantTextureNode>(&project);

	const olive::TexturePtr base = make_dummy_texture(64, 48);
	constant->set_texture(base);
	olive::Node::connect_edge(
		constant, olive::NodeInput(node, olive::GeneratorWithMerge::k_base_input));

	olive::NodeValueTable table = generate_table(node, sequence_params(320, 240));

	// With a base the mask multiplies it by the generated polygon
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());
	EXPECT_EQ(out->params().width(), base->params().width());
	EXPECT_EQ(out->params().height(), base->params().height());

	auto *merge = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(merge);
	EXPECT_EQ(merge->get_shader_id(), QStringLiteral("mrg"));
	EXPECT_EQ(merge->get(QStringLiteral("tex_a")).to_texture(), base);

	// Without a feather, tex_b is the rgb generation pipeline directly
	const olive::TexturePtr tex_b =
		merge->get(QStringLiteral("tex_b")).to_texture();
	ASSERT_TRUE(tex_b);
	ASSERT_TRUE(tex_b->is_job());
	auto *rgb = dynamic_cast<olive::ShaderJob *>(tex_b->job());
	ASSERT_TRUE(rgb);
	EXPECT_EQ(rgb->get_shader_id(), QStringLiteral("rgb"));
}

TEST(MaskDistortNode, ValueWithFeatherNestsBlurJob)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::MaskDistortNode>(&project);
	node->set_standard_value(olive::MaskDistortNode::k_feather_input, 10.0);

	auto *constant = add_node<ConstantTextureNode>(&project);
	const olive::TexturePtr base = make_dummy_texture(64, 48);
	constant->set_texture(base);
	olive::Node::connect_edge(
		constant, olive::NodeInput(node, olive::GeneratorWithMerge::k_base_input));

	olive::NodeValueTable table = generate_table(node, sequence_params(320, 240));

	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *merge = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(merge);
	EXPECT_EQ(merge->get_shader_id(), QStringLiteral("mrg"));

	// With a feather, tex_b becomes a two-iteration gaussian blur of the mask
	const olive::TexturePtr tex_b =
		merge->get(QStringLiteral("tex_b")).to_texture();
	ASSERT_TRUE(tex_b);
	ASSERT_TRUE(tex_b->is_job());

	auto *feather = dynamic_cast<olive::ShaderJob *>(tex_b->job());
	ASSERT_TRUE(feather);
	EXPECT_EQ(feather->get_shader_id(), QStringLiteral("feather"));
	EXPECT_EQ(feather->get_iteration_count(), 2);
	EXPECT_EQ(feather->get_iterative_input(), olive::BlurFilterNode::k_texture_input);
	EXPECT_DOUBLE_EQ(
		feather->get(olive::BlurFilterNode::k_radius_input).to_double(), 10.0);
	EXPECT_EQ(feather->get(olive::BlurFilterNode::k_method_input).to_int(),
			  int(olive::BlurFilterNode::k_gaussian));
	EXPECT_EQ(feather->get(QStringLiteral("resolution_in")).to_vec2(),
			  base->virtual_resolution());
}

// -----------------------------------------------------------------------------
// RippleDistortNode
// -----------------------------------------------------------------------------

TEST(RippleDistortNode, MetadataIsCorrect)
{
	olive::RippleDistortNode node;
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.ripple"));
	EXPECT_EQ(node.name(), QStringLiteral("Ripple"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_distort));

	EXPECT_TRUE(node.get_flags() & olive::Node::k_video_effect);
	EXPECT_EQ(node.get_effect_input_id(), olive::RippleDistortNode::k_texture_input);
}

TEST(RippleDistortNode, InputDefaults)
{
	olive::RippleDistortNode node;

	EXPECT_EQ(
		int(node.get_input_data_type(olive::RippleDistortNode::k_texture_input)),
		int(olive::NodeValue::k_texture));
	EXPECT_FALSE(node.is_input_keyframable(olive::RippleDistortNode::k_texture_input));

	EXPECT_DOUBLE_EQ(node.get_standard_value(olive::RippleDistortNode::k_evolution_input)
						 .toDouble(),
					 0.0);
	EXPECT_DOUBLE_EQ(node.get_standard_value(olive::RippleDistortNode::k_intensity_input)
						 .toDouble(),
					 100.0);
	EXPECT_DOUBLE_EQ(node.get_standard_value(olive::RippleDistortNode::k_frequency_input)
						 .toDouble(),
					 1.0);
	EXPECT_DOUBLE_EQ(node.get_input_property(olive::RippleDistortNode::k_frequency_input,
										   QStringLiteral("base"))
						 .toDouble(),
					 0.01);
	EXPECT_EQ(node.get_standard_value(olive::RippleDistortNode::k_position_input)
				  .value<QVector2D>(),
			  QVector2D(0.0f, 0.0f));
	EXPECT_FALSE(node.get_standard_value(olive::RippleDistortNode::k_stretch_input)
					 .toBool());
}

TEST(RippleDistortNode, RetranslateSetsInputNames)
{
	olive::RippleDistortNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::RippleDistortNode::k_texture_input),
			  QStringLiteral("Input"));
	EXPECT_EQ(node.get_input_name(olive::RippleDistortNode::k_frequency_input),
			  QStringLiteral("Frequency"));
	EXPECT_EQ(node.get_input_name(olive::RippleDistortNode::k_intensity_input),
			  QStringLiteral("Intensity"));
	EXPECT_EQ(node.get_input_name(olive::RippleDistortNode::k_evolution_input),
			  QStringLiteral("Evolution"));
	EXPECT_EQ(node.get_input_name(olive::RippleDistortNode::k_position_input),
			  QStringLiteral("Position"));
	EXPECT_EQ(node.get_input_name(olive::RippleDistortNode::k_stretch_input),
			  QStringLiteral("Stretch"));
}

TEST(RippleDistortNode, GetShaderCodeLoadsRippleShader)
{
	olive::RippleDistortNode node;

	const olive::ShaderCode code = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("anything")));
	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("evolution_in")));
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("intensity_in")));
}

TEST(RippleDistortNode, ValueWithoutTexturePushesNothing)
{
	olive::RippleDistortNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(RippleDistortNode, ValueWithZeroIntensityPassesTextureThrough)
{
	olive::RippleDistortNode node;

	const olive::TexturePtr tex = make_dummy_texture(64, 48);
	olive::NodeValueRow row =
		make_texture_row(olive::RippleDistortNode::k_texture_input, tex);
	row.insert(olive::RippleDistortNode::k_intensity_input, float_value(0.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	EXPECT_EQ(out, tex);
	EXPECT_FALSE(out->is_job());
}

TEST(RippleDistortNode, ValueWithIntensityPushesShaderJob)
{
	olive::RippleDistortNode node;

	// With a non-zero intensity the shader runs and receives the texture's
	// virtual resolution
	const olive::TexturePtr tex = make_dummy_texture(64, 48);
	olive::NodeValueRow row =
		make_texture_row(olive::RippleDistortNode::k_texture_input, tex);
	row.insert(olive::RippleDistortNode::k_intensity_input, float_value(100.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());
	EXPECT_EQ(out->params().width(), tex->params().width());

	auto *job = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(job);
	EXPECT_DOUBLE_EQ(job->get(olive::RippleDistortNode::k_intensity_input).to_double(),
					 100.0);
	EXPECT_EQ(job->get(QStringLiteral("resolution_in")).to_vec2(),
			  tex->virtual_resolution());
}

// -----------------------------------------------------------------------------
// SwirlDistortNode
// -----------------------------------------------------------------------------

TEST(SwirlDistortNode, MetadataIsCorrect)
{
	olive::SwirlDistortNode node;
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.swirl"));
	EXPECT_EQ(node.name(), QStringLiteral("Swirl"));
	EXPECT_EQ(node.description(),
			  QStringLiteral("Distorts an image by swirling it around a center point."));
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_distort));

	EXPECT_TRUE(node.get_flags() & olive::Node::k_video_effect);
	EXPECT_EQ(node.get_effect_input_id(), olive::SwirlDistortNode::k_texture_input);
}

TEST(SwirlDistortNode, InputDefaults)
{
	olive::SwirlDistortNode node;

	EXPECT_EQ(int(node.get_input_data_type(olive::SwirlDistortNode::k_texture_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_FALSE(node.is_input_keyframable(olive::SwirlDistortNode::k_texture_input));

	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::SwirlDistortNode::k_radius_input).toDouble(),
		200.0);
	EXPECT_DOUBLE_EQ(node.get_input_property(olive::SwirlDistortNode::k_radius_input,
										   QStringLiteral("min"))
						 .toDouble(),
					 0.0);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::SwirlDistortNode::k_angle_input).toDouble(),
		10.0);
	EXPECT_DOUBLE_EQ(node.get_input_property(olive::SwirlDistortNode::k_angle_input,
										   QStringLiteral("base"))
						 .toDouble(),
					 0.1);
	EXPECT_EQ(node.get_standard_value(olive::SwirlDistortNode::k_position_input)
				  .value<QVector2D>(),
			  QVector2D(0.0f, 0.0f));
}

TEST(SwirlDistortNode, RetranslateSetsInputNames)
{
	olive::SwirlDistortNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::SwirlDistortNode::k_texture_input),
			  QStringLiteral("Input"));
	EXPECT_EQ(node.get_input_name(olive::SwirlDistortNode::k_radius_input),
			  QStringLiteral("Radius"));
	EXPECT_EQ(node.get_input_name(olive::SwirlDistortNode::k_angle_input),
			  QStringLiteral("Angle"));
	EXPECT_EQ(node.get_input_name(olive::SwirlDistortNode::k_position_input),
			  QStringLiteral("Position"));
}

TEST(SwirlDistortNode, GetShaderCodeLoadsSwirlShader)
{
	olive::SwirlDistortNode node;

	const olive::ShaderCode code = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("anything")));
	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("radius_in")));
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("angle_in")));
}

TEST(SwirlDistortNode, ValueWithoutTexturePushesNothing)
{
	olive::SwirlDistortNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(SwirlDistortNode, ValueWithZeroAngleOrRadiusPassesTextureThrough)
{
	olive::SwirlDistortNode node;

	const olive::TexturePtr tex = make_dummy_texture(64, 48);

	// Zero angle neutralizes the swirl
	olive::NodeValueRow row =
		make_texture_row(olive::SwirlDistortNode::k_texture_input, tex);
	row.insert(olive::SwirlDistortNode::k_angle_input, float_value(0.0));
	row.insert(olive::SwirlDistortNode::k_radius_input, float_value(200.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	EXPECT_EQ(get_output_texture(table), tex);

	// So does a zero radius
	row.insert(olive::SwirlDistortNode::k_angle_input, float_value(10.0));
	row.insert(olive::SwirlDistortNode::k_radius_input, float_value(0.0));

	olive::NodeValueTable zero_radius_table;
	node.value(row, olive::NodeGlobals(), &zero_radius_table);

	ASSERT_EQ(zero_radius_table.count(), 1);
	EXPECT_EQ(get_output_texture(zero_radius_table), tex);
}

TEST(SwirlDistortNode, ValueWithAngleAndRadiusPushesShaderJob)
{
	olive::SwirlDistortNode node;

	// With a non-zero angle and radius the shader runs and receives the
	// texture's virtual resolution
	const olive::TexturePtr tex = make_dummy_texture(64, 48);
	olive::NodeValueRow row =
		make_texture_row(olive::SwirlDistortNode::k_texture_input, tex);
	row.insert(olive::SwirlDistortNode::k_angle_input, float_value(10.0));
	row.insert(olive::SwirlDistortNode::k_radius_input, float_value(200.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(job);
	EXPECT_DOUBLE_EQ(job->get(olive::SwirlDistortNode::k_angle_input).to_double(),
					 10.0);
	EXPECT_EQ(job->get(QStringLiteral("resolution_in")).to_vec2(),
			  tex->virtual_resolution());
}

// -----------------------------------------------------------------------------
// TileDistortNode
// -----------------------------------------------------------------------------

TEST(TileDistortNode, MetadataIsCorrect)
{
	olive::TileDistortNode node;
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.tile"));
	EXPECT_EQ(node.name(), QStringLiteral("Tile"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_distort));

	EXPECT_TRUE(node.get_flags() & olive::Node::k_video_effect);
	EXPECT_EQ(node.get_effect_input_id(), olive::TileDistortNode::k_texture_input);
}

TEST(TileDistortNode, InputDefaults)
{
	olive::TileDistortNode node;

	EXPECT_EQ(int(node.get_input_data_type(olive::TileDistortNode::k_texture_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_FALSE(node.is_input_keyframable(olive::TileDistortNode::k_texture_input));

	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::TileDistortNode::k_scale_input).toDouble(),
		0.5);
	EXPECT_DOUBLE_EQ(node.get_input_property(olive::TileDistortNode::k_scale_input,
										   QStringLiteral("min"))
						 .toDouble(),
					 0.0);
	EXPECT_EQ(node.get_input_property(olive::TileDistortNode::k_scale_input,
									QStringLiteral("view"))
				  .toInt(),
			  int(olive::slider::k_percentage));

	EXPECT_EQ(node.get_standard_value(olive::TileDistortNode::k_position_input)
				  .value<QVector2D>(),
			  QVector2D(0.0f, 0.0f));

	EXPECT_EQ(int(node.get_input_data_type(olive::TileDistortNode::k_anchor_input)),
			  int(olive::NodeValue::k_combo));
	// The Anchor enum is private; 4 is kMiddleCenter
	EXPECT_EQ(node.get_standard_value(olive::TileDistortNode::k_anchor_input).toInt(),
			  4);

	EXPECT_FALSE(node.get_standard_value(olive::TileDistortNode::k_mirror_x_input)
					 .toBool());
	EXPECT_FALSE(node.get_standard_value(olive::TileDistortNode::k_mirror_y_input)
					 .toBool());
}

TEST(TileDistortNode, RetranslateSetsNamesAndAnchorComboStrings)
{
	olive::TileDistortNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::TileDistortNode::k_texture_input),
			  QStringLiteral("Input"));
	EXPECT_EQ(node.get_input_name(olive::TileDistortNode::k_scale_input),
			  QStringLiteral("Scale"));
	EXPECT_EQ(node.get_input_name(olive::TileDistortNode::k_position_input),
			  QStringLiteral("Position"));
	EXPECT_EQ(node.get_input_name(olive::TileDistortNode::k_anchor_input),
			  QStringLiteral("Anchor"));
	EXPECT_EQ(node.get_input_name(olive::TileDistortNode::k_mirror_x_input),
			  QStringLiteral("Mirror Horizontally"));
	EXPECT_EQ(node.get_input_name(olive::TileDistortNode::k_mirror_y_input),
			  QStringLiteral("Mirror Vertically"));

	const QStringList anchors =
		node.get_combo_box_strings(olive::TileDistortNode::k_anchor_input);
	ASSERT_EQ(anchors.size(), 9);
	EXPECT_EQ(anchors.at(0), QStringLiteral("Top-Left"));
	EXPECT_EQ(anchors.at(1), QStringLiteral("Top-Center"));
	EXPECT_EQ(anchors.at(2), QStringLiteral("Top-Right"));
	EXPECT_EQ(anchors.at(3), QStringLiteral("Middle-Left"));
	EXPECT_EQ(anchors.at(4), QStringLiteral("Middle-Center"));
	EXPECT_EQ(anchors.at(5), QStringLiteral("Middle-Right"));
	EXPECT_EQ(anchors.at(6), QStringLiteral("Bottom-Left"));
	EXPECT_EQ(anchors.at(7), QStringLiteral("Bottom-Center"));
	EXPECT_EQ(anchors.at(8), QStringLiteral("Bottom-Right"));
}

TEST(TileDistortNode, GetShaderCodeLoadsTileShader)
{
	olive::TileDistortNode node;

	const olive::ShaderCode code = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("anything")));
	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("mirrorx_in")));
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("anchor_in")));
}

TEST(TileDistortNode, ValueWithoutTexturePushesNothing)
{
	olive::TileDistortNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(TileDistortNode, ValueWithUnitScalePassesTextureThrough)
{
	olive::TileDistortNode node;

	// A scale of exactly 1.0 is a no-op
	const olive::TexturePtr tex = make_dummy_texture(64, 48);
	olive::NodeValueRow row =
		make_texture_row(olive::TileDistortNode::k_texture_input, tex);
	row.insert(olive::TileDistortNode::k_scale_input, float_value(1.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	EXPECT_EQ(out, tex);
	EXPECT_FALSE(out->is_job());
}

TEST(TileDistortNode, ValueWithNonUnitScalePushesShaderJob)
{
	olive::TileDistortNode node;

	// Any scale other than 1.0 runs the shader
	const olive::TexturePtr tex = make_dummy_texture(64, 48);
	olive::NodeValueRow row =
		make_texture_row(olive::TileDistortNode::k_texture_input, tex);
	row.insert(olive::TileDistortNode::k_scale_input, float_value(0.5));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(job);
	EXPECT_DOUBLE_EQ(job->get(olive::TileDistortNode::k_scale_input).to_double(),
					 0.5);
	EXPECT_EQ(job->get(QStringLiteral("resolution_in")).to_vec2(),
			  tex->virtual_resolution());
}

// -----------------------------------------------------------------------------
// WaveDistortNode
// -----------------------------------------------------------------------------

TEST(WaveDistortNode, MetadataIsCorrect)
{
	olive::WaveDistortNode node;
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.wave"));
	EXPECT_EQ(node.name(), QStringLiteral("Wave"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_distort));

	EXPECT_TRUE(node.get_flags() & olive::Node::k_video_effect);
	EXPECT_EQ(node.get_effect_input_id(), olive::WaveDistortNode::k_texture_input);
}

TEST(WaveDistortNode, InputDefaults)
{
	olive::WaveDistortNode node;

	EXPECT_EQ(int(node.get_input_data_type(olive::WaveDistortNode::k_texture_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_FALSE(node.is_input_keyframable(olive::WaveDistortNode::k_texture_input));

	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::WaveDistortNode::k_frequency_input).toDouble(),
		10.0);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::WaveDistortNode::k_intensity_input).toDouble(),
		10.0);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::WaveDistortNode::k_evolution_input).toDouble(),
		0.0);

	EXPECT_EQ(int(node.get_input_data_type(olive::WaveDistortNode::k_vertical_input)),
			  int(olive::NodeValue::k_combo));
	EXPECT_EQ(node.get_standard_value(olive::WaveDistortNode::k_vertical_input).toInt(),
			  0);
}

TEST(WaveDistortNode, RetranslateSetsNamesAndComboStrings)
{
	olive::WaveDistortNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::WaveDistortNode::k_texture_input),
			  QStringLiteral("Input"));
	EXPECT_EQ(node.get_input_name(olive::WaveDistortNode::k_frequency_input),
			  QStringLiteral("Frequency"));
	EXPECT_EQ(node.get_input_name(olive::WaveDistortNode::k_intensity_input),
			  QStringLiteral("Intensity"));
	EXPECT_EQ(node.get_input_name(olive::WaveDistortNode::k_evolution_input),
			  QStringLiteral("Evolution"));
	EXPECT_EQ(node.get_input_name(olive::WaveDistortNode::k_vertical_input),
			  QStringLiteral("Direction"));

	const QStringList directions =
		node.get_combo_box_strings(olive::WaveDistortNode::k_vertical_input);
	ASSERT_EQ(directions.size(), 2);
	EXPECT_EQ(directions.at(0), QStringLiteral("Horizontal"));
	EXPECT_EQ(directions.at(1), QStringLiteral("Vertical"));
}

TEST(WaveDistortNode, GetShaderCodeLoadsWaveShader)
{
	olive::WaveDistortNode node;

	const olive::ShaderCode code = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("anything")));
	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("vertical_in")));
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("intensity_in")));
}

TEST(WaveDistortNode, ValueWithoutTexturePushesNothing)
{
	olive::WaveDistortNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(WaveDistortNode, ValueWithZeroIntensityPassesTextureThrough)
{
	olive::WaveDistortNode node;

	const olive::TexturePtr tex = make_dummy_texture(64, 48);
	olive::NodeValueRow row =
		make_texture_row(olive::WaveDistortNode::k_texture_input, tex);
	row.insert(olive::WaveDistortNode::k_intensity_input, float_value(0.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	EXPECT_EQ(out, tex);
	EXPECT_FALSE(out->is_job());
}

TEST(WaveDistortNode, ValueWithIntensityPushesShaderJob)
{
	olive::WaveDistortNode node;

	// With a non-zero intensity the shader runs
	const olive::TexturePtr tex = make_dummy_texture(64, 48);
	olive::NodeValueRow row =
		make_texture_row(olive::WaveDistortNode::k_texture_input, tex);
	row.insert(olive::WaveDistortNode::k_intensity_input, float_value(10.0));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	ASSERT_EQ(table.count(), 1);
	const olive::TexturePtr out = get_output_texture(table);
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	// Unlike the other distorters, WaveDistortNode does not insert a
	// resolution_in; the job simply carries the row values with the input
	// texture's params
	EXPECT_EQ(out->params().width(), tex->params().width());
	EXPECT_EQ(out->params().height(), tex->params().height());

	auto *job = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(job);
	EXPECT_DOUBLE_EQ(job->get(olive::WaveDistortNode::k_intensity_input).to_double(),
					 10.0);
	EXPECT_TRUE(job->get(QStringLiteral("resolution_in")).to_vec2().isNull());
}
