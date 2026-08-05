#include <gtest/gtest.h>

#include <QAction>
#include <QGuiApplication>
#include <QMenu>
#include <QMenuBar>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSignalSpy>

#include <kddockwidgets/KDDockWidgets.h>

#include "audio/audiomanager.h"
#include "common/tooltypes.h"
#include "config/config.h"
#include "core.h"
#include "engineeventbridge.h"
#include "node/color/colormanager/colormanager.h"
#include "node/math/math/math.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/project/folder/folder.h"
#include "node/project/sequence/sequence.h"
#include "oakengine/undo.h"
#include "oakengine/viewer.h"
#include "oakutil/oaknode.h"
#include "panel/footageviewer/footageviewer.h"
#include "panel/panelmanager.h"
#include "panel/project/footagemanagementpanel.h"
#include "panel/timebased/timebased.h"
#include "panel/viewer/viewerbase.h"
#include "render/diskmanager.h"
#include "render/rendermanager.h"
#include "task/taskmanager.h"
#include "undo/undocommand.h"
#include "widget/menu/menu.h"
#include "widget/menu/menushared.h"
#include "widget/timebased/timebasedwidget.h"
#include "window/mainwindow/mainmenu.h"
#include "window/mainwindow/mainwindow.h"
#include "window/mainwindow/mainwindowundo.h"

using namespace olive;

namespace
{

// Widgets/panels that talk to Core::instance() at construction require the
// application singleton, but not a MainWindow
void ensure_core()
{
	if (!Core::instance()) {
		new Core(); // intentionally leaked
	}
}

// Panels register with the PanelManager singleton and ViewerWidget-based
// panels talk to TaskManager/RenderManager in their constructors. Same
// pattern as PanelEnvironment in panel_test.cpp.
class PanelTestEnvironment {
public:
	PanelTestEnvironment()
	{
		ColorManager::set_up_default_config();

		ensure_core();

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

	~PanelTestEnvironment()
	{
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

// Full environment for MainWindow construction, mirroring the setup in
// mainwindow_test.cpp (MainWindow needs MenuShared and AudioManager on top
// of the panel singletons)
class MainWindowEnvironment : public PanelTestEnvironment {
public:
	MainWindowEnvironment()
	{
		if (!MenuShared::instance()) {
			MenuShared::create_instance();
			created_menu_shared_ = true;
		}
		if (!AudioManager::instance()) {
			AudioManager::create_instance();
			created_audio_manager_ = true;
		}

		// Suppress the modal welcome dialog shown on first show
		saved_welcome_ = Config::current()[QStringLiteral("ShowWelcomeDialog")];
		Config::current()[QStringLiteral("ShowWelcomeDialog")] = false;
	}

	~MainWindowEnvironment()
	{
		Config::current()[QStringLiteral("ShowWelcomeDialog")] =
			saved_welcome_;
		if (created_audio_manager_) {
			AudioManager::destroy_instance();
		}
		if (created_menu_shared_) {
			MenuShared::destroy_instance();
		}
	}

private:
	bool created_menu_shared_ = false;
	bool created_audio_manager_ = false;
	QVariant saved_welcome_;
};

// MainWindow instantiates viewer panels containing QOpenGLWidget, which the
// offscreen QPA cannot paint and which crashes on platforms without a usable
// GL implementation (see the MainWindow test in mainwindow_test.cpp)
bool main_window_unsupported_here()
{
	if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
		return true;
	}
	QOffscreenSurface probe_surface;
	probe_surface.create();
	QOpenGLContext probe_context;
	const bool gl_available =
		probe_context.create() && probe_context.makeCurrent(&probe_surface);
	probe_context.doneCurrent();
	return !gl_available;
}

// GTEST_SKIP only returns from the enclosing function, so the guard has to
// sit in the test body itself
#define SKIP_WITHOUT_MAIN_WINDOW() \
	if (main_window_unsupported_here()) { \
		GTEST_SKIP() << "MainWindow requires a GL-capable platform"; \
	}

// Recursively finds the menu item conformed with the given "id" property.
// Iterates actions() rather than findChildren() because some items (the
// engine undo actions, the alternate delete item) are not menu children.
QAction *find_item_by_id(QMenu *menu, const QString &id)
{
	foreach (QAction *a, menu->actions()) {
		if (a->property("id").toString() == id) {
			return a;
		}
		if (QMenu *sub = a->menu()) {
			if (QAction *found = find_item_by_id(sub, id)) {
				return found;
			}
		}
	}
	return nullptr;
}

QMenu *top_level_menu(QMenuBar *bar, const QString &title)
{
	foreach (QAction *a, bar->actions()) {
		if (a->menu() && a->menu()->title() == title) {
			return a->menu();
		}
	}
	return nullptr;
}

// Exposes the protected set_viewer_widget and counts the playback overrides
// so signal routing into the panel can be observed
class ProbeViewerPanel : public ViewerPanelBase {
public:
	explicit ProbeViewerPanel(const QString &object_name)
		: ViewerPanelBase(object_name)
	{
	}

	void install_widget(ViewerWidget *vw)
	{
		set_viewer_widget(vw);
	}

	virtual void play_pause() override
	{
		play_pause_count_++;
		ViewerPanelBase::play_pause();
	}
	virtual void play_in_to_out() override
	{
		play_in_to_out_count_++;
		ViewerPanelBase::play_in_to_out();
	}
	virtual void shuttle_left() override
	{
		shuttle_left_count_++;
		ViewerPanelBase::shuttle_left();
	}
	virtual void shuttle_stop() override
	{
		shuttle_stop_count_++;
		ViewerPanelBase::shuttle_stop();
	}
	virtual void shuttle_right() override
	{
		shuttle_right_count_++;
		ViewerPanelBase::shuttle_right();
	}

	int play_pause_count_ = 0;
	int play_in_to_out_count_ = 0;
	int shuttle_left_count_ = 0;
	int shuttle_stop_count_ = 0;
	int shuttle_right_count_ = 0;
};

class ProbeTimeBasedPanel : public TimeBasedPanel {
public:
	using TimeBasedPanel::TimeBasedPanel;
	using TimeBasedPanel::set_time_based_widget;
};

// Minimal FootageManagementPanel implementation for interface dispatch tests
class FakeFootageManagementPanel : public FootageManagementPanel {
public:
	virtual QVector<OakEngineNode *> get_selected_footage() const override
	{
		return selection_;
	}

	QVector<OakEngineNode *> selection_;
};

int g_counting_redo_calls = 0;
int g_counting_undo_calls = 0;

void counting_redo(void *userdata)
{
	Q_UNUSED(userdata)
	g_counting_redo_calls++;
}

void counting_undo(void *userdata)
{
	Q_UNUSED(userdata)
	g_counting_undo_calls++;
}

} // namespace

class EngineEventBridgeTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();
		ensure_core();
		project_.initialize();
	}

