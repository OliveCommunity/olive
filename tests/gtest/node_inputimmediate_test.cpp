#include <gtest/gtest.h>

#include <memory>

#include <QObject>
#include <QPointF>
#include <QVector2D>

#include "node/color/colormanager/colormanager.h"
#include "node/generator/matrix/matrix.h"
#include "node/generator/solid/solid.h"
#include "node/inputimmediate.h"
#include "node/keyframe.h"
#include "node/math/math/math.h"
#include "node/node.h"
#include "node/project.h"
#include "node/time/timeoffset/timeoffsetnode.h"
#include "undo/undocommand.h"

namespace
{

// NodeInputImmediate is not a QObject, so keyframes inserted directly into it
// (rather than through parenting to a Node) must be removed and deleted by
// hand to avoid leaks.
void clear_immediate(olive::NodeInputImmediate *imm)
{
	for (int i = 0; i < imm->keyframe_tracks().size(); i++) {
		const QVector<olive::NodeKeyframe *> keys = imm->keyframe_tracks().at(i);
		for (olive::NodeKeyframe *key : keys) {
			imm->remove_keyframe(key);
			delete key;
		}
	}
}

olive::NodeKeyframe *make_key(const olive::Rational &time, const QVariant &value,
							 int track,
							 olive::NodeKeyframe::Type type =
								 olive::NodeKeyframe::k_linear)
{
	return new olive::NodeKeyframe(time, value, type, track, -1,
								   QStringLiteral("test_in"));
}

} // namespace

TEST(NodeInputImmediate, StandardValueRoundTrip)
{
	olive::NodeInputImmediate imm(olive::NodeValue::k_float, { 1.5 });

	EXPECT_FALSE(imm.is_keyframing());
	EXPECT_TRUE(imm.is_using_standard_value(0));

	// The default value seeds the standard value
	ASSERT_EQ(imm.get_split_standard_value().size(), 1);
	EXPECT_DOUBLE_EQ(imm.get_split_standard_value().at(0).toDouble(), 1.5);
	EXPECT_DOUBLE_EQ(imm.get_split_standard_value_on_track(0).toDouble(), 1.5);

	imm.set_split_standard_value({ 2.5 });
	EXPECT_DOUBLE_EQ(imm.get_split_standard_value().at(0).toDouble(), 2.5);

	imm.set_standard_value_on_track(3.5, 0);
	EXPECT_DOUBLE_EQ(imm.get_split_standard_value_on_track(0).toDouble(), 3.5);
}

TEST(NodeInputImmediate, SetSplitStandardValueCopiesOnlyOverlappingTracks)
{
	olive::NodeInputImmediate imm(olive::NodeValue::k_vec2, { 0.0, 0.0 });
	ASSERT_EQ(imm.get_split_standard_value().size(), 2);

	// A shorter split only overwrites the tracks it covers
	imm.set_split_standard_value({ 5.0 });
	EXPECT_DOUBLE_EQ(imm.get_split_standard_value_on_track(0).toDouble(), 5.0);
	EXPECT_DOUBLE_EQ(imm.get_split_standard_value_on_track(1).toDouble(), 0.0);

	// A longer split is clamped to the existing track count
	imm.set_split_standard_value({ 1.0, 2.0, 3.0 });
	ASSERT_EQ(imm.get_split_standard_value().size(), 2);
	EXPECT_DOUBLE_EQ(imm.get_split_standard_value_on_track(0).toDouble(), 1.0);
	EXPECT_DOUBLE_EQ(imm.get_split_standard_value_on_track(1).toDouble(), 2.0);
}

TEST(NodeInputImmediate, SetDataTypeResizesTracksAndReappliesDefault)
{
	olive::NodeInputImmediate imm(olive::NodeValue::k_float, { 7.0 });
	ASSERT_EQ(imm.keyframe_tracks().size(), 1);
	ASSERT_EQ(imm.get_split_standard_value().size(), 1);

	// Growing to a four-track type keeps the default on the first track and
	// leaves the new tracks null, since the default split only has one entry
	imm.set_data_type(olive::NodeValue::k_vec4);
	EXPECT_EQ(imm.keyframe_tracks().size(), 4);
	ASSERT_EQ(imm.get_split_standard_value().size(), 4);
	EXPECT_DOUBLE_EQ(imm.get_split_standard_value_on_track(0).toDouble(), 7.0);
	EXPECT_TRUE(imm.get_split_standard_value_on_track(1).isNull());
	EXPECT_TRUE(imm.get_split_standard_value_on_track(2).isNull());
	EXPECT_TRUE(imm.get_split_standard_value_on_track(3).isNull());

	imm.set_data_type(olive::NodeValue::k_float);
	EXPECT_EQ(imm.keyframe_tracks().size(), 1);
	ASSERT_EQ(imm.get_split_standard_value().size(), 1);
	EXPECT_DOUBLE_EQ(imm.get_split_standard_value_on_track(0).toDouble(), 7.0);
}

