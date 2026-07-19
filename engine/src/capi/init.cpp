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

#include "oakengine/init.h"

#include <QCoreApplication>
#include <QGuiApplication>

#include "codec/conformmanager.h"
#include "codec/proxymanager.h"
#include "config/config.h"
#include "coreengine.h"
#include "node/color/colormanager/colormanager.h"
#include "node/factory.h"
#include "node/project/serializer/serializer.h"
#include "render/diskmanager.h"
#include "render/framemanager.h"
#include "render/rendermanager.h"
#include "task/taskmanager.h"

namespace
{

// Currently initialized OAKENGINE_INIT_* bits.
int g_flags = 0;

// Qt requires exactly one application object for the process and cannot
// safely destroy and re-create one, so when the library has to create it the
// object (and its argv storage) is leaked intentionally.
//
// This is a QGuiApplication, not a plain QCoreApplication: EngineCore's
// UndoStack member creates QActions in its constructor, and Qt6 QActions
// dereference QGuiApplication private state (they crash without one). A
// QGuiApplication is still a QCoreApplication, and with the offscreen QPA
// plugin (defaulted below, overridable by the caller) no display
// connection, window or other UI is ever created.
void ensure_qcoreapplication()
{
	if (QCoreApplication::instance()) {
		return;
	}

	// Same default as the gtest harness (tests/gtest/main.cpp).
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}

	static int argc = 1;
	static char app_name[] = "oakengine";
	static char *argv[] = { app_name, nullptr };
	new QGuiApplication(argc, argv);

	// Same identity as the editor (app/main.cpp) so Config and friends land
	// in the same locations.
	QCoreApplication::setOrganizationName(QStringLiteral("oakvideoeditor.org"));
	QCoreApplication::setApplicationName(QStringLiteral("Oak Video Editor"));
}

} // namespace

extern "C"
{

int oakengine_init(int flags)
{
	if (flags == 0 || (flags & ~(OAKENGINE_INIT_HEADLESS | OAKENGINE_INIT_RENDER)) != 0) {
		return OAKENGINE_E_INVALID;
	}

	if ((flags & OAKENGINE_INIT_HEADLESS) != 0 &&
		(g_flags & OAKENGINE_INIT_HEADLESS) == 0) {
		ensure_qcoreapplication();

		// EngineCore shell: provides EngineCore::instance() and the global
		// undo stack. Never deleted -- EngineCore does not reset instance_ in
		// its destructor, so deleting would leave a dangling singleton. This
		// mirrors the render worker's headless bootstrap.
		if (!olive::EngineCore::instance()) {
			new olive::EngineCore(olive::EngineCore::CoreParams());
		}

		olive::Config::load();
		olive::NodeFactory::initialize();
		olive::ColorManager::set_up_default_config();
		olive::TaskManager::create_instance();
		olive::ConformManager::create_instance();
		olive::ProxyManager::create_instance();
		olive::FrameManager::create_instance();
		// Not in EngineCore::start(), but required headless: loading a project
		// touches PlaybackCache::load_state() which dereferences
		// DiskManager::instance() (the render worker creates it for the same
		// reason).
		olive::DiskManager::create_instance();
		olive::ProjectSerializer::initialize();

		g_flags |= OAKENGINE_INIT_HEADLESS;
	}

	if ((flags & OAKENGINE_INIT_RENDER) != 0 &&
		(g_flags & OAKENGINE_INIT_RENDER) == 0) {
		olive::RenderManager::create_instance();

		g_flags |= OAKENGINE_INIT_RENDER;
	}

	return OAKENGINE_OK;
}

int oakengine_shutdown(void)
{
	// Reverse of oakengine_init(), mirroring EngineCore::stop(). The
	// QCoreApplication and the EngineCore shell intentionally survive (see
	// oakengine_init()).
	if ((g_flags & OAKENGINE_INIT_RENDER) != 0) {
		olive::RenderManager::destroy_instance();
	}

	if ((g_flags & OAKENGINE_INIT_HEADLESS) != 0) {
		olive::Config::save();

		olive::ProjectSerializer::destroy();

		olive::DiskManager::destroy_instance();

		olive::ConformManager::destroy_instance();

		olive::ProxyManager::destroy_instance();

		olive::FrameManager::destroy_instance();

		olive::TaskManager::destroy_instance();

		olive::NodeFactory::destroy();
	}

	g_flags = 0;

	return OAKENGINE_OK;
}

int oakengine_init_flags(void)
{
	return g_flags;
}

} // extern "C"