	// Adds a node of type T parented to the fixture project
	template <typename T> T *add_node()
	{
		auto *node = new T();
		node->setParent(&project_);
		return node;
	}

	Project project_;
};

class ViewerPanelBaseTest : public ::testing::Test {
protected:
	PanelTestEnvironment env_;
};

class FootageManagementPanelTest : public ::testing::Test {
protected:
	PanelTestEnvironment env_;
};

// --- EngineEventBridge (app/engineeventbridge) ---

TEST_F(EngineEventBridgeTest, SubscribeRejectsNullAndMismatchedHandles)
{
	auto *math = add_node<MathNode>();
	auto *math_handle = reinterpret_cast<OakEngineNode *>(math);

	EngineEventBridge bridge;

	// NULL handle fails, per the oakengine_event_subscribe contract
	EXPECT_EQ(bridge.subscribe(nullptr, OAKENGINE_EVENT_NODE_LABEL_CHANGED), 0);

	// A plain node is neither a Project nor a viewer: both families reject it
	EXPECT_EQ(bridge.subscribe(math_handle,
							   OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED),
			  0);
	EXPECT_EQ(bridge.subscribe(math_handle,
							   OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED),
			  0);
}

TEST_F(EngineEventBridgeTest, NodeLabelChangedForwardsSourceAndLabel)
{
	auto *math = add_node<MathNode>();
	auto *math_handle = reinterpret_cast<OakEngineNode *>(math);

	EngineEventBridge bridge;
	ASSERT_GT(bridge.subscribe(math_handle, OAKENGINE_EVENT_NODE_LABEL_CHANGED),
			  0);

	QSignalSpy spy(&bridge, &EngineEventBridge::node_label_changed);

	math->set_label(QStringLiteral("Bridge Label"));
	ASSERT_EQ(spy.count(), 1);
	EXPECT_EQ(spy.first().at(0).value<OakEngineNode *>(), math_handle);
	EXPECT_EQ(spy.first().at(1).toString(), QStringLiteral("Bridge Label"));

	// Node::set_label() guards on actual changes; re-setting emits nothing
	math->set_label(QStringLiteral("Bridge Label"));
	EXPECT_EQ(spy.count(), 1);
}

