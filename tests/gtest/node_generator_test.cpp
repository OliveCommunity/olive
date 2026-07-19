#include <gtest/gtest.h>

#include <QMatrix4x4>
#include <QStringList>
#include <QVector2D>
#include <QVector3D>

#include "node/color/colormanager/colormanager.h"
#include "node/generator/matrix/matrix.h"
#include "node/generator/noise/noise.h"
#include "node/generator/polygon/polygon.h"
#include "node/generator/shape/shapenode.h"
#include "node/generator/solid/solid.h"
#include "node/generator/text/textv3.h"
#include "node/math/merge/merge.h"
#include "node/project.h"
#include "node/traverser.h"
#include "olive/core/util/color.h"
#include "render/job/generatejob.h"
#include "render/job/shaderjob.h"
#include "render/texture.h"
#include "widget/slider/floatslider.h"

namespace
{

// Node that pushes a fixed dummy texture, used to feed the base input of
// merge-capable generators without any renderer.
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
		return QStringLiteral("org.oak.test.constant_texture");
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

olive::VideoParams test_video_params()
{
	return olive::VideoParams(320, 240, olive::core::PixelFormat::u8, 4);
}

// A fresh traverser per call: NodeTraverser caches tables per node/range, so
// reusing one would return stale results after changing standard values.
olive::NodeValueTable generate_table(const olive::Node *node)
{
	olive::NodeTraverser traverser;
	return traverser.generate_table(node, first_frame());
}

olive::NodeValueTable generate_table(const olive::Node *node,
									const olive::VideoParams &vparams,
									const olive::TimeRange &range)
{
	olive::NodeTraverser traverser;
	traverser.set_cache_video_params(vparams);
	return traverser.generate_table(node, range);
}

olive::NodeValueTable generate_table(const olive::Node *node,
									const olive::VideoParams &vparams)
{
	return generate_table(node, vparams, first_frame());
}

olive::TexturePtr get_output_texture(const olive::NodeValueTable &table)
{
	return table.get(olive::NodeValue::k_texture).to_texture();
}

} // namespace

TEST(MatrixGenerator, MetadataIsCorrect)
{
	olive::MatrixGenerator node;
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.ortho"));
	EXPECT_EQ(node.name(), QStringLiteral("Orthographic Matrix"));
	EXPECT_EQ(node.short_name(), QStringLiteral("Ortho"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_generator));
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_math));
}

TEST(MatrixGenerator, InputDefaultsAndProperties)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::MatrixGenerator>(&project);

	ASSERT_TRUE(node->has_input_with_id(olive::MatrixGenerator::k_position_input));
	ASSERT_TRUE(node->has_input_with_id(olive::MatrixGenerator::k_rotation_input));
	ASSERT_TRUE(node->has_input_with_id(olive::MatrixGenerator::k_scale_input));
	ASSERT_TRUE(
		node->has_input_with_id(olive::MatrixGenerator::k_uniform_scale_input));
	ASSERT_TRUE(node->has_input_with_id(olive::MatrixGenerator::k_anchor_input));

	EXPECT_EQ(int(node->get_input_data_type(olive::MatrixGenerator::k_position_input)),
			  int(olive::NodeValue::k_vec2));
	EXPECT_EQ(int(node->get_input_data_type(olive::MatrixGenerator::k_rotation_input)),
			  int(olive::NodeValue::k_float));
	EXPECT_EQ(int(node->get_input_data_type(olive::MatrixGenerator::k_scale_input)),
			  int(olive::NodeValue::k_vec2));
	EXPECT_EQ(
		int(node->get_input_data_type(olive::MatrixGenerator::k_uniform_scale_input)),
		int(olive::NodeValue::k_boolean));
	EXPECT_EQ(int(node->get_input_data_type(olive::MatrixGenerator::k_anchor_input)),
			  int(olive::NodeValue::k_vec2));

	EXPECT_EQ(node->get_standard_value(olive::MatrixGenerator::k_position_input)
				  .value<QVector2D>(),
			  QVector2D(0.0f, 0.0f));
	EXPECT_EQ(node->get_standard_value(olive::MatrixGenerator::k_rotation_input)
				  .toDouble(),
			  0.0);
	EXPECT_EQ(
		node->get_standard_value(olive::MatrixGenerator::k_scale_input).value<QVector2D>(),
		QVector2D(1.0f, 1.0f));
	EXPECT_TRUE(node->get_standard_value(olive::MatrixGenerator::k_uniform_scale_input)
					.toBool());
	EXPECT_EQ(node->get_standard_value(olive::MatrixGenerator::k_anchor_input)
				  .value<QVector2D>(),
			  QVector2D(0.0f, 0.0f));

	// Scale slider is percentage-based, floored at zero, and starts with its
	// second track disabled because uniform scale defaults to on
	EXPECT_EQ(node->get_input_property(olive::MatrixGenerator::k_scale_input,
									 QStringLiteral("view"))
				  .toInt(),
			  int(olive::FloatSlider::k_percentage));
	EXPECT_EQ(node->get_input_property(olive::MatrixGenerator::k_scale_input,
									 QStringLiteral("min"))
				  .value<QVector2D>(),
			  QVector2D(0.0f, 0.0f));
	EXPECT_TRUE(node->get_input_property(olive::MatrixGenerator::k_scale_input,
									   QStringLiteral("disable1"))
					.toBool());

	// Uniform scale is a UI toggle, not a renderable parameter
	EXPECT_FALSE(node->is_input_connectable(
		olive::MatrixGenerator::k_uniform_scale_input));
	EXPECT_FALSE(node->is_input_keyframable(
		olive::MatrixGenerator::k_uniform_scale_input));
	EXPECT_TRUE(
		node->is_input_connectable(olive::MatrixGenerator::k_position_input));
	EXPECT_TRUE(
		node->is_input_keyframable(olive::MatrixGenerator::k_position_input));
}

