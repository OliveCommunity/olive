#include <gtest/gtest.h>

#include <cmath>

#include <QStringList>

#include "node/block/clip/clip.h"
#include "node/block/subtitle/subtitle.h"
#include "node/block/transition/crossdissolve/crossdissolvetransition.h"
#include "node/block/transition/diptocolor/diptocolortransition.h"
#include "node/color/colormanager/colormanager.h"
#include "node/globals.h"
#include "node/math/merge/merge.h"
#include "node/math/trigonometry/trigonometry.h"
#include "node/project.h"
#include "node/traverser.h"
#include "olive/core/render/audioparams.h"
#include "olive/core/render/samplebuffer.h"
#include "olive/core/util/color.h"
#include "olive/core/util/rational.h"
#include "olive/core/util/timerange.h"
#include "render/job/shaderjob.h"
#include "render/loopmode.h"
#include "render/texture.h"

namespace
{

constexpr double k_pi = 3.14159265358979323846;

// A "dummy" texture has no renderer backend and is therefore safe to pass
// around in a headless, CPU-only test.
olive::TexturePtr
make_dummy_texture(int channels = olive::VideoParams::k_rgba_channel_count)
{
	return std::make_shared<olive::Texture>(
		olive::VideoParams(64, 64, olive::core::PixelFormat::f32, channels));
}

olive::core::AudioParams test_audio_params()
{
	return olive::core::AudioParams(48000, olive::core::k_channel_layout_stereo,
									olive::core::SampleFormat::f32_p);
}

// Stereo buffer with every sample in both channels set to the same value
olive::core::SampleBuffer make_constant_buffer(float value, size_t sample_count)
{
	olive::core::SampleBuffer buffer(test_audio_params(), sample_count);
	for (size_t i = 0; i < sample_count; i++) {
		buffer.data(0)[i] = value;
		buffer.data(1)[i] = value;
	}
	return buffer;
}

olive::NodeValue sample_value(const olive::core::SampleBuffer &buffer)
{
	return olive::NodeValue(olive::NodeValue::k_samples,
							QVariant::fromValue(buffer));
}

// Globals for the audio path of TransitionBlock::Value, mixing over [in, out)
olive::NodeGlobals audio_globals(const olive::core::Rational &in,
								const olive::core::Rational &out)
{
	return olive::NodeGlobals(olive::VideoParams(), test_audio_params(),
							  olive::TimeRange(in, out),
							  olive::LoopMode::k_loop_mode_off);
}

// A fresh traverser per call: NodeTraverser caches tables per node/range, so
// reusing one would return stale results after changing standard values.
double generate_trig_result(olive::TrigonometryNode *node)
{
	olive::NodeTraverser traverser;
	olive::NodeValueTable table = traverser.generate_table(
		node, olive::TimeRange(olive::core::Rational(0),
							   olive::core::Rational(1, 30)));
	return table.get(olive::NodeValue::k_float).to_double();
}

template <typename T> T *add_node(olive::Project *project)
{
	T *node = new T();
	node->setParent(project);
	return node;
}

// Records the video/audio params seen in NodeGlobals so the traverser's
// NodeGlobals construction can be observed.
class GlobalsProbeNode : public olive::Node {
public:
	GlobalsProbeNode() = default;

	NODE_DEFAULT_FUNCTIONS(GlobalsProbeNode)

	virtual QString name() const override
	{
		return QStringLiteral("Globals Probe");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.oak.test.globalsprobe");
	}

	virtual QVector<CategoryID> category() const override
	{
		return {};
	}

	virtual void value(const olive::NodeValueRow &value,
					   const olive::NodeGlobals &globals,
					   olive::NodeValueTable *table) const override
	{
		Q_UNUSED(value)

		last_vparams_ = globals.vparams();
		last_aparams_ = globals.aparams();
		table->push(olive::NodeValue::k_float, 0.0, this);
	}

	const olive::VideoParams &last_vparams() const
	{
		return last_vparams_;
	}

	const olive::core::AudioParams &last_aparams() const
	{
		return last_aparams_;
	}

private:
	mutable olive::VideoParams last_vparams_;
	mutable olive::core::AudioParams last_aparams_;
};

} // namespace

// -----------------------------------------------------------------------------
// TrigonometryNode
// -----------------------------------------------------------------------------

