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

#ifndef OAK_ICONS_H
#define OAK_ICONS_H

#include <QIcon>

#include "oakutil/define.h"

namespace olive
{

namespace icon
{

// Playback Icons
extern QIcon go_to_start;
extern QIcon prev_frame;
extern QIcon play;
extern QIcon pause;
extern QIcon next_frame;
extern QIcon go_to_end;

// Project Management Toolbar Icons
extern QIcon New;
extern QIcon open;
extern QIcon save;
extern QIcon undo;
extern QIcon redo;
extern QIcon tree_view;
extern QIcon list_view;
extern QIcon icon_view;

// Tool Icons
extern QIcon tool_pointer;
extern QIcon tool_edit;
extern QIcon tool_ripple;
extern QIcon tool_rolling;
extern QIcon tool_razor;
extern QIcon tool_slip;
extern QIcon tool_slide;
extern QIcon tool_hand;
extern QIcon tool_transition;
extern QIcon tool_track_select;

// Project Icons
extern QIcon folder;
extern QIcon sequence;
extern QIcon video;
extern QIcon audio;
extern QIcon image;

// Node Icons
extern QIcon mini_map;

// Triangle Arrows
extern QIcon tri_up;
extern QIcon tri_left;
extern QIcon tri_down;
extern QIcon tri_right;

// Text
extern QIcon text_bold;
extern QIcon text_italic;
extern QIcon text_underline;
extern QIcon text_strikethrough;
extern QIcon text_small_caps;
extern QIcon text_align_left;
extern QIcon text_align_right;
extern QIcon text_align_center;
extern QIcon text_align_justify;
extern QIcon text_align_top;
extern QIcon text_align_bottom;
extern QIcon text_align_middle;

// Miscellaneous Icons
extern QIcon snapping;
extern QIcon zoom_in;
extern QIcon zoom_out;
extern QIcon record;
extern QIcon add;
extern QIcon error;
extern QIcon dir_up;
extern QIcon clock;
extern QIcon diamond;
extern QIcon plus;
extern QIcon minus;
extern QIcon add_effect;
extern QIcon eye_opened;
extern QIcon eye_closed;
extern QIcon lock_opened;
extern QIcon lock_closed;
extern QIcon pencil;
extern QIcon subtitles;
extern QIcon color_picker;

/**
 * @brief Look up a loaded icon by its resource name
 *
 * Engine-side metadata (e.g. Node::data(Node::icon)) carries icon identity as a
 * plain resource name string ("folder", "video", ...) so the headless engine
 * never has to depend on QIcon. This maps such a name back to the corresponding
 * globally loaded icon. Returns a null QIcon for unknown names.
 */
QIcon from_name(const QString &name);

/**
 * @brief Create an icon object loaded from file
 *
 * Using `name`, this function will load icon files to create an icon object that can be used throughout the
 * application.
 *
 * Olive's icons are stored in a very specific format. They are all sourced from SVGs, but stored as PNGs of various
 * sizes. See `app/ui/icons/genicons.sh`, as this script not only generates the multiple sizes but also the QRC file
 * used to compile the icons into the executable.
 *
 * This function is heavily tied into `genicons.sh` and will load all the different sized images (using the same
 * filename formatting and QRC resource directory) that `genicons.sh` generates into one QIcon file. If you change
 * either this function or `genicons.sh`, you will very likely have to change the other too.
 *
 * There is not much reason to call this outside of LoadAll() (which stores icons globally in memory so they don't
 * have to be reloaded each time a new object needs an icon).
 *
 * @param theme
 *
 * Name of the theme (used in the URL as the folder to load PNGs from)
 *
 * @param name
 *
 * Name of the icon (will correspond to the original SVG's filename with no path or extension)
 *
 * @return
 *
 * A QIcon object containing the various icon sizes loaded from resource
 */
QIcon create(const QString &theme, const QString &name);

/**
 * @brief Methodically load all Olive icons into global variables that can be accessed throughout the application
 *
 * It's recommended to load any UI icons here so they're ready at startup and don't need to be re-loaded upon each
 * use.
 */
void load_all(const QString &theme);

}

}

#endif // OAK_ICONS_H
