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
		ColorManager::SetUpDefaultConfig();

		if (!Core::instance()) {
			new Core(Core::CoreParams()); // intentionally leaked
		}

		KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);

		if (!PanelManager::instance()) {
			PanelManager::CreateInstance();
			created_panel_manager_ = true;
		}

		if (!TaskManager::instance()) {
			TaskManager::CreateInstance();
			created_task_manager_ = true;
		}

		if (!RenderManager::instance()) {
			// Another suite may have left an experimental backend in the
			// config; RenderManager needs a real one to create its cacher
			saved_backend_ =
				Config::Current()[QStringLiteral("GraphicsBackend")];
			Config::Current()[QStringLiteral("GraphicsBackend")] =
				QStringLiteral("opengl");
			RenderManager::CreateInstance();
			created_render_manager_ = true;
		}

		if (!DiskManager::instance()) {
			DiskManager::CreateInstance();
			created_disk_manager_ = true;
		}
	}

	~PanelEnvironment()
	{
		// Panels must be gone before the manager that tracks them
		if (created_panel_manager_) {
			PanelManager::instance()->DeleteAllPanels();
			PanelManager::DestroyInstance();
		}
		if (created_task_manager_) {
			TaskManager::DestroyInstance();
		}
		if (created_render_manager_) {
			RenderManager::DestroyInstance();
			Config::Current()[QStringLiteral("GraphicsBackend")] =
				saved_backend_;
		}
		if (created_disk_manager_) {
			DiskManager::DestroyInstance();
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

	using PanelWidget::SetSubtitle;
	using PanelWidget::SetTitle;
};

class TestTimeBasedPanel : public TimeBasedPanel {
public:
	using TimeBasedPanel::SetTimeBasedWidget;
	using TimeBasedPanel::TimeBasedPanel;
};

class DummyTask : public Task {
public:
	DummyTask()
	{
		SetTitle(QStringLiteral("Panel Test Task"));
	}

protected:
	virtual bool Run() override
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

	template <typename T> T *AddNode(Project *project)
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

	panel.SetTitle(QStringLiteral("Title"));
	EXPECT_EQ(panel.title(), QStringLiteral("Title"));

	panel.SetSubtitle(QStringLiteral("Sub"));
	EXPECT_EQ(panel.title(), QStringLiteral("Title: Sub"));

	// Clearing the subtitle falls back to the bare title
	panel.SetSubtitle(QString());
	EXPECT_EQ(panel.title(), QStringLiteral("Title"));
}

TEST_F(PanelTest, PanelWidgetBaseRegistersWithPanelManager)
{
	PanelManager *manager = PanelManager::instance();
	const int panel_count_before = manager->panels().size();

	auto *panel = new TestPanel(QStringLiteral("RegisteredPanel"));
	EXPECT_TRUE(manager->panels().contains(panel));
	EXPECT_EQ(manager->panels().size(), panel_count_before + 1);
	EXPECT_EQ(manager->GetPanelWithName(QStringLiteral("RegisteredPanel")),
			  panel);
	EXPECT_TRUE(manager->GetPanelsOfType<TestPanel>().contains(panel));

	delete panel;
	EXPECT_FALSE(manager->panels().contains(panel));
	EXPECT_EQ(manager->GetPanelWithName(QStringLiteral("RegisteredPanel")),
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
	panel.SetSignalInsteadOfClose(true);
	QSignalSpy spy(&panel, &PanelWidget::CloseRequested);
	panel.show();
	ASSERT_TRUE(panel.isVisible());
	panel.close();
	EXPECT_EQ(spy.count(), 1);
	EXPECT_TRUE(panel.isVisible());
}

TEST_F(PanelTest, PanelWidgetBaseDefaultActionsLeaveStateUnchanged)
{
	TestPanel panel(QStringLiteral("NoOpTestPanel"));
	panel.SetTitle(QStringLiteral("NoOp"));
	panel.show();
	ASSERT_TRUE(panel.isVisible());

	// Default SaveData is empty and LoadData accepts anything
	EXPECT_TRUE(panel.SaveData().empty());
	panel.LoadData(PanelWidget::Info());

	// None of the default actions may request a close
	QSignalSpy close_spy(&panel, &PanelWidget::CloseRequested);

	// All default actions are no-ops and must not crash
	panel.ZoomIn();
	panel.ZoomOut();
	panel.GoToStart();
	panel.PrevFrame();
	panel.PlayPause();
	panel.PlayInToOut();
	panel.NextFrame();
	panel.GoToEnd();
	panel.SelectAll();
	panel.DeselectAll();
	panel.RippleToIn();
	panel.RippleToOut();
	panel.EditToIn();
	panel.EditToOut();
	panel.ShuttleLeft();
	panel.ShuttleStop();
	panel.ShuttleRight();
	panel.GoToPrevCut();
	panel.GoToNextCut();
	panel.RenameSelected();
	panel.DeleteSelected();
	panel.RippleDelete();
	panel.IncreaseTrackHeight();
	panel.DecreaseTrackHeight();
	panel.SetIn();
	panel.SetOut();
	panel.ResetIn();
	panel.ResetOut();
	panel.ClearInOut();
	panel.SetMarker();
	panel.ToggleLinks();
	panel.CutSelected();
	panel.CopySelected();
	panel.Paste();
	panel.PasteInsert();
	panel.ToggleShowAll();
	panel.GoToIn();
	panel.GoToOut();
	panel.DeleteInToOut();
	panel.RippleDeleteInToOut();
	panel.ToggleSelectedEnabled();
	panel.Duplicate();
	panel.SetColorLabel(1);
	panel.NudgeLeft();
	panel.NudgeRight();
	panel.MoveInToPlayhead();
	panel.MoveOutToPlayhead();

	// The sweep must not have altered any observable panel state
	EXPECT_EQ(panel.title(), QStringLiteral("NoOp"));
	EXPECT_TRUE(panel.isVisible());
	EXPECT_TRUE(panel.SaveData().empty());
	EXPECT_EQ(close_spy.count(), 0);
}

TEST_F(PanelTest, PanelWidgetBaseBorderAndFocus)
{
	TestPanel panel(QStringLiteral("BorderTestPanel"));
	panel.SetBorderVisible(true);
	panel.show();

	// The shown signal is wired to grab focus
	emit panel.shown(Qt::OtherFocusReason);
	emit panel.hidden();

	// Focus history lookup by type works through PanelManager
	EXPECT_EQ(PanelManager::instance()->MostRecentlyFocused<TestPanel>(),
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
	panel.SetValues(red, green);

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
	TaskManager::instance()->AddTask(task);

	auto *view = panel.findChild<TaskView *>();
	ASSERT_NE(view, nullptr);
	EXPECT_NE(view->findChild<TaskViewItem *>(), nullptr);

	// TaskManager owns the task now; cancel it and let the removal propagate
	TaskManager::instance()->CancelTaskAndWait(task);
	QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_F(PanelTest, CurvePanelConstructionAndScaling)
{
	CurvePanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("CurvePanel"));
	EXPECT_TRUE(panel.title().startsWith(QStringLiteral("Curve Editor")));
	ASSERT_NE(panel.GetTimeBasedWidget(), nullptr);

	auto *curve = static_cast<CurveWidget *>(panel.GetTimeBasedWidget());

	// Track height actions scale the curve view vertically
	const double initial_scale = curve->GetVerticalScale();
	panel.IncreaseTrackHeight();
	EXPECT_DOUBLE_EQ(curve->GetVerticalScale(), initial_scale * 2);
	panel.DecreaseTrackHeight();
	EXPECT_DOUBLE_EQ(curve->GetVerticalScale(), initial_scale);

	// Selection actions on an empty view are harmless
	panel.SelectAll();
	panel.DeselectAll();
	panel.DeleteSelected();
}

TEST_F(PanelTest, CurvePanelSetNodes)
{
	Project project;
	project.Initialize();
	auto *math = AddNode<MathNode>(&project);

	CurvePanel panel;
	auto *tree = panel.findChild<NodeTreeView *>();
	ASSERT_NE(tree, nullptr);
	EXPECT_EQ(tree->topLevelItemCount(), 0);

	// A single node appears as one top-level item listing its keyframable
	// inputs (MathNode has three: the base "enabled" input and parameters
	// A and B)
	panel.SetNode(math);
	ASSERT_EQ(tree->topLevelItemCount(), 1);
	EXPECT_EQ(tree->topLevelItem(0)->text(0), math->Name());
	EXPECT_EQ(tree->topLevelItem(0)->childCount(), 3);

	// A null node clears the tree again
	panel.SetNode(nullptr);
	EXPECT_EQ(tree->topLevelItemCount(), 0);

	// Same through the multi-node slot
	panel.SetNodes({ math });
	EXPECT_EQ(tree->topLevelItemCount(), 1);
	panel.SetNodes({});
	EXPECT_EQ(tree->topLevelItemCount(), 0);
}

TEST_F(PanelTest, ParamPanelConstructionAndContexts)
{
	Project project;
	project.Initialize();

	ParamPanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("ParamPanel"));
	EXPECT_EQ(panel.title(), QStringLiteral("Parameter Editor"));
	ASSERT_NE(panel.GetParamView(), nullptr);

	EXPECT_TRUE(panel.GetContexts().isEmpty());
	panel.SetContexts({ project.root() });
	ASSERT_EQ(panel.GetContexts().size(), 1);
	EXPECT_EQ(panel.GetContexts().first(), project.root());

	// Selection slots on an empty selection are harmless
	panel.SelectAll();
	panel.DeselectAll();
	panel.DeleteSelected();
}

TEST_F(PanelTest, ParamPanelForwardsViewSignals)
{
	Project project;
	project.Initialize();
	auto *math = AddNode<MathNode>(&project);

	ParamPanel panel;

	QSignalSpy focused_spy(&panel, &ParamPanel::FocusedNodeChanged);
	emit panel.GetParamView()->FocusedNodeChanged(math);
	ASSERT_EQ(focused_spy.count(), 1);
	EXPECT_EQ(focused_spy.first().first().value<Node *>(), math);

	QSignalSpy selected_spy(&panel, &ParamPanel::SelectedNodesChanged);
	emit panel.GetParamView()->SelectedNodesChanged({ { math, nullptr } });
	EXPECT_EQ(selected_spy.count(), 1);

	QSignalSpy text_spy(&panel, &ParamPanel::RequestViewerToStartEditingText);
	emit panel.GetParamView()->RequestViewerToStartEditingText();
	EXPECT_EQ(text_spy.count(), 1);
}

TEST_F(PanelTest, ProjectPanelTracksProject)
{
	Project project;
	project.Initialize();
	project.set_filename(QStringLiteral("/tmp/panel_test_project.ove"));

	ProjectPanel panel(QStringLiteral("ProjectPanelTest"));
	EXPECT_EQ(panel.objectName(), QStringLiteral("ProjectPanelTest"));
	EXPECT_EQ(panel.project(), nullptr);

	QSignalSpy name_spy(&panel, &ProjectPanel::ProjectNameChanged);
	panel.set_project(&project);
	EXPECT_EQ(panel.project(), &project);
	EXPECT_EQ(name_spy.count(), 1);
	EXPECT_EQ(panel.get_root(), project.root());

	// The subtitle reflects the project name in the panel title
	EXPECT_TRUE(panel.title().contains(project.name()));

	// A child folder can become the shown root
	auto *folder = AddNode<Folder>(&project);
	FolderAddChild(project.root(), folder).redo_now();
	panel.set_root(folder);
	EXPECT_EQ(panel.get_root(), folder);
}

TEST_F(PanelTest, ProjectPanelSelectsChildNodes)
{
	Project project;
	project.Initialize();
	auto *math = AddNode<MathNode>(&project);
	FolderAddChild(project.root(), math).redo_now();

	ProjectPanel panel(QStringLiteral("ProjectPanelSelectTest"));
	panel.set_project(&project);

	QSignalSpy selection_spy(&panel, &ProjectPanel::SelectionChanged);

	ASSERT_TRUE(panel.SelectItem(math));
	EXPECT_TRUE(panel.SelectedItems().contains(math));
	EXPECT_GT(selection_spy.count(), 0);
}

TEST_F(PanelTest, TimeBasedPanelSignalsAndTimebase)
{
	TestTimeBasedPanel panel(QStringLiteral("TimeBasedTestPanel"));
	panel.SetTimeBasedWidget(new TimeBasedWidget(false, false, &panel));

	QSignalSpy play_pause_spy(&panel, &TimeBasedPanel::PlayPauseRequested);
	panel.PlayPause();
	EXPECT_EQ(play_pause_spy.count(), 1);

	QSignalSpy play_in_out_spy(&panel, &TimeBasedPanel::PlayInToOutRequested);
	panel.PlayInToOut();
	EXPECT_EQ(play_in_out_spy.count(), 1);

	QSignalSpy shuttle_left_spy(&panel, &TimeBasedPanel::ShuttleLeftRequested);
	panel.ShuttleLeft();
	EXPECT_EQ(shuttle_left_spy.count(), 1);

	QSignalSpy shuttle_stop_spy(&panel, &TimeBasedPanel::ShuttleStopRequested);
	panel.ShuttleStop();
	EXPECT_EQ(shuttle_stop_spy.count(), 1);

	QSignalSpy shuttle_right_spy(&panel,
								 &TimeBasedPanel::ShuttleRightRequested);
	panel.ShuttleRight();
	EXPECT_EQ(shuttle_right_spy.count(), 1);

	panel.SetTimebase(rational(1, 30));
	EXPECT_EQ(panel.timebase(), rational(1, 30));
}

TEST_F(PanelTest, TimeBasedPanelConnectViewerUpdatesSubtitle)
{
	Project project;
	project.Initialize();
	auto *viewer = AddNode<ViewerOutput>(&project);
	viewer->SetLabel(QStringLiteral("My Viewer"));

	TestTimeBasedPanel panel(QStringLiteral("TimeBasedConnectPanel"));
	panel.SetTimeBasedWidget(new TimeBasedWidget(false, false, &panel));
	EXPECT_EQ(panel.GetConnectedViewer(), nullptr);

	panel.ConnectViewerNode(viewer);
	EXPECT_EQ(panel.GetConnectedViewer(), viewer);
	EXPECT_TRUE(panel.title().contains(QStringLiteral("My Viewer")));

	// Label changes on the viewer propagate to the panel title
	viewer->SetLabel(QStringLiteral("Renamed Viewer"));
	EXPECT_TRUE(panel.title().contains(QStringLiteral("Renamed Viewer")));

	panel.DisconnectViewerNode();
	EXPECT_EQ(panel.GetConnectedViewer(), nullptr);
}

TEST_F(PanelTest, NodeTablePanelConstruction)
{
	Project project;
	project.Initialize();
	auto *math = AddNode<MathNode>(&project);

	NodeTablePanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("NodeTablePanel"));
	EXPECT_EQ(panel.title(), QStringLiteral("Table View"));
	ASSERT_NE(panel.GetTimeBasedWidget(), nullptr);

	auto *view = panel.findChild<NodeTableView *>();
	ASSERT_NE(view, nullptr);
	EXPECT_EQ(view->columnCount(), 6);
	EXPECT_EQ(view->headerItem()->text(0), QStringLiteral("Type"));
	EXPECT_EQ(view->topLevelItemCount(), 0);

	// Selecting a node adds a top-level row labeled with the node
	panel.SelectNodes({ math });
	ASSERT_EQ(view->topLevelItemCount(), 1);
	EXPECT_EQ(view->topLevelItem(0)->text(0), math->GetLabelAndName());

	// Deselecting removes it again
	panel.DeselectNodes({ math });
	EXPECT_EQ(view->topLevelItemCount(), 0);
}

TEST_F(PanelTest, MulticamPanelConstruction)
{
	MulticamPanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("MultiCamPanel"));
	EXPECT_TRUE(panel.title().startsWith(QStringLiteral("Multi-Cam")));
	EXPECT_NE(panel.GetMulticamWidget(), nullptr);
	EXPECT_EQ(panel.GetConnectedViewer(), nullptr);
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
	project.Initialize();
	auto *viewer = AddNode<ViewerOutput>(&project);

	FootageViewerPanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("FootageViewerPanel"));
	EXPECT_TRUE(panel.title().startsWith(QStringLiteral("Footage Viewer")));
	EXPECT_NE(panel.GetFootageViewerWidget(), nullptr);

	// With nothing connected there is no selected footage
	EXPECT_TRUE(panel.GetSelectedFootage().isEmpty());

	panel.ConnectViewerNode(viewer);
	ASSERT_EQ(panel.GetSelectedFootage().size(), 1);
	EXPECT_EQ(panel.GetSelectedFootage().first(), viewer);

	panel.DisconnectViewerNode();
	EXPECT_TRUE(panel.GetSelectedFootage().isEmpty());
}

