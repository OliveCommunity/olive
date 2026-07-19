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

#include "icons.h"

namespace olive
{

/// Works in conjunction with `genicons.sh` to generate and utilize icons of specific sizes
const int icon_size_count = 4;
const int icon_sizes[] = { 16, 32, 64, 128 };

/// Internal icon library for use throughout Olive without having to regenerate constantly
QIcon icon::go_to_start;
QIcon icon::prev_frame;
QIcon icon::play;
QIcon icon::pause;
QIcon icon::next_frame;
QIcon icon::go_to_end;
QIcon icon::New;
QIcon icon::open;
QIcon icon::save;
QIcon icon::undo;
QIcon icon::redo;
QIcon icon::tree_view;
QIcon icon::list_view;
QIcon icon::icon_view;
QIcon icon::tool_pointer;
QIcon icon::tool_edit;
QIcon icon::tool_ripple;
QIcon icon::tool_rolling;
QIcon icon::tool_razor;
QIcon icon::tool_slip;
QIcon icon::tool_slide;
QIcon icon::tool_hand;
QIcon icon::tool_transition;
QIcon icon::tool_track_select;
QIcon icon::folder;
QIcon icon::sequence;
QIcon icon::video;
QIcon icon::audio;
QIcon icon::image;
QIcon icon::mini_map;
QIcon icon::tri_up;
QIcon icon::tri_left;
QIcon icon::tri_down;
QIcon icon::tri_right;
QIcon icon::text_bold;
QIcon icon::text_italic;
QIcon icon::text_underline;
QIcon icon::text_strikethrough;
QIcon icon::text_small_caps;
QIcon icon::text_align_left;
QIcon icon::text_align_right;
QIcon icon::text_align_center;
QIcon icon::text_align_justify;
QIcon icon::text_align_top;
QIcon icon::text_align_bottom;
QIcon icon::text_align_middle;
QIcon icon::snapping;
QIcon icon::zoom_in;
QIcon icon::zoom_out;
QIcon icon::record;
QIcon icon::add;
QIcon icon::error;
QIcon icon::dir_up;
QIcon icon::clock;
QIcon icon::diamond;
QIcon icon::plus;
QIcon icon::minus;
QIcon icon::add_effect;
QIcon icon::eye_opened;
QIcon icon::eye_closed;
QIcon icon::lock_opened;
QIcon icon::lock_closed;
QIcon icon::pencil;
QIcon icon::subtitles;
QIcon icon::color_picker;

void icon::load_all(const QString &theme)
{
	go_to_start = create(theme, "prev");
	prev_frame = create(theme, "rew");
	play = create(theme, "play");
	pause = create(theme, "pause");
	next_frame = create(theme, "ff");
	go_to_end = create(theme, "next");

	New = create(theme, "new");
	open = create(theme, "open");
	save = create(theme, "save");
	undo = create(theme, "undo");
	redo = create(theme, "redo");
	tree_view = create(theme, "treeview");
	list_view = create(theme, "listview");
	icon_view = create(theme, "iconview");

	tool_pointer = create(theme, "arrow");
	tool_edit = create(theme, "beam");
	tool_ripple = create(theme, "ripple");
	tool_rolling = create(theme, "rolling");
	tool_razor = create(theme, "razor");
	tool_slip = create(theme, "slip");
	tool_slide = create(theme, "slide");
	tool_hand = create(theme, "hand");
	tool_transition = create(theme, "transition-tool");
	tool_track_select = create(theme, "track-tool");

	folder = create(theme, "folder");
	sequence = create(theme, "sequence");
	video = create(theme, "videosource");
	audio = create(theme, "audiosource");
	image = create(theme, "imagesource");

	mini_map = create(theme, "map");

	tri_up = create(theme, "tri-up");
	tri_left = create(theme, "tri-left");
	tri_down = create(theme, "tri-down");
	tri_right = create(theme, "tri-right");

	text_bold = create(theme, "text-bold");
	text_italic = create(theme, "text-italic");
	text_underline = create(theme, "text-underline");
	text_strikethrough = create(theme, "text-strikethrough");
	text_small_caps = create(theme, "text-small-caps");
	text_align_left = create(theme, "align-left");
	text_align_right = create(theme, "align-right");
	text_align_center = create(theme, "align-center");
	text_align_justify = create(theme, "align-justify-all");
	text_align_top = create(theme, "align-v-top");
	text_align_bottom = create(theme, "align-v-bottom");
	text_align_middle = create(theme, "align-v-middle");

	snapping = create(theme, "magnet");
	zoom_in = create(theme, "zoomin");
	zoom_out = create(theme, "zoomout");
	record = create(theme, "record");
	add = create(theme, "add-button");
	error = create(theme, "error");
	dir_up = create(theme, "dirup");
	clock = create(theme, "clock");
	diamond = create(theme, "diamond");
	plus = create(theme, "plus");
	minus = create(theme, "minus");
	add_effect = create(theme, "add-effect");
	color_picker = create(theme, "color-picker");

	eye_opened = create(theme, "eye-opened");
	eye_closed = create(theme, "eye-closed");
	lock_opened = create(theme, "lock-opened");
	lock_closed = create(theme, "lock-closed");

	pencil = create(theme, "text-edit");
	subtitles = create(theme, "subtitles");
}

QIcon icon::create(const QString &theme, const QString &name)
{
	QIcon icon;

	for (int i = 0; i < icon_size_count; i++) {
		icon.addFile(QStringLiteral("%1/png/%2.%3.png")
						 .arg(theme, name, QString::number(icon_sizes[i])),
					 QSize(icon_sizes[i], icon_sizes[i]), QIcon::Normal);
		icon.addFile(QStringLiteral("%1/png/%2.%3.disabled.png")
						 .arg(theme, name, QString::number(icon_sizes[i])),
					 QSize(icon_sizes[i], icon_sizes[i]), QIcon::Disabled);
	}

	return icon;
}

}
