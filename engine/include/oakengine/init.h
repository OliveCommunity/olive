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

#ifndef OAKENGINE_INIT_H
#define OAKENGINE_INIT_H

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file init.h
 * @brief C ABI for engine process initialization and shutdown
 *
 * oakengine_init() brings up the UI-independent engine services a consumer of
 * liboakengine needs before touching projects or timelines. It never creates
 * any UI: no QApplication, no main window, no display connection. When no
 * application object exists yet, an offscreen QGuiApplication is created (Qt
 * requires exactly one application object; it is leaked intentionally because
 * Qt cannot safely re-create one). A QGuiApplication -- not a plain
 * QCoreApplication -- is required because EngineCore's undo stack creates
 * QActions in its constructor, and Qt6 QActions need QGuiApplication state;
 * with the offscreen QPA plugin (the default, overridable through
 * QT_QPA_PLATFORM) nothing graphical ever happens.
 *
 * The flag mask selects the service set:
 *
 *   - OAKENGINE_INIT_HEADLESS: Config, NodeFactory, ColorManager, TaskManager,
 *     ConformManager, ProxyManager, FrameManager, DiskManager,
 *     ProjectSerializer and a process-wide EngineCore shell (holds the global
 *     undo stack and the EngineCore::instance() pointer engine code
 *     dereferences). DiskManager is not part of EngineCore::start() but is
 *     required because loading a project touches PlaybackCache state which
 *     dereferences DiskManager::instance(). Everything here runs headless;
 *     no GL is required.
 *
 *   - OAKENGINE_INIT_RENDER: additionally creates the RenderManager. Only
 *     with this bit may a consumer end up needing a GL context (the actual
 *     render backends are dynamic engine plugins loaded on demand).
 *
 * Initialization is modelled on the render worker's headless bootstrap
 * (worker/workermain.cpp) rather than EngineCore::start(), because start()
 * unconditionally creates the RenderManager, starts the autorecovery timer
 * and reads the recent-projects list -- application behavior that does not
 * belong behind a library boundary.
 *
 * Conventions:
 *   - Return codes: 0 (OAKENGINE_OK) on success, a negative OAKENGINE_E_*
 *     error code on failure.
 *   - oakengine_init() is idempotent: calling it again is a no-op for flag
 *     bits already initialized and only brings up the missing bits (e.g.
 *     upgrading HEADLESS to HEADLESS|RENDER).
 *   - oakengine_shutdown() pairs with oakengine_init() and tears down the
 *     initialized services in reverse order. The QCoreApplication and the
 *     EngineCore shell are kept alive (see above), so oakengine_init() may be
 *     called again afterwards.
 */

/**
 * @brief Status and error codes shared by the init/project/timeline families.
 */
#define OAKENGINE_OK 0 /**< Success. */
#define OAKENGINE_E_INVALID (-1) /**< NULL handle or invalid argument. */
#define OAKENGINE_E_STATE (-2) /**< Call not valid in the current state. */
#define OAKENGINE_E_FAILED (-3) /**< The engine reported a failure. */
#define OAKENGINE_E_NOT_FOUND (-4) /**< Index out of range / entry not found. */

/** @brief Base headless engine services (no GL required). */
#define OAKENGINE_INIT_HEADLESS 0x01
/** @brief Render services on top of HEADLESS (may require GL). */
#define OAKENGINE_INIT_RENDER 0x02

/**
 * @brief Initialize the engine services selected by `flags`.
 *
 * `flags` is a bitmask of OAKENGINE_INIT_HEADLESS and/or
 * OAKENGINE_INIT_RENDER; 0 is invalid. Repeated calls are idempotent and may
 * add the RENDER bit to a running HEADLESS instance.
 *
 * @return OAKENGINE_OK on success, OAKENGINE_E_INVALID for an empty mask.
 */
OAKENGINE_API int oakengine_init(int flags);

/**
 * @brief Tear down the services brought up by oakengine_init().
 *
 * Safe to call when not initialized (a no-op then). The QCoreApplication and
 * the EngineCore shell survive shutdown intentionally.
 *
 * @return OAKENGINE_OK.
 */
OAKENGINE_API int oakengine_shutdown(void);

/**
 * @brief Current initialization state as a flag bitmask (0 = not initialized).
 *
 * Only services that are actually up are reported, e.g. after upgrading a
 * HEADLESS instance with the RENDER bit the result includes both bits.
 */
OAKENGINE_API int oakengine_init_flags(void);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_INIT_H */