TEST(MatrixGenerator, RetranslateSetsInputNames)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::MatrixGenerator>(&project);
	node->retranslate();

	EXPECT_EQ(node->get_input_name(olive::MatrixGenerator::k_position_input),
			  QStringLiteral("Position"));
	EXPECT_EQ(node->get_input_name(olive::MatrixGenerator::k_rotation_input),
			  QStringLiteral("Rotation"));
	EXPECT_EQ(node->get_input_name(olive::MatrixGenerator::k_scale_input),
			  QStringLiteral("Scale"));
	EXPECT_EQ(node->get_input_name(olive::MatrixGenerator::k_uniform_scale_input),
			  QStringLiteral("Uniform Scale"));
	EXPECT_EQ(node->get_input_name(olive::MatrixGenerator::k_anchor_input),
			  QStringLiteral("Anchor Point"));
}

TEST(MatrixGenerator, UniformScaleTogglesScaleSecondTrack)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::MatrixGenerator>(&project);
	EXPECT_TRUE(node->get_input_property(olive::MatrixGenerator::k_scale_input,
									   QStringLiteral("disable1"))
					.toBool());

	node->set_standard_value(olive::MatrixGenerator::k_uniform_scale_input, false);
	EXPECT_FALSE(node->get_input_property(olive::MatrixGenerator::k_scale_input,
										QStringLiteral("disable1"))
					 .toBool());

	node->set_standard_value(olive::MatrixGenerator::k_uniform_scale_input, true);
	EXPECT_TRUE(node->get_input_property(olive::MatrixGenerator::k_scale_input,
									   QStringLiteral("disable1"))
					.toBool());
}

TEST(MatrixGenerator, DefaultValueIsIdentityMatrix)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::MatrixGenerator>(&project);

	olive::NodeValueTable table = generate_table(node);
	olive::NodeValue value = table.get(olive::NodeValue::k_matrix);
	ASSERT_EQ(int(value.type()), int(olive::NodeValue::k_matrix));
	EXPECT_TRUE(value.to_matrix().isIdentity());
}

TEST(MatrixGenerator, PositionTranslatesMatrix)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::MatrixGenerator>(&project);
	node->set_standard_value(olive::MatrixGenerator::k_position_input,
						   QVector2D(100.0f, 50.0f));

	olive::NodeValueTable table = generate_table(node);
	const QVector3D mapped =
		table.get(olive::NodeValue::k_matrix).to_matrix().map(QVector3D(0, 0, 0));
	EXPECT_FLOAT_EQ(mapped.x(), 100.0f);
	EXPECT_FLOAT_EQ(mapped.y(), 50.0f);
	EXPECT_FLOAT_EQ(mapped.z(), 0.0f);
}

TEST(MatrixGenerator, RotationAppliesAroundZAxis)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::MatrixGenerator>(&project);
	node->set_standard_value(olive::MatrixGenerator::k_rotation_input, 90.0);

	olive::NodeValueTable table = generate_table(node);
	const QVector3D mapped =
		table.get(olive::NodeValue::k_matrix).to_matrix().map(QVector3D(1, 0, 0));
	EXPECT_NEAR(mapped.x(), 0.0f, 1e-5f);
	EXPECT_NEAR(mapped.y(), 1.0f, 1e-5f);
	EXPECT_NEAR(mapped.z(), 0.0f, 1e-5f);
}

TEST(MatrixGenerator, PositionAppliesBeforeRotation)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::MatrixGenerator>(&project);
	node->set_standard_value(olive::MatrixGenerator::k_position_input,
						   QVector2D(10.0f, 0.0f));
	node->set_standard_value(olive::MatrixGenerator::k_rotation_input, 90.0);

	// The transform chain is translate * rotate, so (1,0) is first rotated to
	// (0,1) and then shifted by the position
	olive::NodeValueTable table = generate_table(node);
	const QVector3D mapped =
		table.get(olive::NodeValue::k_matrix).to_matrix().map(QVector3D(1, 0, 0));
	EXPECT_NEAR(mapped.x(), 10.0f, 1e-5f);
	EXPECT_NEAR(mapped.y(), 1.0f, 1e-5f);
}

TEST(MatrixGenerator, UniformScaleUsesXForBothAxes)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::MatrixGenerator>(&project);
	node->set_standard_value(olive::MatrixGenerator::k_scale_input,
						   QVector2D(2.0f, 3.0f));

	// Uniform scale on: the X component drives both axes
	node->set_standard_value(olive::MatrixGenerator::k_uniform_scale_input, true);
	olive::NodeValueTable table = generate_table(node);
	QVector3D mapped =
		table.get(olive::NodeValue::k_matrix).to_matrix().map(QVector3D(1, 1, 0));
	EXPECT_FLOAT_EQ(mapped.x(), 2.0f);
	EXPECT_FLOAT_EQ(mapped.y(), 2.0f);

	// Uniform scale off: each axis uses its own component
	node->set_standard_value(olive::MatrixGenerator::k_uniform_scale_input, false);
	table = generate_table(node);
	mapped =
		table.get(olive::NodeValue::k_matrix).to_matrix().map(QVector3D(1, 1, 0));
	EXPECT_FLOAT_EQ(mapped.x(), 2.0f);
	EXPECT_FLOAT_EQ(mapped.y(), 3.0f);
}

