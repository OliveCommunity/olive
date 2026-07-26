#include <gtest/gtest.h>

#include <QLabel>
#include <QProgressBar>
#include <QSignalSpy>
#include <QSplitter>

#include <kddockwidgets/KDDockWidgets.h>

#include "core.h"
#include "config/config.h"
#include "node/color/colormanager/colormanager.h"
#include "node/math/math/math.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/project/folder/folder.h"
#include "node/project/sequence/sequence.h"
#include "panel/audiomonitor/audiomonitor.h"
#include "panel/curve/curve.h"
#include "panel/footageviewer/footageviewer.h"
#include "panel/history/historypanel.h"
#include "panel/multicam/multicampanel.h"
#include "panel/node/node.h"
#include "panel/panel.h"
#include "panel/panelmanager.h"
#include "panel/param/param.h"
#include "panel/pixelsampler/pixelsamplerpanel.h"
#include "panel/project/project.h"
#include "panel/scope/scope.h"
#include "panel/sequenceviewer/sequenceviewer.h"
#include "panel/table/table.h"
#include "panel/taskmanager/taskmanager.h"
#include "panel/timebased/timebased.h"
#include "panel/timeline/timeline.h"
#include "panel/tool/tool.h"
#include "panel/viewer/viewer.h"
#include "render/diskmanager.h"
#include "render/rendermanager.h"
#include "task/task.h"
#include "task/taskmanager.h"
#include "undo/undostack.h"
#include "widget/curvewidget/curvewidget.h"
#include "widget/history/historywidget.h"
#include "widget/nodetableview/nodetableview.h"
#include "widget/nodetreeview/nodetreeview.h"
#include "widget/pixelsampler/pixelsampler.h"
#include "widget/taskview/taskview.h"
#include "widget/taskview/taskviewitem.h"
#include "widget/timebased/timebasedwidget.h"
#include "widget/toolbar/toolbar.h"

using namespace olive;

namespace
{

// Panels register with the PanelManager singleton and several of them talk to
// Core, TaskManager and RenderManager in their constructors
class PanelEnvironment {
public:
	PanelEnvironment()
	{
		ColorManager::set_up_default_config();

		if (!Core::instance()) {
			new Core(); // intentionally leaked
		}

		KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);

		if (!PanelManager::instance()) {
			PanelManager::create_instance();
			created_panel_manager_ = true;
		}

		if (!TaskManager::instance()) {
			TaskManager::create_instance();
			created_task_manager_ = true;
		}

		if (!RenderManager::instance()) {
			// Another suite may have left an experimental backend in the
			// config; RenderManager needs a real one to create its cacher
			saved_backend_ =
				Config::current()[QStringLiteral("GraphicsBackend")];
			Config::current()[QStringLiteral("GraphicsBackend")] =
				QStringLiteral("opengl");
			RenderManager::create_instance();
			created_render_manager_ = true;
		}

		if (!DiskManager::instance()) {
			DiskManager::create_instance();
			created_disk_manager_ = true;
		}
	}

	~PanelEnvironment()
	{
		// Panels must be gone before the manager that tracks them
		if (created_panel_manager_) {
			PanelManager::instance()->delete_all_panels();
			PanelManager::destroy_instance();
		}
		if (created_task_manager_) {
			TaskManager::destroy_instance();
		}
		if (created_render_manager_) {
			RenderManager::destroy_instance();
			Config::current()[QStringLiteral("GraphicsBackend")] =
				saved_backend_;
		}
		if (created_disk_manager_) {
			DiskManager::destroy_instance();
		}
	}

private:
	bool created_panel_manager_ = false;
	bool created_task_manager_ = false;
	bool created_render_manager_ = false;
	bool created_disk_manager_ = false;
	QVariant saved_backend_;
};

// Exposes the protected title/subtitle slots of the base class for testing
class TestPanel : public PanelWidget {
public:
	explicit TestPanel(const QString &name)
		: PanelWidget(name)
	{
	}

