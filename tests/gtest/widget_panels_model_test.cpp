#include <gtest/gtest.h>

#include <memory>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidgetItem>

#include "core.h"
#include "node/block/clip/clip.h"
#include "node/color/colormanager/colormanager.h"
#include "node/generator/solid/solid.h"
#include "node/input/multicam/multicamnode.h"
#include "node/math/math/math.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/project/folder/folder.h"
#include "render/diskmanager.h"
#include "render/rendermanager.h"
#include "task/task.h"
#include "undo/undostack.h"
#include "widget/colorbutton/colorbutton.h"
#include "widget/history/historywidget.h"
#include "widget/multicam/multicamwidget.h"
#include "widget/nodeparamview/nodeparamviewwidgetbridge.h"
#include "widget/nodetableview/nodetableview.h"
#include "widget/nodetreeview/nodetreeview.h"
#include "widget/nodeview/nodeviewscene.h"
#include "widget/slider/floatslider.h"
#include "widget/taskview/taskview.h"
#include "widget/taskview/taskviewitem.h"
#include "widget/timelinewidget/timelinewidgetselections.h"

using namespace olive;

namespace
{

// Bridges, history and multicam widgets all talk to the Core singleton
void ensure_app_singletons()
{
	if (!olive::Core::instance()) {
		new olive::Core(); // intentionally leaked
	}
	if (!olive::DiskManager::instance()) {
		olive::DiskManager::create_instance();
	}
}

class DummyTask : public Task {
public:
	DummyTask()
	{
		set_title(QStringLiteral("Test Task"));
		set_error(QStringLiteral("boom"));
	}

protected:
	virtual bool run() override
	{
		return true;
	}
};

class IncrementCommand : public UndoCommand {
public:
	explicit IncrementCommand(int *value)
		: value_(value)
	{
	}

	virtual Project *get_relevant_project() const override
	{
		return nullptr;
	}

protected:
	virtual void redo() override
	{
		++(*value_);
	}

	virtual void undo() override
	{
		--(*value_);
	}

private:
	int *value_;
};

// NodeTreeView stores these on its items (mirrors the private constants)
const int k_item_type_role = Qt::UserRole;
const int k_item_input_reference_role = Qt::UserRole + 1;

} // namespace

class WidgetPanelsTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();
		ensure_app_singletons();

		project_ = std::make_unique<Project>();
		project_->initialize();
	}

	template <typename T> T *add_node()
	{
		auto *node = new T();
		node->setParent(project_.get());
		return node;
	}

	std::unique_ptr<Project> project_;
};

TEST_F(WidgetPanelsTest, NodeTableSelectNodesCreatesTopLevelItems)
{
	auto *solid = add_node<SolidGenerator>();

	NodeTableView view;
	EXPECT_EQ(view.topLevelItemCount(), 0);

	view.select_nodes({ solid });
	ASSERT_EQ(view.topLevelItemCount(), 1);
	EXPECT_EQ(view.topLevelItem(0)->text(0), solid->get_label_and_name());

	auto *math = add_node<MathNode>();
	view.select_nodes({ math });
	EXPECT_EQ(view.topLevelItemCount(), 2);

	view.deselect_nodes({ solid });
	EXPECT_EQ(view.topLevelItemCount(), 1);

	view.deselect_nodes({ math });
	EXPECT_EQ(view.topLevelItemCount(), 0);
}

TEST_F(WidgetPanelsTest, NodeTableSetTimePopulatesInputRows)
{
	auto *solid = add_node<SolidGenerator>();
	solid->retranslate();

	NodeTableView view;
	view.select_nodes({ solid });

	QTreeWidgetItem *top = view.topLevelItem(0);
	ASSERT_NE(top, nullptr);

	// Rows appear for the base-class enabled input and the color input
	ASSERT_EQ(top->childCount(), 2);

	// The solid's color input appears as a named child row
	QTreeWidgetItem *color_row = nullptr;
	for (int i = 0; i < top->childCount(); i++) {
		if (top->child(i)->data(0, Qt::UserRole).toString() ==
			SolidGenerator::k_color_input) {
			color_row = top->child(i);
			break;
		}
	}
	ASSERT_NE(color_row, nullptr);
	EXPECT_EQ(color_row->text(0), solid->get_input_name(SolidGenerator::k_color_input));

	// The value row shows the data type name and the split RGBA columns
	ASSERT_EQ(color_row->childCount(), 1);
	QTreeWidgetItem *value_row = color_row->child(0);
	EXPECT_EQ(value_row->text(0),
			  NodeValue::get_pretty_data_type_name(NodeValue::k_color));
	EXPECT_FALSE(value_row->text(1).isEmpty());
	for (int col = 2; col <= 5; col++) {
		EXPECT_FALSE(value_row->text(col).isEmpty()) << "column" << col;
	}

	// Re-evaluating at another time keeps the same structure
	view.set_time(Rational(1));
	EXPECT_EQ(top->childCount(), 2);
	EXPECT_EQ(color_row->childCount(), 1);
}

