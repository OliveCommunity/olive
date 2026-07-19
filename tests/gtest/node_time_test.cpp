#include <gtest/gtest.h>

#include <memory>

#include <QString>
#include <QVariant>

#include "node/block/gap/gap.h"
#include "node/color/colormanager/colormanager.h"
#include "node/globals.h"
#include "node/input/time/timeinput.h"
#include "node/keyframe.h"
#include "node/project.h"
#include "node/time/timeformat/timeformat.h"
#include "node/time/timeoffset/timeoffsetnode.h"
#include "node/time/timeremap/timeremap.h"
#include "node/traverser.h"
#include "olive/core/util/rational.h"
#include "olive/core/util/timerange.h"

namespace
{

class NodeTimeTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		olive::ColorManager::set_up_default_config();

		project_ = std::make_unique<olive::Project>();
		project_->initialize();
	}

	template <typename T> T *add_node()
	{
		T *node = new T();
		node->setParent(project_.get());
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

	// Generates the node's output table at a single time with a fresh
	// traverser (the traverser caches tables per node+range, so reusing one
	// would return stale values after the node's parameters change)
	olive::NodeValueTable generate_table(const olive::Node *node,
										const olive::core::Rational &time)
	{
		olive::NodeTraverser traverser;
		return traverser.generate_table(
			node, olive::TimeRange(time, time + olive::core::Rational(1, 30)));
	}

	std::unique_ptr<olive::Project> project_;
};

} // namespace

TEST(GapBlock, Metadata)
{
	olive::GapBlock gap;

	EXPECT_EQ(gap.name(), QStringLiteral("Gap"));
	EXPECT_EQ(gap.id(), QStringLiteral("org.olivevideoeditor.Olive.gap"));
	EXPECT_FALSE(gap.description().isEmpty());
	EXPECT_TRUE(gap.category().contains(olive::Node::k_category_timeline));
}

TEST(GapBlock, DefaultLengthIsZero)
{
	olive::GapBlock gap;

	EXPECT_EQ(gap.length(), olive::core::Rational(0));
}

TEST_F(NodeTimeTest, GapBlockStoresRationalLength)
{
	auto *gap = add_node<olive::GapBlock>();

	gap->set_length_and_media_out(olive::core::Rational(7, 2));
	EXPECT_EQ(gap->length(), olive::core::Rational(7, 2));

	gap->set_length_and_media_out(olive::core::Rational(0));
	EXPECT_EQ(gap->length(), olive::core::Rational(0));
}