TEST_F(PanelTest, NodePanelConstructionAndContexts)
{
	Project project;
	project.Initialize();

	NodePanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("NodePanel"));
	EXPECT_EQ(panel.title(), QStringLiteral("Node Editor"));
	EXPECT_NE(panel.GetNodeWidget(), nullptr);

	EXPECT_TRUE(panel.GetContexts().isEmpty());
	panel.SetContexts({ project.root() });
	ASSERT_EQ(panel.GetContexts().size(), 1);
	EXPECT_EQ(panel.GetContexts().first(), project.root());

	// Node selection actions on an empty scene are harmless
	panel.SelectAll();
	panel.DeselectAll();
}

TEST_F(PanelTest, NodePanelForwardsViewSignals)
{
	Project project;
	project.Initialize();
	auto *math = AddNode<MathNode>(&project);

	NodePanel panel;
	panel.SetContexts({ project.root() });

	QSignalSpy selected_spy(&panel, &NodePanel::NodesSelected);
	emit panel.GetNodeWidget()->view()->NodesSelected({ math });
	ASSERT_EQ(selected_spy.count(), 1);

	QSignalSpy deselected_spy(&panel, &NodePanel::NodesDeselected);
	emit panel.GetNodeWidget()->view()->NodesDeselected({ math });
	EXPECT_EQ(deselected_spy.count(), 1);

	QSignalSpy selection_spy(&panel, &NodePanel::NodeSelectionChanged);
	emit panel.GetNodeWidget()->view()->NodeSelectionChanged({ math });
	EXPECT_EQ(selection_spy.count(), 1);
}

