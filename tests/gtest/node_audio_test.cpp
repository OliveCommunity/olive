#include <gtest/gtest.h>

#include <vector>

#include <QStringList>
#include <QVector3D>

#include "node/audio/pan/pan.h"
#include "node/audio/volume/volume.h"
#include "node/color/colormanager/colormanager.h"
#include "node/globals.h"
#include "node/input/time/timeinput.h"
#include "node/input/value/valuenode.h"
#include "node/keyframe.h"
#include "node/project.h"
#include "node/traverser.h"
#include "olive/core/render/audioparams.h"
#include "olive/core/render/samplebuffer.h"
#include "olive/core/util/rational.h"
#include "render/job/samplejob.h"
#include "widget/slider/floatslider.h"

namespace
{

// Minimal node that emits a single configurable value, used to feed the
// samples input of the audio nodes without any decoder or audio hardware.
class ConstantValueNode : public olive::Node {
public:
	ConstantValueNode() = default;

	NODE_DEFAULT_FUNCTIONS(ConstantValueNode)

	virtual QString name() const override
	{
		return QStringLiteral("Test Constant");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.oak.test.constant");
	}

	virtual QVector<CategoryID> category() const override
	{
		return { k_category_generator };
	}

	void set_output(const olive::NodeValue &value)
	{
		output_ = value;
	}

	virtual void value(const olive::NodeValueRow &value,
					   const olive::NodeGlobals &globals,
					   olive::NodeValueTable *table) const override
	{
		Q_UNUSED(value)
		Q_UNUSED(globals)

		table->push(output_);
	}

private:
	olive::NodeValue output_;
};

// Traverser that resolves SampleJobs on the CPU (no audio hardware) so the
// non-static (job) paths of the audio nodes can be verified end to end.
class SampleResolvingTraverser : public olive::NodeTraverser {
public:
	void resolve(olive::NodeValue &value)
	{
		resolve_jobs(value);
	}

protected:
	virtual olive::core::SampleBuffer
	create_sample_buffer(const olive::core::AudioParams &params,
					   int sample_count) override
	{
		return olive::core::SampleBuffer(params, size_t(sample_count));
	}

	virtual void process_samples(olive::core::SampleBuffer &destination,
								const olive::Node *node,
								const olive::TimeRange &range,
								const olive::SampleJob &job) override
	{
		Q_UNUSED(range)

		for (size_t i = 0; i < destination.sample_count(); i++) {
			node->process_samples(job.get_values(), job.samples(), destination,
								 int(i));
		}
	}
};

template <typename T> T *add_node(olive::Project *project)
{
	T *node = new T();
	node->setParent(project);
	return node;
}

ConstantValueNode *add_constant(olive::Project *project,
							   const olive::NodeValue &value)
{
	auto *node = new ConstantValueNode();
	node->setParent(project);
	node->set_output(value);
	return node;
}

olive::NodeKeyframe *add_key(olive::Node *node, const QString &input,
							const olive::core::Rational &time,
							const QVariant &value)
{
	auto *key = new olive::NodeKeyframe(
		time, value, olive::NodeKeyframe::k_linear, 0, -1, input);
	key->setParent(node);
	return key;
}

// A fresh traverser per call: NodeTraverser caches tables per node+range, so
// reusing one would return stale results after the node's parameters change.
olive::NodeValueTable generate_table_at(const olive::Node *node,
									  const olive::core::Rational &time)
{
	olive::NodeTraverser traverser;
	return traverser.generate_table(
		node, olive::TimeRange(time, time + olive::core::Rational(1, 30)));
}

olive::core::AudioParams stereo_params()
{
	return olive::core::AudioParams(48000, olive::core::k_channel_layout_stereo,
									olive::core::SampleFormat::f32_p);
}

olive::core::AudioParams mono_params()
{
	return olive::core::AudioParams(48000, olive::core::k_channel_layout_mono,
									olive::core::SampleFormat::f32_p);
}

// Creates a stereo buffer with the given per-channel samples. Both channels
// must have the same number of samples.
olive::core::SampleBuffer make_stereo_buffer(const std::vector<float> &channel0,
										   const std::vector<float> &channel1)
{
	olive::core::SampleBuffer buffer(stereo_params(), channel0.size());
	for (size_t i = 0; i < channel0.size(); i++) {
		buffer.data(0)[i] = channel0[i];
	}
	for (size_t i = 0; i < channel1.size(); i++) {
		buffer.data(1)[i] = channel1[i];
	}
	return buffer;
}

olive::core::SampleBuffer make_mono_buffer(const std::vector<float> &samples)
{
	olive::core::SampleBuffer buffer(mono_params(), samples.size());
	for (size_t i = 0; i < samples.size(); i++) {
		buffer.data(0)[i] = samples[i];
	}
	return buffer;
}

olive::NodeValue sample_value(const olive::core::SampleBuffer &buffer)
{
	return olive::NodeValue(olive::NodeValue::k_samples,
							QVariant::fromValue(buffer));
}

// Connects a stereo buffer {1,2,3,4}/{5,6,7,8} to the node's samples input.
ConstantValueNode *connect_test_samples(olive::Project *project,
									  olive::Node *node,
									  const QString &samples_input)
{
	ConstantValueNode *samples = add_constant(
		project, sample_value(make_stereo_buffer({ 1.0f, 2.0f, 3.0f, 4.0f },
											  { 5.0f, 6.0f, 7.0f, 8.0f })));
	olive::Node::connect_edge(samples, olive::NodeInput(node, samples_input));
	return samples;
}

} // namespace