TEST(NodeInputImmediate, KeyframeLookupRequiresKeyframingEnabled)
{
	olive::NodeInputImmediate imm(olive::NodeValue::k_float, { 0.0 });

	olive::NodeKeyframe *key = make_key(olive::Rational(2), 1.0, 0);
	imm.insert_keyframe(key);

	// Without keyframing enabled the track reports that it uses the standard
	// value and all keyframe lookups come back empty
	EXPECT_TRUE(imm.is_using_standard_value(0));
	EXPECT_FALSE(imm.has_keyframe_at_time(olive::Rational(2)));
	EXPECT_EQ(imm.get_keyframe_at_time_on_track(olive::Rational(2), 0),
			  nullptr);
	EXPECT_TRUE(imm.get_keyframe_at_time(olive::Rational(2)).isEmpty());
	EXPECT_EQ(imm.get_closest_keyframe_to_time_on_track(olive::Rational(2), 0),
			  nullptr);

	imm.set_is_keyframing(true);
	EXPECT_FALSE(imm.is_using_standard_value(0));
	EXPECT_TRUE(imm.has_keyframe_at_time(olive::Rational(2)));
	EXPECT_EQ(imm.get_keyframe_at_time_on_track(olive::Rational(2), 0), key);
	ASSERT_EQ(imm.get_keyframe_at_time(olive::Rational(2)).size(), 1);
	EXPECT_EQ(imm.get_keyframe_at_time(olive::Rational(2)).first(), key);
	EXPECT_EQ(imm.get_closest_keyframe_to_time_on_track(olive::Rational(2), 0),
			  key);
	EXPECT_FALSE(imm.has_keyframe_at_time(olive::Rational(3)));

	clear_immediate(&imm);
}

TEST(NodeInputImmediate, InsertKeyframeSortsByTimeAndLinksSiblings)
{
	olive::NodeInputImmediate imm(olive::NodeValue::k_float, { 0.0 });

	// Insert out of order; the track must stay sorted by time
	olive::NodeKeyframe *key_late = make_key(olive::Rational(10), 10.0, 0);
	olive::NodeKeyframe *key_early = make_key(olive::Rational(0), 0.0, 0);
	olive::NodeKeyframe *key_mid = make_key(olive::Rational(5), 5.0, 0);
	imm.insert_keyframe(key_late);
	imm.insert_keyframe(key_early);
	imm.insert_keyframe(key_mid);

	const olive::NodeKeyframeTrack &track = imm.keyframe_tracks().at(0);
	ASSERT_EQ(track.size(), 3);
	EXPECT_EQ(track.at(0), key_early);
	EXPECT_EQ(track.at(1), key_mid);
	EXPECT_EQ(track.at(2), key_late);

	// Sibling links follow the sorted order
	EXPECT_EQ(key_early->previous(), nullptr);
	EXPECT_EQ(key_early->next(), key_mid);
	EXPECT_EQ(key_mid->previous(), key_early);
	EXPECT_EQ(key_mid->next(), key_late);
	EXPECT_EQ(key_late->previous(), key_mid);
	EXPECT_EQ(key_late->next(), nullptr);

	EXPECT_EQ(imm.get_earliest_keyframe(), key_early);
	EXPECT_EQ(imm.get_latest_keyframe(), key_late);

	// Removing the middle keyframe re-links its siblings
	imm.remove_keyframe(key_mid);
	EXPECT_EQ(key_early->next(), key_late);
	EXPECT_EQ(key_late->previous(), key_early);
	EXPECT_EQ(key_mid->previous(), nullptr);
	EXPECT_EQ(key_mid->next(), nullptr);
	ASSERT_EQ(track.size(), 2);
	delete key_mid;

	clear_immediate(&imm);
}

