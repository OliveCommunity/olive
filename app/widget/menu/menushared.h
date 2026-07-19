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

#ifndef OAK_MENUSHARED_H
#define OAK_MENUSHARED_H

#include <olive/core/core.h>
#include "widget/colorlabelmenu/colorlabelmenu.h"
#include "widget/menu/menu.h"

namespace olive
{

using namespace core;

/**
 * @brief A static object that provides various "stock" menus for use throughout the application
 */
class MenuShared : public QObject {
	Q_OBJECT
public:
	MenuShared();
	virtual ~MenuShared() override;

	static void create_instance();
	static void destroy_instance();

	void retranslate();

	void add_items_for_new_menu(Menu *m);
	void add_items_for_edit_menu(Menu *m, bool for_clips);
	void add_items_for_addable_objects_menu(Menu *m);
	void add_items_for_in_out_menu(Menu *m);
	void add_color_coding_menu(Menu *m);
	void add_items_for_clip_edit_menu(Menu *m);
	void add_items_for_time_ruler_menu(Menu *m);

	void about_to_show_time_ruler_actions(const Rational &timebase);

	static MenuShared *instance();

	QAction *edit_delete_item()
	{
		return edit_delete_item_;
	}

public slots:
	void delete_selected_triggered();

private:
	// "New" menu shared items
	QAction *new_project_item_;
	QAction *new_sequence_item_;
	QAction *new_folder_item_;

	// "Edit" menu shared items
	QAction *edit_cut_item_;
	QAction *edit_copy_item_;
	QAction *edit_paste_item_;
	QAction *edit_paste_insert_item_;
	QAction *edit_duplicate_item_;
	QAction *edit_rename_item_;
	QAction *edit_delete_item_;
	QAction *edit_ripple_delete_item_;
	QAction *edit_split_item_;
	QAction *edit_speedduration_item_;

	// List of addable items
	QVector<QAction *> addable_items_;

	// "In/Out" menu shared items
	QAction *inout_set_in_item_;
	QAction *inout_set_out_item_;
	QAction *inout_reset_in_item_;
	QAction *inout_reset_out_item_;
	QAction *inout_clear_inout_item_;

	// "Clip Edit" menu shared items
	QAction *clip_add_default_transition_item_;
	QAction *clip_link_unlink_item_;
	QAction *clip_enable_disable_item_;
	QAction *clip_nest_item_;

	// TimeRuler menu shared items
	QActionGroup *frame_view_mode_group_;
	QAction *view_timecode_view_dropframe_item_;
	QAction *view_timecode_view_nondropframe_item_;
	QAction *view_timecode_view_seconds_item_;
	QAction *view_timecode_view_frames_item_;
	QAction *view_timecode_view_milliseconds_item_;

	// Color coding menu items
	ColorLabelMenu *color_coding_menu_;

	static MenuShared *instance_;

private slots:
	void split_at_playhead_triggered();

	void ripple_delete_triggered();

	void set_in_triggered();

	void set_out_triggered();

	void reset_in_triggered();

	void reset_out_triggered();

	void clear_in_out_triggered();

	void toggle_links_triggered();

	void cut_triggered();

	void copy_triggered();

	void paste_triggered();

	void paste_insert_triggered();

	void duplicate_triggered();

	void rename_selected_triggered();

	void enable_disable_triggered();

	void nest_triggered();

	void default_transition_triggered();

	/**
   * @brief A slot for the timecode display menu items
   *
   * Assumes a QAction* sender() and its data() is a member of enum Timecode::Display. Uses the data() to signal a
   * timecode change throughout the rest of the application.
   */
	void timecode_display_triggered();

	void color_label_triggered(int color_index);

	void speed_duration_triggered();

	void addable_item_triggered();
};

}

#endif // OAK_MENUSHARED_H
