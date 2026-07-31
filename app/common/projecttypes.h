/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#ifndef OAK_PROJECTTYPES_H
#define OAK_PROJECTTYPES_H

namespace olive
{

/**
 * @brief App-side mirror of the engine's olive::Project enum(s)
 * (engine/node/project.h).
 *
 * Enumerator ordinals must stay in sync with the engine enum: the C ABI
 * (oakengine_project_get_cache_location_setting() etc.) transports these
 * values as plain ints.
 */
class Project {
public:
	enum CacheSetting {
		k_cache_use_default_location,
		k_cache_store_alongside_project,
		k_cache_custom_path
	};
};

}

#endif // OAK_PROJECTTYPES_H