TEST_F(PanelTest, TimelinePanelConstructionAndSequence)
{
	Project project;
	project.Initialize();
	auto *sequence = AddNode<Sequence>(&project);

	TimelinePanel panel(QStringLiteral("TimelinePanelTest"));
	EXPECT_EQ(panel.objectName(), QStringLiteral("TimelinePanelTest"));
	EXPECT_NE(panel.timeline_widget(), nullptr);
	EXPECT_EQ(panel.GetSequence(), nullptr);

	panel.ConnectViewerNode(sequence);
	EXPECT_EQ(panel.GetConnectedViewer(), sequence);
	EXPECT_EQ(panel.GetSequence(), sequence);

	// Selection actions on an empty sequence are harmless
	panel.SelectAll();
	panel.DeselectAll();
	EXPECT_TRUE(panel.GetSelectedBlocks().isEmpty());
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

	PanelWidget::Info info = panel.SaveData();
	ASSERT_EQ(info.size(), 1);
	EXPECT_TRUE(info.count(QStringLiteral("splitter")));

	// Disturb the layout so the restore has something to undo
	splitter->setSizes({ 500, 100, 100 });
	ASSERT_NE(splitter->sizes(), saved_sizes);

	// LoadData must restore the splitter layout captured by SaveData
	panel.LoadData(info);
	EXPECT_EQ(splitter->sizes(), saved_sizes);
	EXPECT_EQ(panel.timeline_widget()->SaveSplitterState(),
			  QByteArray::fromBase64(
				  info.at(QStringLiteral("splitter")).toUtf8()));

	// Loading twice is idempotent
	panel.LoadData(info);
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
	Core::instance()->SetTool(Tool::kPointer);
	emit toolbar->ToolChanged(Tool::kHand);
	EXPECT_EQ(Core::instance()->tool(), Tool::kHand);

	// ...and snapping state
	emit toolbar->SnappingChanged(false);
	EXPECT_FALSE(Core::instance()->snapping());
	emit toolbar->SnappingChanged(true);
	EXPECT_TRUE(Core::instance()->snapping());

	Core::instance()->SetTool(Tool::kPointer);
}