TEST_F(WidgetPanelsTest, NodeTreeSetNodesBuildsInputHierarchy)
{
	auto *math = add_node<MathNode>();
	math->retranslate();

	NodeTreeView view;
	view.set_nodes({ math });

	ASSERT_EQ(view.topLevelItemCount(), 1);
	QTreeWidgetItem *node_item = view.topLevelItem(0);
	EXPECT_EQ(node_item->data(0, k_item_type_role).toInt(), 0); // kItemTypeNode

	// All four inputs are visible: the base-class enabled checkbox, the
	// method combo, and the two float params
	ASSERT_EQ(node_item->childCount(), 4);
	const QStringList expected_inputs = { Node::k_enabled_input, MathNode::k_method_in,
										  MathNode::k_param_a_in, MathNode::k_param_b_in };
	for (int i = 0; i < expected_inputs.size(); i++) {
		QTreeWidgetItem *input_item = node_item->child(i);
		EXPECT_EQ(input_item->data(0, k_item_type_role).toInt(), 1); // kItemTypeInput
		const NodeKeyframeTrackReference ref =
			input_item->data(0, k_item_input_reference_role)
				.value<NodeKeyframeTrackReference>();
		EXPECT_EQ(ref.input().node(), math);
		EXPECT_EQ(ref.input().input(), expected_inputs.at(i));
	}
}

TEST_F(WidgetPanelsTest, NodeTreeOnlyShowKeyframableFiltersInputs)
{
	auto *math = add_node<MathNode>();

	NodeTreeView view;
	view.set_only_show_keyframable(true);
	view.set_nodes({ math });

	// The method combo is flagged not-keyframable; enabled and the two
	// float params remain
	ASSERT_EQ(view.topLevelItemCount(), 1);
	EXPECT_EQ(view.topLevelItem(0)->childCount(), 3);

	// Of a bare viewer's inputs only "enabled" is keyframable, so it is the
	// sole row left standing
	auto *viewer = add_node<ViewerOutput>();
	view.set_nodes({ viewer });
	ASSERT_EQ(view.topLevelItemCount(), 1);
	EXPECT_EQ(view.topLevelItem(0)->childCount(), 1);

	// Without the filter its buffer inputs show up as well
	view.set_only_show_keyframable(false);
	view.set_nodes({ viewer });
	ASSERT_EQ(view.topLevelItemCount(), 1);
	EXPECT_EQ(view.topLevelItem(0)->childCount(), 3);
}

TEST_F(WidgetPanelsTest, NodeTreeCheckboxesToggleEnableStateAndEmit)
{
	auto *math = add_node<MathNode>();

	NodeTreeView view;
	view.set_check_boxes_enabled(true);
	view.set_nodes({ math });

	Node *node_signal_node = nullptr;
	bool node_signal_enabled = true;
	int node_emissions = 0;
	QObject::connect(&view, &NodeTreeView::node_enable_changed,
					 [&node_signal_node, &node_signal_enabled,
					  &node_emissions](OakEngineNode *n, bool e) {
						 node_signal_node = reinterpret_cast<Node *>(n);
						 node_signal_enabled = e;
						 ++node_emissions;
					 });

	NodeKeyframeTrackReference input_signal_ref;
	bool input_signal_enabled = true;
	int input_emissions = 0;
	QObject::connect(&view, &NodeTreeView::input_enable_changed,
					 [&input_signal_ref, &input_signal_enabled,
					  &input_emissions](const NodeKeyframeTrackReference &ref,
										bool e) {
						 input_signal_ref = ref;
						 input_signal_enabled = e;
						 ++input_emissions;
					 });

	QTreeWidgetItem *node_item = view.topLevelItem(0);
	ASSERT_NE(node_item, nullptr);
	ASSERT_EQ(node_item->checkState(0), Qt::Checked);

	// Unchecking the node disables it and emits
	node_item->setCheckState(0, Qt::Unchecked);
	EXPECT_EQ(node_emissions, 1);
	EXPECT_EQ(node_signal_node, math);
	EXPECT_FALSE(node_signal_enabled);
	EXPECT_FALSE(view.is_node_enabled(math));

	// Re-checking restores it
	node_item->setCheckState(0, Qt::Checked);
	EXPECT_EQ(node_emissions, 2);
	EXPECT_TRUE(node_signal_enabled);
	EXPECT_TRUE(view.is_node_enabled(math));

	// Same behavior on input rows
	QTreeWidgetItem *input_item = node_item->child(0);
	ASSERT_NE(input_item, nullptr);
	input_item->setCheckState(0, Qt::Unchecked);
	EXPECT_EQ(input_emissions, 1);
	EXPECT_EQ(input_signal_ref.input().input(), Node::k_enabled_input);
	EXPECT_FALSE(input_signal_enabled);
	EXPECT_FALSE(view.is_input_enabled(input_signal_ref));
}