TEST(TrigonometryNode, MetadataIsCorrect)
{
	olive::TrigonometryNode node;

	EXPECT_EQ(node.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.trigonometry"));
	EXPECT_EQ(node.name(), QStringLiteral("Trigonometry"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_math));

	EXPECT_EQ(int(node.get_input_data_type(olive::TrigonometryNode::k_method_in)),
			  int(olive::NodeValue::k_combo));
	EXPECT_EQ(int(node.get_input_data_type(olive::TrigonometryNode::k_x_in)),
			  int(olive::NodeValue::k_float));

	// Method defaults to sine, value defaults to zero
	EXPECT_EQ(node.get_standard_value(olive::TrigonometryNode::k_method_in).toInt(),
			  0);
	EXPECT_DOUBLE_EQ(
		node.get_standard_value(olive::TrigonometryNode::k_x_in).toDouble(), 0.0);

	// The method is a static UI choice: neither connectable nor keyframable
	EXPECT_FALSE(
		node.is_input_connectable(olive::TrigonometryNode::k_method_in));
	EXPECT_FALSE(
		node.is_input_keyframable(olive::TrigonometryNode::k_method_in));
}

TEST(TrigonometryNode, RetranslateSetsInputNamesAndComboStrings)
{
	olive::TrigonometryNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::TrigonometryNode::k_method_in),
			  QStringLiteral("Method"));
	EXPECT_EQ(node.get_input_name(olive::TrigonometryNode::k_x_in),
			  QStringLiteral("Value"));

	const QStringList methods =
		node.get_input_property(olive::TrigonometryNode::k_method_in,
							  QStringLiteral("combo_str"))
			.toStringList();
	ASSERT_EQ(methods.size(), 9);
	EXPECT_EQ(methods.at(0), QStringLiteral("Sine"));
	EXPECT_EQ(methods.at(1), QStringLiteral("Cosine"));
	EXPECT_EQ(methods.at(2), QStringLiteral("Tangent"));
	EXPECT_EQ(methods.at(3), QStringLiteral("Inverse Sine"));
	EXPECT_EQ(methods.at(4), QStringLiteral("Inverse Cosine"));
	EXPECT_EQ(methods.at(5), QStringLiteral("Inverse Tangent"));
	EXPECT_EQ(methods.at(6), QStringLiteral("Hyperbolic Sine"));
	EXPECT_EQ(methods.at(7), QStringLiteral("Hyperbolic Cosine"));
	EXPECT_EQ(methods.at(8), QStringLiteral("Hyperbolic Tangent"));

	// The combo list contains no separator entries, so combo indexes match
	// the Operation enum used by Value() exactly
}

TEST(TrigonometryNode, SineCosineTangent)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TrigonometryNode>(&project);

	node->set_standard_value(olive::TrigonometryNode::k_method_in, 0);
	node->set_standard_value(olive::TrigonometryNode::k_x_in, k_pi / 2);
	EXPECT_NEAR(generate_trig_result(node), 1.0, 1e-12);

	node->set_standard_value(olive::TrigonometryNode::k_method_in, 1);
	node->set_standard_value(olive::TrigonometryNode::k_x_in, k_pi);
	EXPECT_NEAR(generate_trig_result(node), -1.0, 1e-12);

	node->set_standard_value(olive::TrigonometryNode::k_method_in, 2);
	node->set_standard_value(olive::TrigonometryNode::k_x_in, k_pi / 4);
	EXPECT_NEAR(generate_trig_result(node), 1.0, 1e-12);
}

TEST(TrigonometryNode, InverseOperations)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TrigonometryNode>(&project);

	node->set_standard_value(olive::TrigonometryNode::k_method_in, 3);
	node->set_standard_value(olive::TrigonometryNode::k_x_in, 1.0);
	EXPECT_NEAR(generate_trig_result(node), k_pi / 2, 1e-12);

	node->set_standard_value(olive::TrigonometryNode::k_method_in, 4);
	node->set_standard_value(olive::TrigonometryNode::k_x_in, -1.0);
	EXPECT_NEAR(generate_trig_result(node), k_pi, 1e-12);

	node->set_standard_value(olive::TrigonometryNode::k_method_in, 5);
	node->set_standard_value(olive::TrigonometryNode::k_x_in, 1.0);
	EXPECT_NEAR(generate_trig_result(node), k_pi / 4, 1e-12);
}

TEST(TrigonometryNode, HyperbolicOperations)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TrigonometryNode>(&project);
	node->set_standard_value(olive::TrigonometryNode::k_x_in, 1.0);

	node->set_standard_value(olive::TrigonometryNode::k_method_in, 6);
	EXPECT_NEAR(generate_trig_result(node), std::sinh(1.0), 1e-12);

	node->set_standard_value(olive::TrigonometryNode::k_method_in, 7);
	EXPECT_NEAR(generate_trig_result(node), std::cosh(1.0), 1e-12);

	node->set_standard_value(olive::TrigonometryNode::k_method_in, 8);
	EXPECT_NEAR(generate_trig_result(node), std::tanh(1.0), 1e-12);
}

TEST(TrigonometryNode, ComboIndexMatchesOperationEnum)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TrigonometryNode>(&project);

	// The combo list has no separator entries, so a combo index selects the
	// Operation enum value with the same index
	node->set_standard_value(olive::TrigonometryNode::k_method_in, 3);
	node->set_standard_value(olive::TrigonometryNode::k_x_in, 0.5);
	EXPECT_NEAR(generate_trig_result(node), std::asin(0.5), 1e-12);

	node->set_standard_value(olive::TrigonometryNode::k_method_in, 4);
	node->set_standard_value(olive::TrigonometryNode::k_x_in, 0.5);
	EXPECT_NEAR(generate_trig_result(node), std::acos(0.5), 1e-12);

	node->set_standard_value(olive::TrigonometryNode::k_method_in, 6);
	node->set_standard_value(olive::TrigonometryNode::k_x_in, 1.0);
	EXPECT_NEAR(generate_trig_result(node), std::sinh(1.0), 1e-12);

	node->set_standard_value(olive::TrigonometryNode::k_method_in, 8);
	node->set_standard_value(olive::TrigonometryNode::k_x_in, 1.0);
	EXPECT_NEAR(generate_trig_result(node), std::tanh(1.0), 1e-12);
}

// -----------------------------------------------------------------------------
// MergeNode
// -----------------------------------------------------------------------------

