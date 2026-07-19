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

#ifndef OAK_SLIDERDISPLAYTYPE_H
#define OAK_SLIDERDISPLAYTYPE_H

namespace olive
{

/**
 * @brief Slider display type enums shared by nodes and widgets
 *
 * Nodes reference these enums in their input properties ("view"); the
 * slider widgets in app/widget/slider use the same values to render. The
 * definitions live in the engine layer so nodes do not depend on widget
 * headers. Enumerator order is ABI/feature compatible with the previous
 * FloatSlider::DisplayType and RationalSlider::DisplayType.
 */
namespace slider
{

enum FloatDisplayType { k_normal, k_decibel, k_percentage };

enum RationalDisplayType { k_time, k_float, k_rational };

} // namespace slider

} // namespace olive

#endif // OAK_SLIDERDISPLAYTYPE_H
