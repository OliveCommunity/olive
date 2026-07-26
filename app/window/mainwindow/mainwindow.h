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

#ifndef OAK_MAINWINDOW_H
#define OAK_MAINWINDOW_H

#include <kddockwidgets/Config.h>
#include <kddockwidgets/MainWindow.h>

#include "node/project/serializer/serializedlayoutinfo.h"
#include "node/project.h"
#include "panel/multicam/multicampanel.h"
#include "panel/panelmanager.h"
#include "panel/audiomonitor/audiomonitor.h"
#include "panel/curve/curve.h"
#include "panel/history/historypanel.h"
#include "panel/node/node.h"
#include "panel/param/param.h"
#include "panel/project/project.h"
#include "panel/scope/scope.h"
#include "panel/table/table.h"
#include "panel/taskmanager/taskmanager.h"
#include "panel/timeline/timeline.h"
#include "panel/tool/tool.h"
#include "panel/footageviewer/footageviewer.h"
#include "panel/sequenceviewer/sequenceviewer.h"
#include "panel/pixelsampler/pixelsamplerpanel.h"
#include "engineeventbridge.h"

#ifdef Q_OS_WINDOWS
#include <shobjidl.h>
#endif

namespace olive
{

class EngineEventBridge;
class MainStatusBar;

/**
 * @brief Olive's main window responsible for docking widgets and the main menu bar.
 */
class MainWindow : public KDDockWidgets::QtWidgets::MainWindow {
	Q_OBJECT
public:
	MainWindow(QWidget *parent = nullptr);

	virtual ~MainWindow() override;

	void load_layout(const SerializedLayoutInfo &info);

	SerializedLayoutInfo save_layout() const;

	TimelinePanel *open_sequence(Sequence *sequence, bool enable_focus = true);

	void close_sequence(Sequence *sequence);

	bool is_sequence_open(Sequence *sequence) const;

	void open_folder(Folder *i, bool floating);

	void open_node_in_viewer(ViewerOutput *node);

	enum ProgressStatus { k_progress_none, k_progress_show, k_progress_error };

	/**
   * @brief Where applicable, show progress on an operating system level
   *
   * * For Windows, this is shown as progress in the taskbar.
   * * For macOS, this is shown as progress in the dock.
   */
	void set_application_progress_status(ProgressStatus status);

	/**
   * @brief If SetApplicationProgressStatus is set to kShowProgress, set the value with this
   *
   * Expects a percentage (0-100 inclusive).
   */
	void set_application_progress_value(int value);

	void select_footage(const QVector<Footage *> &e);

public:
	// Not a slot: signature uses the engine C++ type Project*, which must not
	// be exposed to MOC (it would pull Project::staticMetaObject across the ABI
	// boundary). It is only ever called directly, never via connect().
	void set_project(Project *p);

public slots:
	void set_fullscreen(bool fullscreen);

	void toggle_maximized_panel();

	void set_default_layout();

protected:
	virtual void showEvent(QShowEvent *e) override;

	virtual void closeEvent(QCloseEvent *e) override;

#ifdef Q_OS_WINDOWS
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	virtual bool nativeEvent(const QByteArray &eventType, void *message,
							 qintptr *result) override;
#else
	virtual bool nativeEvent(const QByteArray &eventType, void *message,
							 long *result) override;
#endif
#endif

private:
	TimelinePanel *append_timeline_panel();

	template <typename T>
	T *append_panel_internal(const QString &panel_name, QList<T *> &list);

	template <typename T> void remove_panel_internal(QList<T *> &list, T *panel);

	void remove_timeline_panel(TimelinePanel *panel);

	void timeline_focused(ViewerOutput *viewer);

	static QString get_custom_shortcuts_file();

	void load_custom_shortcuts();

	void save_custom_shortcuts();

	void update_audio_monitor_params(ViewerOutput *viewer);

	void update_node_panel_context_from_timeline_panel(TimelinePanel *panel);

	void select_footage_for_project_panel(const QVector<Footage *> &e,
									  ProjectPanel *p);

	QByteArray premaximized_state_;

	// Standard panels
	ProjectPanel *project_panel_;
	NodePanel *node_panel_;
	ParamPanel *param_panel_;
	CurvePanel *curve_panel_;
	SequenceViewerPanel *sequence_viewer_panel_;
	FootageViewerPanel *footage_viewer_panel_;
	QList<ProjectPanel *> folder_panels_;
	ToolPanel *tool_panel_;
	QList<TimelinePanel *> timeline_panels_;
	AudioMonitorPanel *audio_monitor_panel_;
	TaskManagerPanel *task_man_panel_;
	EngineEventBridge *event_bridge_;
	MainStatusBar *status_bar_;
	PixelSamplerPanel *pixel_sampler_panel_;
	ScopePanel *scope_panel_;
	QList<ViewerPanel *> viewer_panels_;
	MulticamPanel *multicam_panel_;
	HistoryPanel *history_panel_;

#ifdef Q_OS_WINDOWS
	unsigned int taskbar_btn_id_;

	ITaskbarList3 *taskbar_interface_;
#endif

	bool first_show_;

	Project *project_;

private slots:
	void focused_panel_changed(PanelWidget *panel);

	void update_title();

	void timeline_close_requested();

	void viewer_close_requested();

	void viewer_with_panel_removed_from_graph(OakEngineNode *source);

	void folder_panel_close_requested();

	void status_bar_double_clicked();

	void node_panel_group_opened_or_closed();

#ifdef Q_OS_LINUX
	void show_nouveau_warning();
#endif

	void show_welcome_dialog();

	void reveal_viewer_in_project(OakEngineNode *r);
	void reveal_viewer_in_footage_viewer(OakEngineNode *r, const TimeRange &range);

private:
	void timeline_panel_selection_changed(const QVector<OakEngineBlock *> &blocks);

	EngineEventBridge *bridge_ = nullptr;
	QHash<ViewerOutput *, int64_t> removed_from_graph_subs_;
};

}

#endif