TEST(MergeNode, MetadataIsCorrect)
{
	olive::MergeNode node;

	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.merge"));
	EXPECT_EQ(node.name(), QStringLiteral("Merge"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_math));

	EXPECT_EQ(int(node.get_input_data_type(olive::MergeNode::k_base_in)),
			  int(olive::NodeValue::k_texture));
	EXPECT_EQ(int(node.get_input_data_type(olive::MergeNode::k_blend_in)),
			  int(olive::NodeValue::k_texture));

	// Textures cannot be keyframed, and the merge node is an internal
	// compositing building block hidden from the param view
	EXPECT_FALSE(node.is_input_keyframable(olive::MergeNode::k_base_in));
	EXPECT_FALSE(node.is_input_keyframable(olive::MergeNode::k_blend_in));
	EXPECT_TRUE(node.get_flags() & olive::Node::k_dont_show_in_param_view);
}

TEST(MergeNode, RetranslateSetsInputNames)
{
	olive::MergeNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::MergeNode::k_base_in),
			  QStringLiteral("Base"));
	EXPECT_EQ(node.get_input_name(olive::MergeNode::k_blend_in),
			  QStringLiteral("Blend"));
}

TEST(MergeNode, ShaderCodeLoadsAlphaOver)
{
	olive::MergeNode node;

	const olive::ShaderCode code =
		node.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("x")));
	EXPECT_FALSE(code.frag_code().isEmpty());
}

TEST(MergeNode, ValueWithNoTexturesPushesNothing)
{
	olive::MergeNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

TEST(MergeNode, ValueWithOnlyBasePassesBaseThrough)
{
	olive::MergeNode node;

	olive::TexturePtr base = make_dummy_texture();
	olive::NodeValueRow row;
	row.insert(olive::MergeNode::k_base_in,
			   olive::NodeValue(olive::NodeValue::k_texture, base));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	EXPECT_EQ(table.get(olive::NodeValue::k_texture).to_texture(), base);
}

TEST(MergeNode, ValueWithOnlyBlendPassesBlendThrough)
{
	olive::MergeNode node;

	olive::TexturePtr blend = make_dummy_texture();
	olive::NodeValueRow row;
	row.insert(olive::MergeNode::k_blend_in,
			   olive::NodeValue(olive::NodeValue::k_texture, blend));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	EXPECT_EQ(table.get(olive::NodeValue::k_texture).to_texture(), blend);
}

TEST(MergeNode, ValueWithRgbBlendPassesBlendThrough)
{
	olive::MergeNode node;

	// An RGB blend texture has no alpha channel, so alpha-over is skipped
	// and the blend passes through even when a base is present
	olive::TexturePtr base = make_dummy_texture();
	olive::TexturePtr blend = make_dummy_texture(3);
	olive::NodeValueRow row;
	row.insert(olive::MergeNode::k_base_in,
			   olive::NodeValue(olive::NodeValue::k_texture, base));
	row.insert(olive::MergeNode::k_blend_in,
			   olive::NodeValue(olive::NodeValue::k_texture, blend));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	EXPECT_EQ(table.get(olive::NodeValue::k_texture).to_texture(), blend);
}

TEST(MergeNode, ValueWithBaseAndRgbaBlendPushesAlphaOverJob)
{
	olive::MergeNode node;

	olive::TexturePtr base = make_dummy_texture();
	olive::TexturePtr blend = make_dummy_texture();
	olive::NodeValueRow row;
	row.insert(olive::MergeNode::k_base_in,
			   olive::NodeValue(olive::NodeValue::k_texture, base));
	row.insert(olive::MergeNode::k_blend_in,
			   olive::NodeValue(olive::NodeValue::k_texture, blend));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	olive::TexturePtr out = table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(out);
	ASSERT_TRUE(out->is_job());

	auto *job = dynamic_cast<olive::ShaderJob *>(out->job());
	ASSERT_TRUE(job);
	EXPECT_EQ(job->get(olive::MergeNode::k_base_in).to_texture(), base);
	EXPECT_EQ(job->get(olive::MergeNode::k_blend_in).to_texture(), blend);

	// The job texture inherits the base texture's parameters
	EXPECT_EQ(out->params().width(), base->params().width());
	EXPECT_EQ(out->params().height(), base->params().height());
}

// -----------------------------------------------------------------------------
// TransitionBlock (through the concrete CrossDissolveTransition and
// DipToColorTransition subclasses)
// -----------------------------------------------------------------------------

TEST(TransitionBlock, MetadataIsCorrect)
{
	olive::CrossDissolveTransition cross;
	EXPECT_EQ(cross.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.crossdissolve"));
	EXPECT_EQ(cross.name(), QStringLiteral("Cross Dissolve"));
	EXPECT_FALSE(cross.description().isEmpty());
	EXPECT_TRUE(cross.category().contains(olive::Node::k_category_transition));

	olive::DipToColorTransition dip;
	EXPECT_EQ(dip.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.diptocolor"));
	EXPECT_EQ(dip.name(), QStringLiteral("Dip To Color"));
	EXPECT_FALSE(dip.description().isEmpty());
	EXPECT_TRUE(dip.category().contains(olive::Node::k_category_transition));

	EXPECT_EQ(int(cross.get_input_data_type(
				  olive::TransitionBlock::k_out_block_input)),
			  int(olive::NodeValue::k_none));
	EXPECT_EQ(int(cross.get_input_data_type(
				  olive::TransitionBlock::k_in_block_input)),
			  int(olive::NodeValue::k_none));
	EXPECT_EQ(
		int(cross.get_input_data_type(olive::TransitionBlock::k_curve_input)),
		int(olive::NodeValue::k_combo));
	EXPECT_EQ(
		int(cross.get_input_data_type(olive::TransitionBlock::k_center_input)),
		int(olive::NodeValue::k_rational));

	// The curve is a static UI choice defaulting to linear
	EXPECT_FALSE(
		cross.is_input_connectable(olive::TransitionBlock::k_curve_input));
	EXPECT_FALSE(
		cross.is_input_keyframable(olive::TransitionBlock::k_curve_input));
	EXPECT_EQ(cross.get_standard_value(olive::TransitionBlock::k_curve_input)
				  .toInt(),
			  0);
	EXPECT_EQ(cross.offset_center(), olive::core::Rational(0));

	// Blocks hide from the param view by default, transitions re-enable it
	EXPECT_FALSE(cross.get_flags() & olive::Node::k_dont_show_in_param_view);

	// Dip To Color adds a color parameter defaulting to black
	const olive::core::Color color =
		dip.get_standard_value(olive::DipToColorTransition::k_color_input)
			.value<olive::core::Color>();
	EXPECT_FLOAT_EQ(color.red(), 0.0f);
	EXPECT_FLOAT_EQ(color.green(), 0.0f);
	EXPECT_FLOAT_EQ(color.blue(), 0.0f);
}

TEST(TransitionBlock, RetranslateSetsInputNamesAndCurveStrings)
{
	olive::CrossDissolveTransition cross;
	cross.retranslate();

	EXPECT_EQ(cross.get_input_name(olive::TransitionBlock::k_out_block_input),
			  QStringLiteral("From"));
	EXPECT_EQ(cross.get_input_name(olive::TransitionBlock::k_in_block_input),
			  QStringLiteral("To"));
	EXPECT_EQ(cross.get_input_name(olive::TransitionBlock::k_curve_input),
			  QStringLiteral("Curve"));
	EXPECT_EQ(cross.get_input_name(olive::TransitionBlock::k_center_input),
			  QStringLiteral("Center Offset"));

	const QStringList curves =
		cross.get_input_property(olive::TransitionBlock::k_curve_input,
							   QStringLiteral("combo_str"))
			.toStringList();
	ASSERT_EQ(curves.size(), 3);
	EXPECT_EQ(curves.at(0), QStringLiteral("Linear"));
	EXPECT_EQ(curves.at(1), QStringLiteral("Exponential"));
	EXPECT_EQ(curves.at(2), QStringLiteral("Logarithmic"));

	olive::DipToColorTransition dip;
	dip.retranslate();
	EXPECT_EQ(dip.get_input_name(olive::DipToColorTransition::k_color_input),
			  QStringLiteral("Color"));
}

TEST(TransitionBlock, ShaderCodeLoadsTransitionShaders)
{
	olive::CrossDissolveTransition cross;
	EXPECT_FALSE(
		cross.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("x")))
			.frag_code()
			.isEmpty());

	olive::DipToColorTransition dip;
	EXPECT_FALSE(
		dip.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("x")))
			.frag_code()
			.isEmpty());
}

