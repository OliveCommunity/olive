/***

  Oak Video Editor - Non-Linear Video Editor
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

#ifndef OAK_QTUTILS_H
#define OAK_QTUTILS_H

#include <chrono>
#include <cstdint>
#include <filesystem>

namespace olive
{

/**
 * @brief Qt-free replacement for the former Qt-based QtUtils helper class.
 *
 * Only the functions actually used by the engine are kept. The UI-bound
 * helpers (font metrics, combo boxes, QFrame creation, QColor conversion)
 * remain available to Qt callers through the original Qt version.
 */
class QtUtils {
public:
	/**
	 * @brief Convert a pointer to an integer value that can be passed around as data
	 */
	static uintptr_t ptr_to_value(void *ptr)
	{
		return reinterpret_cast<uintptr_t>(ptr);
	}

	/**
	 * @brief Convert an integer produced by ptr_to_value() back to a pointer of any kind
	 */
	template <class T> static T *value_to_ptr(uintptr_t value)
	{
		return reinterpret_cast<T *>(value);
	}

	/**
	 * @brief Walk the parent chain of an object looking for an ancestor of type T
	 *
	 * Works with any object type that provides a parent() member returning a
	 * pointer to the same (or a base) type, e.g. QObject.
	 */
	template <typename T, typename Obj>
	static T *get_parent_of_type(Obj *child)
	{
		auto *t = child ? child->parent() : nullptr;

		while (t) {
			if (T *p = dynamic_cast<T *>(t)) {
				return p;
			}
			t = t->parent();
		}

		return nullptr;
	}

	/**
	 * @brief Get the creation (birth) time of a file
	 *
	 * Falls back to the last metadata change / modification time on
	 * filesystems that do not record birth times.
	 */
	static std::chrono::system_clock::time_point
	get_creation_date(const std::filesystem::path &path);
};

}

#endif // OAK_QTUTILS_H