TEST_F(EngineEventBridgeTest, FolderInsertAndRemoveForwardChildAndIndex)
{
	auto *math = add_node<MathNode>();

	Folder *root = project_.root();
	auto *root_handle = reinterpret_cast<OakEngineNode *>(root);
	auto *child_handle = reinterpret_cast<OakEngineNode *>(math);

	EngineEventBridge bridge;
	EXPECT_GT(bridge.subscribe(root_handle,
							   OAKENGINE_EVENT_FOLDER_BEGIN_INSERT_ITEM),
			  0);
	EXPECT_GT(bridge.subscribe(root_handle,
							   OAKENGINE_EVENT_FOLDER_END_INSERT_ITEM),
			  0);
	EXPECT_GT(bridge.subscribe(root_handle,
							   OAKENGINE_EVENT_FOLDER_BEGIN_REMOVE_ITEM),
			  0);
	EXPECT_GT(bridge.subscribe(root_handle,
							   OAKENGINE_EVENT_FOLDER_END_REMOVE_ITEM),
			  0);

	QSignalSpy begin_insert(&bridge,
							&EngineEventBridge::folder_begin_insert_item);
	QSignalSpy end_insert(&bridge, &EngineEventBridge::folder_end_insert_item);
	QSignalSpy begin_remove(&bridge,
							&EngineEventBridge::folder_begin_remove_item);
	QSignalSpy end_remove(&bridge, &EngineEventBridge::folder_end_remove_item);

	// The insert index is always the current child count (append-only)
	const int insert_index = root->item_child_count();

	FolderAddChild add(root, math);
	add.redo_now();

	ASSERT_EQ(begin_insert.count(), 1);
	EXPECT_EQ(begin_insert.first().at(0).value<OakEngineNode *>(), root_handle);
	EXPECT_EQ(begin_insert.first().at(1).value<OakEngineNode *>(),
			  child_handle);
	EXPECT_EQ(begin_insert.first().at(2).toInt(), insert_index);
	ASSERT_EQ(end_insert.count(), 1);
	EXPECT_EQ(end_insert.first().first().value<OakEngineNode *>(), root_handle);

	// Undoing the add runs through Folder::InputDisconnectedEvent
	add.undo_now();

	ASSERT_EQ(begin_remove.count(), 1);
	EXPECT_EQ(begin_remove.first().at(0).value<OakEngineNode *>(), root_handle);
	EXPECT_EQ(begin_remove.first().at(1).value<OakEngineNode *>(),
			  child_handle);
	EXPECT_EQ(begin_remove.first().at(2).toInt(), insert_index);
	ASSERT_EQ(end_remove.count(), 1);
	EXPECT_EQ(end_remove.first().first().value<OakEngineNode *>(), root_handle);
}

TEST_F(EngineEventBridgeTest, ProjectNameAndModifiedForward)
{
	auto *project_handle = reinterpret_cast<OakEngineProject *>(&project_);

	EngineEventBridge bridge;
	EXPECT_GT(bridge.subscribe(project_handle,
							   OAKENGINE_EVENT_PROJECT_NAME_CHANGED),
			  0);
	EXPECT_GT(bridge.subscribe(project_handle,
							   OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED),
			  0);

	QSignalSpy name_spy(&bridge, &EngineEventBridge::project_name_changed);
	QSignalSpy modified_spy(&bridge,
							&EngineEventBridge::project_modified_changed);

	project_.set_filename(QStringLiteral("/tmp/bridge_test_project.ove"));
	EXPECT_EQ(name_spy.count(), 1);

	project_.set_modified(true);
	ASSERT_EQ(modified_spy.count(), 1);
	EXPECT_TRUE(modified_spy.first().first().toBool());

	project_.set_modified(false);
	ASSERT_EQ(modified_spy.count(), 2);
	EXPECT_FALSE(modified_spy.at(1).first().toBool());
}

TEST_F(EngineEventBridgeTest, ViewerPlayheadForwardsRationalPayload)
{
	auto *sequence = add_node<Sequence>();
	auto *sequence_handle = reinterpret_cast<OakEngineNode *>(sequence);

	EngineEventBridge bridge;
	ASSERT_GT(bridge.subscribe(sequence_handle,
							   OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED),
			  0);

	QSignalSpy spy(&bridge, &EngineEventBridge::viewer_playhead_changed);

	// Rational payloads are delivered as num/den pairs
	ASSERT_EQ(oakengine_viewer_set_playhead(sequence_handle, 2, 1),
			  OAKENGINE_OK);
	ASSERT_EQ(spy.count(), 1);
	EXPECT_EQ(spy.first().at(0).value<OakEngineNode *>(), sequence_handle);
	EXPECT_EQ(spy.first().at(1).toLongLong(), 2);
	EXPECT_EQ(spy.first().at(2).toLongLong(), 1);
}