	using PanelWidget::set_subtitle;
	using PanelWidget::set_title;
};

class TestTimeBasedPanel : public TimeBasedPanel {
public:
	using TimeBasedPanel::set_time_based_widget;
	using TimeBasedPanel::TimeBasedPanel;
};

class DummyTask : public Task {
public:
	DummyTask()
	{
		set_title(QStringLiteral("Panel Test Task"));
	}

protected:
	virtual bool run() override
	{
		return true;
	}
};

} // namespace

class PanelTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		env_ = new PanelEnvironment();
	}

	void TearDown() override
	{
		delete env_;
	}

	template <typename T> T *add_node(Project *project)
	{
		auto *node = new T();
		node->setParent(project);
		return node;
	}

	PanelEnvironment *env_;
};

TEST_F(PanelTest, PanelWidgetBaseTitleSubtitleFormatting)
{
	TestPanel panel(QStringLiteral("BaseTestPanel"));
	EXPECT_EQ(panel.objectName(), QStringLiteral("BaseTestPanel"));

	panel.set_title(QStringLiteral("Title"));
	EXPECT_EQ(panel.title(), QStringLiteral("Title"));

	panel.set_subtitle(QStringLiteral("Sub"));
	EXPECT_EQ(panel.title(), QStringLiteral("Title: Sub"));

	// Clearing the subtitle falls back to the bare title
	panel.set_subtitle(QString());
	EXPECT_EQ(panel.title(), QStringLiteral("Title"));
}

TEST_F(PanelTest, PanelWidgetBaseRegistersWithPanelManager)
{
	PanelManager *manager = PanelManager::instance();
	const int panel_count_before = manager->panels().size();

	auto *panel = new TestPanel(QStringLiteral("RegisteredPanel"));
	EXPECT_TRUE(manager->panels().contains(panel));
	EXPECT_EQ(manager->panels().size(), panel_count_before + 1);
	EXPECT_EQ(manager->get_panel_with_name(QStringLiteral("RegisteredPanel")),
			  panel);
	EXPECT_TRUE(manager->get_panels_of_type<TestPanel>().contains(panel));

	delete panel;
	EXPECT_FALSE(manager->panels().contains(panel));
	EXPECT_EQ(manager->get_panel_with_name(QStringLiteral("RegisteredPanel")),
			  nullptr);
}

TEST_F(PanelTest, PanelWidgetBaseCloseBehavior)
{
	TestPanel panel(QStringLiteral("CloseTestPanel"));
	panel.show();
	ASSERT_TRUE(panel.isVisible());

	// Default behavior: close hides the panel
	panel.close();
	EXPECT_FALSE(panel.isVisible());

	// With SetSignalInsteadOfClose the close is vetoed and CloseRequested is
	// emitted instead
	panel.set_signal_instead_of_close(true);
	QSignalSpy spy(&panel, &PanelWidget::close_requested);
	panel.show();
	ASSERT_TRUE(panel.isVisible());
	panel.close();
	EXPECT_EQ(spy.count(), 1);
	EXPECT_TRUE(panel.isVisible());
}

