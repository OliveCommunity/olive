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

#ifndef OAK_SLIDERDISPLAYTYPEAPP_H
#define OAK_SLIDERDISPLAYTYPEAPP_H

namespace olive
{

/**
 * @brief App-side mirror of engine node/sliderdisplaytype.h
 *
 * Nodes reference these enums in their input properties ("view"); the
 * slider widgets in app/widget/slider use the same values to render.
 * Ordinals MUST stay in sync with the engine definitions: the values cross
 * the C ABI as ints inside node input properties. Enumerator order is
 * ABI/feature compatible with the previous FloatSlider::DisplayType and
 * RationalSlider::DisplayType. Update both sides together.
 */
namespace slider
{

enum FloatDisplayType { k_normal, k_decibel, k_percentage };

enum RationalDisplayType { k_time, k_float, k_rational };

} // namespace slider

// Ordinal sync guards against engine node/sliderdisplaytype.h.
static_assert(slider::k_percentage == 2,
			  "slider::FloatDisplayType out of sync with engine");
static_assert(slider::k_rational == 2,
			  "slider::RationalDisplayType out of sync with engine");

} // namespace olive

#endif // OAK_SLIDERDISPLAYTYPEAPP_H