TEST_F(EngineEventBridgeTest, UnsubscribeStopsDelivery)
{
	auto *math = add_node<MathNode>();
	auto *math_handle = reinterpret_cast<OakEngineNode *>(math);

	EngineEventBridge bridge;
	const int64_t sub =
		bridge.subscribe(math_handle, OAKENGINE_EVENT_NODE_LABEL_CHANGED);
	ASSERT_GT(sub, 0);

	QSignalSpy spy(&bridge, &EngineEventBridge::node_label_changed);

	math->set_label(QStringLiteral("Before"));
	ASSERT_EQ(spy.count(), 1);

	bridge.unsubscribe(sub);
	math->set_label(QStringLiteral("After"));
	EXPECT_EQ(spy.count(), 1);

	// Unknown or already-dead ids are ignored
	bridge.unsubscribe(sub);
	bridge.unsubscribe(0);
	bridge.unsubscribe(424242);
	math->set_label(QStringLiteral("Later"));
	EXPECT_EQ(spy.count(), 1);
}

TEST_F(EngineEventBridgeTest, UnsubscribeAllStopsEverySubscription)
{
	auto *math = add_node<MathNode>();
	auto *math_handle = reinterpret_cast<OakEngineNode *>(math);
	auto *project_handle = reinterpret_cast<OakEngineProject *>(&project_);

	EngineEventBridge bridge;
	ASSERT_GT(bridge.subscribe(math_handle, OAKENGINE_EVENT_NODE_LABEL_CHANGED),
			  0);
	ASSERT_GT(bridge.subscribe(project_handle,
							   OAKENGINE_EVENT_PROJECT_NAME_CHANGED),
			  0);

	QSignalSpy label_spy(&bridge, &EngineEventBridge::node_label_changed);
	QSignalSpy name_spy(&bridge, &EngineEventBridge::project_name_changed);

	bridge.unsubscribe_all();

	math->set_label(QStringLiteral("Muted"));
	project_.set_filename(QStringLiteral("/tmp/muted_project.ove"));
	EXPECT_EQ(label_spy.count(), 0);
	EXPECT_EQ(name_spy.count(), 0);
}

TEST_F(EngineEventBridgeTest, DestroyingBridgeDropsItsSubscriptions)
{
	auto *math = add_node<MathNode>();
	auto *math_handle = reinterpret_cast<OakEngineNode *>(math);

	// A second bridge proves live subscribers are still notified afterwards
	EngineEventBridge control;
	ASSERT_GT(control.subscribe(math_handle, OAKENGINE_EVENT_NODE_LABEL_CHANGED),
			  0);
	QSignalSpy control_spy(&control, &EngineEventBridge::node_label_changed);

	auto *doomed = new EngineEventBridge();
	ASSERT_GT(doomed->subscribe(math_handle, OAKENGINE_EVENT_NODE_LABEL_CHANGED),
			  0);
	delete doomed;

	// Must not dispatch into the freed bridge
	math->set_label(QStringLiteral("After Delete"));
	ASSERT_EQ(control_spy.count(), 1);
	EXPECT_EQ(control_spy.first().at(1).toString(),
			  QStringLiteral("After Delete"));
}

TEST_F(EngineEventBridgeTest, UndoIndexChangedFiresOnPushAndJump)
{
	ASSERT_NE(oakengine_undo_handle(), nullptr);

	EngineEventBridge bridge;
	ASSERT_GT(bridge.subscribe(oakengine_undo_handle(),
							   OAKENGINE_EVENT_UNDO_INDEX_CHANGED),
			  0);

	QSignalSpy spy(&bridge, &EngineEventBridge::undo_index_changed);

	const int64_t index_before = oakengine_undo_index();
	g_counting_redo_calls = 0;
	g_counting_undo_calls = 0;

	void *cmd = oakengine_undo_command_create("gtest bridge command",
											  counting_redo, counting_undo,
											  nullptr, nullptr);
	ASSERT_NE(cmd, nullptr);

	ASSERT_EQ(oakengine_undo_push(cmd, "gtest bridge command"), OAKENGINE_OK);
	EXPECT_EQ(g_counting_redo_calls, 1);
	EXPECT_EQ(oakengine_undo_index(), index_before + 1);
	ASSERT_GE(spy.count(), 1);
	EXPECT_EQ(spy.last().first().toInt(), int(index_before + 1));

	ASSERT_EQ(oakengine_undo_jump(index_before), OAKENGINE_OK);
	EXPECT_EQ(g_counting_undo_calls, 1);
	EXPECT_EQ(oakengine_undo_index(), index_before);
	EXPECT_EQ(spy.last().first().toInt(), int(index_before));
}