TEST_F(PanelTest, PanelWidgetBaseDefaultActionsLeaveStateUnchanged)
{
	TestPanel panel(QStringLiteral("NoOpTestPanel"));
	panel.set_title(QStringLiteral("NoOp"));
	panel.show();
	ASSERT_TRUE(panel.isVisible());

	// Default SaveData is empty and LoadData accepts anything
	EXPECT_TRUE(panel.save_data().empty());
	panel.load_data(PanelWidget::Info());

	// None of the default actions may request a close
	QSignalSpy close_spy(&panel, &PanelWidget::close_requested);

	// All default actions are no-ops and must not crash
	panel.zoom_in();
	panel.zoom_out();
	panel.go_to_start();
	panel.prev_frame();
	panel.play_pause();
	panel.play_in_to_out();
	panel.next_frame();
	panel.go_to_end();
	panel.select_all();
	panel.deselect_all();
	panel.ripple_to_in();
	panel.ripple_to_out();
	panel.edit_to_in();
	panel.edit_to_out();
	panel.shuttle_left();
	panel.shuttle_stop();
	panel.shuttle_right();
	panel.go_to_prev_cut();
	panel.go_to_next_cut();
	panel.rename_selected();
	panel.delete_selected();
	panel.ripple_delete();
	panel.increase_track_height();
	panel.decrease_track_height();
	panel.set_in();
	panel.set_out();
	panel.reset_in();
	panel.reset_out();
	panel.clear_in_out();
	panel.set_marker();
	panel.toggle_links();
	panel.cut_selected();
	panel.copy_selected();
	panel.paste();
	panel.paste_insert();
	panel.toggle_show_all();
	panel.go_to_in();
	panel.go_to_out();
	panel.delete_in_to_out();
	panel.ripple_delete_in_to_out();
	panel.toggle_selected_enabled();
	panel.duplicate();
	panel.set_color_label(1);
	panel.nudge_left();
	panel.nudge_right();
	panel.move_in_to_playhead();
	panel.move_out_to_playhead();

	// The sweep must not have altered any observable panel state
	EXPECT_EQ(panel.title(), QStringLiteral("NoOp"));
	EXPECT_TRUE(panel.isVisible());
	EXPECT_TRUE(panel.save_data().empty());
	EXPECT_EQ(close_spy.count(), 0);
}

TEST_F(PanelTest, PanelWidgetBaseBorderAndFocus)
{
	TestPanel panel(QStringLiteral("BorderTestPanel"));
	panel.set_border_visible(true);
	panel.show();

	// The shown signal is wired to grab focus
	emit panel.shown(Qt::OtherFocusReason);
	emit panel.hidden();

	// Focus history lookup by type works through PanelManager
	EXPECT_EQ(PanelManager::instance()->most_recently_focused<TestPanel>(),
			  &panel);
}

TEST_F(PanelTest, PixelSamplerPanelConstruction)
{
	PixelSamplerPanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("PixelSamplerPanel"));
	EXPECT_EQ(panel.title(), QStringLiteral("Pixel Sampler"));

	// The panel hosts the two sampler views of a ManagedPixelSamplerWidget
	const auto samplers = panel.findChildren<PixelSamplerWidget *>();
	ASSERT_EQ(samplers.size(), 2);

	// Feeding values through the slot updates the displayed components
	Color red(1.0, 0.0, 0.0, 1.0);
	Color green(0.0, 1.0, 0.0, 1.0);
	panel.set_values(red, green);

	// First child is the display view, second the reference view
	EXPECT_TRUE(samplers.at(0)->findChild<QLabel *>()->text().contains(
		QStringLiteral("G: 1 (255)")));
	EXPECT_TRUE(samplers.at(1)->findChild<QLabel *>()->text().contains(
		QStringLiteral("R: 1 (255)")));

	// The panel hooks its visibility into Core's pixel sampling requests
	emit panel.shown(Qt::OtherFocusReason);
	emit panel.hidden();
}

TEST_F(PanelTest, TaskManagerPanelReflectsTaskManager)
{
	TaskManagerPanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("TaskManagerPanel"));
	EXPECT_EQ(panel.title(), QStringLiteral("Task Manager"));

	// The panel's TaskView is wired to the TaskManager singleton
	auto *task = new DummyTask();
	TaskManager::instance()->add_task(task);

	auto *view = panel.findChild<TaskView *>();
	ASSERT_NE(view, nullptr);
	EXPECT_NE(view->findChild<TaskViewItem *>(), nullptr);

	// TaskManager owns the task now; cancel it and let the removal propagate
	TaskManager::instance()->cancel_task_and_wait(task);
	QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_F(PanelTest, CurvePanelConstructionAndScaling)
{
	CurvePanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("CurvePanel"));
	EXPECT_TRUE(panel.title().startsWith(QStringLiteral("Curve Editor")));
	ASSERT_NE(panel.get_time_based_widget(), nullptr);

	auto *curve = static_cast<CurveWidget *>(panel.get_time_based_widget());

	// Track height actions scale the curve view vertically
	const double initial_scale = curve->get_vertical_scale();
	panel.increase_track_height();
	EXPECT_DOUBLE_EQ(curve->get_vertical_scale(), initial_scale * 2);
	panel.decrease_track_height();
	EXPECT_DOUBLE_EQ(curve->get_vertical_scale(), initial_scale);

	// Selection actions on an empty view are harmless
	panel.select_all();
	panel.deselect_all();
	panel.delete_selected();
}