TEST_F(PanelTest, ViewerPanelConstruction)
{
	ViewerPanel panel(QStringLiteral("ViewerPanelTest"));
	EXPECT_EQ(panel.objectName(), QStringLiteral("ViewerPanelTest"));
	EXPECT_TRUE(panel.title().startsWith(QStringLiteral("Viewer")));
	EXPECT_NE(panel.GetViewerWidget(), nullptr);
	EXPECT_EQ(panel.GetConnectedViewer(), nullptr);
}

TEST_F(PanelTest, SequenceViewerPanelConstruction)
{
	SequenceViewerPanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("SequenceViewerPanel"));
	EXPECT_TRUE(panel.title().startsWith(QStringLiteral("Sequence Viewer")));
	EXPECT_NE(panel.GetViewerWidget(), nullptr);
}

TEST_F(PanelTest, ViewerPanelConnectTimeBasedPanel)
{
	ViewerPanel viewer_panel(QStringLiteral("ViewerWiringPanel"));
	CurvePanel curve_panel;

	// Routing playback commands from a timebased panel to the viewer must not
	// crash, even with nothing connected to the viewer
	viewer_panel.ConnectTimeBasedPanel(&curve_panel);

	QSignalSpy spy(&curve_panel, &TimeBasedPanel::PlayPauseRequested);
	curve_panel.PlayPause();
	EXPECT_EQ(spy.count(), 1);

	curve_panel.ShuttleLeft();
	curve_panel.ShuttleStop();
	curve_panel.ShuttleRight();

	viewer_panel.DisconnectTimeBasedPanel(&curve_panel);
}