TEST(NodeInputImmediate, ClosestKeyframeToTimeOnTrackClampsAndPicksNearest)
{
	olive::NodeInputImmediate imm(olive::NodeValue::k_vec2, { 0.0, 0.0 });
	imm.set_is_keyframing(true);

	olive::NodeKeyframe *key_a = make_key(olive::Rational(0), 0.0, 0);
	olive::NodeKeyframe *key_b = make_key(olive::Rational(10), 10.0, 0);
	imm.insert_keyframe(key_a);
	imm.insert_keyframe(key_b);

	// Outside the keyed range the closest keyframe clamps to the ends
	EXPECT_EQ(imm.get_closest_keyframe_to_time_on_track(olive::Rational(-3), 0),
			  key_a);
	EXPECT_EQ(imm.get_closest_keyframe_to_time_on_track(olive::Rational(20), 0),
			  key_b);

	// Between the keys the nearer one wins
	EXPECT_EQ(imm.get_closest_keyframe_to_time_on_track(olive::Rational(3), 0),
			  key_a);
	EXPECT_EQ(imm.get_closest_keyframe_to_time_on_track(olive::Rational(7), 0),
			  key_b);

	// Exactly halfway the earlier keyframe wins the tie
	EXPECT_EQ(imm.get_closest_keyframe_to_time_on_track(olive::Rational(5), 0),
			  key_a);

	// A track with no keyframes still counts as using the standard value
	EXPECT_EQ(imm.get_closest_keyframe_to_time_on_track(olive::Rational(5), 1),
			  nullptr);

	clear_immediate(&imm);
}

TEST(NodeInputImmediate, ClosestKeyframeBeforeAfterSpansAllTracks)
{
	olive::NodeInputImmediate imm(olive::NodeValue::k_vec2, { 0.0, 0.0 });
	imm.set_is_keyframing(true);

	olive::NodeKeyframe *key_t0 = make_key(olive::Rational(0), 0.0, 0);
	olive::NodeKeyframe *key_t10 = make_key(olive::Rational(10), 10.0, 0);
	olive::NodeKeyframe *key_t4_track1 = make_key(olive::Rational(4), 4.0, 1);
	imm.insert_keyframe(key_t0);
	imm.insert_keyframe(key_t10);
	imm.insert_keyframe(key_t4_track1);

	// The closest keyframe before 5 is the one at 4 on the other track
	EXPECT_EQ(imm.get_closest_keyframe_before_time(olive::Rational(5)),
			  key_t4_track1);
	EXPECT_EQ(imm.get_closest_keyframe_after_time(olive::Rational(5)), key_t10);

	// Strictly before/after: nothing exists outside the keyed range
	EXPECT_EQ(imm.get_closest_keyframe_before_time(olive::Rational(0)),
			  nullptr);
	EXPECT_EQ(imm.get_closest_keyframe_after_time(olive::Rational(10)),
			  nullptr);

	EXPECT_EQ(imm.get_closest_keyframe_before_time(olive::Rational(4)), key_t0);
	EXPECT_EQ(imm.get_closest_keyframe_after_time(olive::Rational(4)), key_t10);

	clear_immediate(&imm);
}

TEST(NodeInputImmediate, BestKeyframeTypeForTimeFollowsClosestKey)
{
	olive::NodeInputImmediate imm(olive::NodeValue::k_float, { 0.0 });

	// With no keyframes there is no reference, so the default type is used
	EXPECT_EQ(int(imm.get_best_keyframe_type_for_time(olive::Rational(5), 0)),
			  int(olive::NodeKeyframe::k_default_type));

	olive::NodeKeyframe *key_hold =
		make_key(olive::Rational(0), 0.0, 0, olive::NodeKeyframe::k_hold);
	olive::NodeKeyframe *key_linear = make_key(olive::Rational(10), 10.0, 0);
	imm.insert_keyframe(key_hold);
	imm.insert_keyframe(key_linear);
	imm.set_is_keyframing(true);

	EXPECT_EQ(int(imm.get_best_keyframe_type_for_time(olive::Rational(2), 0)),
			  int(olive::NodeKeyframe::k_hold));
	EXPECT_EQ(int(imm.get_best_keyframe_type_for_time(olive::Rational(8), 0)),
			  int(olive::NodeKeyframe::k_linear));

	clear_immediate(&imm);
}

TEST(NodeInputImmediate, GetKeyframeAtTimeAggregatesAcrossTracks)
{
	olive::NodeInputImmediate imm(olive::NodeValue::k_vec2, { 0.0, 0.0 });
	imm.set_is_keyframing(true);

	olive::NodeKeyframe *key_track0 = make_key(olive::Rational(3), 1.0, 0);
	olive::NodeKeyframe *key_track1 = make_key(olive::Rational(3), 2.0, 1);
	olive::NodeKeyframe *key_later = make_key(olive::Rational(7), 3.0, 0);
	imm.insert_keyframe(key_track0);
	imm.insert_keyframe(key_track1);
	imm.insert_keyframe(key_later);

	// Both tracks have a keyframe at t=3
	QVector<olive::NodeKeyframe *> at_three =
		imm.get_keyframe_at_time(olive::Rational(3));
	ASSERT_EQ(at_three.size(), 2);
	EXPECT_TRUE(at_three.contains(key_track0));
	EXPECT_TRUE(at_three.contains(key_track1));

	// Only track 0 has one at t=7, and there is nothing at t=99
	EXPECT_EQ(imm.get_keyframe_at_time(olive::Rational(7)).size(), 1);
	EXPECT_TRUE(imm.get_keyframe_at_time(olive::Rational(99)).isEmpty());

	clear_immediate(&imm);
}