TEST_F(PanelTest, CurvePanelSetNodes)
{
	Project project;
	project.initialize();
	auto *math = add_node<MathNode>(&project);

	CurvePanel panel;
	auto *tree = panel.findChild<NodeTreeView *>();
	ASSERT_NE(tree, nullptr);
	EXPECT_EQ(tree->topLevelItemCount(), 0);

	// A single node appears as one top-level item listing its keyframable
	// inputs (MathNode has three: the base "enabled" input and parameters
	// A and B)
	panel.set_node(reinterpret_cast<OakEngineNode *>(math));
	ASSERT_EQ(tree->topLevelItemCount(), 1);
	EXPECT_EQ(tree->topLevelItem(0)->text(0), math->name());
	EXPECT_EQ(tree->topLevelItem(0)->childCount(), 3);

	// A null node clears the tree again
	panel.set_node(nullptr);
	EXPECT_EQ(tree->topLevelItemCount(), 0);

	// Same through the multi-node slot
	panel.set_nodes({ math });
	EXPECT_EQ(tree->topLevelItemCount(), 1);
	panel.set_nodes({});
	EXPECT_EQ(tree->topLevelItemCount(), 0);
}

TEST_F(PanelTest, ParamPanelConstructionAndContexts)
{
	Project project;
	project.initialize();

	ParamPanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("ParamPanel"));
	EXPECT_EQ(panel.title(), QStringLiteral("Parameter Editor"));
	ASSERT_NE(panel.get_param_view(), nullptr);

	EXPECT_TRUE(panel.get_contexts().isEmpty());
	panel.set_contexts({ project.root() });
	ASSERT_EQ(panel.get_contexts().size(), 1);
	EXPECT_EQ(panel.get_contexts().first(), project.root());

	// Selection slots on an empty selection are harmless
	panel.select_all();
	panel.deselect_all();
	panel.delete_selected();
}

TEST_F(PanelTest, ParamPanelForwardsViewSignals)
{
	Project project;
	project.initialize();
	auto *math = add_node<MathNode>(&project);

	ParamPanel panel;

	QSignalSpy focused_spy(&panel, &ParamPanel::focused_node_changed);
	emit panel.get_param_view()->focused_node_changed(reinterpret_cast<OakEngineNode *>(math));
	ASSERT_EQ(focused_spy.count(), 1);
	EXPECT_EQ(focused_spy.first().first().value<OakEngineNode *>(), reinterpret_cast<OakEngineNode *>(math));

	QSignalSpy selected_spy(&panel, &ParamPanel::selected_nodes_changed);
	emit panel.get_param_view()->selected_nodes_changed({ { reinterpret_cast<OakEngineNode *>(math), nullptr } });
	EXPECT_EQ(selected_spy.count(), 1);

	QSignalSpy text_spy(&panel, &ParamPanel::request_viewer_to_start_editing_text);
	emit panel.get_param_view()->request_viewer_to_start_editing_text();
	EXPECT_EQ(text_spy.count(), 1);
}

