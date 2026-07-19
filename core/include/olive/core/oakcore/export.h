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

#ifndef OAKCORE_EXPORT_H
#define OAKCORE_EXPORT_H

/**
 * @file export.h
 * @brief Symbol visibility macros for liboakcore
 *
 * liboakcore exposes a pure C ABI: every public function is declared with
 * OAKCORE_API and everything else is hidden. No C++ symbols cross the
 * library boundary.
 */

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef OAKCORE_BUILD
    #define OAKCORE_API __declspec(dllexport)
  #else
    #define OAKCORE_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #define OAKCORE_API __attribute__((visibility("default")))
#else
  #define OAKCORE_API
#endif

#endif /* OAKCORE_EXPORT_H */