class NodeInputImmediateNodeTest : public ::testing::Test {
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
								const olive::Rational &time,
								const QVariant &value, int track,
								olive::NodeKeyframe::Type type =
									olive::NodeKeyframe::k_linear)
	{
		auto *key = new olive::NodeKeyframe(time, value, type, track, -1, input);
		key->setParent(node);
		return key;
	}

	std::unique_ptr<olive::Project> project_;
};

TEST_F(NodeInputImmediateNodeTest, SetValueAtTimeCreatesAndUpdatesKeyframes)
{
	auto *node = add_node<olive::MathNode>();
	const olive::NodeInput input(node, olive::MathNode::k_param_a_in);
	node->set_input_is_keyframing(olive::MathNode::k_param_a_in, true);

	// A new keyframe is inserted where none exists yet
	olive::MultiUndoCommand cmd;
	olive::Node::set_value_at_time(input, olive::Rational(5), 42.0, 0, &cmd, true);
	EXPECT_EQ(cmd.child_count(), 1);
	cmd.redo_now();

	olive::NodeKeyframe *key = node->get_keyframe_at_time_on_track(
		olive::MathNode::k_param_a_in, olive::Rational(5), 0);
	ASSERT_NE(key, nullptr);
	EXPECT_DOUBLE_EQ(key->value().toDouble(), 42.0);
	EXPECT_DOUBLE_EQ(node->get_value_at_time(olive::MathNode::k_param_a_in,
										  olive::Rational(5))
						 .toDouble(),
					 42.0);

	// Setting the same time again updates the existing keyframe in place
	olive::MultiUndoCommand update_cmd;
	olive::Node::set_value_at_time(input, olive::Rational(5), 43.0, 0,
								&update_cmd, true);
	EXPECT_EQ(update_cmd.child_count(), 1);
	update_cmd.redo_now();

	const QVector<olive::NodeKeyframeTrack> &tracks =
		node->get_keyframe_tracks(olive::MathNode::k_param_a_in, -1);
	ASSERT_EQ(tracks.at(0).size(), 1);
	EXPECT_EQ(tracks.at(0).first(), key);
	EXPECT_DOUBLE_EQ(key->value().toDouble(), 43.0);
}

TEST_F(NodeInputImmediateNodeTest, SetValueAtTimeWithoutKeyframingSetsStandardValue)
{
	auto *node = add_node<olive::MathNode>();
	const olive::NodeInput input(node, olive::MathNode::k_param_a_in);

	olive::MultiUndoCommand cmd;
	olive::Node::set_value_at_time(input, olive::Rational(5), 9.0, 0, &cmd, true);
	EXPECT_EQ(cmd.child_count(), 1);
	cmd.redo_now();

	EXPECT_DOUBLE_EQ(
		node->get_standard_value(olive::MathNode::k_param_a_in).toDouble(), 9.0);
	EXPECT_TRUE(node->get_keyframe_tracks(olive::MathNode::k_param_a_in, -1)
					.at(0)
					.isEmpty());
}