TEST(PanNode, Metadata)
{
	olive::PanNode pan;

	EXPECT_EQ(pan.name(), QStringLiteral("Pan"));
	EXPECT_EQ(pan.id(), QStringLiteral("org.olivevideoeditor.Olive.pan"));
	EXPECT_FALSE(pan.description().isEmpty());
	EXPECT_TRUE(pan.category().contains(olive::Node::k_category_filter));

	// Registered as an audio effect with the samples input as effect input
	EXPECT_TRUE(pan.get_flags() & olive::Node::k_audio_effect);
	EXPECT_EQ(pan.get_effect_input_id(), olive::PanNode::k_samples_input);
}

TEST(PanNode, InputDefaults)
{
	olive::PanNode pan;

	EXPECT_EQ(pan.get_input_data_type(olive::PanNode::k_samples_input),
			  olive::NodeValue::k_samples);
	EXPECT_FALSE(pan.is_input_keyframable(olive::PanNode::k_samples_input));
	EXPECT_TRUE(pan.is_input_connectable(olive::PanNode::k_samples_input));

	EXPECT_EQ(pan.get_input_data_type(olive::PanNode::k_panning_input),
			  olive::NodeValue::k_float);
	EXPECT_TRUE(pan.is_input_keyframable(olive::PanNode::k_panning_input));
	EXPECT_DOUBLE_EQ(
		pan.get_standard_value(olive::PanNode::k_panning_input).toDouble(), 0.0);
	EXPECT_DOUBLE_EQ(pan.get_input_property(olive::PanNode::k_panning_input,
										  QStringLiteral("min"))
						 .toDouble(),
					 -1.0);
	EXPECT_DOUBLE_EQ(pan.get_input_property(olive::PanNode::k_panning_input,
										  QStringLiteral("max"))
						 .toDouble(),
					 1.0);
	EXPECT_EQ(int(pan.get_input_property(olive::PanNode::k_panning_input,
									   QStringLiteral("view"))
					  .toInt()),
			  int(olive::FloatSlider::k_percentage));
}

TEST(PanNode, RetranslateSetsInputNames)
{
	olive::PanNode pan;

	pan.retranslate();

	EXPECT_EQ(pan.get_input_name(olive::PanNode::k_samples_input),
			  QStringLiteral("Samples"));
	EXPECT_EQ(pan.get_input_name(olive::PanNode::k_panning_input),
			  QStringLiteral("Pan"));
}

