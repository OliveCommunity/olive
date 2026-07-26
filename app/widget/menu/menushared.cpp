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

#include "menushared.h"

#include <QActionGroup>

#include "core.h"
#include "panel/panelmanager.h"
#include "panel/timeline/timeline.h"
#include "window/mainwindow/mainwindow.h"

#include "oakengine/undo.h"
namespace olive
{

MenuShared *MenuShared::instance_ = nullptr;

MenuShared::MenuShared()
{
	// "New" menu shared items
	new_project_item_ = Menu::create_item(this, "newproj", Core::instance(),
										 &Core::create_new_project, tr("Ctrl+N"));
	new_sequence_item_ = Menu::create_item(this, "newseq", Core::instance(),
										  &Core::create_new_sequence,
										  tr("Ctrl+Shift+N"));
	new_folder_item_ = Menu::create_item(this, "newfolder", Core::instance(),
										&Core::create_new_folder);

	// "Edit" menu shared items
	edit_cut_item_ = Menu::create_item(this, "cut", this,
									  &MenuShared::cut_triggered, tr("Ctrl+X"));
	edit_copy_item_ = Menu::create_item(
		this, "copy", this, &MenuShared::copy_triggered, tr("Ctrl+C"));
	edit_paste_item_ = Menu::create_item(
		this, "paste", this, &MenuShared::paste_triggered, tr("Ctrl+V"));
	edit_paste_insert_item_ =
		Menu::create_item(this, "pasteinsert", this,
						 &MenuShared::paste_insert_triggered, tr("Ctrl+Shift+V"));
	edit_duplicate_item_ = Menu::create_item(
		this, "duplicate", this, &MenuShared::duplicate_triggered, tr("Ctrl+D"));
	edit_rename_item_ = Menu::create_item(
		this, "rename", this, &MenuShared::rename_selected_triggered, tr("F2"));
	edit_delete_item_ = Menu::create_item(
		this, "delete", this, &MenuShared::delete_selected_triggered, tr("Del"));
	edit_ripple_delete_item_ =
		Menu::create_item(this, "rippledelete", this,
						 &MenuShared::ripple_delete_triggered, tr("Shift+Del"));
	edit_split_item_ = Menu::create_item(this, "split", this,
										&MenuShared::split_at_playhead_triggered,
										tr("Ctrl+K"));
	edit_speedduration_item_ =
		Menu::create_item(this, "speeddur", this,
						 &MenuShared::speed_duration_triggered, tr("Ctrl+R"));

	// List of addable items
	for (int i = 0; i < Tool::k_addable_count; i++) {
		Tool::AddableObject t = static_cast<Tool::AddableObject>(i);
		QAction *a = Menu::create_item(
			this, QStringLiteral("add:%1").arg(Tool::get_addable_object_id(t)),
			this, &MenuShared::addable_item_triggered);
		a->setData(t);
		addable_items_.append(a);
	}

	// "In/Out" menu shared items
	inout_set_in_item_ = Menu::create_item(this, "setinpoint", this,
										  &MenuShared::set_in_triggered, tr("I"));
	inout_set_out_item_ = Menu::create_item(
		this, "setoutpoint", this, &MenuShared::set_out_triggered, tr("O"));
	inout_reset_in_item_ =
		Menu::create_item(this, "resetin", this, &MenuShared::reset_in_triggered);
	inout_reset_out_item_ = Menu::create_item(this, "resetout", this,
											 &MenuShared::reset_out_triggered);
	inout_clear_inout_item_ = Menu::create_item(
		this, "clearinout", this, &MenuShared::clear_in_out_triggered, tr("G"));

	// "Clip Edit" menu shared items
	clip_add_default_transition_item_ = Menu::create_item(
		this, "deftransition", this, &MenuShared::default_transition_triggered,
		tr("Ctrl+Shift+D"));
	clip_link_unlink_item_ = Menu::create_item(this, "linkunlink", this,
											  &MenuShared::toggle_links_triggered,
											  tr("Ctrl+L"));
	clip_enable_disable_item_ =
		Menu::create_item(this, "enabledisable", this,
						 &MenuShared::enable_disable_triggered, tr("Shift+E"));
	clip_nest_item_ =
		Menu::create_item(this, "nest", this, &MenuShared::nest_triggered);

	// TimeRuler menu shared items
	frame_view_mode_group_ = new QActionGroup(this);

	view_timecode_view_dropframe_item_ = Menu::create_item(
		this, "modedropframe", this, &MenuShared::timecode_display_triggered);
	view_timecode_view_dropframe_item_->setData(Timecode::k_timecode_drop_frame);
	view_timecode_view_dropframe_item_->setCheckable(true);
	frame_view_mode_group_->addAction(view_timecode_view_dropframe_item_);

	view_timecode_view_nondropframe_item_ = Menu::create_item(
		this, "modenondropframe", this, &MenuShared::timecode_display_triggered);
	view_timecode_view_nondropframe_item_->setData(
		Timecode::k_timecode_non_drop_frame);
	view_timecode_view_nondropframe_item_->setCheckable(true);
	frame_view_mode_group_->addAction(view_timecode_view_nondropframe_item_);

	view_timecode_view_seconds_item_ = Menu::create_item(
		this, "modeseconds", this, &MenuShared::timecode_display_triggered);
	view_timecode_view_seconds_item_->setData(Timecode::k_timecode_seconds);
	view_timecode_view_seconds_item_->setCheckable(true);
	frame_view_mode_group_->addAction(view_timecode_view_seconds_item_);

	view_timecode_view_frames_item_ = Menu::create_item(
		this, "modeframes", this, &MenuShared::timecode_display_triggered);
	view_timecode_view_frames_item_->setData(Timecode::k_frames);
	view_timecode_view_frames_item_->setCheckable(true);
	frame_view_mode_group_->addAction(view_timecode_view_frames_item_);

	view_timecode_view_milliseconds_item_ = Menu::create_item(
		this, "milliseconds", this, &MenuShared::timecode_display_triggered);
	view_timecode_view_milliseconds_item_->setData(Timecode::k_milliseconds);
	view_timecode_view_milliseconds_item_->setCheckable(true);
	frame_view_mode_group_->addAction(view_timecode_view_milliseconds_item_);

	// Color coding menu items
	color_coding_menu_ = new ColorLabelMenu();
	connect(color_coding_menu_, &ColorLabelMenu::color_selected, this,
			&MenuShared::color_label_triggered);

	retranslate();
}

MenuShared::~MenuShared()
{
	delete color_coding_menu_;
}

void MenuShared::create_instance()
{
	instance_ = new MenuShared();
}

void MenuShared::destroy_instance()
{
	delete instance_;
}

void MenuShared::add_items_for_new_menu(Menu *m)
{
	m->addAction(new_project_item_);
	m->addSeparator();
	m->addAction(new_sequence_item_);
	m->addAction(new_folder_item_);
}

void MenuShared::add_items_for_edit_menu(Menu *m, bool for_clips)
{
	m->addAction(reinterpret_cast<QAction*>(oakengine_undo_undo_action()));
	m->addAction(reinterpret_cast<QAction*>(oakengine_undo_redo_action()));

	m->addSeparator();

	m->addAction(edit_cut_item_);
	m->addAction(edit_copy_item_);
	m->addAction(edit_paste_item_);
	m->addAction(edit_paste_insert_item_);
	m->addAction(edit_duplicate_item_);
	m->addAction(edit_rename_item_);
	m->addAction(edit_delete_item_);

	if (for_clips) {
		m->addAction(edit_ripple_delete_item_);
		m->addAction(edit_split_item_);
		m->addAction(edit_speedduration_item_);

		m->addSeparator();

		m->addAction(clip_add_default_transition_item_);
		m->addAction(clip_link_unlink_item_);
		m->addAction(clip_enable_disable_item_);
		m->addAction(clip_nest_item_);
	}
}

void MenuShared::add_items_for_addable_objects_menu(Menu *m)
{
	for (QAction *a : qAsConst(addable_items_)) {
		a->setChecked((a->data().toInt() ==
					   Core::instance()->get_selected_addable_object()));
		m->addAction(a);
	}
}

void MenuShared::add_items_for_in_out_menu(Menu *m)
{
	m->addAction(inout_set_in_item_);
	m->addAction(inout_set_out_item_);
	m->addSeparator();
	m->addAction(inout_reset_in_item_);
	m->addAction(inout_reset_out_item_);
	m->addAction(inout_clear_inout_item_);
}

void MenuShared::add_color_coding_menu(Menu *m)
{
	m->addMenu(color_coding_menu_);
}

void MenuShared::add_items_for_clip_edit_menu(Menu *m)
{
	m->addAction(clip_add_default_transition_item_);
	m->addAction(clip_link_unlink_item_);
	m->addAction(clip_enable_disable_item_);
	m->addAction(clip_nest_item_);
}

void MenuShared::add_items_for_time_ruler_menu(Menu *m)
{
	m->addAction(view_timecode_view_dropframe_item_);
	m->addAction(view_timecode_view_nondropframe_item_);
	m->addAction(view_timecode_view_seconds_item_);
	m->addAction(view_timecode_view_frames_item_);
	m->addAction(view_timecode_view_milliseconds_item_);
}

void MenuShared::about_to_show_time_ruler_actions(const Rational &timebase)
{
	QList<QAction *> timecode_display_actions =
		frame_view_mode_group_->actions();
	Timecode::Display current_timecode_display =
		Core::instance()->get_timecode_display();

	// Only show the drop-frame option if the timebase is drop-frame
	view_timecode_view_dropframe_item_->setVisible(
		!timebase.isNull() && Timecode::timebase_is_drop_frame(timebase));

	if (!view_timecode_view_dropframe_item_->isVisible() &&
		current_timecode_display == Timecode::k_timecode_drop_frame) {
		// If the current setting is drop-frame, correct to non-drop frame
		current_timecode_display = Timecode::k_timecode_non_drop_frame;
	}

	foreach (QAction *a, timecode_display_actions) {
		if (a->data() == current_timecode_display) {
			a->setChecked(true);
			break;
		}
	}
}

MenuShared *MenuShared::instance()
{
	return instance_;
}

void MenuShared::split_at_playhead_triggered()
{
	TimelinePanel *timeline =
		PanelManager::instance()->most_recently_focused<TimelinePanel>();

	if (timeline != nullptr) {
		timeline->split_at_playhead();
	}
}

void MenuShared::delete_selected_triggered()
{
	PanelManager::instance()->currently_focused()->delete_selected();
}

void MenuShared::ripple_delete_triggered()
{
	PanelManager::instance()->currently_focused()->ripple_delete();
}

void MenuShared::set_in_triggered()
{
	PanelManager::instance()->currently_focused()->set_in();
}

void MenuShared::set_out_triggered()
{
	PanelManager::instance()->currently_focused()->set_out();
}

void MenuShared::reset_in_triggered()
{
	PanelManager::instance()->currently_focused()->reset_in();
}

void MenuShared::reset_out_triggered()
{
	PanelManager::instance()->currently_focused()->reset_out();
}

void MenuShared::clear_in_out_triggered()
{
	PanelManager::instance()->currently_focused()->clear_in_out();
}

void MenuShared::toggle_links_triggered()
{
	PanelManager::instance()->currently_focused()->toggle_links();
}

void MenuShared::cut_triggered()
{
	PanelManager::instance()->currently_focused()->cut_selected();
}

void MenuShared::copy_triggered()
{
	PanelManager::instance()->currently_focused()->copy_selected();
}

void MenuShared::paste_triggered()
{
	PanelManager::instance()->currently_focused()->paste();
}

void MenuShared::paste_insert_triggered()
{
	PanelManager::instance()->currently_focused()->paste_insert();
}

void MenuShared::duplicate_triggered()
{
	PanelManager::instance()->currently_focused()->duplicate();
}

void MenuShared::rename_selected_triggered()
{
	PanelManager::instance()->currently_focused()->rename_selected();
}

void MenuShared::enable_disable_triggered()
{
	PanelManager::instance()->currently_focused()->toggle_selected_enabled();
}

void MenuShared::nest_triggered()
{
	PanelManager::instance()
		->most_recently_focused<TimelinePanel>()
		->nest_selected_clips();
}

void MenuShared::default_transition_triggered()
{
	PanelManager::instance()
		->most_recently_focused<TimelinePanel>()
		->add_default_transitions_to_selected();
}

void MenuShared::timecode_display_triggered()
{
	// Assume the sender is a QAction
	QAction *action = static_cast<QAction *>(sender());

	// Assume its data() is a member of Timecode::Display
	Timecode::Display display =
		static_cast<Timecode::Display>(action->data().toInt());

	// Set the current display mode
	Core::instance()->set_timecode_display(display);
}

void MenuShared::color_label_triggered(int color_index)
{
	PanelManager::instance()->currently_focused()->set_color_label(color_index);
}

void MenuShared::speed_duration_triggered()
{
	TimelinePanel *timeline =
		PanelManager::instance()->most_recently_focused<TimelinePanel>();

	if (timeline) {
		timeline->show_speed_duration_dialog_for_selected_clips();
	}
}

void MenuShared::addable_item_triggered()
{
	QAction *a = static_cast<QAction *>(sender());
	Tool::AddableObject i = static_cast<Tool::AddableObject>(a->data().toInt());
	Core::instance()->set_tool(Tool::k_add);
	Core::instance()->set_selected_addable_object(i);
}

void MenuShared::retranslate()
{
	// "New" menu shared items
	new_project_item_->setText(tr("&Project"));
	new_sequence_item_->setText(tr("&Sequence"));
	new_folder_item_->setText(tr("&Folder"));

	// "Edit" menu shared items
	edit_cut_item_->setText(tr("Cu&t"));
	edit_copy_item_->setText(tr("Cop&y"));
	edit_paste_item_->setText(tr("&Paste"));
	edit_paste_insert_item_->setText(tr("Paste Insert"));
	edit_duplicate_item_->setText(tr("Duplicate"));
	edit_rename_item_->setText(tr("Rename"));
	edit_delete_item_->setText(tr("Delete"));
	edit_ripple_delete_item_->setText(tr("Ripple Delete"));
	edit_split_item_->setText(tr("Split"));
	edit_speedduration_item_->setText(tr("Speed/Duration"));

	for (QAction *a : qAsConst(addable_items_)) {
		a->setText(Tool::get_addable_object_name(
			static_cast<Tool::AddableObject>(a->data().toInt())));
	}

	// "In/Out" menu shared items
	inout_set_in_item_->setText(tr("Set In Point"));
	inout_set_out_item_->setText(tr("Set Out Point"));
	inout_reset_in_item_->setText(tr("Reset In Point"));
	inout_reset_out_item_->setText(tr("Reset Out Point"));
	inout_clear_inout_item_->setText(tr("Clear In/Out Point"));

	// "Clip Edit" menu shared items
	clip_add_default_transition_item_->setText(tr("Add Default Transition"));
	clip_link_unlink_item_->setText(tr("Link/Unlink"));
	clip_enable_disable_item_->setText(tr("Enable/Disable"));
	clip_nest_item_->setText(tr("Nest"));

	// TimeRuler menu shared items
	view_timecode_view_frames_item_->setText(tr("Frames"));
	view_timecode_view_dropframe_item_->setText(tr("Drop Frame"));
	view_timecode_view_nondropframe_item_->setText(tr("Non-Drop Frame"));
	view_timecode_view_milliseconds_item_->setText(tr("Milliseconds"));
	view_timecode_view_seconds_item_->setText(tr("Seconds"));
}

}