// --- MainWindow undo commands (window/mainwindow/mainwindowundo) ---

TEST(MainWindowUndo, CommandsAreDistinctAndFreeable)
{
	ColorManager::set_up_default_config();
	ensure_core();

	Project project;
	project.initialize();
	auto *sequence = new Sequence();
	sequence->setParent(&project);
	const oak::Node sequence_node(reinterpret_cast<OakEngineNode *>(sequence));

	const int64_t rows_before = oakengine_undo_count();

	void *open_cmd = make_open_sequence_command(sequence_node);
	void *close_cmd = make_close_sequence_command(sequence_node);
	ASSERT_NE(open_cmd, nullptr);
	ASSERT_NE(close_cmd, nullptr);
	EXPECT_NE(open_cmd, close_cmd);

	// Creation must not execute the commands or touch the undo stack
	EXPECT_EQ(oakengine_undo_count(), rows_before);

	oakengine_undo_command_free(open_cmd);
	oakengine_undo_command_free(close_cmd);
	EXPECT_EQ(oakengine_undo_count(), rows_before);
}

TEST(MainWindowUndo, RedoUndoDriveMainWindowSequenceState)
{
	SKIP_WITHOUT_MAIN_WINDOW();

	MainWindowEnvironment env;

	// The command callbacks reach the window through Core::main_window(),
	// which is only assigned by the private Core::start_gui(); a directly
	// constructed MainWindow does not register itself there, so the redo/undo
	// path cannot run in this harness.
	if (!Core::instance()->main_window()) {
		GTEST_SKIP() << "Core::main_window() is unset without Core::start_gui()";
	}

	Project project;
	project.initialize();
	auto *sequence = new Sequence();
	sequence->setParent(&project);
	const oak::Node sequence_node(reinterpret_cast<OakEngineNode *>(sequence));
	auto *sequence_handle = reinterpret_cast<OakEngineNode *>(sequence);

	MainWindow *window = Core::instance()->main_window();
	EXPECT_FALSE(window->is_sequence_open(sequence_handle));

	// Open command: redo opens, undo closes; the close command mirrors it
	void *open_cmd = make_open_sequence_command(sequence_node);
	ASSERT_NE(open_cmd, nullptr);
	ASSERT_EQ(oakengine_undo_command_redo_now(open_cmd), OAKENGINE_OK);
	EXPECT_TRUE(window->is_sequence_open(sequence_handle));
	ASSERT_EQ(oakengine_undo_command_undo_now(open_cmd), OAKENGINE_OK);
	EXPECT_FALSE(window->is_sequence_open(sequence_handle));
	oakengine_undo_command_free(open_cmd);

	ASSERT_NE(window->open_sequence(sequence_handle), nullptr);
	void *close_cmd = make_close_sequence_command(sequence_node);
	ASSERT_NE(close_cmd, nullptr);
	ASSERT_EQ(oakengine_undo_command_redo_now(close_cmd), OAKENGINE_OK);
	EXPECT_FALSE(window->is_sequence_open(sequence_handle));
	ASSERT_EQ(oakengine_undo_command_undo_now(close_cmd), OAKENGINE_OK);
	EXPECT_TRUE(window->is_sequence_open(sequence_handle));
	oakengine_undo_command_free(close_cmd);

	window->close_sequence(sequence_handle);
}

// --- ViewerPanelBase (panel/viewer/viewerbase) ---

TEST_F(ViewerPanelBaseTest, ConstructionWiresViewerWidget)
{
	ProbeViewerPanel panel(QStringLiteral("ViewerBaseConstructPanel"));
	EXPECT_EQ(panel.objectName(), QStringLiteral("ViewerBaseConstructPanel"));

	auto *vw = new ViewerWidget(&panel);
	panel.install_widget(vw);

	EXPECT_EQ(panel.get_viewer_widget(), vw);
	EXPECT_EQ(panel.get_time_based_widget(), vw);

	// play_internal() returns early without a connected viewer node, so none
	// of the playback commands may start playback on a fresh panel
	EXPECT_FALSE(vw->is_playing());

	panel.play_pause();
	EXPECT_EQ(panel.play_pause_count_, 1);
	EXPECT_FALSE(vw->is_playing());

	panel.play_in_to_out();
	EXPECT_EQ(panel.play_in_to_out_count_, 1);
	EXPECT_FALSE(vw->is_playing());

	panel.shuttle_left();
	EXPECT_EQ(panel.shuttle_left_count_, 1);
	panel.shuttle_right();
	EXPECT_EQ(panel.shuttle_right_count_, 1);
	panel.shuttle_stop();
	EXPECT_EQ(panel.shuttle_stop_count_, 1);
	EXPECT_FALSE(vw->is_playing());
}