TEST(PanNode, StaticCenterPanLeavesSamplesUnchanged)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::PanNode *pan = add_node<olive::PanNode>(&project);
	connect_test_samples(&project, pan, olive::PanNode::k_samples_input);

	// Pan 0 is a no-op, but the (unmodified) buffer is still pushed
	const olive::NodeValueTable table = generate_table_at(pan, olive::core::Rational(0));
	const olive::core::SampleBuffer out =
		table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(out.is_allocated());
	ASSERT_EQ(out.sample_count(), 4u);
	for (int i = 0; i < 4; i++) {
		EXPECT_FLOAT_EQ(out.data(0)[i], float(i + 1));
		EXPECT_FLOAT_EQ(out.data(1)[i], float(i + 5));
	}
}

TEST(PanNode, StaticRightPanAttenuatesLeftChannel)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::PanNode *pan = add_node<olive::PanNode>(&project);
	pan->set_standard_value(olive::PanNode::k_panning_input, 0.5);
	connect_test_samples(&project, pan, olive::PanNode::k_samples_input);

	const olive::NodeValueTable table = generate_table_at(pan, olive::core::Rational(0));
	const olive::core::SampleBuffer out =
		table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(out.is_allocated());
	for (int i = 0; i < 4; i++) {
		EXPECT_FLOAT_EQ(out.data(0)[i], float(i + 1) * 0.5f);
		EXPECT_FLOAT_EQ(out.data(1)[i], float(i + 5));
	}
}

TEST(PanNode, StaticLeftPanAttenuatesRightChannel)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::PanNode *pan = add_node<olive::PanNode>(&project);
	pan->set_standard_value(olive::PanNode::k_panning_input, -0.5);
	connect_test_samples(&project, pan, olive::PanNode::k_samples_input);

	const olive::NodeValueTable table = generate_table_at(pan, olive::core::Rational(0));
	const olive::core::SampleBuffer out =
		table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(out.is_allocated());
	for (int i = 0; i < 4; i++) {
		EXPECT_FLOAT_EQ(out.data(0)[i], float(i + 1));
		EXPECT_FLOAT_EQ(out.data(1)[i], float(i + 5) * 0.5f);
	}
}

TEST(PanNode, StaticFullRightPanSilencesLeftChannel)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::PanNode *pan = add_node<olive::PanNode>(&project);
	pan->set_standard_value(olive::PanNode::k_panning_input, 1.0);
	connect_test_samples(&project, pan, olive::PanNode::k_samples_input);

	const olive::NodeValueTable table = generate_table_at(pan, olive::core::Rational(0));
	const olive::core::SampleBuffer out =
		table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(out.is_allocated());
	for (int i = 0; i < 4; i++) {
		EXPECT_FLOAT_EQ(out.data(0)[i], 0.0f);
		EXPECT_FLOAT_EQ(out.data(1)[i], float(i + 5));
	}
}

TEST(PanNode, NonStereoSamplesPassThroughUnchanged)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::PanNode *pan = add_node<olive::PanNode>(&project);
	pan->set_standard_value(olive::PanNode::k_panning_input, 0.5);

	ConstantValueNode *samples = add_constant(
		&project, sample_value(make_mono_buffer({ 1.0f, -2.0f, 3.0f })));
	olive::Node::connect_edge(
		samples, olive::NodeInput(pan, olive::PanNode::k_samples_input));

	// Pan only supports stereo: a mono buffer is pushed through untouched
	const olive::NodeValueTable table = generate_table_at(pan, olive::core::Rational(0));
	const olive::core::SampleBuffer out =
		table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(out.is_allocated());
	ASSERT_EQ(out.audio_params().channel_count(), 1);
	ASSERT_EQ(out.sample_count(), 3u);
	EXPECT_FLOAT_EQ(out.data(0)[0], 1.0f);
	EXPECT_FLOAT_EQ(out.data(0)[1], -2.0f);
	EXPECT_FLOAT_EQ(out.data(0)[2], 3.0f);
}