TEST(TimeOffsetNode, Metadata)
{
	olive::TimeOffsetNode offset;

	EXPECT_EQ(offset.name(), QStringLiteral("Time Offset"));
	EXPECT_EQ(offset.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.timeoffset"));
	EXPECT_FALSE(offset.description().isEmpty());
	EXPECT_TRUE(offset.category().contains(olive::Node::k_category_time));
}

TEST(TimeOffsetNode, InputFlags)
{
	olive::TimeOffsetNode offset;

	// The time parameter is keyframable but cannot take an edge
	EXPECT_FALSE(offset.is_input_connectable(olive::TimeOffsetNode::k_time_input));
	EXPECT_TRUE(offset.is_input_keyframable(olive::TimeOffsetNode::k_time_input));

	// The data input takes an edge but cannot be keyframed
	EXPECT_TRUE(offset.is_input_connectable(olive::TimeOffsetNode::k_input_input));
	EXPECT_FALSE(offset.is_input_keyframable(olive::TimeOffsetNode::k_input_input));

	// The time offset defaults to zero
	EXPECT_EQ(offset.get_standard_value(olive::TimeOffsetNode::k_time_input)
				  .value<olive::core::Rational>(),
			  olive::core::Rational(0));
}

TEST(TimeOffsetNode, RetranslateSetsInputNames)
{
	olive::TimeOffsetNode offset;

	offset.retranslate();

	EXPECT_EQ(offset.get_input_name(olive::TimeOffsetNode::k_time_input),
			  QStringLiteral("Time"));
	EXPECT_EQ(offset.get_input_name(olive::TimeOffsetNode::k_input_input),
			  QStringLiteral("Input"));
}

TEST_F(NodeTimeTest, TimeOffsetAppliesStaticOffset)
{
	auto *offset = add_node<olive::TimeOffsetNode>();
	offset->set_standard_value(olive::TimeOffsetNode::k_time_input,
							 QVariant::fromValue(olive::core::Rational(3)));

	// The connected input is evaluated offset seconds later
	EXPECT_EQ(offset->input_time_adjustment(
				  olive::TimeOffsetNode::k_input_input, -1,
				  olive::TimeRange(olive::core::Rational(2),
								   olive::core::Rational(4)),
				  true),
			  olive::TimeRange(olive::core::Rational(5),
							   olive::core::Rational(7)));

	// Any other input passes time through unchanged
	const olive::TimeRange range(olive::core::Rational(2),
								 olive::core::Rational(4));
	EXPECT_EQ(offset->input_time_adjustment(olive::TimeOffsetNode::k_time_input,
										  -1, range, true),
			  range);
}

TEST_F(NodeTimeTest, TimeOffsetAppliesNegativeOffset)
{
	auto *offset = add_node<olive::TimeOffsetNode>();
	offset->set_standard_value(olive::TimeOffsetNode::k_time_input,
							 QVariant::fromValue(olive::core::Rational(-3)));

	// Negative offsets move the requested time before the sequence time,
	// even across zero
	EXPECT_EQ(offset->input_time_adjustment(
				  olive::TimeOffsetNode::k_input_input, -1,
				  olive::TimeRange(olive::core::Rational(2),
								   olive::core::Rational(4)),
				  true),
			  olive::TimeRange(olive::core::Rational(-1),
							   olive::core::Rational(1)));
}

TEST_F(NodeTimeTest, TimeOffsetZeroOffsetIsIdentity)
{
	auto *offset = add_node<olive::TimeOffsetNode>();

	const olive::TimeRange range(olive::core::Rational(2),
								 olive::core::Rational(4));
	EXPECT_EQ(offset->input_time_adjustment(olive::TimeOffsetNode::k_input_input,
										  -1, range, true),
			  range);
}

TEST_F(NodeTimeTest, TimeOffsetOutputAdjustmentAppliesInverseOffset)
{
	auto *offset = add_node<olive::TimeOffsetNode>();
	offset->set_standard_value(olive::TimeOffsetNode::k_time_input,
							 QVariant::fromValue(olive::core::Rational(3)));

	// The inverse mapping subtracts the offset again: input-side times are
	// mapped back to the output by the negated offset
	EXPECT_EQ(offset->output_time_adjustment(
				  olive::TimeOffsetNode::k_input_input, -1,
				  olive::TimeRange(olive::core::Rational(2),
								   olive::core::Rational(4))),
			  olive::TimeRange(olive::core::Rational(-1),
							   olive::core::Rational(1)));

	// Non-input inputs never adjust time
	const olive::TimeRange range(olive::core::Rational(2),
								 olive::core::Rational(4));
	EXPECT_EQ(offset->output_time_adjustment(olive::TimeOffsetNode::k_time_input,
										   -1, range),
			  range);

	// Round trip through both adjustments returns the original range
	EXPECT_EQ(offset->output_time_adjustment(
				  olive::TimeOffsetNode::k_input_input, -1,
				  offset->input_time_adjustment(
					  olive::TimeOffsetNode::k_input_input, -1, range, true)),
			  range);
}

TEST_F(NodeTimeTest, TimeOffsetAppliesKeyframedOffset)
{
	auto *offset = add_node<olive::TimeOffsetNode>();
	offset->set_input_is_keyframing(olive::TimeOffsetNode::k_time_input, true);

	add_key(offset, olive::TimeOffsetNode::k_time_input, olive::core::Rational(0),
		   QVariant::fromValue(olive::core::Rational(0)));
	add_key(offset, olive::TimeOffsetNode::k_time_input,
		   olive::core::Rational(10),
		   QVariant::fromValue(olive::core::Rational(10)));

	// The offset is sampled per endpoint: 2 + 2 = 4 and 4 + 4 = 8
	EXPECT_EQ(offset->input_time_adjustment(
				  olive::TimeOffsetNode::k_input_input, -1,
				  olive::TimeRange(olive::core::Rational(2),
								   olive::core::Rational(4)),
				  true),
			  olive::TimeRange(olive::core::Rational(4),
							   olive::core::Rational(8)));
}

TEST_F(NodeTimeTest, TimeOffsetValuePassesConnectedInputThrough)
{
	auto *offset = add_node<olive::TimeOffsetNode>();
	offset->set_standard_value(olive::TimeOffsetNode::k_time_input,
							 QVariant::fromValue(olive::core::Rational(5)));

	auto *time = add_node<olive::TimeInput>();
	olive::Node::connect_edge(
		time, olive::NodeInput(offset, olive::TimeOffsetNode::k_input_input));

	// The connected node is evaluated at the offset time: 3 + 5 = 8
	const olive::NodeValueTable table = generate_table(offset, olive::core::Rational(3));
	EXPECT_DOUBLE_EQ(table.get(olive::NodeValue::k_float).to_double(), 8.0);
}

TEST_F(NodeTimeTest, TimeOffsetValueWithoutConnectionProducesNoFloat)
{
	auto *offset = add_node<olive::TimeOffsetNode>();

	// With nothing connected the node pushes the (typeless) standard value
	const olive::NodeValueTable table = generate_table(offset, olive::core::Rational(3));
	EXPECT_EQ(table.get(olive::NodeValue::k_float).type(),
			  olive::NodeValue::k_none);
}

TEST(TimeRemapNode, Metadata)
{
	olive::TimeRemapNode remap;

	EXPECT_EQ(remap.name(), QStringLiteral("Time Remap"));
	EXPECT_EQ(remap.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.timeremap"));
	EXPECT_FALSE(remap.description().isEmpty());
	EXPECT_TRUE(remap.category().contains(olive::Node::k_category_time));
}

TEST(TimeRemapNode, InputFlags)
{
	olive::TimeRemapNode remap;

	EXPECT_FALSE(remap.is_input_connectable(olive::TimeRemapNode::k_time_input));
	EXPECT_TRUE(remap.is_input_keyframable(olive::TimeRemapNode::k_time_input));

	EXPECT_TRUE(remap.is_input_connectable(olive::TimeRemapNode::k_input_input));
	EXPECT_FALSE(remap.is_input_keyframable(olive::TimeRemapNode::k_input_input));

	EXPECT_EQ(remap.get_standard_value(olive::TimeRemapNode::k_time_input)
				  .value<olive::core::Rational>(),
			  olive::core::Rational(0));
}

TEST(TimeRemapNode, RetranslateSetsInputNames)
{
	olive::TimeRemapNode remap;

	remap.retranslate();

	EXPECT_EQ(remap.get_input_name(olive::TimeRemapNode::k_time_input),
			  QStringLiteral("Time"));
	EXPECT_EQ(remap.get_input_name(olive::TimeRemapNode::k_input_input),
			  QStringLiteral("Input"));
}

TEST_F(NodeTimeTest, TimeRemapStaticTimeCollapsesRange)
{
	auto *remap = add_node<olive::TimeRemapNode>();
	remap->set_standard_value(olive::TimeRemapNode::k_time_input,
							QVariant::fromValue(olive::core::Rational(7)));

	// A constant remap maps every sequence time onto the same media time
	const olive::TimeRange adjusted = remap->input_time_adjustment(
		olive::TimeRemapNode::k_input_input, -1,
		olive::TimeRange(olive::core::Rational(2), olive::core::Rational(4)),
		true);
	EXPECT_EQ(adjusted.in(), olive::core::Rational(7));
	EXPECT_EQ(adjusted.out(), olive::core::Rational(7));
}

TEST_F(NodeTimeTest, TimeRemapNonInputPassesThrough)
{
	auto *remap = add_node<olive::TimeRemapNode>();
	remap->set_standard_value(olive::TimeRemapNode::k_time_input,
							QVariant::fromValue(olive::core::Rational(7)));

	const olive::TimeRange range(olive::core::Rational(2),
								 olive::core::Rational(4));
	EXPECT_EQ(remap->input_time_adjustment(olive::TimeRemapNode::k_time_input, -1,
										 range, true),
			  range);
	EXPECT_EQ(remap->output_time_adjustment(olive::TimeRemapNode::k_input_input,
										  -1, range),
			  range);
	EXPECT_EQ(remap->output_time_adjustment(olive::TimeRemapNode::k_time_input, -1,
										  range),
			  range);
}

TEST_F(NodeTimeTest, TimeRemapAppliesKeyframedLinearRamp)
{
	auto *remap = add_node<olive::TimeRemapNode>();
	remap->set_input_is_keyframing(olive::TimeRemapNode::k_time_input, true);

	add_key(remap, olive::TimeRemapNode::k_time_input, olive::core::Rational(0),
		   QVariant::fromValue(olive::core::Rational(0)));
	add_key(remap, olive::TimeRemapNode::k_time_input, olive::core::Rational(10),
		   QVariant::fromValue(olive::core::Rational(100)));

	// The linear ramp multiplies time by ten: 2 -> 20 and 4 -> 40
	EXPECT_EQ(remap->input_time_adjustment(
				  olive::TimeRemapNode::k_input_input, -1,
				  olive::TimeRange(olive::core::Rational(2),
								   olive::core::Rational(4)),
				  true),
			  olive::TimeRange(olive::core::Rational(20),
							   olive::core::Rational(40)));
}

TEST_F(NodeTimeTest, TimeRemapReversedRampNormalizesRange)
{
	auto *remap = add_node<olive::TimeRemapNode>();
	remap->set_input_is_keyframing(olive::TimeRemapNode::k_time_input, true);

	add_key(remap, olive::TimeRemapNode::k_time_input, olive::core::Rational(0),
		   QVariant::fromValue(olive::core::Rational(10)));
	add_key(remap, olive::TimeRemapNode::k_time_input, olive::core::Rational(10),
		   QVariant::fromValue(olive::core::Rational(0)));

	// Decreasing time values invert the range: 2 -> 8 and 4 -> 6, which
	// TimeRange normalizes back to [6, 8]
	EXPECT_EQ(remap->input_time_adjustment(
				  olive::TimeRemapNode::k_input_input, -1,
				  olive::TimeRange(olive::core::Rational(2),
								   olive::core::Rational(4)),
				  true),
			  olive::TimeRange(olive::core::Rational(6),
							   olive::core::Rational(8)));
}

TEST_F(NodeTimeTest, TimeRemapValuePassesConnectedInputThrough)
{
	auto *remap = add_node<olive::TimeRemapNode>();
	remap->set_input_is_keyframing(olive::TimeRemapNode::k_time_input, true);

	add_key(remap, olive::TimeRemapNode::k_time_input, olive::core::Rational(0),
		   QVariant::fromValue(olive::core::Rational(0)));
	add_key(remap, olive::TimeRemapNode::k_time_input, olive::core::Rational(10),
		   QVariant::fromValue(olive::core::Rational(100)));

	auto *time = add_node<olive::TimeInput>();
	olive::Node::connect_edge(
		time, olive::NodeInput(remap, olive::TimeRemapNode::k_input_input));

	// The connected node is evaluated at the remapped time: 3 -> 30
	const olive::NodeValueTable table = generate_table(remap, olive::core::Rational(3));
	EXPECT_DOUBLE_EQ(table.get(olive::NodeValue::k_float).to_double(), 30.0);
}

TEST(TimeFormatNode, Metadata)
{
	olive::TimeFormatNode format;

	EXPECT_EQ(format.name(), QStringLiteral("Time Format"));
	EXPECT_EQ(format.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.timeformat"));
	EXPECT_FALSE(format.description().isEmpty());
	EXPECT_TRUE(format.category().contains(olive::Node::k_category_generator));
}

TEST(TimeFormatNode, RetranslateSetsInputNames)
{
	olive::TimeFormatNode format;

	format.retranslate();

	EXPECT_EQ(format.get_input_name(olive::TimeFormatNode::k_time_input),
			  QStringLiteral("Time"));
	EXPECT_EQ(format.get_input_name(olive::TimeFormatNode::k_format_input),
			  QStringLiteral("Format"));
	EXPECT_EQ(format.get_input_name(olive::TimeFormatNode::k_local_time_input),
			  QStringLiteral("Interpret time as local time"));
}

TEST_F(NodeTimeTest, TimeFormatDefaultsToUtcEpoch)
{
	auto *format = add_node<olive::TimeFormatNode>();

	// Local time interpretation is off by default, keeping output
	// independent of the machine timezone
	EXPECT_FALSE(format->get_standard_value(olive::TimeFormatNode::k_local_time_input)
					 .toBool());
	EXPECT_EQ(format->get_standard_value(olive::TimeFormatNode::k_format_input)
				  .toString(),
			  QStringLiteral("hh:mm:ss"));

	// A null time value behaves as 0, the Unix epoch
	const olive::NodeValueTable table = generate_table(format, olive::core::Rational(0));
	EXPECT_EQ(table.get(olive::NodeValue::k_text).to_string(),
			  QStringLiteral("00:00:00"));
}

TEST_F(NodeTimeTest, TimeFormatFormatsUtcTime)
{
	auto *format = add_node<olive::TimeFormatNode>();
	format->set_standard_value(olive::TimeFormatNode::k_time_input, 3661.0);

	const olive::NodeValueTable table = generate_table(format, olive::core::Rational(0));
	EXPECT_EQ(table.get(olive::NodeValue::k_text).to_string(),
			  QStringLiteral("01:01:01"));
}

TEST_F(NodeTimeTest, TimeFormatHonorsCustomFormat)
{
	auto *format = add_node<olive::TimeFormatNode>();
	format->set_standard_value(olive::TimeFormatNode::k_time_input, 3661.0);
	format->set_standard_value(olive::TimeFormatNode::k_format_input,
							 QStringLiteral("yyyy-MM-dd mm:ss"));

	// The custom format replaces the default; 3661s = 01:01:01 UTC
	const olive::NodeValueTable table = generate_table(format, olive::core::Rational(0));
	EXPECT_EQ(table.get(olive::NodeValue::k_text).to_string(),
			  QStringLiteral("1970-01-01 01:01"));
}

TEST_F(NodeTimeTest, TimeFormatFormatsNegativeTimeBeforeEpoch)
{
	auto *format = add_node<olive::TimeFormatNode>();
	format->set_standard_value(olive::TimeFormatNode::k_time_input, -1.0);
	format->set_standard_value(olive::TimeFormatNode::k_format_input,
							 QStringLiteral("yyyy-MM-dd hh:mm:ss"));

	// One second before the epoch
	const olive::NodeValueTable table = generate_table(format, olive::core::Rational(0));
	EXPECT_EQ(table.get(olive::NodeValue::k_text).to_string(),
			  QStringLiteral("1969-12-31 23:59:59"));
}