TEST_F(ViewerPanelBaseTest, ForwardsViewerWidgetSignals)
{
	ProbeViewerPanel panel(QStringLiteral("ViewerBaseForwardPanel"));
	auto *vw = new ViewerWidget(&panel);
	panel.install_widget(vw);

	// Lambdas avoid relying on opaque-handle metatypes for QSignalSpy
	void *received_texture = nullptr;
	int texture_count = 0;
	QObject::connect(&panel, &ViewerPanelBase::texture_changed,
					 [&received_texture, &texture_count](void *t) {
						 received_texture = t;
						 texture_count++;
					 });

	OakEngineColorManager *received_manager =
		reinterpret_cast<OakEngineColorManager *>(quintptr(1));
	int manager_count = 0;
	QObject::connect(&panel, &ViewerPanelBase::color_manager_changed,
					 [&received_manager, &manager_count](OakEngineColorManager *m) {
						 received_manager = m;
						 manager_count++;
					 });

	emit vw->texture_changed(&panel);
	EXPECT_EQ(texture_count, 1);
	EXPECT_EQ(received_texture, static_cast<void *>(&panel));

	emit vw->color_manager_changed(nullptr);
	EXPECT_EQ(manager_count, 1);
	EXPECT_EQ(received_manager, nullptr);
}

TEST_F(ViewerPanelBaseTest, RoutesTimeBasedPanelPlaybackRequests)
{
	ProbeViewerPanel panel(QStringLiteral("ViewerBaseRoutingPanel"));
	panel.install_widget(new ViewerWidget(&panel));

	ProbeTimeBasedPanel source(QStringLiteral("ViewerBaseRoutingSource"));
	source.set_time_based_widget(new TimeBasedWidget(false, false, &source));

	panel.connect_time_based_panel(&source);

	// TimeBasedPanel actions become request signals the viewer panel hears
	source.play_pause();
	EXPECT_EQ(panel.play_pause_count_, 1);
	source.play_in_to_out();
	EXPECT_EQ(panel.play_in_to_out_count_, 1);
	source.shuttle_left();
	EXPECT_EQ(panel.shuttle_left_count_, 1);
	source.shuttle_stop();
	EXPECT_EQ(panel.shuttle_stop_count_, 1);
	source.shuttle_right();
	EXPECT_EQ(panel.shuttle_right_count_, 1);

	// After disconnecting, requests no longer reach the panel
	panel.disconnect_time_based_panel(&source);
	source.play_pause();
	source.shuttle_left();
	EXPECT_EQ(panel.play_pause_count_, 1);
	EXPECT_EQ(panel.shuttle_left_count_, 1);
}

TEST_F(ViewerPanelBaseTest, FocusChangeSignalsAreSafeWhenIdle)
{
	ProbeViewerPanel panel_a(QStringLiteral("ViewerBaseFocusPanelA"));
	panel_a.install_widget(new ViewerWidget(&panel_a));
	ProbeViewerPanel panel_b(QStringLiteral("ViewerBaseFocusPanelB"));
	panel_b.install_widget(new ViewerWidget(&panel_b));

	ProbeTimeBasedPanel non_viewer(QStringLiteral("ViewerBaseFocusOther"));
	non_viewer.set_time_based_widget(
		new TimeBasedWidget(false, false, &non_viewer));

	// Nothing is playing, so focus changes must leave both viewers untouched
	emit PanelManager::instance()->focused_panel_changed(&non_viewer);
	EXPECT_FALSE(panel_a.get_viewer_widget()->is_playing());

	emit PanelManager::instance()->focused_panel_changed(&panel_b);
	EXPECT_FALSE(panel_a.get_viewer_widget()->is_playing());
	EXPECT_FALSE(panel_b.get_viewer_widget()->is_playing());

	emit PanelManager::instance()->focused_panel_changed(nullptr);
	EXPECT_FALSE(panel_a.get_viewer_widget()->is_playing());
}

// --- FootageManagementPanel (panel/project/footagemanagementpanel) ---