TEST(PanNode, NoSamplesInputProducesNoOutput)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::PanNode *pan = add_node<olive::PanNode>(&project);
	pan->set_standard_value(olive::PanNode::k_panning_input, 0.5);

	// Without an allocated buffer on the samples input, Value() pushes nothing
	const olive::NodeValueTable table = generate_table_at(pan, olive::core::Rational(0));
	EXPECT_EQ(table.get(olive::NodeValue::k_samples).type(),
			  olive::NodeValue::k_none);
}

TEST(PanNode, KeyframedPanProducesSampleJobWithPanValue)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::PanNode *pan = add_node<olive::PanNode>(&project);
	connect_test_samples(&project, pan, olive::PanNode::k_samples_input);

	// A non-static (keyframed) pan makes Value() push a SampleJob instead of
	// processing the buffer immediately
	pan->set_input_is_keyframing(olive::PanNode::k_panning_input, true);
	add_key(pan, olive::PanNode::k_panning_input, olive::core::Rational(0), 1.0);

	const olive::NodeValueTable table = generate_table_at(pan, olive::core::Rational(0));
	olive::NodeValue result = table.get(olive::NodeValue::k_samples);
	ASSERT_EQ(result.type(), olive::NodeValue::k_samples);
	ASSERT_TRUE(result.canConvert<olive::SampleJob>());

	// Like VolumeNode, PanNode::Value() inserts the panning value into the
	// SampleJob so ProcessSamples() sees the keyframed pan
	const olive::SampleJob job = result.value<olive::SampleJob>();
	ASSERT_TRUE(job.get_values().contains(olive::PanNode::k_panning_input));
	EXPECT_DOUBLE_EQ(job.get_values().value(olive::PanNode::k_panning_input).to_double(),
					 1.0);

	SampleResolvingTraverser resolver;
	resolver.resolve(result);

	// Pan 1.0 (full right) silences the left channel and leaves the right
	const olive::core::SampleBuffer out = result.to_samples();
	ASSERT_TRUE(out.is_allocated());
	ASSERT_EQ(out.sample_count(), 4u);
	for (int i = 0; i < 4; i++) {
		EXPECT_FLOAT_EQ(out.data(0)[i], 0.0f);
		EXPECT_FLOAT_EQ(out.data(1)[i], float(i + 5));
	}
}

TEST(PanNode, ProcessSamplesAppliesPanPerSample)
{
	olive::PanNode pan;

	olive::NodeValueRow row;
	row.insert(olive::PanNode::k_panning_input,
			   olive::NodeValue(olive::NodeValue::k_float, 0.5));

	olive::core::SampleBuffer input(stereo_params(), 2);
	olive::core::SampleBuffer output(stereo_params(), 2);
	input.data(0)[0] = 1.5f;
	input.data(0)[1] = -2.0f;
	input.data(1)[0] = 0.25f;
	input.data(1)[1] = 8.0f;

	pan.process_samples(row, input, output, 0);
	pan.process_samples(row, input, output, 1);

	// Panning right attenuates the left channel only
	EXPECT_FLOAT_EQ(output.data(0)[0], 0.75f);
	EXPECT_FLOAT_EQ(output.data(0)[1], -1.0f);
	EXPECT_FLOAT_EQ(output.data(1)[0], 0.25f);
	EXPECT_FLOAT_EQ(output.data(1)[1], 8.0f);

	// Panning left attenuates the right channel only
	row.insert(olive::PanNode::k_panning_input,
			   olive::NodeValue(olive::NodeValue::k_float, -0.25));
	pan.process_samples(row, input, output, 0);

	EXPECT_FLOAT_EQ(output.data(0)[0], 1.5f);
	EXPECT_FLOAT_EQ(output.data(1)[0], 0.1875f);
}