TEST_F(WidgetPanelsTest, NodeTreeKeyframeTracksBecomeRows)
{
	auto *solid = add_node<SolidGenerator>();
	solid->retranslate();

	NodeTreeView view;
	view.set_show_keyframe_tracks_as_rows(true);
	view.set_nodes({ solid });

	QTreeWidgetItem *node_item = view.topLevelItem(0);
	ASSERT_NE(node_item, nullptr);
	ASSERT_EQ(node_item->childCount(), 2); // enabled + color

	// The four-track color input expands into one row per track
	QTreeWidgetItem *color_item = node_item->child(1);
	EXPECT_EQ(color_item->text(0), QStringLiteral("Color"));
	ASSERT_EQ(color_item->childCount(), 4);
	const QStringList track_names = { QStringLiteral("R"), QStringLiteral("G"),
									  QStringLiteral("B"), QStringLiteral("A") };
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ(color_item->child(i)->text(0), track_names.at(i));
		const NodeKeyframeTrackReference ref =
			color_item->child(i)
				->data(0, k_item_input_reference_role)
				.value<NodeKeyframeTrackReference>();
		EXPECT_EQ(ref.track(), i);
	}

	// A single-track float input stays a single row
	auto *math = add_node<MathNode>();
	math->retranslate();
	view.set_nodes({ math });
	QTreeWidgetItem *param_item =
		view.topLevelItem(0)->child(2); // after enabled and the method combo
	ASSERT_NE(param_item, nullptr);
	EXPECT_EQ(param_item->text(0), QStringLiteral("Value"));
	EXPECT_EQ(param_item->childCount(), 0);
}

TEST_F(WidgetPanelsTest, BridgeCreatesSliderForFloatInput)
{
	auto *math = add_node<MathNode>();

	QWidget parent;
	NodeParamViewWidgetBridge bridge(NodeInput(math, MathNode::k_param_a_in), &parent);

	ASSERT_EQ(bridge.widgets().size(), 1);
	EXPECT_NE(qobject_cast<FloatSlider *>(bridge.widgets().first()), nullptr);
}

TEST_F(WidgetPanelsTest, BridgeCreatesColorButtonForColorInput)
{
	auto *solid = add_node<SolidGenerator>();

	QWidget parent;
	NodeParamViewWidgetBridge bridge(NodeInput(solid, SolidGenerator::k_color_input),
									 &parent);

	ASSERT_EQ(bridge.widgets().size(), 1);
	EXPECT_NE(qobject_cast<ColorButton *>(bridge.widgets().first()), nullptr);
}

TEST_F(WidgetPanelsTest, BridgeCreatesComboBoxForComboInput)
{
	auto *math = add_node<MathNode>();
	math->retranslate();

	QWidget parent;
	NodeParamViewWidgetBridge bridge(NodeInput(math, MathNode::k_method_in), &parent);

	ASSERT_EQ(bridge.widgets().size(), 1);
	auto *combo = qobject_cast<QComboBox *>(bridge.widgets().first());
	ASSERT_NE(combo, nullptr);
	EXPECT_EQ(combo->count(), math->get_combo_box_strings(MathNode::k_method_in).size());
	EXPECT_GT(combo->count(), 0);
}

TEST_F(WidgetPanelsTest, BridgeCreatesCheckBoxForBooleanInput)
{
	auto *clip = add_node<ClipBlock>();

	QWidget parent;
	NodeParamViewWidgetBridge bridge(NodeInput(clip, ClipBlock::k_reverse_input),
									 &parent);

	ASSERT_EQ(bridge.widgets().size(), 1);
	EXPECT_NE(qobject_cast<QCheckBox *>(bridge.widgets().first()), nullptr);
}