TEST_F(NodeInputImmediateNodeTest, SetValueAtTimeInsertsOnAllTracksOnlyWhenAsked)
{
	auto *solid = add_node<olive::SolidGenerator>();
	solid->set_input_is_keyframing(olive::SolidGenerator::k_color_input, true);
	const olive::NodeInput input(solid, olive::SolidGenerator::k_color_input);

	// With insert_on_all_tracks_if_no_key set, keyframes are created on every
	// track; sibling tracks capture the value they currently evaluate to (the
	// standard value red = (1, 0, 0, 1) here)
	olive::MultiUndoCommand cmd;
	olive::Node::set_value_at_time(input, olive::Rational(5), 0.5, 2, &cmd, true);
	EXPECT_EQ(cmd.child_count(), 4);
	cmd.redo_now();

	const QVector<olive::NodeKeyframeTrack> &tracks =
		solid->get_keyframe_tracks(olive::SolidGenerator::k_color_input, -1);
	ASSERT_EQ(tracks.size(), 4);
	for (int i = 0; i < tracks.size(); i++) {
		ASSERT_EQ(tracks.at(i).size(), 1);
		EXPECT_EQ(tracks.at(i).first()->time(), olive::Rational(5));
	}
	EXPECT_DOUBLE_EQ(tracks.at(0).first()->value().toDouble(), 1.0);
	EXPECT_DOUBLE_EQ(tracks.at(1).first()->value().toDouble(), 0.0);
	EXPECT_DOUBLE_EQ(tracks.at(2).first()->value().toDouble(), 0.5);
	EXPECT_DOUBLE_EQ(tracks.at(3).first()->value().toDouble(), 1.0);

	const olive::Color c =
		solid->get_value_at_time(olive::SolidGenerator::k_color_input,
							  olive::Rational(5))
			.value<olive::Color>();
	EXPECT_FLOAT_EQ(c.red(), 1.0f);
	EXPECT_FLOAT_EQ(c.green(), 0.0f);
	EXPECT_FLOAT_EQ(c.blue(), 0.5f);
	EXPECT_FLOAT_EQ(c.alpha(), 1.0f);

	// Without the flag only the requested track receives a keyframe
	auto *single = add_node<olive::SolidGenerator>();
	single->set_input_is_keyframing(olive::SolidGenerator::k_color_input, true);
	const olive::NodeInput single_input(single,
										olive::SolidGenerator::k_color_input);

	olive::MultiUndoCommand single_cmd;
	olive::Node::set_value_at_time(single_input, olive::Rational(5), 0.5, 2,
								&single_cmd, false);
	EXPECT_EQ(single_cmd.child_count(), 1);
	single_cmd.redo_now();

	const QVector<olive::NodeKeyframeTrack> &single_tracks =
		single->get_keyframe_tracks(olive::SolidGenerator::k_color_input, -1);
	ASSERT_EQ(single_tracks.size(), 4);
	EXPECT_TRUE(single_tracks.at(0).isEmpty());
	EXPECT_TRUE(single_tracks.at(1).isEmpty());
	ASSERT_EQ(single_tracks.at(2).size(), 1);
	EXPECT_DOUBLE_EQ(single_tracks.at(2).first()->value().toDouble(), 0.5);
	EXPECT_TRUE(single_tracks.at(3).isEmpty());
}

TEST_F(NodeInputImmediateNodeTest, GetValueAtTimeInterpolatesColorTracks)
{
	auto *solid = add_node<olive::SolidGenerator>();
	solid->set_input_is_keyframing(olive::SolidGenerator::k_color_input, true);

	// Black to white over ten seconds on all four tracks
	for (int track = 0; track < 4; track++) {
		add_key(solid, olive::SolidGenerator::k_color_input, olive::Rational(0),
			   0.0, track);
		add_key(solid, olive::SolidGenerator::k_color_input, olive::Rational(10),
			   1.0, track);
	}

	const olive::Color mid =
		solid->get_value_at_time(olive::SolidGenerator::k_color_input,
							  olive::Rational(5))
			.value<olive::Color>();
	EXPECT_FLOAT_EQ(mid.red(), 0.5f);
	EXPECT_FLOAT_EQ(mid.green(), 0.5f);
	EXPECT_FLOAT_EQ(mid.blue(), 0.5f);
	EXPECT_FLOAT_EQ(mid.alpha(), 0.5f);

	const olive::SplitValue split = solid->get_split_value_at_time(
		olive::SolidGenerator::k_color_input, olive::Rational(5));
	ASSERT_EQ(split.size(), 4);
	for (int i = 0; i < split.size(); i++) {
		EXPECT_DOUBLE_EQ(split.at(i).toDouble(), 0.5);
	}

	// Outside the keyed range the end values hold
	const olive::Color before =
		solid->get_value_at_time(olive::SolidGenerator::k_color_input,
							  olive::Rational(-2))
			.value<olive::Color>();
	EXPECT_FLOAT_EQ(before.red(), 0.0f);
	const olive::Color after =
		solid->get_value_at_time(olive::SolidGenerator::k_color_input,
							  olive::Rational(20))
			.value<olive::Color>();
	EXPECT_FLOAT_EQ(after.alpha(), 1.0f);
}

