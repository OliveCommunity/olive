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

#include "mainmenu.h"

#include <QActionGroup>
#include <QDesktopServices>
#include <QEvent>
#include <QStyleFactory>

#include "core.h"
#include "oakengine/project.h"
#include "oakengine/undo.h"
#include "dialog/actionsearch/actionsearch.h"
#include "dialog/diskcache/diskcachedialog.h"
#include "dialog/proxy/proxydialog.h"
#include "dialog/task/task.h"
#include "panel/panelmanager.h"
#include "common/tooltypes.h"
#include "ui/style/style.h"
#include "widget/menu/menushared.h"
#include "mainwindow.h"
#include "common/configwrapper.h"

namespace olive
{

MainMenu::MainMenu(MainWindow *parent)
	: QMenuBar(parent)
{
	//
	// FILE MENU
	//
	file_menu_ = new Menu(this, this, &MainMenu::file_menu_about_to_show);
	file_new_menu_ = new Menu(file_menu_);
	MenuShared::instance()->add_items_for_new_menu(file_new_menu_);
	file_open_item_ = file_menu_->add_item("openproj", Core::instance(),
										  &Core::open_project, tr("Ctrl+O"));
	file_open_recent_menu_ = new Menu(file_menu_);
	file_open_recent_separator_ = file_open_recent_menu_->addSeparator();
	file_open_recent_clear_item_ = file_open_recent_menu_->add_item(
		"clearopenrecent", Core::instance(), &Core::clear_open_recent_list);
	file_save_item_ = file_menu_->add_item("saveproj", Core::instance(),
										  &Core::save_project, tr("Ctrl+S"));
	file_save_as_item_ = file_menu_->add_item("saveprojas", Core::instance(),
											 &Core::save_project_as,
											 tr("Ctrl+Shift+S"));
	file_menu_->addSeparator();
	file_revert_item_ = file_menu_->add_item("revert", Core::instance(),
											&Core::revert_project, tr("F12"));
	file_menu_->addSeparator();
	file_import_item_ = file_menu_->add_item(
		"import", Core::instance(), &Core::dialog_import_show, tr("Ctrl+I"));
	file_menu_->addSeparator();
	file_export_menu_ = new Menu(file_menu_);
	file_export_media_item_ = file_export_menu_->add_item(
		"export", Core::instance(), &Core::dialog_export_show, tr("Ctrl+M"));
	file_menu_->addSeparator();
	file_project_properties_item_ = file_menu_->add_item(
		"projectproperties", Core::instance(),
		&Core::dialog_project_properties_show, tr("Shift+F10"));
	file_menu_->addSeparator();
	file_exit_item_ = file_menu_->add_item("exit", parent, &MainWindow::close);

	//
	// EDIT MENU
	//
	edit_menu_ = new Menu(this);

	connect(edit_menu_, &Menu::aboutToShow, this,
			&MainMenu::edit_menu_about_to_show);
	connect(edit_menu_, &Menu::aboutToHide, this,
			&MainMenu::edit_menu_about_to_hide);

	edit_undo_item_ = reinterpret_cast<QAction*>(oakengine_undo_undo_action());
	Menu::conform_item(edit_undo_item_, "undo", tr("Ctrl+Z"));
	edit_menu_->addAction(edit_undo_item_);
	edit_redo_item_ = reinterpret_cast<QAction*>(oakengine_undo_redo_action());
	Menu::conform_item(edit_redo_item_, "redo", tr("Ctrl+Shift+Z"));
	edit_menu_->addAction(edit_redo_item_);

	edit_menu_->addSeparator();
	MenuShared::instance()->add_items_for_edit_menu(edit_menu_, true);
	{
		// Create "alternate delete" action so we can pick up backspace as well as delete while still
		// keeping them configurable
		edit_delete2_item_ = new QAction();
		Menu::conform_item(edit_delete2_item_, "delete2", MenuShared::instance(),
						  &MenuShared::delete_selected_triggered,
						  tr("Backspace"));
		auto actions = edit_menu_->actions();
		edit_menu_->insertAction(
			actions.at(
				actions.indexOf(MenuShared::instance()->edit_delete_item()) +
				1),
			edit_delete2_item_);
	}
	edit_menu_->addSeparator();
	edit_select_all_item_ = edit_menu_->add_item(
		"selectall", this, &MainMenu::select_all_triggered, tr("Ctrl+A"));
	edit_deselect_all_item_ = edit_menu_->add_item(
		"deselectall", this, &MainMenu::deselect_all_triggered,
		tr("Ctrl+Shift+A"));
	edit_menu_->addSeparator();
	MenuShared::instance()->add_items_for_clip_edit_menu(edit_menu_);
	edit_menu_->addSeparator();
	edit_insert_item_ = edit_menu_->add_item(
		"insert", this, &MainMenu::insert_triggered, tr(","));
	edit_overwrite_item_ = edit_menu_->add_item(
		"overwrite", this, &MainMenu::overwrite_triggered, tr("."));
	edit_menu_->addSeparator();
	edit_ripple_to_in_item_ = edit_menu_->add_item(
		"rippletoin", this, &MainMenu::ripple_to_in_triggered, tr("Q"));
	edit_ripple_to_out_item_ = edit_menu_->add_item(
		"rippletoout", this, &MainMenu::ripple_to_out_triggered, tr("W"));
	edit_edit_to_in_item_ = edit_menu_->add_item(
		"edittoin", this, &MainMenu::edit_to_in_triggered, tr("Ctrl+Alt+Q"));
	edit_edit_to_out_item_ = edit_menu_->add_item(
		"edittoout", this, &MainMenu::edit_to_out_triggered, tr("Ctrl+Alt+W"));
	edit_menu_->addSeparator();
	edit_nudge_left_item_ = edit_menu_->add_item(
		"nudgeleft", this, &MainMenu::nudge_left_triggered, tr("Alt+Left"));
	edit_nudge_right_item_ = edit_menu_->add_item(
		"nudgeright", this, &MainMenu::nudge_right_triggered, tr("Alt+Right"));
	edit_move_in_to_playhead_item_ =
		edit_menu_->add_item("moveintoplayhead", this,
							&MainMenu::move_in_to_playhead_triggered, tr("["));
	edit_move_out_to_playhead_item_ =
		edit_menu_->add_item("moveouttoplayhead", this,
							&MainMenu::move_out_to_playhead_triggered, tr("]"));
	edit_menu_->addSeparator();
	MenuShared::instance()->add_items_for_in_out_menu(edit_menu_);
	edit_delete_inout_item_ = edit_menu_->add_item(
		"deleteinout", this, &MainMenu::delete_in_out_triggered, tr(";"));
	edit_ripple_delete_inout_item_ =
		edit_menu_->add_item("rippledeleteinout", this,
							&MainMenu::ripple_delete_in_out_triggered, tr("'"));
	edit_menu_->addSeparator();
	edit_set_marker_item_ = edit_menu_->add_item(
		"marker", this, &MainMenu::set_marker_triggered, tr("M"));

	//
	// VIEW MENU
	//
	view_menu_ = new Menu(this, this, &MainMenu::view_menu_about_to_show);
	view_zoom_in_item_ = view_menu_->add_item(
		"zoomin", this, &MainMenu::zoom_in_triggered, tr("="));
	view_zoom_out_item_ = view_menu_->add_item(
		"zoomout", this, &MainMenu::zoom_out_triggered, tr("-"));
	view_increase_track_height_item_ = view_menu_->add_item(
		"vzoomin", this, &MainMenu::increase_track_height_triggered, tr("Ctrl+="));
	view_decrease_track_height_item_ = view_menu_->add_item(
		"vzoomout", this, &MainMenu::decrease_track_height_triggered,
		tr("Ctrl+-"));
	view_show_all_item_ = view_menu_->add_item(
		"showall", this, &MainMenu::toggle_show_all_triggered, tr("\\"));
	view_show_all_item_->setCheckable(true);

	view_menu_->addSeparator();

	view_full_screen_item_ = view_menu_->add_item(
		"fullscreen", parent, &MainWindow::set_fullscreen, tr("F11"));
	view_full_screen_item_->setCheckable(true);

	view_full_screen_viewer_item_ = view_menu_->add_item(
		"fullscreenviewer", this, &MainMenu::full_screen_viewer_triggered);

	//
	// PLAYBACK MENU
	//
	playback_menu_ = new Menu(this, this, &MainMenu::playback_menu_about_to_show);
	playback_gotostart_item_ = playback_menu_->add_item(
		"gotostart", this, &MainMenu::go_to_start_triggered, tr("Home"));
	playback_prevframe_item_ = playback_menu_->add_item(
		"prevframe", this, &MainMenu::prev_frame_triggered, tr("Left"));
	playback_playpause_item_ = playback_menu_->add_item(
		"playpause", this, &MainMenu::play_pause_triggered, tr("Space"));
	playback_playinout_item_ = playback_menu_->add_item(
		"playintoout", this, &MainMenu::play_in_to_out_triggered,
		tr("Shift+Space"));
	playback_nextframe_item_ = playback_menu_->add_item(
		"nextframe", this, &MainMenu::next_frame_triggered, tr("Right"));
	playback_gotoend_item_ = playback_menu_->add_item(
		"gotoend", this, &MainMenu::go_to_end_triggered, tr("End"));

	playback_menu_->addSeparator();

	playback_prevcut_item_ = playback_menu_->add_item(
		"prevcut", this, &MainMenu::go_to_prev_cut_triggered, tr("Up"));
	playback_nextcut_item_ = playback_menu_->add_item(
		"nextcut", this, &MainMenu::go_to_next_cut_triggered, tr("Down"));

	playback_menu_->addSeparator();

	playback_gotoin_item_ = playback_menu_->add_item(
		"gotoin", this, &MainMenu::go_to_in_triggered, tr("Shift+I"));
	playback_gotoout_item_ = playback_menu_->add_item(
		"gotoout", this, &MainMenu::go_to_out_triggered, tr("Shift+O"));

	playback_menu_->addSeparator();

	playback_shuttleleft_item_ = playback_menu_->add_item(
		"decspeed", this, &MainMenu::shuttle_left_triggered, tr("J"));
	playback_shuttlestop_item_ = playback_menu_->add_item(
		"pause", this, &MainMenu::shuttle_stop_triggered, tr("K"));
	playback_shuttleright_item_ = playback_menu_->add_item(
		"incspeed", this, &MainMenu::shuttle_right_triggered, tr("L"));

	playback_menu_->addSeparator();

	playback_loop_item_ =
		playback_menu_->add_item("loop", this, &MainMenu::loop_triggered);
	playback_loop_item_->setCheckable(true);

	//
	// SEQUENCE MENU
	//

	sequence_menu_ = new Menu(this, this, &MainMenu::sequence_menu_about_to_show);
	sequence_cache_item_ = sequence_menu_->add_item(
		"seqcache", this, &MainMenu::sequence_cache_triggered);
	sequence_cache_in_to_out_item_ = sequence_menu_->add_item(
		"seqcacheinout", this, &MainMenu::sequence_cache_in_out_triggered);

	sequence_menu_->addSeparator();

	sequence_disk_cache_clear_item_ = sequence_menu_->add_item(
		"seqcacheclear", this, &MainMenu::sequence_cache_clear_triggered);

	// TEMP: Hide sequence cache items for now. Want to see if clip caching will supersede it.
	sequence_cache_item_->setVisible(false);
	sequence_cache_in_to_out_item_->setVisible(false);

	//
	// WINDOW MENU
	//
	window_menu_ = new Menu(this, this, &MainMenu::window_menu_about_to_show);
	window_menu_separator_ = window_menu_->addSeparator();
	window_maximize_panel_item_ = window_menu_->add_item(
		"maximizepanel", parent, &MainWindow::toggle_maximized_panel, tr("`"));
	window_menu_->addSeparator();
	window_reset_layout_item_ = window_menu_->add_item(
		"resetdefaultlayout", parent, &MainWindow::set_default_layout);

	//
	// TOOLS MENU
	//
	tools_menu_ = new Menu(this, this, &MainMenu::tools_menu_about_to_show);
	tools_menu_->setToolTipsVisible(true);

	tools_group_ = new QActionGroup(this);

	tools_pointer_item_ = tools_menu_->add_item(
		"pointertool", this, &MainMenu::tool_item_triggered, tr("V"));
	tools_pointer_item_->setCheckable(true);
	tools_pointer_item_->setData(Tool::k_pointer);
	tools_group_->addAction(tools_pointer_item_);

	tools_trackselect_item_ = tools_menu_->add_item(
		"trackselecttool", this, &MainMenu::tool_item_triggered, tr("D"));
	tools_trackselect_item_->setCheckable(true);
	tools_trackselect_item_->setData(Tool::k_track_select);
	tools_group_->addAction(tools_trackselect_item_);

	tools_edit_item_ = tools_menu_->add_item(
		"edittool", this, &MainMenu::tool_item_triggered, tr("X"));
	tools_edit_item_->setCheckable(true);
	tools_edit_item_->setData(Tool::k_edit);
	tools_group_->addAction(tools_edit_item_);

	tools_ripple_item_ = tools_menu_->add_item(
		"rippletool", this, &MainMenu::tool_item_triggered, tr("B"));
	tools_ripple_item_->setCheckable(true);
	tools_ripple_item_->setData(Tool::k_ripple);
	tools_group_->addAction(tools_ripple_item_);

	tools_rolling_item_ = tools_menu_->add_item(
		"rollingtool", this, &MainMenu::tool_item_triggered, tr("N"));
	tools_rolling_item_->setCheckable(true);
	tools_rolling_item_->setData(Tool::k_rolling);
	tools_group_->addAction(tools_rolling_item_);

	tools_razor_item_ = tools_menu_->add_item(
		"razortool", this, &MainMenu::tool_item_triggered, tr("C"));
	tools_razor_item_->setCheckable(true);
	tools_razor_item_->setData(Tool::k_razor);
	tools_group_->addAction(tools_razor_item_);

	tools_slip_item_ = tools_menu_->add_item(
		"sliptool", this, &MainMenu::tool_item_triggered, tr("Y"));
	tools_slip_item_->setCheckable(true);
	tools_slip_item_->setData(Tool::k_slip);
	tools_group_->addAction(tools_slip_item_);

	tools_slide_item_ = tools_menu_->add_item(
		"slidetool", this, &MainMenu::tool_item_triggered, tr("U"));
	tools_slide_item_->setCheckable(true);
	tools_slide_item_->setData(Tool::k_slide);
	tools_group_->addAction(tools_slide_item_);

	tools_hand_item_ = tools_menu_->add_item(
		"handtool", this, &MainMenu::tool_item_triggered, tr("H"));
	tools_hand_item_->setCheckable(true);
	tools_hand_item_->setData(Tool::k_hand);
	tools_group_->addAction(tools_hand_item_);

	tools_zoom_item_ = tools_menu_->add_item(
		"zoomtool", this, &MainMenu::tool_item_triggered, tr("Z"));
	tools_zoom_item_->setCheckable(true);
	tools_zoom_item_->setData(Tool::k_zoom);
	tools_group_->addAction(tools_zoom_item_);

	tools_transition_item_ = tools_menu_->add_item(
		"transitiontool", this, &MainMenu::tool_item_triggered, tr("T"));
	tools_transition_item_->setCheckable(true);
	tools_transition_item_->setData(Tool::k_transition);
	tools_group_->addAction(tools_transition_item_);

	tools_add_item_ = tools_menu_->add_item(
		"addtool", this, &MainMenu::tool_item_triggered, tr("A"));
	tools_add_item_->setCheckable(true);
	tools_add_item_->setData(Tool::k_add);
	tools_group_->addAction(tools_add_item_);

	tools_record_item_ = tools_menu_->add_item(
		"recordtool", this, &MainMenu::tool_item_triggered, tr("R"));
	tools_record_item_->setCheckable(true);
	tools_record_item_->setData(Tool::k_record);
	tools_group_->addAction(tools_record_item_);

	tools_menu_->addSeparator();

	tools_add_item_menu_ = new Menu(tools_menu_);
	tools_menu_->addMenu(tools_add_item_menu_);

	MenuShared::instance()->add_items_for_addable_objects_menu(tools_add_item_menu_);

	tools_menu_->addSeparator();

	tools_snapping_item_ = tools_menu_->add_item("snapping", Core::instance(),
												&Core::set_snapping, tr("S"));
	tools_snapping_item_->setCheckable(true);
	tools_snapping_item_->setChecked(Core::instance()->snapping());

	tools_menu_->addSeparator();

	tools_use_proxy_item_ = new QAction(this);
	Menu::conform_item(tools_use_proxy_item_, "useproxymedia");
	tools_use_proxy_item_->setCheckable(true);
	tools_use_proxy_item_->setChecked(
		OAK_CONFIG("UseProxyMedia").toBool());
	connect(tools_use_proxy_item_, &QAction::triggered, Core::instance(),
			&Core::set_use_proxy_media);
	tools_menu_->addAction(tools_use_proxy_item_);

	tools_proxy_settings_item_ = new QAction(this);
	Menu::conform_item(tools_proxy_settings_item_, "proxysettings");
	connect(tools_proxy_settings_item_, &QAction::triggered, this, [this]() {
		ProxyDialog d(this);
		d.exec();
	});
	tools_menu_->addAction(tools_proxy_settings_item_);

	tools_preferences_item_ = tools_menu_->add_item(
		"prefs", Core::instance(), &Core::dialog_preferences_show, tr("Ctrl+,"));
	// On macOS, Qt's text heuristic would relocate an English "Preferences"
	// action to the application menu, making it disappear from the Tools menu.
	// Pin it to this menu on all platforms.
	tools_preferences_item_->setMenuRole(QAction::NoRole);

#ifndef NDEBUG
	tools_magic_item_ =
		tools_menu_->add_item("magic", Core::instance(), &Core::set_magic);
	tools_magic_item_->setCheckable(true);
#endif

	//
	// HELP MENU
	//
	help_menu_ = new Menu(this);
	help_action_search_item_ = help_menu_->add_item(
		"actionsearch", this, &MainMenu::action_search_triggered, tr("/"));
	help_menu_->addSeparator();
	help_feedback_item_ =
		help_menu_->add_item("feedback", this, &MainMenu::help_feedback_triggered);
	help_menu_->addSeparator();
	help_about_item_ =
		help_menu_->add_item("about", Core::instance(), &Core::dialog_about_show);

	connect(Core::instance(), &Core::open_recent_list_changed, this,
			&MainMenu::repopulate_open_recent);
	populate_open_recent();

	retranslate();
}

void MainMenu::changeEvent(QEvent *e)
{
	if (e->type() == QEvent::LanguageChange) {
		retranslate();
	}
	QMenuBar::changeEvent(e);
}

void MainMenu::tool_item_triggered()
{
	// Assume the sender is a QAction
	QAction *action = static_cast<QAction *>(sender());

	// Assume its data() is a member of Tool::Item
	Tool::Item tool = static_cast<Tool::Item>(action->data().toInt());

	// Set the Tool in Core
	Core::instance()->set_tool(tool);
}

void MainMenu::file_menu_about_to_show()
{
	OakEngineProject *active_project = Core::instance()->get_active_project();

	file_save_item_->setEnabled(active_project);
	file_save_as_item_->setEnabled(active_project);

	if (active_project) {
		char name_buf[256];
		oakengine_project_name(active_project, name_buf, sizeof(name_buf));
		file_save_item_->setText(tr("&Save '%1'").arg(name_buf));
		file_save_as_item_->setText(
			tr("Save '%1' &As").arg(name_buf));
	} else {
		file_save_item_->setText(tr("&Save Project"));
		file_save_as_item_->setText(tr("Save Project &As"));
	}
}

void MainMenu::edit_menu_about_to_show()
{
	edit_delete2_item_->setVisible(false);
}

void MainMenu::edit_menu_about_to_hide()
{
	edit_delete2_item_->setVisible(true);
}

void MainMenu::view_menu_about_to_show()
{
	// Parent is QMainWindow
	view_full_screen_item_->setChecked(parentWidget()->isFullScreen());

	// Make sure we're displaying the correct options for the timebase
	TimeBasedPanel *p =
		PanelManager::instance()->most_recently_focused<TimeBasedPanel>();
	if (p) {
		if (p->timebase().denominator() != 0) {
			view_menu_->addSeparator();
			MenuShared::instance()->add_items_for_time_ruler_menu(view_menu_);
		}
	}

	// Ensure checked timecode display mode is correct
	MenuShared::instance()->about_to_show_time_ruler_actions(p->timebase());
}

void MainMenu::tools_menu_about_to_show()
{
	// Ensure checked Tool is correct
	QList<QAction *> tool_actions = tools_group_->actions();
	foreach (QAction *a, tool_actions) {
		if (a->data() == Core::instance()->tool()) {
			a->setChecked(true);
			break;
		}
	}

	// Ensure snapping value is correct
	tools_snapping_item_->setChecked(Core::instance()->snapping());
}

void MainMenu::playback_menu_about_to_show()
{
	playback_loop_item_->setChecked(OAK_CONFIG("Loop").toBool());
}

void MainMenu::sequence_menu_about_to_show()
{
	TimeBasedPanel *p =
		PanelManager::instance()->most_recently_focused<TimeBasedPanel>();

	bool can_cache_sequence = (p && p->get_connected_viewer());

	sequence_cache_item_->setEnabled(can_cache_sequence);
	sequence_cache_in_to_out_item_->setEnabled(can_cache_sequence);
}

void MainMenu::window_menu_about_to_show()
{
	// Remove any previous items
	while (window_menu_->actions().first() != window_menu_separator_) {
		window_menu_->removeAction(window_menu_->actions().first());
	}

	QList<QAction *> panel_actions;

	// Alphabetize actions - keeps actions in a consistent order since PanelManager::panels() is
	// ordered from most recently focused to least, which may be confusing user experience.
	foreach (PanelWidget *panel, PanelManager::instance()->panels()) {
		QAction *panel_action = panel->toggleAction();

		bool inserted = false;

		for (int i = 0; i < panel_actions.size(); i++) {
			if (panel_actions.at(i)->text() > panel_action->text()) {
				panel_actions.insert(i, panel_action);
				inserted = true;
				break;
			}
		}

		if (!inserted) {
			panel_actions.append(panel_action);
		}
	}

	// Add new items
	window_menu_->insertActions(window_menu_separator_, panel_actions);
}

void MainMenu::populate_open_recent()
{
	if (Core::instance()->get_recent_project_count() == 0) {
		// Insert dummy/disabled action to show there's nothing
		QAction *a = new QAction(tr("(None)"));
		a->setEnabled(false);
		file_open_recent_menu_->insertAction(file_open_recent_separator_, a);

	} else {
		// Populate menu with recently opened projects
		for (int i = 0; i < Core::instance()->get_recent_project_count(); i++) {
			QAction *a =
				new QAction(Core::instance()->get_recent_project_at(i));
			a->setData(i);
			connect(a, &QAction::triggered, this,
					&MainMenu::open_recent_item_triggered);
			file_open_recent_menu_->insertAction(file_open_recent_separator_,
												 a);
		}
	}
}

void MainMenu::repopulate_open_recent()
{
	close_open_recent_menu();
	populate_open_recent();
}

void MainMenu::close_open_recent_menu()
{
	while (file_open_recent_menu_->actions().first() !=
		   file_open_recent_separator_) {
		file_open_recent_menu_->removeAction(
			file_open_recent_menu_->actions().first());
	}
}

void MainMenu::zoom_in_triggered()
{
	PanelManager::instance()->currently_focused()->zoom_in();
}

void MainMenu::zoom_out_triggered()
{
	PanelManager::instance()->currently_focused()->zoom_out();
}

void MainMenu::increase_track_height_triggered()
{
	PanelManager::instance()->currently_focused()->increase_track_height();
}

void MainMenu::decrease_track_height_triggered()
{
	PanelManager::instance()->currently_focused()->decrease_track_height();
}

void MainMenu::go_to_start_triggered()
{
	PanelManager::instance()->currently_focused()->go_to_start();
}

void MainMenu::prev_frame_triggered()
{
	PanelManager::instance()->currently_focused()->prev_frame();
}

void MainMenu::play_pause_triggered()
{
	PanelManager::instance()->currently_focused()->play_pause();
}

void MainMenu::play_in_to_out_triggered()
{
	PanelManager::instance()->currently_focused()->play_in_to_out();
}

void MainMenu::loop_triggered(bool enabled)
{
	OAK_CONFIG("Loop") = enabled;
}

void MainMenu::next_frame_triggered()
{
	PanelManager::instance()->currently_focused()->next_frame();
}

void MainMenu::go_to_end_triggered()
{
	PanelManager::instance()->currently_focused()->go_to_end();
}

void MainMenu::select_all_triggered()
{
	PanelManager::instance()->currently_focused()->select_all();
}

void MainMenu::deselect_all_triggered()
{
	PanelManager::instance()->currently_focused()->deselect_all();
}

void MainMenu::insert_triggered()
{
	FootageManagementPanel *project_panel =
		PanelManager::instance()->most_recently_focused<FootageManagementPanel>();
	TimelinePanel *timeline_panel =
		PanelManager::instance()->most_recently_focused<TimelinePanel>();

	if (project_panel && timeline_panel) {
		timeline_panel->insert_footage_at_playhead(
			project_panel->get_selected_footage());
	}
}

void MainMenu::overwrite_triggered()
{
	FootageManagementPanel *project_panel =
		PanelManager::instance()->most_recently_focused<FootageManagementPanel>();
	TimelinePanel *timeline_panel =
		PanelManager::instance()->most_recently_focused<TimelinePanel>();

	if (project_panel && timeline_panel) {
		timeline_panel->overwrite_footage_at_playhead(
			project_panel->get_selected_footage());
	}
}

void MainMenu::ripple_to_in_triggered()
{
	PanelManager::instance()->currently_focused()->ripple_to_in();
}

void MainMenu::ripple_to_out_triggered()
{
	PanelManager::instance()->currently_focused()->ripple_to_out();
}

void MainMenu::edit_to_in_triggered()
{
	PanelManager::instance()->currently_focused()->edit_to_in();
}

void MainMenu::edit_to_out_triggered()
{
	PanelManager::instance()->currently_focused()->edit_to_out();
}

void MainMenu::nudge_left_triggered()
{
	PanelManager::instance()->currently_focused()->nudge_left();
}

void MainMenu::nudge_right_triggered()
{
	PanelManager::instance()->currently_focused()->nudge_right();
}

void MainMenu::move_in_to_playhead_triggered()
{
	PanelManager::instance()->currently_focused()->move_in_to_playhead();
}

void MainMenu::move_out_to_playhead_triggered()
{
	PanelManager::instance()->currently_focused()->move_out_to_playhead();
}

void MainMenu::action_search_triggered()
{
	ActionSearch as(parentWidget());
	as.set_menu_bar(this);
	as.exec();
}

void MainMenu::shuttle_left_triggered()
{
	PanelManager::instance()->currently_focused()->shuttle_left();
}

void MainMenu::shuttle_stop_triggered()
{
	PanelManager::instance()->currently_focused()->shuttle_stop();
}

void MainMenu::shuttle_right_triggered()
{
	PanelManager::instance()->currently_focused()->shuttle_right();
}

void MainMenu::go_to_prev_cut_triggered()
{
	PanelManager::instance()->currently_focused()->go_to_prev_cut();
}

void MainMenu::go_to_next_cut_triggered()
{
	PanelManager::instance()->currently_focused()->go_to_next_cut();
}

void MainMenu::set_marker_triggered()
{
	PanelManager::instance()->currently_focused()->set_marker();
}

void MainMenu::full_screen_viewer_triggered()
{
	PanelManager::instance()
		->most_recently_focused<ViewerPanel>()
		->set_full_screen();
}

void MainMenu::toggle_show_all_triggered()
{
	PanelManager::instance()->currently_focused()->toggle_show_all();
}

void MainMenu::delete_in_out_triggered()
{
	PanelManager::instance()->currently_focused()->delete_in_to_out();
}

void MainMenu::ripple_delete_in_out_triggered()
{
	PanelManager::instance()->currently_focused()->ripple_delete_in_to_out();
}

void MainMenu::go_to_in_triggered()
{
	PanelManager::instance()->currently_focused()->go_to_in();
}

void MainMenu::go_to_out_triggered()
{
	PanelManager::instance()->currently_focused()->go_to_out();
}

void MainMenu::open_recent_item_triggered()
{
	Core::instance()->open_project_from_recent_list(
		static_cast<QAction *>(sender())->data().toInt());
}

void MainMenu::sequence_cache_triggered()
{
	Core::instance()->cache_active_sequence(false);
}

void MainMenu::sequence_cache_in_out_triggered()
{
	Core::instance()->cache_active_sequence(true);
}

void MainMenu::sequence_cache_clear_triggered()
{
	char cache_buf[512];
	oakengine_project_cache_path(
		reinterpret_cast<OakEngineProject *>(
			Core::instance()->get_active_project()),
		cache_buf, sizeof(cache_buf));
	DiskCacheDialog::clear_disk_cache(cache_buf,
		Core::instance()->main_window());
}

void MainMenu::help_feedback_triggered()
{
	QDesktopServices::openUrl(QStringLiteral(
		"https://github.com/OakVideoEditorCommunity/oak/issues"));
}

void MainMenu::retranslate()
{
	// MenuShared is not a QWidget and therefore does not receive a LanguageEvent, we use MainMenu's to update it
	MenuShared::instance()->retranslate();

	// File menu
	file_menu_->setTitle(tr("&File"));
	file_new_menu_->setTitle(tr("&New"));
	file_open_item_->setText(tr("&Open Project"));
	file_open_recent_menu_->setTitle(tr("Open &Recent"));
	file_open_recent_clear_item_->setText(tr("&Clear Recent List"));
	file_revert_item_->setText(tr("Revert"));
	file_import_item_->setText(tr("&Import..."));
	file_export_menu_->setTitle(tr("&Export"));
	file_export_media_item_->setText(tr("&Media..."));
	file_project_properties_item_->setText(tr("Project Properties"));
	file_exit_item_->setText(tr("E&xit"));

	// Edit menu
	edit_menu_->setTitle(tr("&Edit"));
	oakengine_undo_update_actions(); // Update undo and redo
	edit_delete2_item_->setText(tr("Delete (alt)"));
	edit_insert_item_->setText(tr("Insert"));
	edit_overwrite_item_->setText(tr("Overwrite"));
	edit_select_all_item_->setText(tr("Select &All"));
	edit_deselect_all_item_->setText(tr("Deselect All"));
	edit_ripple_to_in_item_->setText(tr("Ripple to In Point"));
	edit_ripple_to_out_item_->setText(tr("Ripple to Out Point"));
	edit_edit_to_in_item_->setText(tr("Edit to In Point"));
	edit_edit_to_out_item_->setText(tr("Edit to Out Point"));
	edit_nudge_left_item_->setText(tr("Nudge Left"));
	edit_nudge_right_item_->setText(tr("Nudge Right"));
	edit_move_in_to_playhead_item_->setText(tr("Move In Point to Playhead"));
	edit_move_out_to_playhead_item_->setText(tr("Move Out Point to Playhead"));
	edit_delete_inout_item_->setText(tr("Delete In/Out Point"));
	edit_ripple_delete_inout_item_->setText(tr("Ripple Delete In/Out Point"));
	edit_set_marker_item_->setText(tr("Set/Edit Marker"));

	// View menu
	view_menu_->setTitle(tr("&View"));
	view_zoom_in_item_->setText(tr("Zoom In"));
	view_zoom_out_item_->setText(tr("Zoom Out"));
	view_increase_track_height_item_->setText(tr("Increase Track Height"));
	view_decrease_track_height_item_->setText(tr("Decrease Track Height"));
	view_show_all_item_->setText(tr("Toggle Show All"));

	// View menu (cont'd)
	view_full_screen_item_->setText(tr("Full Screen"));
	view_full_screen_viewer_item_->setText(tr("Full Screen Viewer"));

	// Playback menu
	playback_menu_->setTitle(tr("&Playback"));
	playback_gotostart_item_->setText(tr("Go to Start"));
	playback_prevframe_item_->setText(tr("Previous Frame"));
	playback_playpause_item_->setText(tr("Play/Pause"));
	playback_playinout_item_->setText(tr("Play In to Out"));
	playback_nextframe_item_->setText(tr("Next Frame"));
	playback_gotoend_item_->setText(tr("Go to End"));
	playback_prevcut_item_->setText(tr("Go to Previous Cut"));
	playback_nextcut_item_->setText(tr("Go to Next Cut"));
	playback_gotoin_item_->setText(tr("Go to In Point"));
	playback_gotoout_item_->setText(tr("Go to Out Point"));
	playback_shuttleleft_item_->setText(tr("Shuttle Left"));
	playback_shuttlestop_item_->setText(tr("Shuttle Stop"));
	playback_shuttleright_item_->setText(tr("Shuttle Right"));
	playback_loop_item_->setText(tr("Loop"));

	// Sequence menu
	sequence_menu_->setTitle(tr("&Sequence"));
	sequence_cache_item_->setText(tr("Cache Entire Sequence"));
	sequence_cache_in_to_out_item_->setText(tr("Cache Sequence In/Out"));
	sequence_disk_cache_clear_item_->setText(tr("Clear Disk Cache"));

	// Window menu
	window_menu_->setTitle(tr("&Window"));
	window_maximize_panel_item_->setText(tr("Maximize Panel"));
	window_reset_layout_item_->setText(tr("Reset to Default Layout"));

	// Tools menu
	tools_menu_->setTitle(tr("&Tools"));
	tools_pointer_item_->setText(tr("Pointer Tool"));
	tools_trackselect_item_->setText(tr("Track Select Tool"));
	tools_edit_item_->setText(tr("Edit Tool"));
	tools_ripple_item_->setText(tr("Ripple Tool"));
	tools_rolling_item_->setText(tr("Rolling Tool"));
	tools_razor_item_->setText(tr("Razor Tool"));
	tools_slip_item_->setText(tr("Slip Tool"));
	tools_slide_item_->setText(tr("Slide Tool"));
	tools_hand_item_->setText(tr("Hand Tool"));
	tools_zoom_item_->setText(tr("Zoom Tool"));
	tools_transition_item_->setText(tr("Transition Tool"));
	tools_add_item_->setText(tr("Add Tool"));
	tools_record_item_->setText(tr("Record Tool"));
	tools_snapping_item_->setText(tr("Enable Snapping"));
	tools_use_proxy_item_->setText(tr("Use Proxy Media"));
	tools_proxy_settings_item_->setText(tr("Proxy Settings..."));
	tools_preferences_item_->setText(tr("Preferences"));
	tools_add_item_menu_->setTitle(tr("Add Tool Item"));
#ifndef NDEBUG
	tools_magic_item_->setText("Magic");
#endif

	// Help menu
	help_menu_->setTitle(tr("&Help"));
	help_action_search_item_->setText(tr("A&ction Search"));
	help_feedback_item_->setText(tr("Send &Feedback..."));
	help_about_item_->setText(tr("&About..."));
}

}