TEST(PanNode, ProcessSamplesWithoutPanValueCopiesInput)
{
	olive::PanNode pan;

	// No panning value in the row: samples are copied unchanged
	olive::NodeValueRow row;

	olive::core::SampleBuffer input(stereo_params(), 1);
	olive::core::SampleBuffer output(stereo_params(), 1);
	input.data(0)[0] = 3.0f;
	input.data(1)[0] = -4.0f;

	pan.process_samples(row, input, output, 0);

	EXPECT_FLOAT_EQ(output.data(0)[0], 3.0f);
	EXPECT_FLOAT_EQ(output.data(1)[0], -4.0f);
}

TEST(VolumeNode, Metadata)
{
	olive::VolumeNode volume;

	EXPECT_EQ(volume.name(), QStringLiteral("Volume"));
	EXPECT_EQ(volume.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.volume"));
	EXPECT_FALSE(volume.description().isEmpty());
	EXPECT_TRUE(volume.category().contains(olive::Node::k_category_filter));

	EXPECT_TRUE(volume.get_flags() & olive::Node::k_audio_effect);
	EXPECT_EQ(volume.get_effect_input_id(), olive::VolumeNode::k_samples_input);
}

TEST(VolumeNode, InputDefaults)
{
	olive::VolumeNode volume;

	EXPECT_EQ(volume.get_input_data_type(olive::VolumeNode::k_samples_input),
			  olive::NodeValue::k_samples);
	EXPECT_FALSE(volume.is_input_keyframable(olive::VolumeNode::k_samples_input));

	EXPECT_EQ(volume.get_input_data_type(olive::VolumeNode::k_volume_input),
			  olive::NodeValue::k_float);
	EXPECT_TRUE(volume.is_input_keyframable(olive::VolumeNode::k_volume_input));
	EXPECT_DOUBLE_EQ(
		volume.get_standard_value(olive::VolumeNode::k_volume_input).toDouble(),
		1.0);
	EXPECT_DOUBLE_EQ(volume.get_input_property(olive::VolumeNode::k_volume_input,
											 QStringLiteral("min"))
						 .toDouble(),
					 0.0);
	EXPECT_EQ(int(volume.get_input_property(olive::VolumeNode::k_volume_input,
										  QStringLiteral("view"))
					  .toInt()),
			  int(olive::FloatSlider::k_decibel));
}

TEST(VolumeNode, RetranslateSetsInputNames)
{
	olive::VolumeNode volume;

	volume.retranslate();

	EXPECT_EQ(volume.get_input_name(olive::VolumeNode::k_samples_input),
			  QStringLiteral("Samples"));
	EXPECT_EQ(volume.get_input_name(olive::VolumeNode::k_volume_input),
			  QStringLiteral("Volume"));
}

TEST(VolumeNode, StaticVolumeScalesSamples)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::VolumeNode *volume = add_node<olive::VolumeNode>(&project);
	volume->set_standard_value(olive::VolumeNode::k_volume_input, 2.0);
	connect_test_samples(&project, volume, olive::VolumeNode::k_samples_input);

	const olive::NodeValueTable table = generate_table_at(volume, olive::core::Rational(0));
	const olive::core::SampleBuffer out =
		table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(out.is_allocated());
	ASSERT_EQ(out.sample_count(), 4u);
	for (int i = 0; i < 4; i++) {
		EXPECT_FLOAT_EQ(out.data(0)[i], float(i + 1) * 2.0f);
		EXPECT_FLOAT_EQ(out.data(1)[i], float(i + 5) * 2.0f);
	}
}