TEST(TransitionBlock, OffsetsWithoutConnectedBlocksAreZero)
{
	olive::CrossDissolveTransition trans;
	trans.set_length_and_media_out(olive::core::Rational(2));

	EXPECT_FALSE(trans.is_dual_transition());
	EXPECT_EQ(trans.connected_out_block(), nullptr);
	EXPECT_EQ(trans.connected_in_block(), nullptr);
	EXPECT_EQ(trans.in_offset(), olive::core::Rational(0));
	EXPECT_EQ(trans.out_offset(), olive::core::Rational(0));

	// With zero offsets only the total progress is meaningful
	EXPECT_DOUBLE_EQ(trans.get_total_progress(0.5), 0.25);
	EXPECT_DOUBLE_EQ(trans.get_out_progress(0.5), 0.0);
	EXPECT_DOUBLE_EQ(trans.get_in_progress(0.5), 0.0);
}

TEST(TransitionBlock, DualTransitionOffsetsAndProgress)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *trans = add_node<olive::CrossDissolveTransition>(&project);
	auto *out_clip = add_node<olive::ClipBlock>(&project);
	auto *in_clip = add_node<olive::ClipBlock>(&project);

	trans->set_length_and_media_out(olive::core::Rational(2));

	olive::Node::connect_edge(
		out_clip,
		olive::NodeInput(trans, olive::TransitionBlock::k_out_block_input));
	olive::Node::connect_edge(
		in_clip, olive::NodeInput(trans, olive::TransitionBlock::k_in_block_input));

	ASSERT_TRUE(trans->is_dual_transition());
	EXPECT_EQ(trans->connected_out_block(), out_clip);
	EXPECT_EQ(trans->connected_in_block(), in_clip);

	// A centered dual transition splits its length evenly on both sides
	EXPECT_EQ(trans->in_offset(), olive::core::Rational(1));
	EXPECT_EQ(trans->out_offset(), olive::core::Rational(1));

	EXPECT_DOUBLE_EQ(trans->get_total_progress(0.0), 0.0);
	EXPECT_DOUBLE_EQ(trans->get_total_progress(0.5), 0.25);
	EXPECT_DOUBLE_EQ(trans->get_total_progress(1.5), 0.75);

	// Out progress runs from 1 to 0 over the out offset
	EXPECT_DOUBLE_EQ(trans->get_out_progress(0.0), 1.0);
	EXPECT_DOUBLE_EQ(trans->get_out_progress(0.5), 0.5);
	EXPECT_DOUBLE_EQ(trans->get_out_progress(1.5), 0.0);

	// In progress runs from 0 to 1 over the in offset and is clamped
	EXPECT_DOUBLE_EQ(trans->get_in_progress(0.5), 0.0);
	EXPECT_DOUBLE_EQ(trans->get_in_progress(1.5), 0.5);
	EXPECT_DOUBLE_EQ(trans->get_in_progress(2.5), 1.0);
}

