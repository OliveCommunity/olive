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

#ifndef OAK_MAINMENU_H
#define OAK_MAINMENU_H

#include <QMainWindow>
#include <QMenuBar>

#include "dialog/actionsearch/actionsearch.h"
#include "widget/menu/menu.h"

namespace olive
{

class MainWindow;

/**
 * @brief Olive's main menubar attached to its main window.
 *
 * Responsible for creating the menu, connecting signals/slots, and retranslating the items on a language change.
 */
class MainMenu : public QMenuBar {
	Q_OBJECT
public:
	MainMenu(MainWindow *parent);

protected:
	/**
   * @brief changeEvent
   *
   * Qt changeEvent override to catch a QEvent::LanguageEvent.
   *
   * @param e
   */
	virtual void changeEvent(QEvent *e);

private slots:
	/**
   * @brief A slot for the Tool selection items
   *
   * Assumes a QAction* sender() and its data() is a member of enum Tool::Item. Uses the data() to signal a
   * Tool change throughout the rest of the application.
   */
	void tool_item_triggered();

	/**
   * @brief Slot triggered just before the File menu shows
   */
	void file_menu_about_to_show();

	/**
   * @brief Slot triggered just before the Edit menu shows
   */
	void edit_menu_about_to_show();
	void edit_menu_about_to_hide();

	/**
   * @brief Slot triggered just before the View menu shows
   */
	void view_menu_about_to_show();

	/**
   * @brief Slot triggered just before the Tools menu shows
   */
	void tools_menu_about_to_show();

	/**
   * @brief Slot triggered just before the Playback menu shows
   */
	void playback_menu_about_to_show();

	/**
   * @brief Slot triggered just before the Sequence menu shows
   */
	void sequence_menu_about_to_show();

	/**
   * @brief Slot triggered just before the Window menu shows
   */
	void window_menu_about_to_show();

	/**
   * @brief Adds items to open recent menu
   */
	void populate_open_recent();

	void repopulate_open_recent();

	/**
   * @brief Clears open recent items when menu closes
   */
	void close_open_recent_menu();

	/**
   * @brief Slot for zooming in
   *
   * Finds the currently focused panel and sends it a "zoom in" signal
   */
	void zoom_in_triggered();

	/**
   * @brief Slot for zooming out
   *
   * Finds the currently focused panel and sends it a "zoom out" signal
   */
	void zoom_out_triggered();

	void increase_track_height_triggered();
	void decrease_track_height_triggered();

	void go_to_start_triggered();
	void prev_frame_triggered();

	/**
   * @brief Slot for play/pause
   *
   * Finds the currently focused panel and sends it a "play/pause" signal
   */
	void play_pause_triggered();

	void play_in_to_out_triggered();

	void loop_triggered(bool enabled);

	void next_frame_triggered();
	void go_to_end_triggered();

	void select_all_triggered();
	void deselect_all_triggered();

	void insert_triggered();
	void overwrite_triggered();

	void ripple_to_in_triggered();
	void ripple_to_out_triggered();
	void edit_to_in_triggered();
	void edit_to_out_triggered();

	void nudge_left_triggered();
	void nudge_right_triggered();
	void move_in_to_playhead_triggered();
	void move_out_to_playhead_triggered();

	void action_search_triggered();

	void shuttle_left_triggered();
	void shuttle_stop_triggered();
	void shuttle_right_triggered();

	void go_to_prev_cut_triggered();
	void go_to_next_cut_triggered();

	void set_marker_triggered();

	void full_screen_viewer_triggered();

	void toggle_show_all_triggered();

	void delete_in_out_triggered();
	void ripple_delete_in_out_triggered();

	void go_to_in_triggered();
	void go_to_out_triggered();

	void open_recent_item_triggered();

	void sequence_cache_triggered();
	void sequence_cache_in_out_triggered();
	void sequence_cache_clear_triggered();