TEST_F(WidgetPanelsTest, BridgeUpdatesWidgetWhenNodeValueChanges)
{
	auto *math = add_node<MathNode>();
	auto *viewer = add_node<ViewerOutput>();

	QWidget parent;
	NodeParamViewWidgetBridge bridge(NodeInput(math, MathNode::k_param_a_in), &parent);
	auto *slider = qobject_cast<FloatSlider *>(bridge.widgets().first());
	ASSERT_NE(slider, nullptr);
	EXPECT_DOUBLE_EQ(slider->get_value(), 0.0);

	// The bridge only refreshes widgets for value changes at the playhead
	// of a connected time target
	bridge.set_time_target(viewer);

	math->set_standard_value(MathNode::k_param_a_in, 2.5);
	EXPECT_DOUBLE_EQ(slider->get_value(), 2.5);
}

TEST_F(WidgetPanelsTest, BridgePushesUndoCommandWhenWidgetChanges)
{
	auto *math = add_node<MathNode>();
	math->retranslate();
	ASSERT_EQ(math->get_standard_value(MathNode::k_method_in).toInt(), 0);

	QWidget parent;
	NodeParamViewWidgetBridge bridge(NodeInput(math, MathNode::k_method_in), &parent);
	auto *combo = qobject_cast<QComboBox *>(bridge.widgets().first());
	ASSERT_NE(combo, nullptr);
	ASSERT_GT(combo->count(), 1);

	combo->setCurrentIndex(1);
	EXPECT_EQ(math->get_standard_value(MathNode::k_method_in).toInt(), 1);

	Core::instance()->undo_stack()->undo();
	EXPECT_EQ(math->get_standard_value(MathNode::k_method_in).toInt(), 0);
	Core::instance()->undo_stack()->clear();
}

TEST(TaskView, TaskLifecycleUpdatesItems)
{
	TaskView view(nullptr);
	DummyTask task;

	view.add_task(reinterpret_cast<OakEngineTask*>(&task));

	auto *item = view.findChild<TaskViewItem *>();
	ASSERT_NE(item, nullptr);

	// Title label mirrors the task title
	bool found_title = false;
	foreach (QLabel *label, item->findChildren<QLabel *>()) {
		if (label->text() == QStringLiteral("Test Task")) {
			found_title = true;
			break;
		}
	}
	EXPECT_TRUE(found_title);

	// Progress signals drive the progress bar
	auto *bar = item->findChild<QProgressBar *>();
	ASSERT_NE(bar, nullptr);
	emit task.progress_changed(0.5);
	EXPECT_EQ(bar->value(), 50);

	// The cancel button relays the task through TaskCancelled
	OakEngineTask *cancelled = nullptr;
	QObject::connect(&view, &TaskView::task_cancelled,
					 [&cancelled](OakEngineTask *t) { cancelled = t; });
	auto *cancel_button = item->findChild<QPushButton *>();
	ASSERT_NE(cancel_button, nullptr);
	cancel_button->click();
	EXPECT_EQ(cancelled, reinterpret_cast<OakEngineTask*>(&task));

	// Failure swaps in the error label
	view.task_failed(reinterpret_cast<OakEngineTask*>(&task));
	bool found_error = false;
	foreach (QLabel *label, item->findChildren<QLabel *>()) {
		if (label->text().contains(QStringLiteral("boom"))) {
			found_error = true;
			break;
		}
	}
	EXPECT_TRUE(found_error);

	// Removal deletes the item once deferred deletions are processed
	view.remove_task(reinterpret_cast<OakEngineTask*>(&task));
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	EXPECT_EQ(view.findChild<TaskViewItem *>(), nullptr);
}

