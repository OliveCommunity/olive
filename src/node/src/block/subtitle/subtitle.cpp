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

#include "subtitle.h"

namespace olive
{

#define super ClipBlock

const std::string SubtitleBlock::k_text_in = "text_in";

SubtitleBlock::SubtitleBlock()
{
	add_input(k_text_in, NodeValue::k_text,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable));

	set_input_flag(k_buffer_in, k_input_flag_hidden);
	set_input_flag(k_length_input, k_input_flag_hidden);
	set_input_flag(k_media_in_input, k_input_flag_hidden);
	set_input_flag(k_speed_input, k_input_flag_hidden);
	set_input_flag(k_reverse_input, k_input_flag_hidden);
	set_input_flag(k_maintain_audio_pitch_input, k_input_flag_hidden);

	// Undo block flag that hides in param view
	set_flag(k_dont_show_in_param_view, false);
}

std::string SubtitleBlock::name() const
{
	if (get_text().empty()) {
		return "Subtitle";
	} else {
		return get_text();
	}
}

std::string SubtitleBlock::id() const
{
	return "org.olivevideoeditor.Olive.subtitle";
}

std::string SubtitleBlock::description() const
{
	return "A time-based node representing a single subtitle element for a certain period of time.";
}

void SubtitleBlock::retranslate()
{
	super::retranslate();

	set_input_name(k_text_in, "Text");
}

}