TEST(MatrixGenerator, AnchorPointShiftsMatrix)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::MatrixGenerator>(&project);
	node->set_standard_value(olive::MatrixGenerator::k_anchor_input,
						   QVector2D(10.0f, 20.0f));

	olive::NodeValueTable table = generate_table(node);
	const QMatrix4x4 mat = table.get(olive::NodeValue::k_matrix).to_matrix();

	// The anchor itself maps back to the origin
	const QVector3D anchor = mat.map(QVector3D(10.0f, 20.0f, 0.0f));
	EXPECT_FLOAT_EQ(anchor.x(), 0.0f);
	EXPECT_FLOAT_EQ(anchor.y(), 0.0f);

	// ...and the origin is pushed away by the anchor offset
	const QVector3D origin = mat.map(QVector3D(0, 0, 0));
	EXPECT_FLOAT_EQ(origin.x(), -10.0f);
	EXPECT_FLOAT_EQ(origin.y(), -20.0f);
}

TEST(ShapeNode, MetadataIsCorrect)
{
	olive::ShapeNode node;
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.shape"));
	EXPECT_EQ(node.name(), QStringLiteral("Shape"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_generator));
}

TEST(ShapeNode, InputDefaults)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::ShapeNode>(&project);

	// From GeneratorWithMerge: base texture is the effect input
	EXPECT_EQ(node->get_effect_input_id(), olive::GeneratorWithMerge::k_base_input);
	EXPECT_TRUE(node->get_flags() & olive::Node::k_video_effect);
	EXPECT_EQ(int(node->get_input_data_type(olive::GeneratorWithMerge::k_base_input)),
			  int(olive::NodeValue::k_texture));
	EXPECT_FALSE(
		node->is_input_keyframable(olive::GeneratorWithMerge::k_base_input));

	// From ShapeNodeBase: position, size and color
	EXPECT_EQ(node->get_standard_value(olive::ShapeNodeBase::k_position_input)
				  .value<QVector2D>(),
			  QVector2D(0.0f, 0.0f));
	EXPECT_EQ(node->get_standard_value(olive::ShapeNodeBase::k_size_input)
				  .value<QVector2D>(),
			  QVector2D(100.0f, 100.0f));
	const olive::core::Color color =
		node->get_standard_value(olive::ShapeNodeBase::k_color_input)
			.value<olive::core::Color>();
	EXPECT_FLOAT_EQ(color.red(), 1.0f);
	EXPECT_FLOAT_EQ(color.green(), 0.0f);
	EXPECT_FLOAT_EQ(color.blue(), 0.0f);
	EXPECT_FLOAT_EQ(color.alpha(), 1.0f);

	// Shape-specific: type combo defaults to rectangle, radius to 20
	EXPECT_EQ(int(node->get_input_data_type(olive::ShapeNode::k_type_input)),
			  int(olive::NodeValue::k_combo));
	EXPECT_EQ(node->get_standard_value(olive::ShapeNode::k_type_input).toInt(),
			  int(olive::ShapeNode::k_rectangle));
	EXPECT_EQ(node->get_standard_value(olive::ShapeNode::k_radius_input).toDouble(),
			  20.0);
	EXPECT_EQ(node->get_input_property(olive::ShapeNode::k_radius_input,
									 QStringLiteral("min"))
				  .toDouble(),
			  0.0);
}

TEST(ShapeNode, RetranslateSetsNamesAndComboStrings)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::ShapeNode>(&project);
	node->retranslate();

	EXPECT_EQ(node->get_input_name(olive::ShapeNode::k_type_input),
			  QStringLiteral("Type"));
	EXPECT_EQ(node->get_input_name(olive::ShapeNode::k_radius_input),
			  QStringLiteral("Radius"));
	EXPECT_EQ(node->get_input_name(olive::ShapeNodeBase::k_position_input),
			  QStringLiteral("Position"));
	EXPECT_EQ(node->get_input_name(olive::ShapeNodeBase::k_size_input),
			  QStringLiteral("Size"));
	EXPECT_EQ(node->get_input_name(olive::ShapeNodeBase::k_color_input),
			  QStringLiteral("Color"));
	EXPECT_EQ(node->get_input_name(olive::GeneratorWithMerge::k_base_input),
			  QStringLiteral("Base"));

	const QStringList types =
		node->get_combo_box_strings(olive::ShapeNode::k_type_input);
	ASSERT_EQ(types.size(), 3);
	EXPECT_EQ(types.at(int(olive::ShapeNode::k_rectangle)),
			  QStringLiteral("Rectangle"));
	EXPECT_EQ(types.at(int(olive::ShapeNode::k_ellipse)),
			  QStringLiteral("Ellipse"));
	EXPECT_EQ(types.at(int(olive::ShapeNode::k_rounded_rectangle)),
			  QStringLiteral("Rounded Rectangle"));
}

TEST(ShapeNode, RadiusHiddenUnlessRoundedRectangle)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::ShapeNode>(&project);

	node->set_standard_value(olive::ShapeNode::k_type_input,
					   int(olive::ShapeNode::k_rounded_rectangle));
	EXPECT_FALSE(node->is_input_hidden(olive::ShapeNode::k_radius_input));

	node->set_standard_value(olive::ShapeNode::k_type_input,
					   int(olive::ShapeNode::k_ellipse));
	EXPECT_TRUE(node->is_input_hidden(olive::ShapeNode::k_radius_input));

	node->set_standard_value(olive::ShapeNode::k_type_input,
					   int(olive::ShapeNode::k_rectangle));
	EXPECT_TRUE(node->is_input_hidden(olive::ShapeNode::k_radius_input));
}