TEST(VolumeNode, StaticUnityVolumeLeavesSamplesUnchanged)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::VolumeNode *volume = add_node<olive::VolumeNode>(&project);
	volume->set_standard_value(olive::VolumeNode::k_volume_input, 1.0);
	connect_test_samples(&project, volume, olive::VolumeNode::k_samples_input);

	// Volume 1 is a no-op, but the (unmodified) buffer is still pushed
	const olive::NodeValueTable table = generate_table_at(volume, olive::core::Rational(0));
	const olive::core::SampleBuffer out =
		table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(out.is_allocated());
	for (int i = 0; i < 4; i++) {
		EXPECT_FLOAT_EQ(out.data(0)[i], float(i + 1));
		EXPECT_FLOAT_EQ(out.data(1)[i], float(i + 5));
	}
}

TEST(VolumeNode, StaticZeroVolumeSilencesSamples)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::VolumeNode *volume = add_node<olive::VolumeNode>(&project);
	volume->set_standard_value(olive::VolumeNode::k_volume_input, 0.0);
	connect_test_samples(&project, volume, olive::VolumeNode::k_samples_input);

	const olive::NodeValueTable table = generate_table_at(volume, olive::core::Rational(0));
	const olive::core::SampleBuffer out =
		table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(out.is_allocated());
	for (int i = 0; i < 4; i++) {
		EXPECT_FLOAT_EQ(out.data(0)[i], 0.0f);
		EXPECT_FLOAT_EQ(out.data(1)[i], 0.0f);
	}
}

TEST(VolumeNode, KeyframedVolumeProducesSampleJobWithInterpolatedValue)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::VolumeNode *volume = add_node<olive::VolumeNode>(&project);
	connect_test_samples(&project, volume, olive::VolumeNode::k_samples_input);

	// A non-static (keyframed) volume makes Value() push a SampleJob carrying
	// the volume value instead of processing the buffer immediately
	volume->set_input_is_keyframing(olive::VolumeNode::k_volume_input, true);
	add_key(volume, olive::VolumeNode::k_volume_input, olive::core::Rational(0),
		   1.0);
	add_key(volume, olive::VolumeNode::k_volume_input, olive::core::Rational(1),
		   3.0);

	// At t=0.5 the linear ramp 1.0 -> 3.0 interpolates to 2.0
	const olive::NodeValueTable table = generate_table_at(volume, olive::core::Rational(1, 2));
	olive::NodeValue result = table.get(olive::NodeValue::k_samples);
	ASSERT_EQ(result.type(), olive::NodeValue::k_samples);
	ASSERT_TRUE(result.canConvert<olive::SampleJob>());

	const olive::SampleJob job = result.value<olive::SampleJob>();
	ASSERT_TRUE(job.get_values().contains(olive::VolumeNode::k_volume_input));
	EXPECT_DOUBLE_EQ(job.get(olive::VolumeNode::k_volume_input).to_double(), 2.0);

	// Resolve the job on the CPU and verify the scaled samples
	SampleResolvingTraverser resolver;
	resolver.resolve(result);

	const olive::core::SampleBuffer out = result.to_samples();
	ASSERT_TRUE(out.is_allocated());
	ASSERT_EQ(out.sample_count(), 4u);
	for (int i = 0; i < 4; i++) {
		EXPECT_FLOAT_EQ(out.data(0)[i], float(i + 1) * 2.0f);
		EXPECT_FLOAT_EQ(out.data(1)[i], float(i + 5) * 2.0f);
	}
}

TEST(VolumeNode, ProcessSamplesMultipliesPerSample)
{
	olive::VolumeNode volume;

	olive::NodeValueRow row;
	row.insert(olive::VolumeNode::k_volume_input,
			   olive::NodeValue(olive::NodeValue::k_float, 2.0));

	olive::core::SampleBuffer input(stereo_params(), 2);
	olive::core::SampleBuffer output(stereo_params(), 2);
	input.data(0)[0] = 1.5f;
	input.data(0)[1] = -2.0f;
	input.data(1)[0] = 0.25f;
	input.data(1)[1] = 8.0f;

	volume.process_samples(row, input, output, 0);
	volume.process_samples(row, input, output, 1);

	EXPECT_FLOAT_EQ(output.data(0)[0], 3.0f);
	EXPECT_FLOAT_EQ(output.data(0)[1], -4.0f);
	EXPECT_FLOAT_EQ(output.data(1)[0], 0.5f);
	EXPECT_FLOAT_EQ(output.data(1)[1], 16.0f);
}

