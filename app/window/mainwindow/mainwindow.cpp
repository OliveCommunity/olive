/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QMessageBox>
#include <QScreen>

#ifdef Q_OS_LINUX
#include <QOffscreenSurface>
#endif

#include "KDDockWidgets/src/qtwidgets/Window_p.h"
#include "dialog/about/about.h"
#include "engineeventbridge.h"
#include "mainmenu.h"
#include "mainstatusbar.h"
#include "KDDockWidgets/src/LayoutSaver.h"
#include "oakengine/timeline.h"
#include "common/configwrapper.h"
#include "oakengine/project.h"
#include "oakengine/viewer.h"
#include "oakengine/undo.h"

#include "widget/viewer/vieweroutpututils.h"
#include "core.h"
namespace olive
{

#define super KDDockWidgets::QtWidgets::MainWindow

MainWindow::MainWindow(QWidget *parent)
	: super(QStringLiteral("OakMain"), KDDockWidgets::MainWindowOption_None,
			parent)
	, project_(nullptr)
{
	bridge_ = new EngineEventBridge(this);
	connect(bridge_, &EngineEventBridge::node_removed_from_graph, this,
			&MainWindow::viewer_with_panel_removed_from_graph);
	// Resizes main window to desktop geometry on startup. Fixes the following issues:
	// * Qt on Windows has a bug that "de-maximizes" the window when widgets are added, resizing the
	//   window beforehand works around that issue and we just set it to whatever size is available.
	// * On Linux, it seems the window starts off at a vastly different size and then maximizes
	//   which throws off the proportions and makes the resulting layout wonky.
	if (!qApp->screens().empty()) {
		resize(qApp->screens().at(0)->availableSize());
	}

#ifdef Q_OS_WINDOWS
	// Set up taskbar button progress bar (used for some modal tasks like exporting)
	taskbar_btn_id_ = RegisterWindowMessage(TEXT("TaskbarButtonCreated"));
	taskbar_interface_ = nullptr;
#endif

	first_show_ = true;

	// Create and set main menu
	MainMenu *main_menu = new MainMenu(this);
	setMenuBar(main_menu);

	load_custom_shortcuts();

	// Create and set status bar
	event_bridge_ = new EngineEventBridge(this);
	status_bar_ = new MainStatusBar(this);
	status_bar_->connect_task_manager(event_bridge_);
	connect(status_bar_, &MainStatusBar::double_clicked, this,
			&MainWindow::status_bar_double_clicked);
	setStatusBar(status_bar_);

	// Create standard panels
	node_panel_ = new NodePanel();
	footage_viewer_panel_ = new FootageViewerPanel();
	param_panel_ = new ParamPanel();
	curve_panel_ = new CurvePanel();
	sequence_viewer_panel_ = new SequenceViewerPanel();
	multicam_panel_ = new MulticamPanel();
	pixel_sampler_panel_ = new PixelSamplerPanel();
	project_panel_ = new ProjectPanel(QStringLiteral("ProjectPanel"));
	tool_panel_ = new ToolPanel();
	task_man_panel_ = new TaskManagerPanel();
	append_timeline_panel();
	audio_monitor_panel_ = new AudioMonitorPanel();
	scope_panel_ = new ScopePanel();
	history_panel_ = new HistoryPanel();

	// HACK: The pixel sampler is closed by default, which signals to Core that
	//       it's no longer visible. However KDDockWidgets doesn't appear to
	//       emit the "shown" signal before emitting the "hidden" signals, resulting
	//       in Core thinking there are -1 pixel samplers open. To mitigate that,
	//       we force "shown" to emit ourselves here.
	emit pixel_sampler_panel_->shown(Qt::OtherFocusReason);

	// Make node-related connections
	connect(node_panel_, &NodePanel::node_selection_changed_with_contexts,
			param_panel_, &ParamPanel::set_selected_nodes);
	connect(node_panel_, &NodePanel::node_group_opened, this,
			&MainWindow::node_panel_group_opened_or_closed);
	connect(node_panel_, &NodePanel::node_group_closed, this,
			&MainWindow::node_panel_group_opened_or_closed);
	connect(param_panel_, &ParamPanel::focused_node_changed,
			sequence_viewer_panel_, &ViewerPanel::set_gizmos);
	connect(param_panel_, &ParamPanel::request_viewer_to_start_editing_text,
			sequence_viewer_panel_, &ViewerPanel::request_start_editing_text);
	connect(param_panel_, &ParamPanel::focused_node_changed, curve_panel_,
			&CurvePanel::set_node);
	connect(param_panel_, &ParamPanel::selected_nodes_changed, node_panel_,
			&NodePanel::select);
	connect(project_panel_, &ProjectPanel::project_name_changed, this,
			&MainWindow::update_title);

	connect(node_panel_, &NodePanel::node_selection_changed,
			sequence_viewer_panel_, &ViewerPanel::set_node_view_selections);

	// Route play/pause/shuttle commands from these panels to the sequence viewer
	sequence_viewer_panel_->connect_time_based_panel(param_panel_);
	sequence_viewer_panel_->connect_time_based_panel(curve_panel_);
	sequence_viewer_panel_->connect_time_based_panel(multicam_panel_);

	connect(PanelManager::instance(), &PanelManager::focused_panel_changed, this,
			&MainWindow::focused_panel_changed);

	sequence_viewer_panel_->add_playback_device(
		multicam_panel_->get_multicam_widget()->get_display_widget());
	sequence_viewer_panel_->connect_multicam_widget(
		multicam_panel_->get_multicam_widget());

	scope_panel_->set_viewer_panel(sequence_viewer_panel_);

	update_title();

	QMetaObject::invokeMethod(this, &MainWindow::set_default_layout,
							  Qt::QueuedConnection);
}

MainWindow::~MainWindow()
{
#ifdef Q_OS_WINDOWS
	if (taskbar_interface_) {
		taskbar_interface_->Release();
	}
#endif
}

void MainWindow::load_layout(const SerializedLayoutInfo &info)
{
	for (OakEngineNode *folder : info.open_folders) {
		open_folder(folder, true);
	}

	for (OakEngineNode *sequence : info.open_sequences) {
		open_sequence(sequence, info.open_sequences.size() == 1);
	}

	for (OakEngineNode *viewer : info.open_viewers) {
		open_node_in_viewer(viewer);
	}

	for (auto it = info.panel_data.cbegin(); it != info.panel_data.cend();
		 it++) {
		// Find panel with this ID
		if (PanelWidget *panel =
				PanelManager::instance()->get_panel_with_name(it->first)) {
			panel->load_data(it->second);
		}
	}

	KDDockWidgets::LayoutSaver().restoreLayout(qUncompress(info.state));
}

QString transform_name_for_serialization(const QString &unique, int i)
{
	return QStringLiteral("%1:%2").arg(unique.split(':').at(0),
									   QString::number(i));
}

void correct_panel_data_if_necessary(const QString &unique_name, int index,
								 SerializedLayoutInfo &info, QByteArray &layout)
{
	QString corrected = transform_name_for_serialization(unique_name, index);
	if (corrected != unique_name) {
		info.panel_data[corrected] = info.panel_data[unique_name];
	info.panel_data.erase(unique_name);
		layout.replace(unique_name.toUtf8(), corrected.toUtf8());
	}
}

SerializedLayoutInfo MainWindow::save_layout() const
{
	SerializedLayoutInfo info;

	QByteArray layout = premaximized_state_.isEmpty() ?
							KDDockWidgets::LayoutSaver().serializeLayout() :
							premaximized_state_;

	foreach (PanelWidget *panel, PanelManager::instance()->panels()) {
		info.panel_data[panel->uniqueName()] = panel->save_data();
	}

	for (int i = 0; i < folder_panels_.size(); i++) {
		auto panel = folder_panels_.at(i);
		info.open_folders.push_back(panel->get_root().handle());
		correct_panel_data_if_necessary(panel->uniqueName(), i, info, layout);
	}

	for (int i = 0; i < timeline_panels_.size(); i++) {
		auto panel = timeline_panels_.at(i);
		info.open_sequences.push_back(
			reinterpret_cast<OakEngineNode *>(panel->get_sequence()));
		correct_panel_data_if_necessary(panel->uniqueName(), i, info, layout);
	}

	for (int i = 0; i < viewer_panels_.size(); i++) {
		auto panel = viewer_panels_.at(i);
		info.open_viewers.push_back(
			reinterpret_cast<OakEngineNode *>(panel->get_connected_viewer()));
		correct_panel_data_if_necessary(panel->uniqueName(), i, info, layout);
	}

	info.state = qCompress(layout);

	return info;
}

TimelinePanel *MainWindow::open_sequence(OakEngineNode *sequence, bool enable_focus)
{
	// See if this sequence is already open, and switch to it if so
	foreach (TimelinePanel *tl, timeline_panels_) {
		if (reinterpret_cast<OakEngineNode *>(tl->get_connected_viewer()) == sequence) {
			tl->raise();
			return tl;
		}
	}

	// See if we have any sequences open or not
	TimelinePanel *panel;

	if (!timeline_panels_.first()->get_connected_viewer()) {
		panel = timeline_panels_.first();
	} else {
		panel = append_timeline_panel();
		//enable_focus = false;
	}

	panel->connect_viewer_node(sequence);

	if (enable_focus) {
		timeline_focused(sequence);
		update_audio_monitor_params(sequence);
	}

	return panel;
}

void MainWindow::close_sequence(OakEngineNode *sequence)
{
	// We defer to RemoveTimelinePanel() to close the panels, which may delete and remove indices from timeline_panels_.
	// We make a copy so that our array here doesn't get ruined by what RemoveTimelinePanel() does
	QList<TimelinePanel *> copy = timeline_panels_;

	foreach (TimelinePanel *tp, copy) {
		if (reinterpret_cast<OakEngineNode *>(tp->get_connected_viewer()) == sequence) {
			remove_timeline_panel(tp);
		}
	}
}

bool MainWindow::is_sequence_open(OakEngineNode *sequence) const
{
	foreach (TimelinePanel *tp, timeline_panels_) {
		if (reinterpret_cast<OakEngineNode *>(tp->get_connected_viewer()) == sequence) {
			return true;
		}
	}

	return false;
}

void MainWindow::open_folder(OakEngineNode *i, bool floating)
{
	ProjectPanel *panel =
		append_panel_internal(QStringLiteral("FolderPanel"), folder_panels_);

	oak::Node folder(i);
	panel->set_project(folder.project());
	panel->set_root(folder);

	if (floating) {
		panel->setFloating(floating);
	} else {
		project_panel_->addDockWidgetAsTab(panel);
	}

	// If the panel is closed, just destroy it
	connect(panel, &ProjectPanel::close_requested, this,
			&MainWindow::folder_panel_close_requested);
}

void MainWindow::open_node_in_viewer(OakEngineNode *node)
{
	// Sequences already have the dedicated Sequence Viewer. Opening a
	// floating Viewer on a Sequence just duplicates it and looks like the
	// two viewers' controls are swapped. This also filters out stale
	// <viewer> entries pointing at sequences in saved project layouts.
	if (!node || oak::Node(node).is_sequence()) {
		return;
	}

	ViewerPanel *existing = nullptr;

	for (auto it = viewer_panels_.cbegin(); it != viewer_panels_.cend(); it++) {
		ViewerPanel *it2 = (*it);
		if (reinterpret_cast<OakEngineNode *>(it2->get_connected_viewer()) == node) {
			existing = it2;
			break;
		}
	}

	if (existing) {
		// This node already has a viewer, raise it
		existing->raise();
	} else {
		// Create a viewer for this node
		ViewerPanel *viewer =
			append_panel_internal(QStringLiteral("ViewerPanel"), viewer_panels_);

		viewer->connect_viewer_node(node);

		connect(viewer, &ViewerPanel::close_requested, this,
				&MainWindow::viewer_close_requested);
		auto sub = bridge_->subscribe(
			node,
			OAKENGINE_EVENT_NODE_REMOVED_FROM_GRAPH);
		removed_from_graph_subs_[node] = sub;
	}
}

void MainWindow::set_fullscreen(bool fullscreen)
{
	if (fullscreen) {
		setWindowState(windowState() | Qt::WindowFullScreen);
	} else {
		setWindowState(windowState() & ~Qt::WindowFullScreen);
	}
}

void MainWindow::toggle_maximized_panel()
{
	KDDockWidgets::LayoutSaver saver;

	if (premaximized_state_.isEmpty()) {
		// Assume nothing is maximized at the moment

		// Find the currently focused panel
		PanelWidget *currently_hovered =
			PanelManager::instance()->currently_hovered();

		// If no panel is hovered, fallback to the currently active panel
		if (!currently_hovered) {
			currently_hovered = PanelManager::instance()->currently_focused();

			// If no panel is hovered or focused, do nothing
			if (!currently_hovered) {
				return;
			}
		}

		// If this panel is not actually on the main window, this is a no-op
		if (currently_hovered->isFloating()) {
			return;
		}

		// Save the current state so it can be restored later
		premaximized_state_ = saver.serializeLayout();

		// For every other panel that is on the main window, hide it
		foreach (PanelWidget *panel, PanelManager::instance()->panels()) {
			if (!panel->isFloating() && panel != currently_hovered) {
				panel->close();
			}
		}
	} else {
		// Preserve currently focused panel
		auto currently_focused_panel =
			PanelManager::instance()->currently_focused(false);

		// Assume we are currently maximized, restore the state
		PanelManager::instance()->set_suppress_changed_signal(true);
		saver.restoreLayout(premaximized_state_);
		premaximized_state_.clear();

		currently_focused_panel->raise();
		currently_focused_panel->setFocus(Qt::ActiveWindowFocusReason);

		PanelManager::instance()->set_suppress_changed_signal(false);
	}
}

void MainWindow::set_project(OakEngineProject *p)
{
	if (project_ == p) {
		return;
	}

	if (project_) {
		// Clear all data
		param_panel_->set_contexts(QVector<oak::Node>());
		node_panel_->set_contexts(QVector<oak::Node>());

		// Close any nodes open in TimeBasedWidgets
		foreach (PanelWidget *panel, PanelManager::instance()->panels()) {
			TimeBasedPanel *tbp = dynamic_cast<TimeBasedPanel *>(panel);

			if (tbp && tbp->get_connected_viewer() &&
				oak::Node(reinterpret_cast<OakEngineNode *>(
							  tbp->get_connected_viewer()))
						.project()
						.handle() == project_) {
				if (dynamic_cast<TimelinePanel *>(tbp)) {
					// Prefer our CloseSequence function which will delete any unnecessary timeline panels
					close_sequence(
						reinterpret_cast<OakEngineNode *>(tbp->get_connected_viewer()));
				} else {
					tbp->disconnect_viewer_node();
				}
			}
		}

		// Close any extra folder panels
		foreach (ProjectPanel *panel, folder_panels_) {
			panel->close();
		}

		// Close any extra viewer panels
		foreach (ViewerPanel *viewer, viewer_panels_) {
			viewer->close();
		}
	}

	project_ = p;
	project_panel_->set_project(oak::Project(p));

	if (project_) {
		project_panel_->setFocus(Qt::OtherFocusReason);
	}
}

void MainWindow::set_application_progress_status(ProgressStatus status)
{
#if defined(Q_OS_WINDOWS)
	if (taskbar_interface_) {
		switch (status) {
		case k_progress_show:
			taskbar_interface_->SetProgressState(
				reinterpret_cast<HWND>(this->winId()), TBPF_NORMAL);
			break;
		case k_progress_none:
			taskbar_interface_->SetProgressState(
				reinterpret_cast<HWND>(this->winId()), TBPF_NOPROGRESS);
			break;
		case k_progress_error:
			taskbar_interface_->SetProgressState(
				reinterpret_cast<HWND>(this->winId()), TBPF_ERROR);
			break;
		}
	}

#elif defined(Q_OS_MAC)
#endif
}

void MainWindow::set_application_progress_value(int value)
{
#if defined(Q_OS_WINDOWS)
	if (taskbar_interface_) {
		taskbar_interface_->SetProgressValue(
			reinterpret_cast<HWND>(this->winId()), value, 100);
	}
#elif defined(Q_OS_MAC)
#endif
}

void MainWindow::select_footage(const QVector<OakEngineNode *> &e)
{
	select_footage_for_project_panel(e, project_panel_);
	for (ProjectPanel *p : folder_panels_) {
		select_footage_for_project_panel(e, p);
	}
}

void MainWindow::closeEvent(QCloseEvent *e)
{
	// Try to close all projects (this will return false if the user chooses not to close)
	if (!Core::instance()->close_project(false)) {
		e->ignore();
		return;
	}

	scope_panel_->set_viewer_panel(nullptr);

	PanelManager::instance()->delete_all_panels();

	save_custom_shortcuts();

	QMainWindow::closeEvent(e);
}

#ifdef Q_OS_WINDOWS
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message,
							 qintptr *result)
