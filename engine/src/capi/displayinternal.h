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

#ifndef OAKENGINE_DISPLAYINTERNAL_H
#define OAKENGINE_DISPLAYINTERNAL_H

// Internal (not installed) helpers for bridging engine-internal TexturePtr /
// FramePtr to the opaque C ABI handles defined in capi/display.cpp.

#include "render/texture.h"
#include "codec/frame.h"

/**
 * @brief Wrap an existing TexturePtr into a refcounted ABI handle (refcount=1).
 * Returns nullptr if tp is null.
 */
void *oakengine_internal_wrap_texture(const olive::TexturePtr &tp);

/**
 * @brief Wrap an existing FramePtr into a refcounted ABI handle (refcount=1).
 * Returns nullptr if fp is null.
 */
void *oakengine_internal_wrap_frame(const olive::FramePtr &fp);

/**
 * @brief Unwrap an ABI texture handle to the underlying TexturePtr.
 * Returns empty TexturePtr if handle is null.
 */
olive::TexturePtr oakengine_internal_unwrap_texture(void *handle);

/**
 * @brief Unwrap an ABI frame handle to the underlying FramePtr.
 * Returns empty FramePtr if handle is null.
 */
olive::FramePtr oakengine_internal_unwrap_frame(void *handle);

#endif // OAKENGINE_DISPLAYINTERNAL_H