TEST(TransitionBlock, OffsetCenterShiftsInOutOffsets)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *trans = add_node<olive::CrossDissolveTransition>(&project);
	auto *out_clip = add_node<olive::ClipBlock>(&project);
	auto *in_clip = add_node<olive::ClipBlock>(&project);

	trans->set_length_and_media_out(olive::core::Rational(2));
	olive::Node::connect_edge(
		out_clip,
		olive::NodeInput(trans, olive::TransitionBlock::k_out_block_input));
	olive::Node::connect_edge(
		in_clip, olive::NodeInput(trans, olive::TransitionBlock::k_in_block_input));

	trans->set_offset_center(olive::core::Rational(1, 2));
	EXPECT_EQ(trans->offset_center(), olive::core::Rational(1, 2));

	// A positive center offset moves the midpoint towards the out clip
	EXPECT_EQ(trans->in_offset(), olive::core::Rational(3, 2));
	EXPECT_EQ(trans->out_offset(), olive::core::Rational(1, 2));

	EXPECT_DOUBLE_EQ(trans->get_out_progress(0.25), 0.5);
	EXPECT_DOUBLE_EQ(trans->get_in_progress(1.25), 0.5);
}

TEST(TransitionBlock, SingleSidedTransitionOffsets)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	// Only an outgoing clip: the whole length is the out offset (fade out)
	auto *out_trans = add_node<olive::CrossDissolveTransition>(&project);
	auto *out_clip = add_node<olive::ClipBlock>(&project);
	out_trans->set_length_and_media_out(olive::core::Rational(2));
	olive::Node::connect_edge(
		out_clip,
		olive::NodeInput(out_trans, olive::TransitionBlock::k_out_block_input));

	EXPECT_FALSE(out_trans->is_dual_transition());
	EXPECT_EQ(out_trans->out_offset(), olive::core::Rational(2));
	EXPECT_EQ(out_trans->in_offset(), olive::core::Rational(0));
	EXPECT_DOUBLE_EQ(out_trans->get_out_progress(1.0), 0.5);
	EXPECT_DOUBLE_EQ(out_trans->get_in_progress(1.0), 0.0);

	// Only an incoming clip: the whole length is the in offset (fade in)
	auto *in_trans = add_node<olive::CrossDissolveTransition>(&project);
	auto *in_clip = add_node<olive::ClipBlock>(&project);
	in_trans->set_length_and_media_out(olive::core::Rational(2));
	olive::Node::connect_edge(
		in_clip,
		olive::NodeInput(in_trans, olive::TransitionBlock::k_in_block_input));

	EXPECT_FALSE(in_trans->is_dual_transition());
	EXPECT_EQ(in_trans->in_offset(), olive::core::Rational(2));
	EXPECT_EQ(in_trans->out_offset(), olive::core::Rational(0));
	EXPECT_DOUBLE_EQ(in_trans->get_in_progress(1.0), 0.5);
	EXPECT_DOUBLE_EQ(in_trans->get_out_progress(1.0), 0.0);
}

TEST(TransitionBlock, SetOffsetsAndLengthSetsLengthAndCenter)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *trans = add_node<olive::CrossDissolveTransition>(&project);
	auto *out_clip = add_node<olive::ClipBlock>(&project);
	auto *in_clip = add_node<olive::ClipBlock>(&project);
	olive::Node::connect_edge(
		out_clip,
		olive::NodeInput(trans, olive::TransitionBlock::k_out_block_input));
	olive::Node::connect_edge(
		in_clip, olive::NodeInput(trans, olive::TransitionBlock::k_in_block_input));

	trans->set_offsets_and_length(olive::core::Rational(1, 4),
								  olive::core::Rational(3, 4));

	// The length is always the sum of both offsets
	EXPECT_EQ(trans->length(), olive::core::Rational(1));
	EXPECT_EQ(trans->offset_center(), olive::core::Rational(1, 4));

	// The offset arguments are named from the adjoining clips' perspective
	// (OTIO convention): the in_offset argument describes the overlap with
	// the previous clip, which is the transition's out side
	EXPECT_EQ(trans->out_offset(), olive::core::Rational(1, 4));
	EXPECT_EQ(trans->in_offset(), olive::core::Rational(3, 4));
}

TEST(TransitionBlock, ConnectAndDisconnectUpdateLinkedClips)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *trans = add_node<olive::CrossDissolveTransition>(&project);
	auto *clip = add_node<olive::ClipBlock>(&project);

	olive::Node::connect_edge(
		clip, olive::NodeInput(trans, olive::TransitionBlock::k_out_block_input));
	EXPECT_EQ(trans->connected_out_block(), clip);
	EXPECT_EQ(clip->out_transition(), trans);

	olive::Node::disconnect_edge(
		clip, olive::NodeInput(trans, olive::TransitionBlock::k_out_block_input));
	EXPECT_EQ(trans->connected_out_block(), nullptr);
	EXPECT_EQ(clip->out_transition(), nullptr);

	// Connecting a node that is not a clip leaves the linked block null
	auto *not_a_clip = add_node<GlobalsProbeNode>(&project);
	olive::Node::connect_edge(
		not_a_clip,
		olive::NodeInput(trans, olive::TransitionBlock::k_out_block_input));
	EXPECT_EQ(trans->connected_out_block(), nullptr);
	olive::Node::disconnect_edge(
		not_a_clip,
		olive::NodeInput(trans, olive::TransitionBlock::k_out_block_input));
}