	void help_feedback_triggered();

private:
	/**
   * @brief Set strings based on the current application language.
   */
	void retranslate();

	Menu *file_menu_;
	Menu *file_new_menu_;
	QAction *file_open_item_;
	Menu *file_open_recent_menu_;
	QAction *file_open_recent_separator_;
	QAction *file_open_recent_clear_item_;
	QAction *file_save_item_;
	QAction *file_save_as_item_;
	QAction *file_revert_item_;
	QAction *file_import_item_;
	Menu *file_export_menu_;
	QAction *file_export_media_item_;
	QAction *file_project_properties_item_;
	QAction *file_exit_item_;

	Menu *edit_menu_;
	QAction *edit_undo_item_;
	QAction *edit_redo_item_;
	QAction *edit_delete2_item_;
	QAction *edit_select_all_item_;
	QAction *edit_deselect_all_item_;
	QAction *edit_insert_item_;
	QAction *edit_overwrite_item_;
	QAction *edit_ripple_to_in_item_;
	QAction *edit_ripple_to_out_item_;
	QAction *edit_edit_to_in_item_;
	QAction *edit_edit_to_out_item_;
	QAction *edit_nudge_left_item_;
	QAction *edit_nudge_right_item_;
	QAction *edit_move_in_to_playhead_item_;
	QAction *edit_move_out_to_playhead_item_;
	QAction *edit_delete_inout_item_;
	QAction *edit_ripple_delete_inout_item_;
	QAction *edit_set_marker_item_;

	Menu *view_menu_;
	QAction *view_zoom_in_item_;
	QAction *view_zoom_out_item_;
	QAction *view_increase_track_height_item_;
	QAction *view_decrease_track_height_item_;
	QAction *view_show_all_item_;
	QAction *view_full_screen_item_;
	QAction *view_full_screen_viewer_item_;

	Menu *playback_menu_;
	QAction *playback_gotostart_item_;
	QAction *playback_prevframe_item_;
	QAction *playback_playpause_item_;
	QAction *playback_playinout_item_;
	QAction *playback_nextframe_item_;
	QAction *playback_gotoend_item_;
	QAction *playback_prevcut_item_;
	QAction *playback_nextcut_item_;
	QAction *playback_gotoin_item_;
	QAction *playback_gotoout_item_;
	QAction *playback_shuttleleft_item_;
	QAction *playback_shuttlestop_item_;
	QAction *playback_shuttleright_item_;
	QAction *playback_loop_item_;

	Menu *sequence_menu_;
	QAction *sequence_cache_item_;
	QAction *sequence_cache_in_to_out_item_;
	QAction *sequence_disk_cache_clear_item_;

	Menu *window_menu_;
	QAction *window_menu_separator_;
	QAction *window_maximize_panel_item_;
	QAction *window_reset_layout_item_;

	Menu *tools_menu_;
	QActionGroup *tools_group_;
	QAction *tools_pointer_item_;
	QAction *tools_trackselect_item_;
	QAction *tools_edit_item_;
	QAction *tools_ripple_item_;
	QAction *tools_rolling_item_;
	QAction *tools_razor_item_;
	QAction *tools_slip_item_;
	QAction *tools_slide_item_;
	QAction *tools_hand_item_;
	QAction *tools_zoom_item_;
	QAction *tools_transition_item_;
	QAction *tools_add_item_;
	QAction *tools_record_item_;
	QAction *tools_snapping_item_;
	QAction *tools_use_proxy_item_;
	QAction *tools_proxy_settings_item_;
	QAction *tools_preferences_item_;
	Menu *tools_add_item_menu_;

#ifndef NDEBUG
	QAction *tools_magic_item_;
#endif

	Menu *help_menu_;
	QAction *help_action_search_item_;
	QAction *help_feedback_item_;
	QAction *help_about_item_;
};

}

#endif // OAK_MAINMENU_H
