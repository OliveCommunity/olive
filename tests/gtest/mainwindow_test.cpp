#include <gtest/gtest.h>

#include <QProgressBar>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSignalSpy>
#include <QTest>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "audio/audiomanager.h"
#include "config/config.h"
#include "node/color/colormanager/colormanager.h"
#include "core.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/project/folder/folder.h"
#include "node/project/sequence/sequence.h"
#include "panel/panelmanager.h"
#include "render/diskmanager.h"
#include "render/rendermanager.h"
#include "task/task.h"
#include "task/taskmanager.h"
#include "oakengine/task.h"
#include "engineeventbridge.h"
#include "widget/menu/menushared.h"
#include "window/mainwindow/mainstatusbar.h"
#include "window/mainwindow/mainwindow.h"
#include "node/project/serializer/serializedlayoutinfo.h"

using namespace olive;

namespace
{

class DummyTask : public Task {
public:
	DummyTask()
	{
		set_title(QStringLiteral("Status Test Task"));
	}

protected:
	virtual bool run() override
	{
		return true;
	}
};

} // namespace

TEST(SerializedLayoutInfo, AccessorsStoreAndRetrieve)
{
	Project project;
	project.initialize();
	auto *folder = new Folder();
	folder->setParent(&project);
	auto *sequence = new Sequence();
	sequence->setParent(&project);
	auto *viewer = new ViewerOutput();
	viewer->setParent(&project);

	SerializedLayoutInfo info;
	EXPECT_TRUE(info.open_folders.empty());
	EXPECT_TRUE(info.open_sequences.empty());
	EXPECT_TRUE(info.open_viewers.empty());
	EXPECT_TRUE(info.panel_data.empty());
	EXPECT_TRUE(info.state.isEmpty());

	info.open_folders.push_back(folder);
	info.open_sequences.push_back(sequence);
	info.open_viewers.push_back(viewer);

	ASSERT_EQ(info.open_folders.size(), 1);
	EXPECT_EQ(info.open_folders.front(), folder);
	ASSERT_EQ(info.open_sequences.size(), 1);
	EXPECT_EQ(info.open_sequences.front(), sequence);
	ASSERT_EQ(info.open_viewers.size(), 1);
	EXPECT_EQ(info.open_viewers.front(), viewer);

	PanelWidget::Info data;
	data[QStringLiteral("key")] = QStringLiteral("value");
	info.panel_data[QStringLiteral("panel_a")] = data;
	ASSERT_EQ(info.panel_data.size(), 1);
	EXPECT_EQ(info.panel_data
				  .at(QStringLiteral("panel_a"))
				  .at(QStringLiteral("key")),
			  QStringLiteral("value"));

	// renaming the entry moves the data
	info.panel_data[QStringLiteral("panel_b")] =
		info.panel_data.at(QStringLiteral("panel_a"));
	info.panel_data.erase(QStringLiteral("panel_a"));
	EXPECT_EQ(info.panel_data.count(QStringLiteral("panel_a")), 0);
	ASSERT_EQ(info.panel_data.count(QStringLiteral("panel_b")), 1);
	EXPECT_EQ(info.panel_data
				  .at(QStringLiteral("panel_b"))
				  .at(QStringLiteral("key")),
			  QStringLiteral("value"));

	info.state = QByteArray("layout-state");
	EXPECT_EQ(info.state, QByteArray("layout-state"));
}

TEST(SerializedLayoutInfo, XmlRoundTripPreservesEverything)
{
	Project project;
	project.initialize();
	auto *folder = new Folder();
	folder->setParent(&project);
	auto *sequence = new Sequence();
	sequence->setParent(&project);
	auto *viewer = new ViewerOutput();
	viewer->setParent(&project);

	SerializedLayoutInfo info;
	info.open_folders.push_back(folder);
	info.open_sequences.push_back(sequence);
	info.open_viewers.push_back(viewer);
	PanelWidget::Info data;
	data[QStringLiteral("splitter")] = QStringLiteral("AAA=");
	info.panel_data[QStringLiteral("TimelinePanel")] = data;
	info.state = QByteArray("binary\x01\x02state", 12);

	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("layout"));
	info.to_xml(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();

	QHash<quintptr, Node *> node_map;
	node_map.insert(reinterpret_cast<quintptr>(folder), folder);
	node_map.insert(reinterpret_cast<quintptr>(sequence), sequence);
	node_map.insert(reinterpret_cast<quintptr>(viewer), viewer);

	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name(), QStringLiteral("layout"));

	SerializedLayoutInfo loaded = SerializedLayoutInfo::from_xml(&reader, node_map);

	ASSERT_EQ(loaded.open_folders.size(), 1);
	EXPECT_EQ(loaded.open_folders.front(), folder);
	ASSERT_EQ(loaded.open_sequences.size(), 1);
	EXPECT_EQ(loaded.open_sequences.front(), sequence);

	// Open viewers must survive the round trip too
	ASSERT_EQ(loaded.open_viewers.size(), 1);
	EXPECT_EQ(loaded.open_viewers.front(), viewer);

	EXPECT_EQ(loaded.state, info.state);

	ASSERT_EQ(loaded.panel_data.count(QStringLiteral("TimelinePanel")), 1);
	EXPECT_EQ(loaded.panel_data
				  .at(QStringLiteral("TimelinePanel"))
				  .at(QStringLiteral("splitter")),
			  QStringLiteral("AAA="));

	// No unknown nodes leak into the viewers list: it must contain exactly the
	// viewer that was added, not a duplicate of the sequences list
	EXPECT_NE(loaded.open_viewers.front(),
			  static_cast<ViewerOutput *>(sequence));
}