TEST(TransitionBlock, TextureValuePushesJobWithTransitionProgress)
{
	olive::CrossDissolveTransition trans;
	trans.set_length_and_media_out(olive::core::Rational(2));

	olive::TexturePtr out_tex = make_dummy_texture();
	olive::TexturePtr in_tex = make_dummy_texture();

	olive::NodeValueRow row;
	row.insert(olive::TransitionBlock::k_out_block_input,
			   olive::NodeValue(olive::NodeValue::k_texture, out_tex));
	row.insert(olive::TransitionBlock::k_in_block_input,
			   olive::NodeValue(olive::NodeValue::k_texture, in_tex));
	row.insert(olive::TransitionBlock::k_curve_input,
			   olive::NodeValue(olive::NodeValue::k_combo, 0));

	// Half-way through a two second transition
	const olive::NodeGlobals globals(
		olive::VideoParams(64, 64, olive::core::PixelFormat::f32,
						   olive::VideoParams::k_rgba_channel_count),
		olive::core::AudioParams(),
		olive::TimeRange(olive::core::Rational(1), olive::core::Rational(2)),
		olive::LoopMode::k_loop_mode_off);

	olive::NodeValueTable table;
	trans.value(row, globals, &table);

	olive::TexturePtr job_tex =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(job_tex);
	ASSERT_TRUE(job_tex->is_job());
	EXPECT_EQ(job_tex->params().width(), 64);

	auto *job = dynamic_cast<olive::ShaderJob *>(job_tex->job());
	ASSERT_TRUE(job);
	EXPECT_EQ(job->get(olive::TransitionBlock::k_out_block_input).to_texture(),
			  out_tex);
	EXPECT_EQ(job->get(olive::TransitionBlock::k_in_block_input).to_texture(),
			  in_tex);
	EXPECT_EQ(job->get(olive::TransitionBlock::k_curve_input).to_int(), 0);

	// Without connected clips the in/out offsets are zero, so only the total
	// transition progress is meaningful
	EXPECT_DOUBLE_EQ(job->get(QStringLiteral("ove_tprog_all")).to_double(),
					 0.5);
	EXPECT_DOUBLE_EQ(job->get(QStringLiteral("ove_tprog_out")).to_double(),
					 0.0);
	EXPECT_DOUBLE_EQ(job->get(QStringLiteral("ove_tprog_in")).to_double(),
					 0.0);
}

TEST(TransitionBlock, TextureValueInsertsNullTextureForMissingSide)
{
	olive::CrossDissolveTransition trans;
	trans.set_length_and_media_out(olive::core::Rational(2));

	olive::TexturePtr in_tex = make_dummy_texture();

	olive::NodeValueRow row;
	row.insert(olive::TransitionBlock::k_in_block_input,
			   olive::NodeValue(olive::NodeValue::k_texture, in_tex));
	row.insert(olive::TransitionBlock::k_curve_input,
			   olive::NodeValue(olive::NodeValue::k_combo, 0));

	olive::NodeValueTable table;
	trans.value(row, olive::NodeGlobals(), &table);

	olive::TexturePtr job_tex =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(job_tex);
	ASSERT_TRUE(job_tex->is_job());

	auto *job = dynamic_cast<olive::ShaderJob *>(job_tex->job());
	ASSERT_TRUE(job);
	EXPECT_EQ(job->get(olive::TransitionBlock::k_in_block_input).to_texture(),
			  in_tex);

	// The missing "from" side is still inserted, as a null texture
	const olive::NodeValue out_side =
		job->get(olive::TransitionBlock::k_out_block_input);
	EXPECT_EQ(out_side.type(), olive::NodeValue::k_texture);
	EXPECT_TRUE(out_side.to_texture() == nullptr);
}

TEST(TransitionBlock, DipToColorValueInsertsColorIntoJob)
{
	olive::DipToColorTransition trans;
	trans.set_length_and_media_out(olive::core::Rational(2));

	olive::TexturePtr out_tex = make_dummy_texture();
	olive::TexturePtr in_tex = make_dummy_texture();

	olive::NodeValueRow row;
	row.insert(olive::TransitionBlock::k_out_block_input,
			   olive::NodeValue(olive::NodeValue::k_texture, out_tex));
	row.insert(olive::TransitionBlock::k_in_block_input,
			   olive::NodeValue(olive::NodeValue::k_texture, in_tex));
	row.insert(olive::TransitionBlock::k_curve_input,
			   olive::NodeValue(olive::NodeValue::k_combo, 0));
	row.insert(olive::DipToColorTransition::k_color_input,
			   olive::NodeValue(olive::NodeValue::k_color,
								QVariant::fromValue(olive::core::Color(
									0.25f, 0.5f, 0.75f, 1.0f))));

	olive::NodeValueTable table;
	trans.value(row, olive::NodeGlobals(), &table);

	olive::TexturePtr job_tex =
		table.get(olive::NodeValue::k_texture).to_texture();
	ASSERT_TRUE(job_tex);
	ASSERT_TRUE(job_tex->is_job());

	auto *job = dynamic_cast<olive::ShaderJob *>(job_tex->job());
	ASSERT_TRUE(job);
	const olive::core::Color color =
		job->get(olive::DipToColorTransition::k_color_input).to_color();
	EXPECT_FLOAT_EQ(color.red(), 0.25f);
	EXPECT_FLOAT_EQ(color.green(), 0.5f);
	EXPECT_FLOAT_EQ(color.blue(), 0.75f);
	EXPECT_FLOAT_EQ(color.alpha(), 1.0f);
}