TEST(ShapeNode, ShaderCodeLoadsShapeAndMergeShaders)
{
	olive::ShapeNode node;

	const olive::ShaderCode shape = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("shape")));
	EXPECT_FALSE(shape.frag_code().isEmpty());
	EXPECT_TRUE(shape.frag_code().contains(QStringLiteral("type_in")));

	// The merge shader comes from GeneratorWithMerge
	const olive::ShaderCode merge = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("mrg")));
	EXPECT_FALSE(merge.frag_code().isEmpty());
	EXPECT_TRUE(merge.frag_code().contains(QStringLiteral("blend_in")));

	// Unknown requests produce no code
	const olive::ShaderCode unknown = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("bogus")));
	EXPECT_TRUE(unknown.frag_code().isEmpty());
	EXPECT_TRUE(unknown.vert_code().isEmpty());
}

TEST(ShapeNode, ValueWithoutBasePushesShapeJob)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::ShapeNode>(&project);

	const olive::VideoParams vparams = test_video_params();
	olive::NodeValueTable table = generate_table(node, vparams);

	olive::TexturePtr texture = get_output_texture(table);
	ASSERT_TRUE(texture);
	ASSERT_TRUE(texture->is_job());
	EXPECT_EQ(texture->params().width(), vparams.width());
	EXPECT_EQ(texture->params().height(), vparams.height());

	auto *job = dynamic_cast<olive::ShaderJob *>(texture->job());
	ASSERT_TRUE(job);
	EXPECT_EQ(job->get_shader_id(), QStringLiteral("shape"));
	EXPECT_EQ(job->get(QStringLiteral("resolution_in")).to_vec2(),
			  vparams.square_resolution());
	EXPECT_EQ(job->get(olive::ShapeNode::k_type_input).to_int(),
			  int(olive::ShapeNode::k_rectangle));
	EXPECT_EQ(job->get(olive::ShapeNode::k_radius_input).to_double(), 20.0);
}

TEST(ShapeNode, ValueWithBasePushesMergeJob)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::ShapeNode>(&project);
	auto *constant = add_node<ConstantTextureNode>(&project);

	const olive::TexturePtr base = std::make_shared<olive::Texture>(
		olive::VideoParams(64, 48, olive::core::PixelFormat::u8, 4));
	constant->set_texture(base);
	olive::Node::connect_edge(constant,
							 olive::NodeInput(
								 node, olive::GeneratorWithMerge::k_base_input));

	olive::NodeValueTable table = generate_table(node, test_video_params());

	// With a base connected the generator composites onto it via the "mrg"
	// merge shader
	olive::TexturePtr texture = get_output_texture(table);
	ASSERT_TRUE(texture);
	ASSERT_TRUE(texture->is_job());
	EXPECT_EQ(texture->params().width(), base->params().width());
	EXPECT_EQ(texture->params().height(), base->params().height());

	auto *merge = dynamic_cast<olive::ShaderJob *>(texture->job());
	ASSERT_TRUE(merge);
	EXPECT_EQ(merge->get_shader_id(), QStringLiteral("mrg"));
	EXPECT_EQ(merge->get(olive::MergeNode::k_base_in).to_texture(), base);

	// The blend input carries the shape generation job, sized after the base
	olive::TexturePtr blend =
		merge->get(olive::MergeNode::k_blend_in).to_texture();
	ASSERT_TRUE(blend);
	ASSERT_TRUE(blend->is_job());
	EXPECT_EQ(blend->params().width(), base->params().width());
	auto *shape_job = dynamic_cast<olive::ShaderJob *>(blend->job());
	ASSERT_TRUE(shape_job);
	EXPECT_EQ(shape_job->get_shader_id(), QStringLiteral("shape"));
	EXPECT_EQ(shape_job->get(QStringLiteral("resolution_in")).to_vec2(),
			  base->virtual_resolution());
}

TEST(SolidGenerator, MetadataIsCorrect)
{
	olive::SolidGenerator node;
	EXPECT_EQ(node.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.solidgenerator"));
	EXPECT_EQ(node.name(), QStringLiteral("Solid"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_generator));
}

TEST(SolidGenerator, DefaultColorIsRed)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::SolidGenerator>(&project);
	EXPECT_EQ(int(node->get_input_data_type(olive::SolidGenerator::k_color_input)),
			  int(olive::NodeValue::k_color));

	const olive::core::Color color =
		node->get_standard_value(olive::SolidGenerator::k_color_input)
			.value<olive::core::Color>();
	EXPECT_FLOAT_EQ(color.red(), 1.0f);
	EXPECT_FLOAT_EQ(color.green(), 0.0f);
	EXPECT_FLOAT_EQ(color.blue(), 0.0f);
	EXPECT_FLOAT_EQ(color.alpha(), 1.0f);
}

TEST(SolidGenerator, RetranslateSetsInputName)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::SolidGenerator>(&project);
	node->retranslate();

	EXPECT_EQ(node->get_input_name(olive::SolidGenerator::k_color_input),
			  QStringLiteral("Color"));
}

TEST(SolidGenerator, ShaderCodeContainsColorUniform)
{
	olive::SolidGenerator node;

	// The request is ignored, the solid shader is always returned
	const olive::ShaderCode code = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("anything")));
	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("color_in")));
}