TEST(HistoryWidget, ReflectsAndDrivesUndoStack)
{
	ensure_app_singletons();
	UndoStack *stack = Core::instance()->undo_stack();
	stack->clear();

	HistoryWidget widget;
	EXPECT_EQ(widget.model(), stack);

	int counter = 0;
	stack->push(new IncrementCommand(&counter), QStringLiteral("First"));
	stack->push(new IncrementCommand(&counter), QStringLiteral("Second"));
	EXPECT_EQ(counter, 2);
	EXPECT_EQ(stack->rowCount(), 3);

	// Row 0 is the "New/Open Project" sentinel the stack starts with, so
	// "First" lives at row 1 and "Second" at row 2. Moving the current row
	// jumps the stack to row+1 applied commands (this is how clicking a
	// history row works).
	widget.selectionModel()->setCurrentIndex(stack->index(1, 0),
											 QItemSelectionModel::ClearAndSelect |
												 QItemSelectionModel::Rows);
	EXPECT_EQ(counter, 1);
	EXPECT_TRUE(stack->can_redo());

	// Moving to the second entry redoes everything again
	widget.selectionModel()->setCurrentIndex(stack->index(2, 0),
											 QItemSelectionModel::ClearAndSelect |
												 QItemSelectionModel::Rows);
	EXPECT_EQ(counter, 2);
	EXPECT_FALSE(stack->can_redo());

	// Moving back to the sentinel row undoes both commands
	widget.selectionModel()->setCurrentIndex(stack->index(0, 0),
											 QItemSelectionModel::ClearAndSelect |
												 QItemSelectionModel::Rows);
	EXPECT_EQ(counter, 0);
	EXPECT_TRUE(stack->can_redo());

	stack->clear();
}

TEST(TimelineSelections, ShiftTimeMovesAllRanges)
{
	TimelineWidgetSelections sel;
	const Track::Reference video0(Track::k_video, 0);
	sel.insert(video0,
			   TimeRangeList({ TimeRange(Rational(0), Rational(10)) }));

	sel.shift_time(Rational(5));
	const TimeRangeList list = sel.value(video0);
	ASSERT_EQ(list.size(), 1);
	EXPECT_EQ(list.first().in(), Rational(5));
	EXPECT_EQ(list.first().out(), Rational(15));
}

TEST(TimelineSelections, ShiftTracksReindexesMatchingTypeOnly)
{
	TimelineWidgetSelections sel;
	sel.insert(Track::Reference(Track::k_video, 0),
			   TimeRangeList({ TimeRange(Rational(0), Rational(10)) }));
	sel.insert(Track::Reference(Track::k_video, 1),
			   TimeRangeList({ TimeRange(Rational(0), Rational(10)) }));
	sel.insert(Track::Reference(Track::k_audio, 0),
			   TimeRangeList({ TimeRange(Rational(0), Rational(10)) }));

	sel.shift_tracks(Track::k_video, 2);

	EXPECT_FALSE(sel.contains(Track::Reference(Track::k_video, 0)));
	EXPECT_FALSE(sel.contains(Track::Reference(Track::k_video, 1)));
	EXPECT_TRUE(sel.contains(Track::Reference(Track::k_video, 2)));
	EXPECT_TRUE(sel.contains(Track::Reference(Track::k_video, 3)));
	EXPECT_TRUE(sel.contains(Track::Reference(Track::k_audio, 0)));
}

TEST(TimelineSelections, TrimInAndOutAdjustRangeEnds)
{
	TimelineWidgetSelections in_sel;
	const Track::Reference video0(Track::k_video, 0);
	in_sel.insert(video0,
				  TimeRangeList({ TimeRange(Rational(0), Rational(10)) }));
	in_sel.trim_in(Rational(2));
	EXPECT_EQ(in_sel.value(video0).first().in(), Rational(2));
	EXPECT_EQ(in_sel.value(video0).first().out(), Rational(10));

	TimelineWidgetSelections out_sel;
	out_sel.insert(video0,
				   TimeRangeList({ TimeRange(Rational(0), Rational(10)) }));
	out_sel.trim_out(Rational(-3));
	EXPECT_EQ(out_sel.value(video0).first().in(), Rational(0));
	EXPECT_EQ(out_sel.value(video0).first().out(), Rational(7));
}

TEST(TimelineSelections, SubtractSplitsAndIgnoresForeignTracks)
{
	TimelineWidgetSelections ours;
	const Track::Reference video0(Track::k_video, 0);
	ours.insert(video0,
				TimeRangeList({ TimeRange(Rational(0), Rational(10)) }));

	TimelineWidgetSelections theirs;
	theirs.insert(video0,
				  TimeRangeList({ TimeRange(Rational(3), Rational(5)) }));
	theirs.insert(Track::Reference(Track::k_audio, 0),
				  TimeRangeList({ TimeRange(Rational(0), Rational(99)) }));

	TimelineWidgetSelections result = ours.subtracted(theirs);

	// The original is untouched by the const version
	EXPECT_EQ(ours.value(video0).size(), 1);

	const TimeRangeList remaining = result.value(video0);
	ASSERT_EQ(remaining.size(), 2);
	EXPECT_EQ(remaining.at(0), TimeRange(Rational(0), Rational(3)));
	EXPECT_EQ(remaining.at(1), TimeRange(Rational(5), Rational(10)));

	// In-place Subtract drops the subtracted span as well
	ours.subtract(theirs);
	EXPECT_EQ(ours.value(video0).size(), 2);
}