TEST_F(PanelTest, ProjectPanelTracksProject)
{
	Project project;
	project.initialize();
	project.set_filename(QStringLiteral("/tmp/panel_test_project.ove"));

	ProjectPanel panel(QStringLiteral("ProjectPanelTest"));
	EXPECT_EQ(panel.objectName(), QStringLiteral("ProjectPanelTest"));
	EXPECT_EQ(panel.project(), nullptr);

	QSignalSpy name_spy(&panel, &ProjectPanel::project_name_changed);
	panel.set_project(&project);
	EXPECT_EQ(panel.project(), &project);
	EXPECT_EQ(name_spy.count(), 1);
	EXPECT_EQ(panel.get_root(), project.root());

	// The subtitle reflects the project name in the panel title
	EXPECT_TRUE(panel.title().contains(project.name()));

	// A child folder can become the shown root
	auto *folder = add_node<Folder>(&project);
	FolderAddChild(project.root(), folder).redo_now();
	panel.set_root(folder);
	EXPECT_EQ(panel.get_root(), folder);
}

TEST_F(PanelTest, ProjectPanelSelectsChildNodes)
{
	Project project;
	project.initialize();
	auto *math = add_node<MathNode>(&project);
	FolderAddChild(project.root(), math).redo_now();

	ProjectPanel panel(QStringLiteral("ProjectPanelSelectTest"));
	panel.set_project(&project);

	QSignalSpy selection_spy(&panel, &ProjectPanel::selection_changed);

	ASSERT_TRUE(panel.select_item(math));
	EXPECT_TRUE(panel.selected_items().contains(math));
	EXPECT_GT(selection_spy.count(), 0);
}

TEST_F(PanelTest, TimeBasedPanelSignalsAndTimebase)
{
	TestTimeBasedPanel panel(QStringLiteral("TimeBasedTestPanel"));
	panel.set_time_based_widget(new TimeBasedWidget(false, false, &panel));

	QSignalSpy play_pause_spy(&panel, &TimeBasedPanel::play_pause_requested);
	panel.play_pause();
	EXPECT_EQ(play_pause_spy.count(), 1);

	QSignalSpy play_in_out_spy(&panel, &TimeBasedPanel::play_in_to_out_requested);
	panel.play_in_to_out();
	EXPECT_EQ(play_in_out_spy.count(), 1);

	QSignalSpy shuttle_left_spy(&panel, &TimeBasedPanel::shuttle_left_requested);
	panel.shuttle_left();
	EXPECT_EQ(shuttle_left_spy.count(), 1);

	QSignalSpy shuttle_stop_spy(&panel, &TimeBasedPanel::shuttle_stop_requested);
	panel.shuttle_stop();
	EXPECT_EQ(shuttle_stop_spy.count(), 1);

	QSignalSpy shuttle_right_spy(&panel,
								 &TimeBasedPanel::shuttle_right_requested);
	panel.shuttle_right();
	EXPECT_EQ(shuttle_right_spy.count(), 1);

	panel.set_timebase(Rational(1, 30));
	EXPECT_EQ(panel.timebase(), Rational(1, 30));
}

TEST_F(PanelTest, TimeBasedPanelConnectViewerUpdatesSubtitle)
{
	Project project;
	project.initialize();
	auto *viewer = add_node<ViewerOutput>(&project);
	viewer->set_label(QStringLiteral("My Viewer"));

	TestTimeBasedPanel panel(QStringLiteral("TimeBasedConnectPanel"));
	panel.set_time_based_widget(new TimeBasedWidget(false, false, &panel));
	EXPECT_EQ(panel.get_connected_viewer(), nullptr);

	panel.connect_viewer_node(viewer);
	EXPECT_EQ(panel.get_connected_viewer(), viewer);
	EXPECT_TRUE(panel.title().contains(QStringLiteral("My Viewer")));

	// Label changes on the viewer propagate to the panel title
	viewer->set_label(QStringLiteral("Renamed Viewer"));
	EXPECT_TRUE(panel.title().contains(QStringLiteral("Renamed Viewer")));

	panel.disconnect_viewer_node();
	EXPECT_EQ(panel.get_connected_viewer(), nullptr);
}