TEST_F(NodeInputImmediateNodeTest, GetValueAtTimeInterpolatesVec2Tracks)
{
	auto *matrix = add_node<olive::MatrixGenerator>();
	matrix->set_input_is_keyframing(olive::MatrixGenerator::k_position_input, true);

	add_key(matrix, olive::MatrixGenerator::k_position_input, olive::Rational(0),
		   0.0, 0);
	add_key(matrix, olive::MatrixGenerator::k_position_input, olive::Rational(10),
		   10.0, 0);
	add_key(matrix, olive::MatrixGenerator::k_position_input, olive::Rational(0),
		   10.0, 1);
	add_key(matrix, olive::MatrixGenerator::k_position_input, olive::Rational(10),
		   20.0, 1);

	const QVector2D mid =
		matrix->get_value_at_time(olive::MatrixGenerator::k_position_input,
							   olive::Rational(5))
			.value<QVector2D>();
	EXPECT_FLOAT_EQ(mid.x(), 5.0f);
	EXPECT_FLOAT_EQ(mid.y(), 15.0f);

	// Each track clamps to its own end keyframes
	const QVector2D clamped_low =
		matrix->get_value_at_time(olive::MatrixGenerator::k_position_input,
							   olive::Rational(-5))
			.value<QVector2D>();
	EXPECT_FLOAT_EQ(clamped_low.x(), 0.0f);
	EXPECT_FLOAT_EQ(clamped_low.y(), 10.0f);
	const QVector2D clamped_high =
		matrix->get_value_at_time(olive::MatrixGenerator::k_position_input,
							   olive::Rational(15))
			.value<QVector2D>();
	EXPECT_FLOAT_EQ(clamped_high.x(), 10.0f);
	EXPECT_FLOAT_EQ(clamped_high.y(), 20.0f);
}

TEST_F(NodeInputImmediateNodeTest, GetValueAtTimeInterpolatesRationalAsRational)
{
	auto *offset = add_node<olive::TimeOffsetNode>();
	offset->set_input_is_keyframing(olive::TimeOffsetNode::k_time_input, true);

	add_key(offset, olive::TimeOffsetNode::k_time_input, olive::Rational(0),
		   QVariant::fromValue(olive::Rational(0)), 0);
	add_key(offset, olive::TimeOffsetNode::k_time_input, olive::Rational(10),
		   QVariant::fromValue(olive::Rational(10)), 0);

	// The interpolated value is converted back into a Rational
	const QVariant mid = offset->get_value_at_time(
		olive::TimeOffsetNode::k_time_input, olive::Rational(5));
	EXPECT_EQ(mid.value<olive::Rational>(), olive::Rational(5));

	const QVariant one_tenth_in = offset->get_value_at_time(
		olive::TimeOffsetNode::k_time_input, olive::Rational(1));
	EXPECT_DOUBLE_EQ(one_tenth_in.value<olive::Rational>().to_double(), 1.0);
}

TEST_F(NodeInputImmediateNodeTest, GetValueAtTimeHoldsRationalUntilNextKey)
{
	auto *offset = add_node<olive::TimeOffsetNode>();
	offset->set_input_is_keyframing(olive::TimeOffsetNode::k_time_input, true);

	add_key(offset, olive::TimeOffsetNode::k_time_input, olive::Rational(0),
		   QVariant::fromValue(olive::Rational(2, 3)), 0,
		   olive::NodeKeyframe::k_hold);
	add_key(offset, olive::TimeOffsetNode::k_time_input, olive::Rational(10),
		   QVariant::fromValue(olive::Rational(4, 3)), 0);

	// A hold keyframe keeps its exact Rational value until the next key
	const QVariant held = offset->get_value_at_time(
		olive::TimeOffsetNode::k_time_input, olive::Rational(9));
	EXPECT_EQ(held.value<olive::Rational>(), olive::Rational(2, 3));

	const QVariant at_next = offset->get_value_at_time(
		olive::TimeOffsetNode::k_time_input, olive::Rational(10));
	EXPECT_EQ(at_next.value<olive::Rational>(), olive::Rational(4, 3));
}

TEST_F(NodeInputImmediateNodeTest, GetValueAtTimeBezierHandlesBendCurve)
{
	auto *node = add_node<olive::MathNode>();
	node->set_input_is_keyframing(olive::MathNode::k_param_a_in, true);

	olive::NodeKeyframe *before =
		add_key(node, olive::MathNode::k_param_a_in, olive::Rational(0), 0.0, 0,
			   olive::NodeKeyframe::k_bezier);
	olive::NodeKeyframe *after =
		add_key(node, olive::MathNode::k_param_a_in, olive::Rational(10), 10.0, 0,
			   olive::NodeKeyframe::k_bezier);

	// Ease-in shape: the outgoing handle pulls the start of the curve flat
	before->set_bezier_control_out(QPointF(2.5, 0.0));
	after->set_bezier_control_in(QPointF(0.0, 0.0));

	const double interpolated = node->get_value_at_time(
		olive::MathNode::k_param_a_in, olive::Rational(5)).toDouble();

	// Independent expectation, derived by hand from the control points
	// P0=(0,0), P1=(2.5,0), P2=(10,10), P3=(10,10):
	//   x(t) = 7.5*(1-t)^2*t + 30*(1-t)*t^2 + 10*t^3
	//        = -12.5*t^3 + 15*t^2 + 7.5*t
	//   x(t) = 5  =>  5*t^3 - 6*t^2 - 3*t + 2 = 0  =>  t = 0.4296537740156094
	//   y(t) = 30*(1-t)*t^2 + 10*t^3 = 30*t^2 - 20*t^3
	//        = 3.9517689049678255
	// (the production bisection solves x(t) to 1e-6, so allow 1e-4 slack)
	EXPECT_NEAR(interpolated, 3.9517689049678255, 1e-4);

	// The ease-in handle keeps the midpoint below the linear value of 5.0
	EXPECT_LT(interpolated, 5.0);
}