TEST(NodeViewScene, AddAndRemoveContexts)
{
	ColorManager::set_up_default_config();
	Project project;
	project.initialize();
	auto *folder = new Folder();
	folder->setParent(&project);

	NodeViewScene scene;
	EXPECT_TRUE(scene.context_map().isEmpty());

	NodeViewContext *ctx = scene.add_context(folder);
	ASSERT_NE(ctx, nullptr);
	EXPECT_TRUE(scene.context_map().contains(folder));
	EXPECT_EQ(ctx->get_context(), folder);
	EXPECT_TRUE(scene.items().contains(ctx));

	// Re-adding the same node returns the existing context item
	EXPECT_EQ(scene.add_context(folder), ctx);
	EXPECT_EQ(scene.context_map().size(), 1);

	scene.remove_context(folder);
	EXPECT_TRUE(scene.context_map().isEmpty());
}

TEST(NodeViewScene, FlowDirectionControlsOrientation)
{
	NodeViewScene scene;
	EXPECT_EQ(scene.get_flow_direction(), NodeViewCommon::k_left_to_right);
	EXPECT_EQ(scene.get_flow_orientation(), Qt::Horizontal);

	scene.set_flow_direction(NodeViewCommon::k_top_to_bottom);
	EXPECT_EQ(scene.get_flow_direction(), NodeViewCommon::k_top_to_bottom);
	EXPECT_EQ(scene.get_flow_orientation(), Qt::Vertical);
}

class MulticamWidgetTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();
		ensure_app_singletons();

		// The display widget pulls the render backend off RenderManager,
		// which the bare Core singleton does not create (Core::Start()
		// would); viewer_display_repro_test does the same
		created_render_manager_ = (RenderManager::instance() == nullptr);
		if (created_render_manager_) {
			RenderManager::create_instance();
		}

		project_ = std::make_unique<Project>();
		project_->initialize();
	}

	void TearDown() override
	{
		project_.reset();

		// Leave the singleton the way we found it: render suites check
		// RenderManager::instance() for null in their own teardowns
		if (created_render_manager_) {
			RenderManager::destroy_instance();
			created_render_manager_ = false;
		}
	}

	std::unique_ptr<Project> project_;
	bool created_render_manager_ = false;
};

TEST_F(MulticamWidgetTest, ConstructionCreatesDisplay)
{
	MulticamWidget widget;
	EXPECT_NE(widget.get_display_widget(), nullptr);
	EXPECT_EQ(widget.get_connected_node(), nullptr);
}

TEST_F(MulticamWidgetTest, SwitchWithoutTimestampAppliesImmediately)
{
	auto *viewer = new ViewerOutput();
	viewer->setParent(project_.get());
	auto *node = new MultiCamNode();
	node->setParent(project_.get());
	auto *clip = new ClipBlock();
	clip->setParent(project_.get());

	MulticamWidget widget;
	widget.set_multicam_node(viewer, node, clip, Rational());
	EXPECT_EQ(widget.get_connected_node(), viewer);
}

TEST_F(MulticamWidgetTest, FutureSwitchWaitsForPlayheadToAdvance)
{
	auto *viewer_a = new ViewerOutput();
	viewer_a->setParent(project_.get());
	auto *viewer_b = new ViewerOutput();
	viewer_b->setParent(project_.get());
	auto *node = new MultiCamNode();
	node->setParent(project_.get());
	auto *clip = new ClipBlock();
	clip->setParent(project_.get());

	MulticamWidget widget;
	widget.set_multicam_node(viewer_a, node, clip, Rational());
	ASSERT_EQ(widget.get_connected_node(), viewer_a);

	// A switch stamped for a later time is queued, not applied
	widget.set_multicam_node(viewer_b, node, clip, Rational(5));
	EXPECT_EQ(widget.get_connected_node(), viewer_a);

	// Once playback time advances, the queued switch takes effect
	viewer_a->set_playhead(Rational(1));
	EXPECT_EQ(widget.get_connected_node(), viewer_b);
}