TEST_F(FootageManagementPanelTest, InterfaceDispatchesToImplementation)
{
	Project project;
	project.initialize();
	auto *math = new MathNode();
	math->setParent(&project);

	FakeFootageManagementPanel fake;
	FootageManagementPanel *iface = &fake;

	EXPECT_TRUE(iface->get_selected_footage().isEmpty());

	fake.selection_.append(reinterpret_cast<OakEngineNode *>(math));
	fake.selection_.append(reinterpret_cast<OakEngineNode *>(project.root()));

	ASSERT_EQ(iface->get_selected_footage().size(), 2);
	EXPECT_EQ(iface->get_selected_footage().at(0),
			  reinterpret_cast<OakEngineNode *>(math));
	EXPECT_EQ(iface->get_selected_footage().at(1),
			  reinterpret_cast<OakEngineNode *>(project.root()));
}

TEST_F(FootageManagementPanelTest, FootageViewerDiscoverableThroughInterface)
{
	Project project;
	project.initialize();
	auto *viewer = new ViewerOutput();
	viewer->setParent(&project);
	auto *viewer_handle = reinterpret_cast<OakEngineNode *>(viewer);

	FootageViewerPanel panel;

	// The lookup MainMenu's insert/overwrite actions perform
	emit panel.shown(Qt::OtherFocusReason);
	FootageManagementPanel *found =
		PanelManager::instance()->most_recently_focused<FootageManagementPanel>();
	ASSERT_EQ(found, static_cast<FootageManagementPanel *>(&panel));

	EXPECT_TRUE(found->get_selected_footage().isEmpty());

	panel.connect_viewer_node(viewer_handle);
	ASSERT_EQ(found->get_selected_footage().size(), 1);
	EXPECT_EQ(found->get_selected_footage().first(), viewer_handle);

	panel.disconnect_viewer_node();
	EXPECT_TRUE(found->get_selected_footage().isEmpty());
}

// --- MainMenu (window/mainwindow/mainmenu) - requires a MainWindow ---

TEST(MainMenu, TopLevelMenusHaveExpectedTitles)
{
	SKIP_WITHOUT_MAIN_WINDOW();

	MainWindowEnvironment env;
	MainWindow window;

	auto *menu = qobject_cast<MainMenu *>(window.menuBar());
	ASSERT_NE(menu, nullptr);

	const QStringList expected = { QStringLiteral("&File"), QStringLiteral("&Edit"),
								   QStringLiteral("&View"), QStringLiteral("&Playback"),
								   QStringLiteral("&Sequence"), QStringLiteral("&Window"),
								   QStringLiteral("&Tools"), QStringLiteral("&Help") };

	ASSERT_EQ(window.menuBar()->actions().size(), expected.size());
	for (int i = 0; i < expected.size(); i++) {
		QMenu *m = window.menuBar()->actions().at(i)->menu();
		ASSERT_NE(m, nullptr) << i;
		EXPECT_EQ(m->title(), expected.at(i));
	}
}

TEST(MainMenu, EditMenuContainsUndoActionsAndAlternateDelete)
{
	SKIP_WITHOUT_MAIN_WINDOW();

	MainWindowEnvironment env;
	MainWindow window;

	QMenu *edit_menu =
		top_level_menu(window.menuBar(), QStringLiteral("&Edit"));
	ASSERT_NE(edit_menu, nullptr);

	// The undo/redo items are the engine undo stack's own QActions
	QAction *undo_item = find_item_by_id(edit_menu, QStringLiteral("undo"));
	QAction *redo_item = find_item_by_id(edit_menu, QStringLiteral("redo"));
	ASSERT_NE(undo_item, nullptr);
	ASSERT_NE(redo_item, nullptr);
	EXPECT_EQ(undo_item,
			  reinterpret_cast<QAction *>(oakengine_undo_undo_action()));
	EXPECT_EQ(redo_item,
			  reinterpret_cast<QAction *>(oakengine_undo_redo_action()));

	// The alternate delete item hides while the menu is open
	QAction *delete2 = find_item_by_id(edit_menu, QStringLiteral("delete2"));
	ASSERT_NE(delete2, nullptr);
	EXPECT_TRUE(delete2->isVisible());

	emit edit_menu->aboutToShow();
	EXPECT_FALSE(delete2->isVisible());

	emit edit_menu->aboutToHide();
	EXPECT_TRUE(delete2->isVisible());
}

