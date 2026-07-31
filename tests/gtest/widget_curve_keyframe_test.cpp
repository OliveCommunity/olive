#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include <QSignalSpy>

#include "core.h"
#include "node/color/colormanager/colormanager.h"
#include "node/generator/solid/solid.h"
#include "node/math/math/math.h"
#include "node/nodeundo.h"
#include "node/project.h"
#include "oakengine/app.h"
#include "oakengine/node.h"
#include "oakengine/project.h"
#include "render/diskmanager.h"
#include "undo/undostack.h"
#include "widget/curvewidget/curveview.h"
#include "widget/curvewidget/curvewidget.h"
#include "widget/keyframeview/keyframeview.h"
#include "widget/nodetreeview/nodetreeview.h"

using namespace olive;

namespace
{

// Keyframe deletion goes through the global undo stack hosted by Core
void ensure_app_singletons()
{
	if (!olive::Core::instance()) {
		new olive::Core(); // intentionally leaked
	}
	if (!olive::DiskManager::instance()) {
		olive::DiskManager::create_instance();
	}
}

// Helpers: wrap engine pointers as oak:: wrapper values for the C ABI
// widget interface
inline oak::Node to_oak(Node *n)
{
	return oak::Node(reinterpret_cast<OakEngineNode *>(n));
}

inline oak::Keyframe to_oak_key(NodeKeyframe *k)
{
	return oak::Keyframe(reinterpret_cast<OakEngineKeyframe *>(k));
}

inline oak::KeyframeTrackRef to_oak_ref(Node *n, const QString &input,
										int track)
{
	return oak::KeyframeTrackRef(
		oak::Input(reinterpret_cast<OakEngineNode *>(n), input), track);
}

// The process-wide undo stack previously reached via Core::undo_stack()
inline UndoStack *app_undo_stack()
{
	return static_cast<UndoStack *>(oakengine_app_undo_stack());
}

NodeKeyframe *insert_keyframe(Node *node, const QString &input,
							 const Rational &time, const QVariant &value,
							 int track = 0)
{
	auto *key =
		new NodeKeyframe(time, value, NodeKeyframe::k_linear, track, -1, input);
	NodeParamInsertKeyframeCommand(node, key).redo_now();
	return key;
}

} // namespace

class KeyframeViewTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();
		ensure_app_singletons();

		project_ = std::make_unique<Project>();
		project_->initialize();
	}

	MathNode *add_math_node()
	{
		auto *node = new MathNode();
		node->setParent(project_.get());
		return node;
	}

	std::unique_ptr<Project> project_;
};

TEST_F(KeyframeViewTest, AddKeyframesOfNodeCreatesConnectionPerKeyframableInput)
{
	MathNode *node = add_math_node();

	KeyframeView view;
	KeyframeView::NodeConnections map = view.add_keyframes_of_node(to_oak(node));

	// A float input has one element (the array-less -1) with one track
	ASSERT_TRUE(map.contains(MathNode::k_param_a_in));
	const KeyframeView::InputConnections &param_a = map[MathNode::k_param_a_in];
	ASSERT_EQ(param_a.size(), 1);
	ASSERT_EQ(param_a.first().size(), 1);
	EXPECT_NE(param_a.first().first(), nullptr);

	ASSERT_TRUE(map.contains(MathNode::k_param_b_in));
	EXPECT_EQ(map[MathNode::k_param_b_in].size(), 1);

	// The base-class enabled checkbox is keyframable too
	ASSERT_TRUE(map.contains(Node::k_enabled_input));
	EXPECT_EQ(map[Node::k_enabled_input].size(), 1);

	// The combo input is flagged not-keyframable, so it gets no connections
	ASSERT_TRUE(map.contains(MathNode::k_method_in));
	EXPECT_TRUE(map[MathNode::k_method_in].isEmpty());

	// One track connection each for enabled, param A and param B
	EXPECT_EQ(view.get_keyframe_tracks().size(), 3);
}