TEST(SerializedLayoutInfo, FromXmlSkipsUnknownElementsAndNodes)
{
	const QString xml = QStringLiteral(
		"<layout version=\"1\">"
		"<folders><folder>1</folder><unknown><nested/></unknown></folders>"
		"<timeline><sequence>2</sequence></timeline>"
		"<viewers><viewer>3</viewer></viewers>"
		"<mystery><deep><deeper/></deep></mystery>"
		"<state>QUJD</state>"
		"</layout>");

	// No node map entries: pointers resolve to nullptr
	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name(), QStringLiteral("layout"));

	SerializedLayoutInfo info = SerializedLayoutInfo::from_xml(&reader, {});

	// Unknown pointers resolve to null but are still listed
	ASSERT_EQ(info.open_folders.size(), 1);
	EXPECT_EQ(info.open_folders.front(), nullptr);
	ASSERT_EQ(info.open_sequences.size(), 1);
	EXPECT_EQ(info.open_sequences.front(), nullptr);
	ASSERT_EQ(info.open_viewers.size(), 1);
	EXPECT_EQ(info.open_viewers.front(), nullptr);
	EXPECT_EQ(info.state, QByteArray("ABC"));
	EXPECT_TRUE(info.panel_data.empty());
}

TEST(MainWindowStatusBar, ConstructionDefaults)
{
	MainStatusBar bar;
	bar.show();

	auto *progress = bar.findChild<QProgressBar *>();
	ASSERT_NE(progress, nullptr);
	EXPECT_FALSE(progress->isVisible());
}