TEST_F(PanelTest, NodeTablePanelConstruction)
{
	Project project;
	project.initialize();
	auto *math = add_node<MathNode>(&project);

	NodeTablePanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("NodeTablePanel"));
	EXPECT_EQ(panel.title(), QStringLiteral("Table View"));
	ASSERT_NE(panel.get_time_based_widget(), nullptr);

	auto *view = panel.findChild<NodeTableView *>();
	ASSERT_NE(view, nullptr);
	EXPECT_EQ(view->columnCount(), 6);
	EXPECT_EQ(view->headerItem()->text(0), QStringLiteral("Type"));
	EXPECT_EQ(view->topLevelItemCount(), 0);

	// Selecting a node adds a top-level row labeled with the node
	panel.select_nodes({ math });
	ASSERT_EQ(view->topLevelItemCount(), 1);
	EXPECT_EQ(view->topLevelItem(0)->text(0), math->get_label_and_name());

	// Deselecting removes it again
	panel.deselect_nodes({ math });
	EXPECT_EQ(view->topLevelItemCount(), 0);
}

TEST_F(PanelTest, MulticamPanelConstruction)
{
	MulticamPanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("MultiCamPanel"));
	EXPECT_TRUE(panel.title().startsWith(QStringLiteral("Multi-Cam")));
	EXPECT_NE(panel.get_multicam_widget(), nullptr);
	EXPECT_EQ(panel.get_connected_viewer(), nullptr);
}

TEST_F(PanelTest, HistoryPanelReflectsUndoStack)
{
	HistoryPanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("HistoryPanel"));
	EXPECT_EQ(panel.title(), QStringLiteral("History"));

	// The embedded HistoryWidget displays Core's undo stack
	auto *widget = panel.findChild<HistoryWidget *>();
	ASSERT_NE(widget, nullptr);
	EXPECT_EQ(widget->model(), Core::instance()->undo_stack());
}

TEST_F(PanelTest, FootageViewerPanelConstruction)
{
	Project project;
	project.initialize();
	auto *viewer = add_node<ViewerOutput>(&project);

	FootageViewerPanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("FootageViewerPanel"));
	EXPECT_TRUE(panel.title().startsWith(QStringLiteral("Footage Viewer")));
	EXPECT_NE(panel.get_footage_viewer_widget(), nullptr);

	// With nothing connected there is no selected footage
	EXPECT_TRUE(panel.get_selected_footage().isEmpty());

	panel.connect_viewer_node(viewer);
	ASSERT_EQ(panel.get_selected_footage().size(), 1);
	EXPECT_EQ(panel.get_selected_footage().first(), reinterpret_cast<OakEngineNode *>(viewer));

	panel.disconnect_viewer_node();
	EXPECT_TRUE(panel.get_selected_footage().isEmpty());
}

TEST_F(PanelTest, NodePanelConstructionAndContexts)
{
	Project project;
	project.initialize();

	NodePanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("NodePanel"));
	EXPECT_EQ(panel.title(), QStringLiteral("Node Editor"));
	EXPECT_NE(panel.get_node_widget(), nullptr);

	EXPECT_TRUE(panel.get_contexts().isEmpty());
	panel.set_contexts({ project.root() });
	ASSERT_EQ(panel.get_contexts().size(), 1);
	EXPECT_EQ(panel.get_contexts().first(), project.root());

	// Node selection actions on an empty scene are harmless
	panel.select_all();
	panel.deselect_all();
}