TEST(VolumeNode, ProcessSamplesWithoutVolumeLeavesOutputUntouched)
{
	olive::VolumeNode volume;

	// Neither the samples nor the volume input carries a number: the output
	// must not be written
	olive::NodeValueRow row;

	olive::core::SampleBuffer input(stereo_params(), 1);
	olive::core::SampleBuffer output(stereo_params(), 1);
	input.data(0)[0] = 10.0f;
	output.data(0)[0] = 123.0f;
	output.data(1)[0] = 45.0f;

	volume.process_samples(row, input, output, 0);

	EXPECT_FLOAT_EQ(output.data(0)[0], 123.0f);
	EXPECT_FLOAT_EQ(output.data(1)[0], 45.0f);
}

TEST(TimeInput, Metadata)
{
	olive::TimeInput time;

	EXPECT_EQ(time.name(), QStringLiteral("Time"));
	EXPECT_EQ(time.id(), QStringLiteral("org.olivevideoeditor.Olive.time"));
	EXPECT_FALSE(time.description().isEmpty());
	EXPECT_TRUE(time.category().contains(olive::Node::k_category_time));
}

TEST(TimeInput, ValuePushesCurrentTimeInSeconds)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::TimeInput *time = add_node<olive::TimeInput>(&project);

	olive::NodeValueTable table = generate_table_at(time, olive::core::Rational(0));
	olive::NodeValue result = table.get(olive::NodeValue::k_float);
	ASSERT_EQ(result.type(), olive::NodeValue::k_float);
	EXPECT_DOUBLE_EQ(result.to_double(), 0.0);

	table = generate_table_at(time, olive::core::Rational(5, 2));
	result = table.get(olive::NodeValue::k_float);
	ASSERT_EQ(result.type(), olive::NodeValue::k_float);
	EXPECT_DOUBLE_EQ(result.to_double(), 2.5);
}

TEST(ValueNode, Metadata)
{
	olive::ValueNode value;

	EXPECT_EQ(value.name(), QStringLiteral("Value"));
	EXPECT_EQ(value.id(), QStringLiteral("org.olivevideoeditor.Olive.value"));
	EXPECT_FALSE(value.description().isEmpty());
	EXPECT_TRUE(value.category().contains(olive::Node::k_category_generator));
}

TEST(ValueNode, InputDefaults)
{
	olive::ValueNode value;

	EXPECT_EQ(value.get_input_data_type(olive::ValueNode::k_type_input),
			  olive::NodeValue::k_combo);
	EXPECT_FALSE(value.is_input_connectable(olive::ValueNode::k_type_input));
	EXPECT_FALSE(value.is_input_keyframable(olive::ValueNode::k_type_input));
	EXPECT_EQ(value.get_standard_value(olive::ValueNode::k_type_input).toInt(), 0);

	// The value input starts out as a float (the first supported type)
	EXPECT_EQ(value.get_input_data_type(olive::ValueNode::k_value_input),
			  olive::NodeValue::k_float);
	EXPECT_FALSE(value.is_input_connectable(olive::ValueNode::k_value_input));
	EXPECT_TRUE(value.is_input_keyframable(olive::ValueNode::k_value_input));
}