TEST_F(KeyframeViewTest, SelectAllAndDeselectAllUpdateSelection)
{
	MathNode *node = add_math_node();
	NodeKeyframe *key_a = insert_keyframe(node, MathNode::k_param_a_in, Rational(0), 0.0);
	NodeKeyframe *key_b = insert_keyframe(node, MathNode::k_param_a_in, Rational(1), 1.0);

	KeyframeView view;
	view.add_keyframes_of_node(to_oak(node));

	QSignalSpy selection_spy(&view, &KeyframeView::selection_changed);

	EXPECT_TRUE(view.get_selected_keyframes().empty());

	view.select_all();
	EXPECT_EQ(view.get_selected_keyframes().size(), 2);
	EXPECT_GE(selection_spy.count(), 1);

	view.deselect_all();
	EXPECT_TRUE(view.get_selected_keyframes().empty());
	EXPECT_GE(selection_spy.count(), 2);

	Q_UNUSED(key_a)
	Q_UNUSED(key_b)
}

TEST_F(KeyframeViewTest, RemoveKeyframesOfTrackDeselectsAndDetaches)
{
	MathNode *node = add_math_node();
	insert_keyframe(node, MathNode::k_param_a_in, Rational(0), 0.0);

	KeyframeView view;
	KeyframeViewInputConnection *connection = view.add_keyframes_of_track(
		to_oak_ref(node, MathNode::k_param_a_in, 0));
	ASSERT_NE(connection, nullptr);
	ASSERT_EQ(view.get_keyframe_tracks().size(), 1);

	view.select_all();
	ASSERT_EQ(view.get_selected_keyframes().size(), 1);

	QSignalSpy selection_spy(&view, &KeyframeView::selection_changed);
	view.remove_keyframes_of_track(connection);

	EXPECT_TRUE(view.get_keyframe_tracks().isEmpty());
	EXPECT_TRUE(view.get_selected_keyframes().empty());
	EXPECT_GE(selection_spy.count(), 1);

	// Removing again is a harmless no-op
	view.remove_keyframes_of_track(connection);
	EXPECT_TRUE(view.get_keyframe_tracks().isEmpty());
}

TEST_F(KeyframeViewTest, ClearRemovesAllTracksAndSelection)
{
	MathNode *node = add_math_node();
	insert_keyframe(node, MathNode::k_param_a_in, Rational(0), 0.0);

	KeyframeView view;
	view.add_keyframes_of_node(to_oak(node));
	view.select_all();
	ASSERT_FALSE(view.get_keyframe_tracks().isEmpty());
	ASSERT_FALSE(view.get_selected_keyframes().empty());

	view.clear();
	EXPECT_TRUE(view.get_keyframe_tracks().isEmpty());
	EXPECT_TRUE(view.get_selected_keyframes().empty());
}

TEST_F(KeyframeViewTest, DeleteSelectedPushesUndoableRemoval)
{
	MathNode *node = add_math_node();
	NodeKeyframe *key = insert_keyframe(node, MathNode::k_param_a_in, Rational(0), 0.0);

	KeyframeView view;
	view.add_keyframes_of_track(
		to_oak_ref(node, MathNode::k_param_a_in, 0));
	view.select_all();
	view.delete_selected();

	// The command was executed on push: the keyframe is gone from the node
	EXPECT_TRUE(node->get_keyframe_tracks(MathNode::k_param_a_in, -1)
					.at(0)
					.isEmpty());

	app_undo_stack()->undo();
	EXPECT_TRUE(node->get_keyframe_tracks(MathNode::k_param_a_in, -1)
					.at(0)
					.contains(key));

	app_undo_stack()->redo();
	EXPECT_TRUE(node->get_keyframe_tracks(MathNode::k_param_a_in, -1)
					.at(0)
					.isEmpty());

	// Keep the shared undo stack clean for other suites
	app_undo_stack()->clear();
}

class KeyframeViewUndoTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();
		ensure_app_singletons();

		project_ = std::make_unique<Project>();
		project_->initialize();

		node_ = new MathNode();
		node_->setParent(project_.get());
	}

	std::unique_ptr<Project> project_;
	MathNode *node_ = nullptr;
};

TEST_F(KeyframeViewUndoTest, SetTypeSwitchesAndRestoresType)
{
	NodeKeyframe *key =
		insert_keyframe(node_, MathNode::k_param_a_in, Rational(0), 0.0);
	ASSERT_EQ(key->type(), NodeKeyframe::k_linear);

	// The app-side command class was replaced by the facade batch call;
	// undo/redo ride the same global undo stack.
	const int64_t times[1] = { 0 };
	const int tracks[1] = { 0 };
	EXPECT_EQ(oakengine_node_keyframes_set_type_many(
				  reinterpret_cast<OakEngineNode *>(node_),
				  MathNode::k_param_a_in.toUtf8().constData(), -1, times,
				  tracks, 1, 1),
			  1);
	EXPECT_EQ(key->type(), NodeKeyframe::k_bezier);

	EXPECT_EQ(oakengine_project_undo(
				  reinterpret_cast<OakEngineProject *>(project_.get())),
			  OAKENGINE_OK);
	EXPECT_EQ(key->type(), NodeKeyframe::k_linear);

	EXPECT_EQ(oakengine_project_redo(
				  reinterpret_cast<OakEngineProject *>(project_.get())),
			  OAKENGINE_OK);
	EXPECT_EQ(key->type(), NodeKeyframe::k_bezier);
}