#else
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message,
							 long *result)
#endif
{
	if (static_cast<MSG *>(message)->message == taskbar_btn_id_) {
		// Attempt to create taskbar button progress handle
		HRESULT hr = CoCreateInstance(
			CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, IID_ITaskbarList3,
			reinterpret_cast<void **>(&taskbar_interface_));

		if (SUCCEEDED(hr)) {
			hr = taskbar_interface_->HrInit();

			if (FAILED(hr)) {
				taskbar_interface_->Release();
				taskbar_interface_ = nullptr;
			}
		}
	}

	return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::status_bar_double_clicked()
{
	task_man_panel_->show();
	task_man_panel_->raise();
}

void MainWindow::node_panel_group_opened_or_closed()
{
	NodePanel *p = static_cast<NodePanel *>(sender());
	param_panel_->set_contexts(p->get_contexts());
}

void MainWindow::timeline_panel_selection_changed(const QVector<OakEngineBlock *> &blocks)
{
	TimelinePanel *panel = static_cast<TimelinePanel *>(sender());

	if (PanelManager::instance()->currently_focused(false) == panel) {
		update_node_panel_context_from_timeline_panel(panel);
		sequence_viewer_panel_->set_timeline_selected_blocks(blocks);
	}
}

void MainWindow::show_welcome_dialog()
{
	if (OAK_CONFIG("show_welcome_dialog").toBool()) {
		AboutDialog ad(true, this);
		ad.exec();
	}
}

void MainWindow::reveal_viewer_in_project(OakEngineNode *r)
{
	// Rather than just using the resident ProjectPanel, find the most recently focused one since
	// that's probably the one people will want
	auto panels = PanelManager::instance()->get_panels_of_type<ProjectPanel>();
	oak::Node node(r);
	foreach (ProjectPanel *p, panels) {
		if (p->select_item(node)) {
			break;
		}
	}
}

void MainWindow::reveal_viewer_in_footage_viewer(OakEngineNode *r,
											 const TimeRange &range)
{
	footage_viewer_panel_->connect_viewer_node(r);

	auto command = oakengine_undo_command_create_multi();
	OakEngineWorkarea *wa = oakengine_viewer_get_workarea_handle(r);
	int64_t old_in_num, old_in_den, old_out_num, old_out_den;
	int old_enabled;
	oakengine_workarea_get(wa, &old_in_num, &old_in_den,
						   &old_out_num, &old_out_den, &old_enabled);
	if (!old_enabled) {
		oakengine_workarea_set_enabled_undoable(wa, 1, command);
	}
	{
		oakengine_workarea_set_range_undoable(wa,
			range.in().numerator(), range.in().denominator(),
			range.out().numerator(), range.out().denominator(),
			old_in_num, old_in_den, old_out_num, old_out_den, command);
	}
	oakengine_undo_push(command, tr("Set Footage Workarea").toUtf8().constData());

	oakengine_viewer_set_playhead(
		r,
		range.in().numerator(), range.in().denominator());
}

#ifdef Q_OS_LINUX
void MainWindow::show_nouveau_warning()
{
	QMessageBox::warning(
		this, tr("Driver Warning"),
		tr("Oak Video Editor has detected your system is using the Nouveau graphics driver.\n\nThis driver is "
		   "known to have stability and performance issues with Oak Video Editor. It is highly recommended "
		   "you install the proprietary NVIDIA driver before continuing to use Oak Video Editor."),
		QMessageBox::Ok);
}
#endif

void MainWindow::update_title()
{
	if (Core::instance()->get_active_project()) {
		char name_buf[256];
		oakengine_project_pretty_filename(
			reinterpret_cast<OakEngineProject *>(
				Core::instance()->get_active_project()),
			name_buf, sizeof(name_buf));
		setWindowTitle(
			QStringLiteral("%1 %2 - [*]%3")
				.arg(QApplication::applicationName(),
					 QApplication::applicationVersion(), name_buf));
	} else {
		setWindowTitle(
			QStringLiteral("%1 %2").arg(QApplication::applicationName(),
										QApplication::applicationVersion()));
	}
}

void MainWindow::timeline_close_requested()
{
	TimelinePanel *t = static_cast<TimelinePanel *>(sender());
	remove_timeline_panel(t);
}

void MainWindow::viewer_close_requested()
{
	ViewerPanel *panel = static_cast<ViewerPanel *>(sender());

	if (panel == scope_panel_->get_connected_viewer_panel()) {
		scope_panel_->set_viewer_panel(sequence_viewer_panel_);
	}

	remove_panel_internal(viewer_panels_, panel);

	panel->deleteLater();
}

void MainWindow::viewer_with_panel_removed_from_graph(OakEngineNode *source)
{
	ViewerPanel *panel = nullptr;

	foreach (ViewerPanel *p, viewer_panels_) {
		if (p->get_connected_viewer() == source) {
			panel = p;
			break;
		}
	}

	if (panel) {
		remove_panel_internal(viewer_panels_, panel);
		panel->deleteLater();
		auto it = removed_from_graph_subs_.find(source);
		if (it != removed_from_graph_subs_.end()) {
			bridge_->unsubscribe(it.value());
			removed_from_graph_subs_.erase(it);
		}
	}
}

void MainWindow::folder_panel_close_requested()
{
	ProjectPanel *panel = static_cast<ProjectPanel *>(sender());
	remove_panel_internal(folder_panels_, panel);
	panel->deleteLater();
}

TimelinePanel *MainWindow::append_timeline_panel()
{
	TimelinePanel *previous = nullptr;
	if (!timeline_panels_.empty()) {
		previous = timeline_panels_.last();
	}

	TimelinePanel *panel =
		append_panel_internal(QStringLiteral("TimelinePanel"), timeline_panels_);

	if (previous) {
		previous->addDockWidgetAsTab(panel);
	} else {
		panel->set_signal_instead_of_close(false);
	}

	connect(panel, &PanelWidget::close_requested, this,
			&MainWindow::timeline_close_requested);
	connect(panel, &TimelinePanel::request_capture_start, sequence_viewer_panel_,
			&SequenceViewerPanel::start_capture);
	connect(panel, &TimelinePanel::block_selection_changed, this,
			&MainWindow::timeline_panel_selection_changed);
	connect(panel, &TimelinePanel::reveal_viewer_in_project, this,
			&MainWindow::reveal_viewer_in_project);
	connect(panel, &TimelinePanel::reveal_viewer_in_footage_viewer, this,
			&MainWindow::reveal_viewer_in_footage_viewer);

	sequence_viewer_panel_->connect_time_based_panel(panel);

	return panel;
}

void MainWindow::remove_timeline_panel(TimelinePanel *panel)
{
	// Stop showing this timeline in the viewer
	timeline_focused(nullptr);
	panel->connect_viewer_node(nullptr);

	if (timeline_panels_.size() != 1) {
		remove_panel_internal(timeline_panels_, panel);
		panel->deleteLater();
	}
}

void MainWindow::timeline_focused(OakEngineNode *viewer)
{
	sequence_viewer_panel_->connect_viewer_node(viewer);
	multicam_panel_->connect_viewer_node(viewer);
	param_panel_->connect_viewer_node(viewer);
	curve_panel_->connect_viewer_node(viewer);

	// Update the global status bar info chips (resolution + frame rate)
	if (viewer) {
		oak::VideoParams vp = viewer_output_video_params(viewer);
		status_bar_->set_sequence_info(vp.width(), vp.height(),
									   vp.frame_rate().to_double());
	} else {
		status_bar_->set_sequence_info(0, 0, 0.0);
	}
}

QString MainWindow::get_custom_shortcuts_file()
{
	return QDir(FileFunctions::get_configuration_location())
		.filePath(QStringLiteral("shortcuts"));
}

void load_custom_shortcuts_internal(QMenu *menu,
								 const QMap<QString, QString> &shortcuts)
{
	QList<QAction *> actions = menu->actions();

	foreach (QAction *a, actions) {
		if (a->menu()) {
			load_custom_shortcuts_internal(a->menu(), shortcuts);
		} else if (!a->isSeparator()) {
			QString action_id = a->property("id").toString();

			if (shortcuts.contains(action_id)) {
				a->setShortcut(shortcuts.value(action_id));
			}
		}
	}
}

void MainWindow::load_custom_shortcuts()
{
	QFile shortcut_file(get_custom_shortcuts_file());
	if (shortcut_file.exists() && shortcut_file.open(QFile::ReadOnly)) {
		QMap<QString, QString> shortcuts;

		QString shortcut_str = QString::fromUtf8(shortcut_file.readAll());

		QStringList shortcut_list = shortcut_str.split(QStringLiteral("\n"));

		foreach (const QString &s, shortcut_list) {
			QStringList shortcut_line = s.split(QStringLiteral("\t"));
			if (shortcut_line.size() >= 2) {
				shortcuts.insert(shortcut_line.at(0), shortcut_line.at(1));
			}
		}

		shortcut_file.close();

		if (!shortcuts.isEmpty()) {
			QList<QAction *> menus = menuBar()->actions();

			foreach (QAction *menu, menus) {
				load_custom_shortcuts_internal(menu->menu(), shortcuts);
			}
		}
	}
}

void save_custom_shortcuts_internal(QMenu *menu, QMap<QString, QString> *shortcuts)
{
	QList<QAction *> actions = menu->actions();

	foreach (QAction *a, actions) {
		if (a->menu()) {
			save_custom_shortcuts_internal(a->menu(), shortcuts);
		} else if (!a->isSeparator()) {
			QString default_shortcut =
				a->property("keydefault").value<QKeySequence>().toString();
			QString current_shortcut = a->shortcut().toString();
			if (current_shortcut != default_shortcut) {
				QString action_id = a->property("id").toString();
				shortcuts->insert(action_id, current_shortcut);
			}
		}
	}
}

void MainWindow::save_custom_shortcuts()
{
	QMap<QString, QString> shortcuts;
	QList<QAction *> menus = menuBar()->actions();

	foreach (QAction *menu, menus) {
		save_custom_shortcuts_internal(menu->menu(), &shortcuts);
	}

	QFile shortcut_file(get_custom_shortcuts_file());
	if (shortcuts.isEmpty()) {
		if (shortcut_file.exists()) {
			// No custom shortcuts, remove any existing file
			shortcut_file.remove();
		}
	} else if (shortcut_file.open(QFile::WriteOnly)) {
		for (auto it = shortcuts.cbegin(); it != shortcuts.cend(); it++) {
			if (it != shortcuts.cbegin()) {
				shortcut_file.write(QStringLiteral("\n").toUtf8());
			}

			shortcut_file.write(it.key().toUtf8());
			shortcut_file.write(QStringLiteral("\t").toUtf8());
			shortcut_file.write(it.value().toUtf8());
		}
		shortcut_file.close();
	} else {
		qCritical() << "Failed to save custom keyboard shortcuts";
	}
}

void MainWindow::update_audio_monitor_params(OakEngineNode *viewer)
{
	if (!audio_monitor_panel_->is_playing()) {
		audio_monitor_panel_->set_params(viewer ? viewer_output_audio_params(viewer) :
												 AudioParams());
	}
}

void MainWindow::update_node_panel_context_from_timeline_panel(TimelinePanel *panel)
{
	// Add selected blocks (if any)
	const QVector<OakEngineBlock *> blocks = panel->get_selected_blocks();
	QVector<oak::Node> context(blocks.size());
	for (int i = 0; i < blocks.size(); i++) {
		context[i] = oak::Node(reinterpret_cast<OakEngineNode *>(blocks.at(i)));
	}

	// If no selected blocks, set the context to the sequence
	OakEngineNode *viewer = panel->get_connected_viewer();
	if (viewer && context.isEmpty()) {
		context.append(oak::Node(viewer));
	}

	node_panel_->set_contexts(context);
	param_panel_->set_contexts(context);
}

void MainWindow::select_footage_for_project_panel(const QVector<OakEngineNode *> &e,
											  ProjectPanel *p)
{
	p->deselect_all();
	for (OakEngineNode *f : e) {
		oak::Node node(f);
		if (p->get_root().has_child_recursive(node)) {
			p->select_item(node, false);
		}
	}
}

void MainWindow::focused_panel_changed(PanelWidget *panel)
{
	// Update audio monitor panel
	if (TimeBasedPanel *tbp = dynamic_cast<TimeBasedPanel *>(panel)) {
		update_audio_monitor_params(tbp->get_connected_viewer());
	}

	if (NodePanel *node_panel = dynamic_cast<NodePanel *>(panel)) {
		// Set param view contexts to these
		const QVector<oak::Node> &new_ctxs = node_panel->get_contexts();

		if (new_ctxs != param_panel_->get_contexts()) {
			param_panel_->set_contexts(new_ctxs);
		}
	} else if (TimelinePanel *timeline = dynamic_cast<TimelinePanel *>(panel)) {
		// Signal timeline focus
		timeline_focused(timeline->get_connected_viewer());

		update_node_panel_context_from_timeline_panel(timeline);
	} else if (ProjectPanel *project = dynamic_cast<ProjectPanel *>(panel)) {
		// Signal project panel focus
		Q_UNUSED(project)
		update_title();
	} else if (ViewerPanelBase *viewer =
				   dynamic_cast<ViewerPanelBase *>(panel)) {
		// Update scopes for viewer
		scope_panel_->set_viewer_panel(viewer);
	}
}

void MainWindow::set_default_layout()
{
	// Default layout per the Oak UI design reference (design/*.png):
	//
	// ┌─────────────┬───────────────────────────┬──────────────┐
	// │ 素材查看器   │ 序列查看器 | 节点编辑器    │ 检查器|历史   │
	// ├─────────────┤                           │              │
	// │ 项目        │                           │              │
	// ├─────────────┴───────────────────────────┴──────────────┤
	// │ 工具条(31px) + 时间线 (full width)                      │
	// ├────────────────────────────────────────────────────────┤
	// │ 音频监视器 (26px thin strip)                            │
	// └────────────────────────────────────────────────────────┘

	KDDockWidgets::InitialOption o;

	// Timeline at the very bottom, spanning the full window width
	addDockWidget(timeline_panels_.first(), KDDockWidgets::Location_OnBottom);

	// Audio monitor: 26px ultra-thin strip below the timeline
	o.preferredSize = QSize(0, 26);
	addDockWidget(audio_monitor_panel_, KDDockWidgets::Location_OnBottom,
				  timeline_panels_.first(), o);

	// Three-column workspace above the timeline. Establish the horizontal
	// (left -> right) structure first so the column separators span the full
	// workspace height, then subdivide the left column vertically.
	o = KDDockWidgets::InitialOption();
	o.preferredSize = QSize(0, centralAreaGeometry().height());

	// Left column - footage viewer (top-left anchor)
	addDockWidget(footage_viewer_panel_, KDDockWidgets::Location_OnTop, nullptr,
				  o);

	// Center column - sequence viewer tabified with node editor
	addDockWidget(sequence_viewer_panel_, KDDockWidgets::Location_OnRight,
				  footage_viewer_panel_, o);
	sequence_viewer_panel_->addDockWidgetAsTab(node_panel_);
	sequence_viewer_panel_->raise();

	// Right column - inspector (param panel) with history
	addDockWidget(param_panel_, KDDockWidgets::Location_OnRight,
				  sequence_viewer_panel_, o);
	param_panel_->addDockWidgetAsTab(history_panel_);
	param_panel_->raise();

	// Left column bottom - project panel (below footage viewer)
	addDockWidget(project_panel_, KDDockWidgets::Location_OnBottom,
				  footage_viewer_panel_);
	project_panel_->raise();

	// Hidden panels (all remain accessible via the Window menu)
	tool_panel_->close();
	pixel_sampler_panel_->close();
	task_man_panel_->close();
	curve_panel_->close();
	scope_panel_->close();
	multicam_panel_->close();
	for (auto it = folder_panels_.cbegin(); it != folder_panels_.cend(); it++) {
		(*it)->close();
	}
	for (auto it = viewer_panels_.cbegin(); it != viewer_panels_.cend(); it++) {
		(*it)->close();
	}
	for (auto it = timeline_panels_.cbegin(); it != timeline_panels_.cend();
		 it++) {
		if (*it != timeline_panels_.first()) {
			(*it)->close();
		}
	}

	// Set to unmaximized panels
	premaximized_state_.clear();
}

void MainWindow::showEvent(QShowEvent *e)
{
	QMainWindow::showEvent(e);

	if (first_show_) {
		QMetaObject::invokeMethod(Core::instance(), "check_for_auto_recoveries",
								  Qt::QueuedConnection);

#ifdef Q_OS_LINUX
		// Check for nouveau since that driver really doesn't work with Olive
		QOffscreenSurface surface;
		surface.create();
		QOpenGLContext context;
		context.create();
		context.makeCurrent(&surface);
		const char *vendor = reinterpret_cast<const char *>(
			context.functions()->glGetString(GL_VENDOR));
		qDebug() << "Using graphics driver:" << vendor;
		if (!strcmp(vendor, "nouveau")) {
			QMetaObject::invokeMethod(this, "show_nouveau_warning",
									  Qt::QueuedConnection);
		}
#endif

		QMetaObject::invokeMethod(this, "show_welcome_dialog",
								  Qt::QueuedConnection);

		first_show_ = false;
	}
}

template <typename T>
T *MainWindow::append_panel_internal(const QString &panel_name, QList<T *> &list)
{
	T *panel = new T(transform_name_for_serialization(panel_name, list.size()));

	// For some reason raise() on its own doesn't do anything, we need both
	panel->show();
	panel->raise();

	list.append(panel);

	// Let us handle the panel closing rather than the panel itself
	panel->set_signal_instead_of_close(true);

	return panel;
}

template <typename T>
void MainWindow::remove_panel_internal(QList<T *> &list, T *panel)
{
	list.removeOne(panel);
}

}