TEST_F(PanelTest, ScopePanelConstructionAndTypeSwitching)
{
	ScopePanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("ScopePanel"));
	EXPECT_EQ(panel.title(), QStringLiteral("Scopes"));
	EXPECT_EQ(panel.GetConnectedViewerPanel(), nullptr);

	// Every scope type has a human readable name
	for (int i = 0; i < ScopePanel::kTypeCount; i++) {
		EXPECT_FALSE(
			ScopePanel::TypeToName(static_cast<ScopePanel::Type>(i)).isEmpty());
	}

	auto *combo = panel.findChild<QComboBox *>();
	auto *stack = panel.findChild<QStackedWidget *>();
	ASSERT_NE(combo, nullptr);
	ASSERT_NE(stack, nullptr);
	ASSERT_EQ(stack->count(), ScopePanel::kTypeCount);

	// SetType switches the visible scope through the combo box
	panel.SetType(ScopePanel::kTypeHistogram);
	EXPECT_EQ(combo->currentIndex(), ScopePanel::kTypeHistogram);

	panel.SetType(ScopePanel::kTypeVectorscope);
	EXPECT_EQ(combo->currentIndex(), ScopePanel::kTypeVectorscope);
}

TEST_F(PanelTest, ScopePanelViewerConnection)
{
	ScopePanel scope_panel;
	ViewerPanel viewer_panel(QStringLiteral("ScopeSourcePanel"));

	scope_panel.SetViewerPanel(&viewer_panel);
	EXPECT_EQ(scope_panel.GetConnectedViewerPanel(), &viewer_panel);

	// Setting the same panel again is a no-op
	scope_panel.SetViewerPanel(&viewer_panel);
	EXPECT_EQ(scope_panel.GetConnectedViewerPanel(), &viewer_panel);

	// Disconnecting clears the reference buffer connection
	scope_panel.SetViewerPanel(nullptr);
	EXPECT_EQ(scope_panel.GetConnectedViewerPanel(), nullptr);
}

TEST_F(PanelTest, AudioMonitorPanelConstruction)
{
	AudioMonitorPanel panel;
	EXPECT_EQ(panel.objectName(), QStringLiteral("AudioMonitor"));
	EXPECT_EQ(panel.title(), QStringLiteral("Audio Monitor"));
	EXPECT_FALSE(panel.IsPlaying());

	panel.SetParams(core::AudioParams(48000, core::kChannelLayoutStereo,
									  core::SampleFormat::F32P));
}