TEST_F(PanelTest, NodePanelForwardsViewSignals)
{
	Project project;
	project.initialize();
	auto *math = add_node<MathNode>(&project);

	NodePanel panel;
	panel.set_contexts({ project.root() });

	QSignalSpy selected_spy(&panel, &NodePanel::nodes_selected);
	emit panel.get_node_widget()->view()->nodes_selected({ reinterpret_cast<OakEngineNode *>(math) });
	ASSERT_EQ(selected_spy.count(), 1);

	QSignalSpy deselected_spy(&panel, &NodePanel::nodes_deselected);
	emit panel.get_node_widget()->view()->nodes_deselected({ reinterpret_cast<OakEngineNode *>(math) });
	EXPECT_EQ(deselected_spy.count(), 1);

	QSignalSpy selection_spy(&panel, &NodePanel::node_selection_changed);
	emit panel.get_node_widget()->view()->node_selection_changed({ reinterpret_cast<OakEngineNode *>(math) });
	EXPECT_EQ(selection_spy.count(), 1);
}

TEST_F(PanelTest, TimelinePanelConstructionAndSequence)
{
	Project project;
	project.initialize();
	auto *sequence = add_node<Sequence>(&project);

	TimelinePanel panel(QStringLiteral("TimelinePanelTest"));
	EXPECT_EQ(panel.objectName(), QStringLiteral("TimelinePanelTest"));
	EXPECT_NE(panel.timeline_widget(), nullptr);
	EXPECT_EQ(panel.get_sequence(), nullptr);

	panel.connect_viewer_node(sequence);
	EXPECT_EQ(panel.get_connected_viewer(), sequence);
	EXPECT_EQ(panel.get_sequence(), sequence);

	// Selection actions on an empty sequence are harmless
	panel.select_all();
	panel.deselect_all();
	EXPECT_TRUE(panel.get_selected_blocks().isEmpty());
}

TEST_F(PanelTest, TimelinePanelSaveLoadDataRoundTrip)
{
	TimelinePanel panel(QStringLiteral("TimelinePanelDataTest"));

	// The vertical view splitter (video/audio/subtitle views) is a direct
	// child of the timeline widget
	const QList<QSplitter *> splitters =
		panel.timeline_widget()->findChildren<QSplitter *>(
			QString(), Qt::FindDirectChildrenOnly);
	ASSERT_EQ(splitters.size(), 1);
	QSplitter *splitter = splitters.first();
	ASSERT_EQ(splitter->count(), 3);

	// Save a known layout
	splitter->setSizes({ 200, 400, 100 });
	const QList<int> saved_sizes = splitter->sizes();

	PanelWidget::Info info = panel.save_data();
	ASSERT_EQ(info.size(), 1);
	EXPECT_TRUE(info.count(QStringLiteral("splitter")));

	// Disturb the layout so the restore has something to undo
	splitter->setSizes({ 500, 100, 100 });
	ASSERT_NE(splitter->sizes(), saved_sizes);

	// LoadData must restore the splitter layout captured by SaveData
	panel.load_data(info);
	EXPECT_EQ(splitter->sizes(), saved_sizes);
	EXPECT_EQ(panel.timeline_widget()->save_splitter_state(),
			  QByteArray::fromBase64(
				  info.at(QStringLiteral("splitter")).toUtf8()));

	// Loading twice is idempotent
	panel.load_data(info);
	EXPECT_EQ(splitter->sizes(), saved_sizes);
}

TEST_F(PanelTest, ToolPanelReflectsCoreToolState)
{
	ToolPanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("ToolPanel"));
	EXPECT_EQ(panel.title(), QStringLiteral("Tools"));

	auto *toolbar = panel.findChild<Toolbar *>();
	ASSERT_NE(toolbar, nullptr);

	// The toolbar drives Core's active tool through the panel's connections
	Core::instance()->set_tool(Tool::k_pointer);
	emit toolbar->tool_changed(Tool::k_hand);
	EXPECT_EQ(Core::instance()->tool(), Tool::k_hand);

	// ...and snapping state
	emit toolbar->snapping_changed(false);
	EXPECT_FALSE(Core::instance()->snapping());
	emit toolbar->snapping_changed(true);
	EXPECT_TRUE(Core::instance()->snapping());

	Core::instance()->set_tool(Tool::k_pointer);
}