TEST(SolidGenerator, ValuePushesShaderJobWithColor)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::SolidGenerator>(&project);
	node->set_standard_value(
		olive::SolidGenerator::k_color_input,
		QVariant::fromValue(olive::core::Color(0.25f, 0.5f, 0.75f, 1.0f)));

	const olive::VideoParams vparams = test_video_params();
	olive::NodeValueTable table = generate_table(node, vparams);

	olive::TexturePtr texture = get_output_texture(table);
	ASSERT_TRUE(texture);
	ASSERT_TRUE(texture->is_job());
	EXPECT_EQ(texture->params().width(), vparams.width());
	EXPECT_EQ(texture->params().height(), vparams.height());
	EXPECT_EQ(int(texture->params().format()),
			  int(olive::core::PixelFormat::u8));

	auto *job = dynamic_cast<olive::ShaderJob *>(texture->job());
	ASSERT_TRUE(job);
	const olive::core::Color color =
		job->get(olive::SolidGenerator::k_color_input).to_color();
	EXPECT_FLOAT_EQ(color.red(), 0.25f);
	EXPECT_FLOAT_EQ(color.green(), 0.5f);
	EXPECT_FLOAT_EQ(color.blue(), 0.75f);
	EXPECT_FLOAT_EQ(color.alpha(), 1.0f);
}

TEST(NoiseGenerator, MetadataAndEffectFlags)
{
	olive::NoiseGeneratorNode node;
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.noise"));
	EXPECT_EQ(node.name(), QStringLiteral("Noise"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_generator));

	EXPECT_TRUE(node.get_flags() & olive::Node::k_video_effect);
	EXPECT_EQ(node.get_effect_input_id(), olive::NoiseGeneratorNode::k_base_in);
}

TEST(NoiseGenerator, InputDefaults)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::NoiseGeneratorNode>(&project);

	EXPECT_EQ(int(node->get_input_data_type(olive::NoiseGeneratorNode::k_base_in)),
			  int(olive::NodeValue::k_texture));
	EXPECT_FALSE(node->is_input_keyframable(olive::NoiseGeneratorNode::k_base_in));

	EXPECT_EQ(
		int(node->get_input_data_type(olive::NoiseGeneratorNode::k_strength_input)),
		int(olive::NodeValue::k_float));
	EXPECT_EQ(node->get_standard_value(olive::NoiseGeneratorNode::k_strength_input)
				  .toDouble(),
			  0.2);
	EXPECT_EQ(node->get_input_property(olive::NoiseGeneratorNode::k_strength_input,
									 QStringLiteral("min"))
				  .toInt(),
			  0);
	EXPECT_EQ(node->get_input_property(olive::NoiseGeneratorNode::k_strength_input,
									 QStringLiteral("view"))
				  .toInt(),
			  int(olive::FloatSlider::k_percentage));

	EXPECT_EQ(int(node->get_input_data_type(olive::NoiseGeneratorNode::k_color_input)),
			  int(olive::NodeValue::k_boolean));
	EXPECT_FALSE(node->get_standard_value(olive::NoiseGeneratorNode::k_color_input)
					 .toBool());
}

TEST(NoiseGenerator, RetranslateSetsInputNames)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::NoiseGeneratorNode>(&project);
	node->retranslate();

	EXPECT_EQ(node->get_input_name(olive::NoiseGeneratorNode::k_base_in),
			  QStringLiteral("Base"));
	EXPECT_EQ(node->get_input_name(olive::NoiseGeneratorNode::k_strength_input),
			  QStringLiteral("Strength"));
	EXPECT_EQ(node->get_input_name(olive::NoiseGeneratorNode::k_color_input),
			  QStringLiteral("Color"));
}

TEST(NoiseGenerator, ShaderCodeLoads)
{
	olive::NoiseGeneratorNode node;

	const olive::ShaderCode code = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("anything")));
	EXPECT_FALSE(code.frag_code().isEmpty());
	EXPECT_TRUE(code.frag_code().contains(QStringLiteral("strength_in")));
}

TEST(NoiseGenerator, ValueInsertsTimeAndUsesCacheParams)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::NoiseGeneratorNode>(&project);

	const olive::VideoParams vparams = test_video_params();
	const olive::TimeRange range(olive::Rational(1, 2), olive::Rational(3, 4));
	olive::NodeValueTable table = generate_table(node, vparams, range);

	olive::TexturePtr texture = get_output_texture(table);
	ASSERT_TRUE(texture);
	ASSERT_TRUE(texture->is_job());
	EXPECT_EQ(texture->params().width(), vparams.width());
	EXPECT_EQ(texture->params().height(), vparams.height());

	auto *job = dynamic_cast<olive::ShaderJob *>(texture->job());
	ASSERT_TRUE(job);

	// The noise is animated by the current time
	const olive::NodeValue time = job->get(QStringLiteral("time_in"));
	ASSERT_EQ(int(time.type()), int(olive::NodeValue::k_float));
	EXPECT_DOUBLE_EQ(time.to_double(), range.in().to_double());

	EXPECT_DOUBLE_EQ(job->get(olive::NoiseGeneratorNode::k_strength_input)
						 .to_double(),
					 0.2);
}

TEST(NoiseGenerator, ValueWithBaseUsesBaseParams)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::NoiseGeneratorNode>(&project);
	auto *constant = add_node<ConstantTextureNode>(&project);

	const olive::TexturePtr base = std::make_shared<olive::Texture>(
		olive::VideoParams(64, 48, olive::core::PixelFormat::u8, 4));
	constant->set_texture(base);
	olive::Node::connect_edge(
		constant, olive::NodeInput(node, olive::NoiseGeneratorNode::k_base_in));

	olive::NodeValueTable table = generate_table(node, test_video_params());

	// The generated noise adopts the base texture's params, not the sequence's
	olive::TexturePtr texture = get_output_texture(table);
	ASSERT_TRUE(texture);
	EXPECT_EQ(texture->params().width(), base->params().width());
	EXPECT_EQ(texture->params().height(), base->params().height());
}

