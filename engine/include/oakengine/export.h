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

#ifndef OAKENGINE_EXPORT_H
#define OAKENGINE_EXPORT_H

/**
 * @file export.h
 * @brief Symbol visibility macros for liboakengine
 *
 * liboakengine is transitioning to a pure C ABI: every public C function is
 * declared with OAKENGINE_API. While the migration is in flight the library
 * is still built with default symbol visibility, so the legacy C++ symbols
 * remain exported alongside the C ABI.
 */
#if defined(OAKENGINE_STATIC)
  /* Internal consumers link the engine object files directly (oakengine-obj)
     instead of the shared library; no dllimport/dllexport is wanted. */
  #define OAKENGINE_API
#elif defined(_WIN32) || defined(__CYGWIN__)
  #ifdef OAKENGINE_BUILD
    #define OAKENGINE_API __declspec(dllexport)
  #else
    #define OAKENGINE_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #define OAKENGINE_API __attribute__((visibility("default")))
#else
  #define OAKENGINE_API
#endif

#endif /* OAKENGINE_EXPORT_H */