TEST_F(NodeInputImmediateNodeTest, GetValueAtTimeQuadraticBezierWithOneHandle)
{
	auto *node = add_node<olive::MathNode>();
	node->set_input_is_keyframing(olive::MathNode::k_param_a_in, true);

	// Bezier into linear uses a quadratic curve with a single control point
	olive::NodeKeyframe *before =
		add_key(node, olive::MathNode::k_param_a_in, olive::Rational(0), 0.0, 0,
			   olive::NodeKeyframe::k_bezier);
	add_key(node, olive::MathNode::k_param_a_in, olive::Rational(10), 10.0, 0,
		   olive::NodeKeyframe::k_linear);
	before->set_bezier_control_out(QPointF(2.5, 0.0));

	const double interpolated = node->get_value_at_time(
		olive::MathNode::k_param_a_in, olive::Rational(5)).toDouble();

	// Independent expectation, derived by hand from the single control point
	// CP=(2.5,0) between P0=(0,0) and P2=(10,10):
	//   x(t) = 2*(1-t)*t*2.5 + t^2*10 = 5*t + 5*t^2
	//   x(t) = 5  =>  t = (sqrt(5)-1)/2 = 0.6180339887498949
	//   y(t) = t^2*10 = 5*(3 - sqrt(5)) = 3.819660112501051
	// (the production bisection solves x(t) to 1e-6, so allow 1e-4 slack)
	EXPECT_NEAR(interpolated, 3.819660112501051, 1e-4);
	EXPECT_LT(interpolated, 5.0);
}

TEST_F(NodeInputImmediateNodeTest, IsUsingStandardValueTransitions)
{
	auto *node = add_node<olive::MathNode>();
	node->set_standard_value(olive::MathNode::k_param_a_in, 3.0);

	// Static input: standard value is always in use
	EXPECT_TRUE(node->is_using_standard_value(olive::MathNode::k_param_a_in, 0));

	// Keyframing enabled but no keyframes yet: still the standard value
	node->set_input_is_keyframing(olive::MathNode::k_param_a_in, true);
	EXPECT_TRUE(node->is_using_standard_value(olive::MathNode::k_param_a_in, 0));

	// With a keyframe present the track switches to the keyed value
	olive::NodeKeyframe *key =
		add_key(node, olive::MathNode::k_param_a_in, olive::Rational(5), 7.0, 0);
	EXPECT_FALSE(node->is_using_standard_value(olive::MathNode::k_param_a_in, 0));

	// Disabling keyframing hides the keyframes again
	node->set_input_is_keyframing(olive::MathNode::k_param_a_in, false);
	EXPECT_TRUE(node->is_using_standard_value(olive::MathNode::k_param_a_in, 0));
	EXPECT_DOUBLE_EQ(node->get_value_at_time(olive::MathNode::k_param_a_in,
										  olive::Rational(5))
						 .toDouble(),
					 3.0);

	// Removing the last keyframe returns the track to the standard value
	node->set_input_is_keyframing(olive::MathNode::k_param_a_in, true);
	EXPECT_FALSE(node->is_using_standard_value(olive::MathNode::k_param_a_in, 0));
	key->setParent(nullptr);
	delete key;
	EXPECT_TRUE(node->is_using_standard_value(olive::MathNode::k_param_a_in, 0));
	EXPECT_DOUBLE_EQ(node->get_value_at_time(olive::MathNode::k_param_a_in,
										  olive::Rational(5))
						 .toDouble(),
					 3.0);
}