TEST(TextGeneratorV3, MetadataIsCorrect)
{
	olive::TextGeneratorV3 node;
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.text3"));
	EXPECT_EQ(node.name(), QStringLiteral("Text"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_generator));
}

TEST(TextGeneratorV3, InputDefaultsAndFlags)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV3>(&project);

	// The default text is a formatted HTML paragraph with the placeholder
	// already substituted ("Sample Text" replaces %1 at construction)
	const QString text =
		node->get_standard_value(olive::TextGeneratorV3::k_text_input).toString();
	EXPECT_TRUE(text.contains(QStringLiteral("Sample Text")));
	EXPECT_TRUE(text.contains(QStringLiteral("<p style=")));
	EXPECT_TRUE(node->get_input_property(olive::TextGeneratorV3::k_text_input,
									   QStringLiteral("vieweronly"))
					.toBool());

	// Text boxes default to 400x300 rather than ShapeNodeBase's 100x100
	EXPECT_EQ(node->get_standard_value(olive::ShapeNodeBase::k_size_input)
				  .value<QVector2D>(),
			  QVector2D(400.0f, 300.0f));

	// Alignment and argument inputs are hidden, non-rendered UI state
	EXPECT_TRUE(node->is_input_hidden(olive::TextGeneratorV3::k_vertical_alignment_input));
	EXPECT_TRUE(
		node->get_input_flags(olive::TextGeneratorV3::k_vertical_alignment_input) &
		olive::k_input_flag_static);
	EXPECT_EQ(node->get_standard_value(olive::TextGeneratorV3::k_vertical_alignment_input)
				  .toInt(),
			  int(olive::TextGeneratorV3::k_v_align_top));

	EXPECT_TRUE(node->is_input_hidden(olive::TextGeneratorV3::k_use_args_input));
	EXPECT_TRUE(node->get_input_flags(olive::TextGeneratorV3::k_use_args_input) &
				olive::k_input_flag_static);
	EXPECT_TRUE(node->get_standard_value(olive::TextGeneratorV3::k_use_args_input)
					.toBool());

	EXPECT_TRUE(node->input_is_array(olive::TextGeneratorV3::k_args_input));
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input), 0);
	EXPECT_EQ(node->get_input_property(olive::TextGeneratorV3::k_args_input,
									 QStringLiteral("arraystart"))
				  .toInt(),
			  1);

	// TextGeneratorV3 has no color input, unlike ShapeNode
	EXPECT_FALSE(node->has_input_with_id(olive::ShapeNodeBase::k_color_input));
}

TEST(TextGeneratorV3, RetranslateSetsNamesAndComboStrings)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV3>(&project);
	node->retranslate();

	EXPECT_EQ(node->get_input_name(olive::TextGeneratorV3::k_text_input),
			  QStringLiteral("Text"));
	EXPECT_EQ(node->get_input_name(olive::TextGeneratorV3::k_vertical_alignment_input),
			  QStringLiteral("Vertical Alignment"));
	EXPECT_EQ(node->get_input_name(olive::TextGeneratorV3::k_args_input),
			  QStringLiteral("Arguments"));

	const QStringList aligns = node->get_combo_box_strings(
		olive::TextGeneratorV3::k_vertical_alignment_input);
	ASSERT_EQ(aligns.size(), 3);
	EXPECT_EQ(aligns.at(int(olive::TextGeneratorV3::k_v_align_top)),
			  QStringLiteral("Top"));
	EXPECT_EQ(aligns.at(int(olive::TextGeneratorV3::k_v_align_middle)),
			  QStringLiteral("Middle"));
	EXPECT_EQ(aligns.at(int(olive::TextGeneratorV3::k_v_align_bottom)),
			  QStringLiteral("Bottom"));
}

TEST(TextGeneratorV3, FormatStringSubstitutesArguments)
{
	EXPECT_EQ(olive::TextGeneratorV3::format_string(QStringLiteral("Hello %1"),
												   { QStringLiteral("world") }),
			  QStringLiteral("Hello world"));
	EXPECT_EQ(olive::TextGeneratorV3::format_string(
				  QStringLiteral("%1 %2 %1"),
				  { QStringLiteral("a"), QStringLiteral("b") }),
			  QStringLiteral("a b a"));
	EXPECT_EQ(olive::TextGeneratorV3::format_string(
				  QStringLiteral("%1%2"),
				  { QStringLiteral("a"), QStringLiteral("b") }),
			  QStringLiteral("ab"));

	// Multi-digit indices are supported
	QStringList args;
	for (int i = 0; i < 12; i++) {
		args.append(QString::number(i + 1));
	}
	EXPECT_EQ(olive::TextGeneratorV3::format_string(QStringLiteral("%12"), args),
			  QStringLiteral("12"));
	EXPECT_EQ(olive::TextGeneratorV3::format_string(
				  QStringLiteral("%01"), { QStringLiteral("x") }),
			  QStringLiteral("x"));
}