TEST(ValueNode, RetranslateSetsInputNamesAndTypeCombo)
{
	olive::ValueNode value;

	value.retranslate();

	EXPECT_EQ(value.get_input_name(olive::ValueNode::k_type_input),
			  QStringLiteral("Type"));
	EXPECT_EQ(value.get_input_name(olive::ValueNode::k_value_input),
			  QStringLiteral("Value"));

	// The type combo lists the pretty name of every supported type, in order
	const QStringList types =
		value.get_input_property(olive::ValueNode::k_type_input,
							   QStringLiteral("combo_str"))
			.toStringList();
	ASSERT_EQ(types.size(), 11);
	EXPECT_EQ(types.at(0), QStringLiteral("Float"));
	EXPECT_EQ(types.at(1), QStringLiteral("Integer"));
	EXPECT_EQ(types.at(2), QStringLiteral("Rational"));
	EXPECT_EQ(types.at(3), QStringLiteral("Vector 2D"));
	EXPECT_EQ(types.at(4), QStringLiteral("Vector 3D"));
	EXPECT_EQ(types.at(5), QStringLiteral("Vector 4D"));
	EXPECT_EQ(types.at(6), QStringLiteral("Color"));
	EXPECT_EQ(types.at(7), QStringLiteral("Text"));
	EXPECT_EQ(types.at(8), QStringLiteral("Matrix"));
	EXPECT_EQ(types.at(9), QStringLiteral("Font"));
	EXPECT_EQ(types.at(10), QStringLiteral("Boolean"));
}

TEST(ValueNode, ChangingTypeSwitchesValueInputDataType)
{
	olive::ValueNode value;

	EXPECT_EQ(value.get_input_data_type(olive::ValueNode::k_value_input),
			  olive::NodeValue::k_float);

	value.set_standard_value(olive::ValueNode::k_type_input, 1);
	EXPECT_EQ(value.get_input_data_type(olive::ValueNode::k_value_input),
			  olive::NodeValue::k_int);

	value.set_standard_value(olive::ValueNode::k_type_input, 3);
	EXPECT_EQ(value.get_input_data_type(olive::ValueNode::k_value_input),
			  olive::NodeValue::k_vec2);

	value.set_standard_value(olive::ValueNode::k_type_input, 6);
	EXPECT_EQ(value.get_input_data_type(olive::ValueNode::k_value_input),
			  olive::NodeValue::k_color);

	value.set_standard_value(olive::ValueNode::k_type_input, 10);
	EXPECT_EQ(value.get_input_data_type(olive::ValueNode::k_value_input),
			  olive::NodeValue::k_boolean);

	value.set_standard_value(olive::ValueNode::k_type_input, 0);
	EXPECT_EQ(value.get_input_data_type(olive::ValueNode::k_value_input),
			  olive::NodeValue::k_float);
}

TEST(ValueNode, ValuePassesThroughFloat)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::ValueNode *value = add_node<olive::ValueNode>(&project);
	value->set_standard_value(olive::ValueNode::k_value_input, 3.5);

	const olive::NodeValueTable table = generate_table_at(value, olive::core::Rational(0));
	const olive::NodeValue result = table.get(olive::NodeValue::k_float);
	ASSERT_EQ(result.type(), olive::NodeValue::k_float);
	EXPECT_DOUBLE_EQ(result.to_double(), 3.5);
}

TEST(ValueNode, ValuePassesThroughVectorAfterTypeSwitch)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::ValueNode *value = add_node<olive::ValueNode>(&project);

	// Switch the value input to kVec3 (index 4) and set a vector value
	value->set_standard_value(olive::ValueNode::k_type_input, 4);
	value->set_standard_value(olive::ValueNode::k_value_input,
							QVector3D(1.0f, 2.0f, 3.0f));

	const olive::NodeValueTable table = generate_table_at(value, olive::core::Rational(0));
	const olive::NodeValue result = table.get(olive::NodeValue::k_vec3);
	ASSERT_EQ(result.type(), olive::NodeValue::k_vec3);
	const QVector3D vec = result.to_vec3();
	EXPECT_FLOAT_EQ(vec.x(), 1.0f);
	EXPECT_FLOAT_EQ(vec.y(), 2.0f);
	EXPECT_FLOAT_EQ(vec.z(), 3.0f);
}