TEST_F(NodeInputImmediateNodeTest, PartiallyKeyedTrackFallsBackToStandardValue)
{
	auto *matrix = add_node<olive::MatrixGenerator>();
	matrix->set_standard_value(olive::MatrixGenerator::k_position_input,
							 QVector2D(1.0f, 2.0f));
	matrix->set_input_is_keyframing(olive::MatrixGenerator::k_position_input, true);

	// Only the X track is keyed; the Y track keeps its standard value
	add_key(matrix, olive::MatrixGenerator::k_position_input, olive::Rational(0),
		   0.0, 0);
	add_key(matrix, olive::MatrixGenerator::k_position_input, olive::Rational(10),
		   10.0, 0);

	EXPECT_FALSE(
		matrix->is_using_standard_value(olive::MatrixGenerator::k_position_input,
									 0));
	EXPECT_TRUE(
		matrix->is_using_standard_value(olive::MatrixGenerator::k_position_input,
									 1));

	const QVector2D value =
		matrix->get_value_at_time(olive::MatrixGenerator::k_position_input,
							   olive::Rational(5))
			.value<QVector2D>();
	EXPECT_FLOAT_EQ(value.x(), 5.0f);
	EXPECT_FLOAT_EQ(value.y(), 2.0f);
}

TEST_F(NodeInputImmediateNodeTest, StandardValueCombinationAcrossTracks)
{
	auto *solid = add_node<olive::SolidGenerator>();

	// The declared default is opaque red
	olive::Color initial =
		solid->get_standard_value(olive::SolidGenerator::k_color_input)
			.value<olive::Color>();
	EXPECT_FLOAT_EQ(initial.red(), 1.0f);
	EXPECT_FLOAT_EQ(initial.green(), 0.0f);
	EXPECT_FLOAT_EQ(initial.blue(), 0.0f);
	EXPECT_FLOAT_EQ(initial.alpha(), 1.0f);

	// Setting a normal value splits it across the four tracks
	solid->set_standard_value(
		olive::SolidGenerator::k_color_input,
		QVariant::fromValue(olive::Color(0.25f, 0.5f, 0.75f, 1.0f)));
	const olive::SplitValue split =
		solid->get_split_standard_value(olive::SolidGenerator::k_color_input);
	ASSERT_EQ(split.size(), 4);
	EXPECT_DOUBLE_EQ(split.at(0).toDouble(), 0.25);
	EXPECT_DOUBLE_EQ(split.at(1).toDouble(), 0.5);
	EXPECT_DOUBLE_EQ(split.at(2).toDouble(), 0.75);
	EXPECT_DOUBLE_EQ(split.at(3).toDouble(), 1.0);
	EXPECT_DOUBLE_EQ(solid->get_split_standard_value_on_track(
						 olive::SolidGenerator::k_color_input, 2)
						 .toDouble(),
					 0.75);

	// A partial split only overwrites the leading tracks
	solid->set_split_standard_value(olive::SolidGenerator::k_color_input,
								 { 0.1, 0.2 });
	const olive::Color combined =
		solid->get_standard_value(olive::SolidGenerator::k_color_input)
			.value<olive::Color>();
	EXPECT_FLOAT_EQ(combined.red(), 0.1f);
	EXPECT_FLOAT_EQ(combined.green(), 0.2f);
	EXPECT_FLOAT_EQ(combined.blue(), 0.75f);
	EXPECT_FLOAT_EQ(combined.alpha(), 1.0f);
}

TEST_F(NodeInputImmediateNodeTest, DeleteAllKeyframesReparentsOrDeletes)
{
	auto *node = add_node<olive::MathNode>();
	node->set_input_is_keyframing(olive::MathNode::k_param_a_in, true);

	olive::NodeKeyframe *key_a =
		add_key(node, olive::MathNode::k_param_a_in, olive::Rational(0), 1.0, 0);
	olive::NodeKeyframe *key_b =
		add_key(node, olive::MathNode::k_param_a_in, olive::Rational(1), 2.0, 0);

	olive::NodeInputImmediate *imm =
		node->get_immediate(olive::MathNode::k_param_a_in, -1);
	ASSERT_NE(imm, nullptr);
	ASSERT_EQ(imm->keyframe_tracks().at(0).size(), 2);

	// With a parent the keyframes are handed over rather than deleted
	QObject guard;
	imm->delete_all_keyframes(&guard);
	EXPECT_TRUE(imm->keyframe_tracks().at(0).isEmpty());
	EXPECT_EQ(guard.children().size(), 2);
	EXPECT_TRUE(guard.children().contains(key_a));
	EXPECT_TRUE(guard.children().contains(key_b));

	// Without a parent the keyframes are deleted outright
	key_a->setParent(node);
	key_b->setParent(node);
	ASSERT_EQ(imm->keyframe_tracks().at(0).size(), 2);
	imm->delete_all_keyframes();
	EXPECT_TRUE(imm->keyframe_tracks().at(0).isEmpty());
}