TEST(TextGeneratorV3, FormatStringHandlesEdgeCases)
{
	// Double percent escapes to a literal one, even without arguments
	EXPECT_EQ(olive::TextGeneratorV3::format_string(QStringLiteral("100%%"), {}),
			  QStringLiteral("100%"));
	EXPECT_EQ(olive::TextGeneratorV3::format_string(QStringLiteral("%%"),
												   { QStringLiteral("x") }),
			  QStringLiteral("%"));
	EXPECT_EQ(olive::TextGeneratorV3::format_string(QStringLiteral("%%%1"),
												   { QStringLiteral("x") }),
			  QStringLiteral("%x"));

	// Out-of-range and zero indices expand to nothing
	EXPECT_EQ(olive::TextGeneratorV3::format_string(QStringLiteral("%1"), {}),
			  QStringLiteral(""));
	EXPECT_EQ(olive::TextGeneratorV3::format_string(QStringLiteral("%5"),
												   { QStringLiteral("a") }),
			  QStringLiteral(""));
	EXPECT_EQ(olive::TextGeneratorV3::format_string(QStringLiteral("%0"),
												   { QStringLiteral("a") }),
			  QStringLiteral(""));

	// A percent not followed by a digit or percent is kept literally
	EXPECT_EQ(olive::TextGeneratorV3::format_string(QStringLiteral("%x"),
												   { QStringLiteral("a") }),
			  QStringLiteral("%x"));
	EXPECT_EQ(olive::TextGeneratorV3::format_string(QStringLiteral("end%"),
												   { QStringLiteral("a") }),
			  QStringLiteral("end%"));
}

TEST(TextGeneratorV3, AlignmentConversions)
{
	EXPECT_EQ(olive::TextGeneratorV3::get_qt_alignment_from_ours(
				  olive::TextGeneratorV3::k_v_align_top),
			  Qt::AlignTop);
	EXPECT_EQ(olive::TextGeneratorV3::get_qt_alignment_from_ours(
				  olive::TextGeneratorV3::k_v_align_middle),
			  Qt::AlignVCenter);
	EXPECT_EQ(olive::TextGeneratorV3::get_qt_alignment_from_ours(
				  olive::TextGeneratorV3::k_v_align_bottom),
			  Qt::AlignBottom);

	// Unknown values map to no alignment
	EXPECT_EQ(olive::TextGeneratorV3::get_qt_alignment_from_ours(
				  static_cast<olive::TextGeneratorV3::VerticalAlignment>(-1)),
			  Qt::Alignment());

	EXPECT_EQ(int(olive::TextGeneratorV3::get_our_alignment_from_qts(Qt::AlignTop)),
			  int(olive::TextGeneratorV3::k_v_align_top));
	EXPECT_EQ(
		int(olive::TextGeneratorV3::get_our_alignment_from_qts(Qt::AlignVCenter)),
		int(olive::TextGeneratorV3::k_v_align_middle));
	EXPECT_EQ(
		int(olive::TextGeneratorV3::get_our_alignment_from_qts(Qt::AlignBottom)),
		int(olive::TextGeneratorV3::k_v_align_bottom));

	// Anything without a vertical component defaults to top
	EXPECT_EQ(int(olive::TextGeneratorV3::get_our_alignment_from_qts(Qt::AlignLeft)),
			  int(olive::TextGeneratorV3::k_v_align_top));
	EXPECT_EQ(
		int(olive::TextGeneratorV3::get_our_alignment_from_qts(Qt::AlignHCenter)),
		int(olive::TextGeneratorV3::k_v_align_top));
}

TEST(TextGeneratorV3, GetVerticalAlignmentFollowsInput)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV3>(&project);
	EXPECT_EQ(int(node->get_vertical_alignment()),
			  int(olive::TextGeneratorV3::k_v_align_top));

	node->set_standard_value(olive::TextGeneratorV3::k_vertical_alignment_input,
						   int(olive::TextGeneratorV3::k_v_align_bottom));
	EXPECT_EQ(int(node->get_vertical_alignment()),
			  int(olive::TextGeneratorV3::k_v_align_bottom));

	node->set_standard_value(olive::TextGeneratorV3::k_vertical_alignment_input,
						   int(olive::TextGeneratorV3::k_v_align_middle));
	EXPECT_EQ(int(node->get_vertical_alignment()),
			  int(olive::TextGeneratorV3::k_v_align_middle));
}

TEST(TextGeneratorV3, ValueFormatsTextIntoGenerateJob)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV3>(&project);
	node->set_standard_value(olive::TextGeneratorV3::k_text_input,
						   QStringLiteral("A %1 B %2"));
	node->input_array_resize(olive::TextGeneratorV3::k_args_input, 2);
	node->set_standard_value(olive::TextGeneratorV3::k_args_input,
						   QStringLiteral("x"), 0);
	node->set_standard_value(olive::TextGeneratorV3::k_args_input,
						   QStringLiteral("y"), 1);

	// Text is always rendered to an 8-bit buffer regardless of sequence depth
	const olive::VideoParams vparams(320, 240, olive::core::PixelFormat::f32, 4);
	olive::NodeValueTable table = generate_table(node, vparams);

	olive::TexturePtr texture = get_output_texture(table);
	ASSERT_TRUE(texture);
	ASSERT_TRUE(texture->is_job());
	EXPECT_EQ(texture->params().width(), vparams.width());
	EXPECT_EQ(int(texture->params().format()),
			  int(olive::core::PixelFormat::u8));

	auto *job = dynamic_cast<olive::GenerateJob *>(texture->job());
	ASSERT_TRUE(job);
	EXPECT_EQ(job->get(olive::TextGeneratorV3::k_text_input).to_string(),
			  QStringLiteral("A x B y"));
}

TEST(TextGeneratorV3, EmptyTextOutputsNoTextureWithoutBase)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV3>(&project);
	node->set_standard_value(olive::TextGeneratorV3::k_text_input, QString());

	olive::NodeValueTable table = generate_table(node, test_video_params());

	// No text and no base: nothing renderable comes out
	EXPECT_TRUE(get_output_texture(table) == nullptr);
}