TEST_F(KeyframeViewUndoTest, SetBezierPointCapturesOldPointFromKeyframe)
{
	NodeKeyframe *key =
		insert_keyframe(node_, MathNode::k_param_a_in, Rational(0), 0.0);
	key->set_type(NodeKeyframe::k_bezier);
	key->set_bezier_control_in(QPointF(0.1, 0.2));

	// NaN old components make the facade capture the current point.
	EXPECT_EQ(oakengine_node_keyframe_set_bezier_point(
				  reinterpret_cast<OakEngineNode *>(node_),
				  MathNode::k_param_a_in.toUtf8().constData(), -1, 0, 0, 0,
				  0.5, 0.6, NAN, NAN),
			  OAKENGINE_OK);
	EXPECT_EQ(key->bezier_control_in(), QPointF(0.5, 0.6));

	EXPECT_EQ(oakengine_project_undo(
				  reinterpret_cast<OakEngineProject *>(project_.get())),
			  OAKENGINE_OK);
	EXPECT_EQ(key->bezier_control_in(), QPointF(0.1, 0.2));
}

TEST_F(KeyframeViewUndoTest, SetBezierPointWithExplicitOldPoint)
{
	NodeKeyframe *key =
		insert_keyframe(node_, MathNode::k_param_a_in, Rational(0), 0.0);
	key->set_type(NodeKeyframe::k_bezier);

	// Explicit old components are recorded for undo (the live-set drag
	// pattern; the current point is not read).
	EXPECT_EQ(oakengine_node_keyframe_set_bezier_point(
				  reinterpret_cast<OakEngineNode *>(node_),
				  MathNode::k_param_a_in.toUtf8().constData(), -1, 0, 0, 1,
				  0.7, 0.8, 0.3, 0.4),
			  OAKENGINE_OK);
	EXPECT_EQ(key->bezier_control_out(), QPointF(0.7, 0.8));

	EXPECT_EQ(oakengine_project_undo(
				  reinterpret_cast<OakEngineProject *>(project_.get())),
			  OAKENGINE_OK);
	EXPECT_EQ(key->bezier_control_out(), QPointF(0.3, 0.4));
}

class CurveViewTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();

		project_ = std::make_unique<Project>();
		project_->initialize();

		solid_ = new SolidGenerator();
		solid_->setParent(project_.get());
	}

	oak::KeyframeTrackRef color_track_ref(int track) const
	{
		return to_oak_ref(solid_, SolidGenerator::k_color_input, track);
	}

	std::unique_ptr<Project> project_;
	SolidGenerator *solid_ = nullptr;
};

TEST_F(CurveViewTest, ConnectAndDisconnectInputManageTrackConnections)
{
	CurveView view;
	EXPECT_TRUE(view.get_connections().isEmpty());

	view.connect_input(color_track_ref(0));
	EXPECT_EQ(view.get_connections().size(), 1);
	EXPECT_TRUE(view.get_connections().contains(color_track_ref(0)));
	EXPECT_EQ(view.get_connections().value(color_track_ref(0))->get_reference(),
			  color_track_ref(0));

	// Connecting the same reference twice is a no-op
	view.connect_input(color_track_ref(0));
	EXPECT_EQ(view.get_connections().size(), 1);

	// A color input has four tracks; connecting another track adds one more
	view.connect_input(color_track_ref(1));
	EXPECT_EQ(view.get_connections().size(), 2);

	view.disconnect_input(color_track_ref(0));
	EXPECT_EQ(view.get_connections().size(), 1);
	EXPECT_FALSE(view.get_connections().contains(color_track_ref(0)));

	// Disconnecting an unconnected reference is a no-op
	view.disconnect_input(color_track_ref(0));
	EXPECT_EQ(view.get_connections().size(), 1);
}