TEST(TransitionBlock, AudioValueMixesLinearCrossfade)
{
	olive::CrossDissolveTransition trans;
	trans.set_length_and_media_out(olive::core::Rational(1));

	// Half a second at 48 kHz
	const size_t sample_count = 24000;
	const olive::core::SampleBuffer from =
		make_constant_buffer(1.0f, sample_count);
	const olive::core::SampleBuffer to = make_constant_buffer(0.5f, sample_count);

	olive::NodeValueRow row;
	row.insert(olive::TransitionBlock::k_out_block_input, sample_value(from));
	row.insert(olive::TransitionBlock::k_in_block_input, sample_value(to));

	olive::NodeValueTable table;
	trans.value(row,
				audio_globals(olive::core::Rational(0),
							 olive::core::Rational(1, 2)),
				&table);

	const olive::NodeValue out_val = table.get(olive::NodeValue::k_samples);
	ASSERT_EQ(out_val.type(), olive::NodeValue::k_samples);
	const olive::core::SampleBuffer out = out_val.to_samples();
	ASSERT_TRUE(out.is_allocated());
	EXPECT_EQ(out.sample_count(), sample_count);

	// Sample 0 is fully "from", sample 12000 is 25% through the transition
	for (int channel = 0; channel < 2; channel++) {
		EXPECT_FLOAT_EQ(out.data(channel)[0], 1.0f);
		EXPECT_FLOAT_EQ(out.data(channel)[12000], 0.875f);
	}
}

TEST(TransitionBlock, AudioValueMixesExponentialCrossfade)
{
	olive::CrossDissolveTransition trans;
	trans.set_length_and_media_out(olive::core::Rational(1));
	trans.set_standard_value(olive::TransitionBlock::k_curve_input, 1);

	const size_t sample_count = 24000;
	const olive::core::SampleBuffer from =
		make_constant_buffer(1.0f, sample_count);
	const olive::core::SampleBuffer to = make_constant_buffer(0.5f, sample_count);

	olive::NodeValueRow row;
	row.insert(olive::TransitionBlock::k_out_block_input, sample_value(from));
	row.insert(olive::TransitionBlock::k_in_block_input, sample_value(to));

	olive::NodeValueTable table;
	trans.value(row,
				audio_globals(olive::core::Rational(0),
							 olive::core::Rational(1, 2)),
				&table);

	const olive::core::SampleBuffer out =
		table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(out.is_allocated());

	// The exponential curve squares the linear progress: at 25% the mix is
	// 1.0 * 0.75^2 + 0.5 * 0.25^2
	EXPECT_FLOAT_EQ(out.data(0)[0], 1.0f);
	EXPECT_FLOAT_EQ(out.data(0)[12000], 0.59375f);
}

TEST(TransitionBlock, AudioValueMixesLogarithmicCrossfade)
{
	olive::CrossDissolveTransition trans;
	trans.set_length_and_media_out(olive::core::Rational(1));
	trans.set_standard_value(olive::TransitionBlock::k_curve_input, 2);

	const size_t sample_count = 24000;
	const olive::core::SampleBuffer from =
		make_constant_buffer(1.0f, sample_count);
	const olive::core::SampleBuffer to = make_constant_buffer(0.5f, sample_count);

	olive::NodeValueRow row;
	row.insert(olive::TransitionBlock::k_out_block_input, sample_value(from));
	row.insert(olive::TransitionBlock::k_in_block_input, sample_value(to));

	olive::NodeValueTable table;
	trans.value(row,
				audio_globals(olive::core::Rational(0),
							 olive::core::Rational(1, 2)),
				&table);

	const olive::core::SampleBuffer out =
		table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(out.is_allocated());

	// The logarithmic curve square-roots the linear progress: at 25% the mix
	// is 1.0 * sqrt(0.75) + 0.5 * sqrt(0.25)
	EXPECT_FLOAT_EQ(out.data(0)[0], 1.0f);
	EXPECT_NEAR(out.data(0)[12000], std::sqrt(0.75) + 0.25, 1e-6);
}

TEST(TransitionBlock, AudioValueWithSingleSideFades)
{
	// Only a "from" buffer: fade out
	olive::CrossDissolveTransition fade_out;
	fade_out.set_length_and_media_out(olive::core::Rational(1));

	const size_t sample_count = 24000;
	olive::NodeValueRow out_row;
	out_row.insert(olive::TransitionBlock::k_out_block_input,
				   sample_value(make_constant_buffer(1.0f, sample_count)));

	olive::NodeValueTable out_table;
	fade_out.value(out_row,
				   audio_globals(olive::core::Rational(0),
								olive::core::Rational(1, 2)),
				   &out_table);

	const olive::core::SampleBuffer faded_out =
		out_table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(faded_out.is_allocated());
	EXPECT_FLOAT_EQ(faded_out.data(0)[0], 1.0f);
	EXPECT_FLOAT_EQ(faded_out.data(0)[12000], 0.75f);

	// Only a "to" buffer: fade in
	olive::CrossDissolveTransition fade_in;
	fade_in.set_length_and_media_out(olive::core::Rational(1));

	olive::NodeValueRow in_row;
	in_row.insert(olive::TransitionBlock::k_in_block_input,
				  sample_value(make_constant_buffer(0.5f, sample_count)));

	olive::NodeValueTable in_table;
	fade_in.value(in_row,
				  audio_globals(olive::core::Rational(0),
							   olive::core::Rational(1, 2)),
				  &in_table);

	const olive::core::SampleBuffer faded_in =
		in_table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(faded_in.is_allocated());
	EXPECT_FLOAT_EQ(faded_in.data(0)[0], 0.0f);
	EXPECT_FLOAT_EQ(faded_in.data(0)[12000], 0.125f);
}

