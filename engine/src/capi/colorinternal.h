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

#ifndef OAKENGINE_COLORINTERNAL_H
#define OAKENGINE_COLORINTERNAL_H

// Internal (not installed) shared definition of the opaque color-processor
// handle between color.cpp and the other capi translation units.  The
// public header (oakengine/color.h) only forward-declares
// OakEngineColorProcessor; capi code that needs to unwrap the handle (e.g.
// renderer.cpp feeding the render cacher) includes this header.

#include "render/colorprocessor.h"

// Owned handle layout: the opaque C type is a heap box around the engine's
// shared pointer (matching the refcounting the C++ API uses).
struct OakEngineColorProcessor {
	olive::ColorProcessorPtr ptr;
};

#endif // OAKENGINE_COLORINTERNAL_H