TEST_F(PanelTest, ViewerPanelConstruction)
{
	ViewerPanel panel(QStringLiteral("ViewerPanelTest"));
	EXPECT_EQ(panel.objectName(), QStringLiteral("ViewerPanelTest"));
	EXPECT_TRUE(panel.title().startsWith(QStringLiteral("Viewer")));
	EXPECT_NE(panel.get_viewer_widget(), nullptr);
	EXPECT_EQ(panel.get_connected_viewer(), nullptr);
}

TEST_F(PanelTest, SequenceViewerPanelConstruction)
{
	SequenceViewerPanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("SequenceViewerPanel"));
	EXPECT_TRUE(panel.title().startsWith(QStringLiteral("Sequence Viewer")));
	EXPECT_NE(panel.get_viewer_widget(), nullptr);
}

TEST_F(PanelTest, ViewerPanelConnectTimeBasedPanel)
{
	ViewerPanel viewer_panel(QStringLiteral("ViewerWiringPanel"));
	CurvePanel curve_panel;

	// Routing playback commands from a timebased panel to the viewer must not
	// crash, even with nothing connected to the viewer
	viewer_panel.connect_time_based_panel(&curve_panel);

	QSignalSpy spy(&curve_panel, &TimeBasedPanel::play_pause_requested);
	curve_panel.play_pause();
	EXPECT_EQ(spy.count(), 1);

	curve_panel.shuttle_left();
	curve_panel.shuttle_stop();
	curve_panel.shuttle_right();

	viewer_panel.disconnect_time_based_panel(&curve_panel);
}

TEST_F(PanelTest, ScopePanelConstructionAndTypeSwitching)
{
	ScopePanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("ScopePanel"));
	EXPECT_EQ(panel.title(), QStringLiteral("Scopes"));
	EXPECT_EQ(panel.get_connected_viewer_panel(), nullptr);

	// Every scope type has a human readable name
	for (int i = 0; i < ScopePanel::k_type_count; i++) {
		EXPECT_FALSE(
			ScopePanel::type_to_name(static_cast<ScopePanel::Type>(i)).isEmpty());
	}

	auto *combo = panel.findChild<QComboBox *>();
	auto *stack = panel.findChild<QStackedWidget *>();
	ASSERT_NE(combo, nullptr);
	ASSERT_NE(stack, nullptr);
	ASSERT_EQ(stack->count(), ScopePanel::k_type_count);

	// SetType switches the visible scope through the combo box
	panel.set_type(ScopePanel::k_type_histogram);
	EXPECT_EQ(combo->currentIndex(), ScopePanel::k_type_histogram);

	panel.set_type(ScopePanel::k_type_vectorscope);
	EXPECT_EQ(combo->currentIndex(), ScopePanel::k_type_vectorscope);
}

TEST_F(PanelTest, ScopePanelViewerConnection)
{
	ScopePanel scope_panel;
	ViewerPanel viewer_panel(QStringLiteral("ScopeSourcePanel"));

	scope_panel.set_viewer_panel(&viewer_panel);
	EXPECT_EQ(scope_panel.get_connected_viewer_panel(), &viewer_panel);

	// Setting the same panel again is a no-op
	scope_panel.set_viewer_panel(&viewer_panel);
	EXPECT_EQ(scope_panel.get_connected_viewer_panel(), &viewer_panel);

	// Disconnecting clears the reference buffer connection
	scope_panel.set_viewer_panel(nullptr);
	EXPECT_EQ(scope_panel.get_connected_viewer_panel(), nullptr);
}

TEST_F(PanelTest, AudioMonitorPanelConstruction)
{
	AudioMonitorPanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("AudioMonitor"));
	EXPECT_EQ(panel.title(), QStringLiteral("Audio Monitor"));
	EXPECT_FALSE(panel.is_playing());

	panel.set_params(core::AudioParams(48000, core::k_channel_layout_stereo,
									  core::SampleFormat::f32_p));
}