TEST_F(CurveViewTest, ConnectionReflectsLiveKeyframeList)
{
	CurveView view;
	view.connect_input(color_track_ref(0));

	KeyframeViewInputConnection *connection =
		view.get_connections().value(color_track_ref(0));
	ASSERT_NE(connection, nullptr);
	EXPECT_TRUE(connection->get_keyframes().isEmpty());

	NodeKeyframe *key =
		insert_keyframe(solid_, SolidGenerator::k_color_input, Rational(0), 0.5, 0);
	EXPECT_EQ(connection->get_keyframes().size(), 1);
	EXPECT_EQ(connection->get_keyframes().first(), to_oak_key(key));
}

TEST_F(CurveViewTest, SetKeyframeTrackColorAppliesToBrush)
{
	CurveView view;

	// Setting the color before connecting is picked up on connect
	view.set_keyframe_track_color(color_track_ref(0), QColor(Qt::red));
	view.connect_input(color_track_ref(0));
	KeyframeViewInputConnection *connection =
		view.get_connections().value(color_track_ref(0));
	ASSERT_NE(connection, nullptr);
	EXPECT_EQ(connection->get_brush().color(), QColor(Qt::red));

	// Setting it afterwards updates the live connection
	view.set_keyframe_track_color(color_track_ref(0), QColor(Qt::blue));
	EXPECT_EQ(connection->get_brush().color(), QColor(Qt::blue));
}

TEST_F(CurveViewTest, SelectKeyframesOfInputSelectsOnlyRequestedTrack)
{
	class SelectionProbeCurveView : public CurveView {
	public:
		using KeyframeView::is_keyframe_selected;
	};

	SelectionProbeCurveView view;
	view.connect_input(color_track_ref(0));
	view.connect_input(color_track_ref(1));

	NodeKeyframe *key0 =
		insert_keyframe(solid_, SolidGenerator::k_color_input, Rational(0), 0.5, 0);
	NodeKeyframe *key1 =
		insert_keyframe(solid_, SolidGenerator::k_color_input, Rational(1), 0.6, 1);

	// Previously the reference was ignored and keyframes of every connected
	// track got selected.
	view.select_keyframes_of_input(color_track_ref(0));
	EXPECT_TRUE(view.is_keyframe_selected(to_oak_key(key0)));
	EXPECT_FALSE(view.is_keyframe_selected(to_oak_key(key1)));

	// Selecting the other track replaces the selection (DeselectAll first)
	view.select_keyframes_of_input(color_track_ref(1));
	EXPECT_FALSE(view.is_keyframe_selected(to_oak_key(key0)));
	EXPECT_TRUE(view.is_keyframe_selected(to_oak_key(key1)));
}

TEST(CurveWidget, VerticalScaleRoundTripsThroughView)
{
	ColorManager::set_up_default_config();

	CurveWidget widget;
	const double original = widget.get_vertical_scale();
	EXPECT_GT(original, 0.0);

	widget.set_vertical_scale(original * 2.0);
	EXPECT_DOUBLE_EQ(widget.get_vertical_scale(), original * 2.0);
}

TEST(CurveWidget, TreeSelectionConnectsTracksAndResolvesNodeId)
{
	ColorManager::set_up_default_config();
	ensure_app_singletons();

	Project project;
	project.initialize();
	auto *solid = new SolidGenerator();
	solid->setParent(&project);
	solid->retranslate();

	CurveWidget widget;
	widget.set_nodes({ to_oak(solid) });

	auto *tree = widget.findChild<NodeTreeView *>();
	ASSERT_NE(tree, nullptr);
	ASSERT_EQ(tree->topLevelItemCount(), 1);

	// Nothing is connected until an input is selected in the tree
	EXPECT_EQ(widget.get_selected_node_with_id(solid->id()), nullptr);

	// The solid has two inputs ("enabled" from the base class, then "Color")
	QTreeWidgetItem *node_item = tree->topLevelItem(0);
	ASSERT_EQ(node_item->childCount(), 2);
	QTreeWidgetItem *color_item = node_item->child(1);

	color_item->setSelected(true);
	EXPECT_EQ(widget.get_selected_node_with_id(solid->id()), to_oak(solid));
	EXPECT_EQ(widget.get_selected_node_with_id(QStringLiteral("org.example.bogus")),
			  nullptr);

	// Clearing the selection disconnects the tracks again
	tree->clearSelection();
	EXPECT_EQ(widget.get_selected_node_with_id(solid->id()), nullptr);
}