TEST(TransitionBlock, ValueWithNoInputsPushesNothing)
{
	olive::CrossDissolveTransition trans;
	trans.set_length_and_media_out(olive::core::Rational(1));

	olive::NodeValueTable table;
	trans.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_EQ(table.count(), 0);
}

// -----------------------------------------------------------------------------
// SubtitleBlock
// -----------------------------------------------------------------------------

TEST(SubtitleBlock, MetadataIsCorrect)
{
	olive::SubtitleBlock node;

	EXPECT_EQ(node.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.subtitle"));
	EXPECT_EQ(node.name(), QStringLiteral("Subtitle"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_timeline));
}

TEST(SubtitleBlock, NameFollowsText)
{
	olive::SubtitleBlock node;

	EXPECT_TRUE(node.get_text().isEmpty());
	EXPECT_EQ(node.name(), QStringLiteral("Subtitle"));

	node.set_text(QStringLiteral("Hello World"));
	EXPECT_EQ(node.get_text(), QStringLiteral("Hello World"));
	EXPECT_EQ(node.name(), QStringLiteral("Hello World"));

	node.set_text(QString());
	EXPECT_EQ(node.name(), QStringLiteral("Subtitle"));
}

TEST(SubtitleBlock, TextInputFlagsAndHiddenClipInputs)
{
	olive::SubtitleBlock node;

	EXPECT_EQ(int(node.get_input_data_type(olive::SubtitleBlock::k_text_in)),
			  int(olive::NodeValue::k_text));

	// The text is edited inline: neither connectable nor keyframable
	EXPECT_FALSE(node.is_input_connectable(olive::SubtitleBlock::k_text_in));
	EXPECT_FALSE(node.is_input_keyframable(olive::SubtitleBlock::k_text_in));

	// The inherited clip inputs are meaningless for a subtitle and hidden
	EXPECT_TRUE(node.is_input_hidden(olive::ClipBlock::k_buffer_in));
	EXPECT_TRUE(node.is_input_hidden(olive::Block::k_length_input));
	EXPECT_TRUE(node.is_input_hidden(olive::ClipBlock::k_media_in_input));
	EXPECT_TRUE(node.is_input_hidden(olive::ClipBlock::k_speed_input));
	EXPECT_TRUE(node.is_input_hidden(olive::ClipBlock::k_reverse_input));
	EXPECT_TRUE(node.is_input_hidden(olive::ClipBlock::k_maintain_audio_pitch_input));

	// Blocks hide from the param view by default, subtitles re-enable it
	EXPECT_FALSE(node.get_flags() & olive::Node::k_dont_show_in_param_view);
}

TEST(SubtitleBlock, RetranslateSetsInputName)
{
	olive::SubtitleBlock node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::SubtitleBlock::k_text_in),
			  QStringLiteral("Text"));
}

// -----------------------------------------------------------------------------
// NodeTraverser: NodeGlobals construction and value caching
// -----------------------------------------------------------------------------

TEST(NodeTraverser, GenerateTablePassesCacheParamsToNodeGlobals)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *probe = add_node<GlobalsProbeNode>(&project);

	olive::NodeTraverser traverser;
	traverser.set_cache_video_params(
		olive::VideoParams(1920, 1080, olive::core::PixelFormat::f32,
						   olive::VideoParams::k_rgba_channel_count));
	traverser.set_cache_audio_params(
		olive::core::AudioParams(44100, olive::core::k_channel_layout_stereo,
								 olive::core::SampleFormat::f32_p));

	traverser.generate_table(probe,
							olive::TimeRange(olive::core::Rational(0),
											 olive::core::Rational(1, 30)));

	EXPECT_EQ(probe->last_vparams().width(), 1920);
	EXPECT_EQ(probe->last_vparams().height(), 1080);
	EXPECT_EQ(probe->last_aparams().sample_rate(), 44100);
}

TEST(NodeTraverser, GenerateTableCachesResultsPerNodeAndRange)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TrigonometryNode>(&project);
	node->set_standard_value(olive::TrigonometryNode::k_x_in, 1.0);

	const olive::TimeRange range(olive::core::Rational(0),
								 olive::core::Rational(1, 30));

	olive::NodeTraverser traverser;
	const double first =
		traverser.generate_table(node, range)
			.get(olive::NodeValue::k_float)
			.to_double();
	EXPECT_NEAR(first, std::sin(1.0), 1e-12);

	// The same traverser returns the cached table for the same node and
	// range, even after the node's inputs change
	node->set_standard_value(olive::TrigonometryNode::k_x_in, 0.0);
	const double cached =
		traverser.generate_table(node, range)
			.get(olive::NodeValue::k_float)
			.to_double();
	EXPECT_DOUBLE_EQ(cached, first);

	// A fresh traverser recomputes
	olive::NodeTraverser fresh;
	const double updated =
		fresh.generate_table(node, range)
			.get(olive::NodeValue::k_float)
			.to_double();
	EXPECT_DOUBLE_EQ(updated, 0.0);
}