TEST(MainWindowStatusBar, ReflectsTaskManagerState)
{
	// The status bar now uses the global TaskManager singleton via the facade.
	if (!TaskManager::instance()) {
		TaskManager::create_instance();
	}

	EngineEventBridge bridge;
	MainStatusBar bar;
	bar.show();
	auto *progress = bar.findChild<QProgressBar *>();
	ASSERT_NE(progress, nullptr);

	bar.connect_task_manager(&bridge);

	auto *task = new DummyTask();
	oakengine_task_manager_add(
		reinterpret_cast<OakEngineTask *>(task));

	// One running task shows its title and the progress bar
	EXPECT_EQ(bar.currentMessage(), QStringLiteral("Status Test Task"));
	EXPECT_TRUE(progress->isVisible());

	// Progress signals are forwarded to the bar through the facade event bridge
	emit task->progress_changed(0.5);
	QTRY_COMPARE_WITH_TIMEOUT(progress->value(), 50, 1000);

	// When the task list empties, the bar hides and the message clears
	oakengine_task_manager_cancel(
		reinterpret_cast<OakEngineTask *>(task));
	// Wait for the deferred delete on the task (facade cancel removes it)
	QTRY_COMPARE_WITH_TIMEOUT(oakengine_task_manager_count(), 0, 2000);
	EXPECT_TRUE(bar.currentMessage().isEmpty());
	EXPECT_FALSE(progress->isVisible());
	EXPECT_EQ(progress->value(), 0);

	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST(MainWindowStatusBar, DoubleClickEmitsSignal)
{
	MainStatusBar bar;
	bar.show();

	QSignalSpy spy(&bar, &MainStatusBar::double_clicked);
	QTest::mouseDClick(&bar, Qt::LeftButton);
	EXPECT_GE(spy.count(), 1);
}

// Evaluation of full MainWindow construction under the offscreen platform.
// Core::StartGUI() is private, so this test replicates its relevant steps:
// it creates the singletons MainWindow depends on (MenuShared, PanelManager,
// AudioManager, DiskManager, plus TaskManager for the status bar and
// RenderManager for the viewer panels' display widgets) and then instantiates
// MainWindow directly.
TEST(MainWindow, ConstructsOffscreenWithPanelsAndMenus)
{
	// MainWindow instantiates viewer panels containing QOpenGLWidget. On
	// platforms without a usable OpenGL implementation (e.g. headless CI
	// runners on the offscreen QPA), constructing it crashes in GL code
	// ("QOpenGLFunctions created with non-current context"). Probe first and
	// skip where GL is unavailable.
	QOffscreenSurface probe_surface;
	probe_surface.create();
	QOpenGLContext probe_context;
	const bool gl_available =
		probe_context.create() && probe_context.makeCurrent(&probe_surface);
	probe_context.doneCurrent();
	if (!gl_available) {
		GTEST_SKIP() << "OpenGL is not usable on this platform";
	}

	// Must precede RenderManager creation: PreviewAutoCacher constructs a
	// Project whose ColorManager dereferences the default OCIO config
	ColorManager::set_up_default_config();

	const bool created_task_manager = (TaskManager::instance() == nullptr);
	if (created_task_manager) {
		TaskManager::create_instance();
	}
	const bool created_render_manager = (RenderManager::instance() == nullptr);
	QVariant saved_backend;
	if (created_render_manager) {
		// Another suite may have left an experimental backend in the config;
		// RenderManager needs a real one to create its cacher
		saved_backend = Config::current()[QStringLiteral("GraphicsBackend")];
		Config::current()[QStringLiteral("GraphicsBackend")] =
			QStringLiteral("opengl");
		RenderManager::create_instance();
	}
	const bool created_disk_manager = (DiskManager::instance() == nullptr);
	if (created_disk_manager) {
		DiskManager::create_instance();
	}
	const bool created_menu_shared = (MenuShared::instance() == nullptr);
	if (created_menu_shared) {
		MenuShared::create_instance();
	}
	const bool created_panel_manager = (PanelManager::instance() == nullptr);
	if (created_panel_manager) {
		PanelManager::create_instance();
	}
	const bool created_audio_manager = (AudioManager::instance() == nullptr);
	if (created_audio_manager) {
		AudioManager::create_instance();
	}
	if (!Core::instance()) {
		new Core(); // intentionally leaked
	}
	KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);

	// Suppress the modal welcome dialog shown on first show
	const QVariant welcome_setting =
		Config::current()[QStringLiteral("ShowWelcomeDialog")];
	Config::current()[QStringLiteral("ShowWelcomeDialog")] = false;

	MainWindow *window = new MainWindow();
	window->showMaximized();

	// The standard panels were created and registered by name
	PanelManager *panels = PanelManager::instance();
	ASSERT_NE(panels, nullptr);
	EXPECT_GE(panels->panels().size(), 10);
	EXPECT_NE(panels->get_panel_with_name(QStringLiteral("NodePanel")), nullptr);
	EXPECT_NE(panels->get_panel_with_name(QStringLiteral("ProjectPanel")),
			  nullptr);
	// Timeline panels get their index appended to the unique name
	EXPECT_NE(panels->get_panel_with_name(QStringLiteral("TimelinePanel:0")),
			  nullptr);
	EXPECT_NE(panels->get_panel_with_name(QStringLiteral("SequenceViewerPanel")),
			  nullptr);
	EXPECT_NE(panels->get_panel_with_name(QStringLiteral("FootageViewerPanel")),
			  nullptr);

	// The menu bar is fully populated (this is what the action search dialog
	// indexes)
	QMenuBar *menu_bar = window->menuBar();
	ASSERT_NE(menu_bar, nullptr);
	int total_actions = 0;
	foreach (QAction *menu_action, menu_bar->actions()) {
		if (QMenu *menu = menu_action->menu()) {
			total_actions += menu->actions().size();
		}
	}
	EXPECT_GE(menu_bar->actions().size(), 5);
	EXPECT_GT(total_actions, 0);

	// Status bar and window title are set up
	EXPECT_NE(window->statusBar(), nullptr);
	EXPECT_FALSE(window->windowTitle().isEmpty());

	// The default layout restore is queued at construction; pumping the event
	// loop must not crash
	QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

	Config::current()[QStringLiteral("ShowWelcomeDialog")] = welcome_setting;

	// Tear down in reverse order: window first, then its panels, then only
	// the singletons this test created
	delete window;
	if (created_panel_manager) {
		PanelManager::instance()->delete_all_panels();
		PanelManager::destroy_instance();
	}
	if (created_audio_manager) {
		AudioManager::destroy_instance();
	}
	if (created_menu_shared) {
		MenuShared::destroy_instance();
	}
	if (created_render_manager) {
		RenderManager::destroy_instance();
		Config::current()[QStringLiteral("GraphicsBackend")] = saved_backend;
	}
	if (created_task_manager) {
		TaskManager::destroy_instance();
	}
	if (created_disk_manager) {
		DiskManager::destroy_instance();
	}
}