TEST(TextGeneratorV3, EmptyTextPassesBaseThrough)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV3>(&project);
	node->set_standard_value(olive::TextGeneratorV3::k_text_input, QString());

	auto *constant = add_node<ConstantTextureNode>(&project);
	const olive::TexturePtr base = std::make_shared<olive::Texture>(
		olive::VideoParams(64, 48, olive::core::PixelFormat::u8, 4));
	constant->set_texture(base);
	olive::Node::connect_edge(constant,
							 olive::NodeInput(
								 node, olive::GeneratorWithMerge::k_base_input));

	olive::NodeValueTable table = generate_table(node, test_video_params());

	// With empty text the base is passed through untouched instead of running
	// the text generation job
	EXPECT_EQ(get_output_texture(table), base);
}

TEST(PolygonGenerator, MetadataIsCorrect)
{
	olive::PolygonGenerator node;
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.polygon"));
	EXPECT_EQ(node.name(), QStringLiteral("Polygon"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_generator));
}

TEST(PolygonGenerator, DefaultPentagonPoints)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::PolygonGenerator>(&project);

	EXPECT_TRUE(node->input_is_array(olive::PolygonGenerator::k_points_input));
	ASSERT_EQ(node->input_array_size(olive::PolygonGenerator::k_points_input), 5);

	// "The Default Pentagon(tm)", as named in the implementation
	const double expected[5][2] = {
		{ 0, -135 }, { 135, -45 }, { 90, 120 }, { -90, 120 }, { -135, -45 }
	};
	for (int i = 0; i < 5; i++) {
		EXPECT_DOUBLE_EQ(node->get_split_standard_value_on_track(
							 olive::PolygonGenerator::k_points_input, 0, i)
							 .toDouble(),
						 expected[i][0])
			<< "Wrong X for point " << i;
		EXPECT_DOUBLE_EQ(node->get_split_standard_value_on_track(
							 olive::PolygonGenerator::k_points_input, 1, i)
							 .toDouble(),
						 expected[i][1])
			<< "Wrong Y for point " << i;
	}

	// Polygons default to white
	const olive::core::Color color =
		node->get_standard_value(olive::PolygonGenerator::k_color_input)
			.value<olive::core::Color>();
	EXPECT_FLOAT_EQ(color.red(), 1.0f);
	EXPECT_FLOAT_EQ(color.green(), 1.0f);
	EXPECT_FLOAT_EQ(color.blue(), 1.0f);
	EXPECT_FLOAT_EQ(color.alpha(), 1.0f);
}

TEST(PolygonGenerator, RetranslateSetsInputNames)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::PolygonGenerator>(&project);
	node->retranslate();

	EXPECT_EQ(node->get_input_name(olive::PolygonGenerator::k_points_input),
			  QStringLiteral("Points"));
	EXPECT_EQ(node->get_input_name(olive::PolygonGenerator::k_color_input),
			  QStringLiteral("Color"));
	EXPECT_EQ(node->get_input_name(olive::GeneratorWithMerge::k_base_input),
			  QStringLiteral("Base"));
}

TEST(PolygonGenerator, ShaderCodeLoadsRgbAndMergeShaders)
{
	olive::PolygonGenerator node;

	// The generated alpha mask is tinted through the rgb shader
	const olive::ShaderCode rgb = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("rgb")));
	EXPECT_FALSE(rgb.frag_code().isEmpty());
	EXPECT_TRUE(rgb.frag_code().contains(QStringLiteral("texture_in")));

	const olive::ShaderCode merge = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("mrg")));
	EXPECT_FALSE(merge.frag_code().isEmpty());
	EXPECT_TRUE(merge.frag_code().contains(QStringLiteral("blend_in")));

	const olive::ShaderCode unknown = node.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("bogus")));
	EXPECT_TRUE(unknown.frag_code().isEmpty());
	EXPECT_TRUE(unknown.vert_code().isEmpty());
}

TEST(PolygonGenerator, ValueWithoutBasePushesNestedGenerateJob)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::PolygonGenerator>(&project);

	const olive::VideoParams vparams(320, 240, olive::core::PixelFormat::f32, 4);
	olive::NodeValueTable table = generate_table(node, vparams);

	// Without a base, the output is the rgb tint job at sequence params
	olive::TexturePtr texture = get_output_texture(table);
	ASSERT_TRUE(texture);
	ASSERT_TRUE(texture->is_job());
	EXPECT_EQ(texture->params().width(), vparams.width());
	EXPECT_EQ(int(texture->params().format()),
			  int(olive::core::PixelFormat::f32));

	auto *rgb = dynamic_cast<olive::ShaderJob *>(texture->job());
	ASSERT_TRUE(rgb);
	EXPECT_EQ(rgb->get_shader_id(), QStringLiteral("rgb"));

	const olive::core::Color color =
		rgb->get(olive::PolygonGenerator::k_color_input).to_color();
	EXPECT_FLOAT_EQ(color.red(), 1.0f);
	EXPECT_FLOAT_EQ(color.green(), 1.0f);
	EXPECT_FLOAT_EQ(color.blue(), 1.0f);
	EXPECT_FLOAT_EQ(color.alpha(), 1.0f);

	// Its texture input is the polygon's CPU-side GenerateJob, always 8-bit
	olive::TexturePtr mask = rgb->get(QStringLiteral("texture_in")).to_texture();
	ASSERT_TRUE(mask);
	ASSERT_TRUE(mask->is_job());
	EXPECT_EQ(int(mask->params().format()), int(olive::core::PixelFormat::u8));
	EXPECT_TRUE(dynamic_cast<olive::GenerateJob *>(mask->job()));
}