TEST(MainMenu, ToolItemsDriveCoreToolState)
{
	SKIP_WITHOUT_MAIN_WINDOW();

	MainWindowEnvironment env;
	MainWindow window;

	QMenu *tools_menu =
		top_level_menu(window.menuBar(), QStringLiteral("&Tools"));
	ASSERT_NE(tools_menu, nullptr);

	QAction *razor = find_item_by_id(tools_menu, QStringLiteral("razortool"));
	QAction *hand = find_item_by_id(tools_menu, QStringLiteral("handtool"));
	ASSERT_NE(razor, nullptr);
	ASSERT_NE(hand, nullptr);

	Core::instance()->set_tool(Tool::k_pointer);
	razor->trigger();
	EXPECT_EQ(Core::instance()->tool(), Tool::k_razor);

	// aboutToShow re-checks whichever tool Core currently holds
	Core::instance()->set_tool(Tool::k_hand);
	emit tools_menu->aboutToShow();
	EXPECT_TRUE(hand->isChecked());
	EXPECT_FALSE(razor->isChecked());

	Core::instance()->set_tool(Tool::k_pointer);
}

TEST(MainMenu, SnappingAndLoopItemsPersistState)
{
	SKIP_WITHOUT_MAIN_WINDOW();

	MainWindowEnvironment env;
	MainWindow window;

	QMenu *tools_menu =
		top_level_menu(window.menuBar(), QStringLiteral("&Tools"));
	ASSERT_NE(tools_menu, nullptr);
	QAction *snapping = find_item_by_id(tools_menu, QStringLiteral("snapping"));
	ASSERT_NE(snapping, nullptr);
	ASSERT_TRUE(snapping->isCheckable());

	// Connected straight to Core::set_snapping
	const bool snapping_before = snapping->isChecked();
	snapping->trigger();
	EXPECT_EQ(Core::instance()->snapping(), !snapping_before);
	snapping->trigger();
	EXPECT_EQ(Core::instance()->snapping(), snapping_before);

	QMenu *playback_menu =
		top_level_menu(window.menuBar(), QStringLiteral("&Playback"));
	ASSERT_NE(playback_menu, nullptr);
	QAction *loop = find_item_by_id(playback_menu, QStringLiteral("loop"));
	ASSERT_NE(loop, nullptr);
	ASSERT_TRUE(loop->isCheckable());

	// aboutToShow syncs the check state from the config
	const QVariant old_loop = Config::current()[QStringLiteral("Loop")];
	Config::current()[QStringLiteral("Loop")] = false;
	emit playback_menu->aboutToShow();
	EXPECT_FALSE(loop->isChecked());

	loop->trigger();
	EXPECT_TRUE(Config::current()[QStringLiteral("Loop")].toBool());
	loop->trigger();
	EXPECT_FALSE(Config::current()[QStringLiteral("Loop")].toBool());

	Config::current()[QStringLiteral("Loop")] = old_loop;
}

TEST(MainMenu, FileMenuReflectsActiveProjectAndRecentList)
{
	SKIP_WITHOUT_MAIN_WINDOW();

	MainWindowEnvironment env;
	MainWindow window;

	QMenu *file_menu =
		top_level_menu(window.menuBar(), QStringLiteral("&File"));
	ASSERT_NE(file_menu, nullptr);

	QAction *save = find_item_by_id(file_menu, QStringLiteral("saveproj"));
	QAction *save_as = find_item_by_id(file_menu, QStringLiteral("saveprojas"));
	ASSERT_NE(save, nullptr);
	ASSERT_NE(save_as, nullptr);

	emit file_menu->aboutToShow();

	// Save items are enabled only with an active project
	const bool has_active_project =
		Core::instance()->get_active_project() != nullptr;
	EXPECT_EQ(save->isEnabled(), has_active_project);
	EXPECT_EQ(save_as->isEnabled(), has_active_project);
	if (!has_active_project) {
		EXPECT_EQ(save->text(), QStringLiteral("&Save Project"));
		EXPECT_EQ(save_as->text(), QStringLiteral("Save Project &As"));
	}

	// With no recent projects the submenu shows a disabled placeholder
	QMenu *open_recent = nullptr;
	foreach (QAction *a, file_menu->actions()) {
		if (a->menu() &&
			a->menu()->title() == QStringLiteral("Open &Recent")) {
			open_recent = a->menu();
			break;
		}
	}
	ASSERT_NE(open_recent, nullptr);

	if (Core::instance()->get_recent_project_count() == 0) {
		ASSERT_FALSE(open_recent->actions().isEmpty());
		QAction *placeholder = open_recent->actions().first();
		EXPECT_EQ(placeholder->text(), QStringLiteral("(None)"));
		EXPECT_FALSE(placeholder->isEnabled());
	} else {
		const int count = Core::instance()->get_recent_project_count();
		int entries = 0;
		foreach (QAction *a, open_recent->actions()) {
			if (a->data().isValid()) {
				entries++;
			}
		}
		EXPECT_EQ(entries, count);
	}
}
