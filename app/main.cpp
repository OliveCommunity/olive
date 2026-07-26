/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

/** \mainpage Oak Video Editor - Code Documentation
 *
 * This documentation is a primarily a developer resource. For information on using Oak Video Editor, visit the website
 * https://www.olivevideoeditor.org/
 *
 * Use the navigation above to find documentation on classes or source files.
 */

#include "oakengine/plugin.h"
#include "pluginSupport/olivehost.h"

#include <csignal>

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QIcon>
#include <QSurfaceFormat>

#include <oakengine/config.h>

#include "core.h"
#include "common/commandlineparser.h"
#include "common/debugapp.h"
#include <oakengine/serializer.h>
#include "version.h"
#include "window/mainwindow/mainwindow.h"

#ifdef _WIN32
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <Windows.h>
#endif

#ifdef USE_CRASHPAD
#include "common/crashpadinterface.h"
#endif // USE_CRASHPAD

static void config_error_handler(const char *title, const char *message,
								 void *)
{
	QWidget *parent = olive::Core::instance() ?
						olive::Core::instance()->main_window() :
						nullptr;
	QMessageBox::critical(parent, QString::fromUtf8(title),
						  QString::fromUtf8(message), QMessageBox::Ok);
}

int decompress_project(const QString &project)
{
	if (project.isEmpty()) {
		printf("%s\n", QCoreApplication::translate(
						   "main", "No project filename set to decompress")
						   .toUtf8()
						   .constData());
		return 1;
	}

	QFile project_file(project);
	if (!project_file.open(QFile::ReadOnly)) {
		printf("%s\n",
			   QCoreApplication::translate("main", "Failed to open file \"%1\"")
				   .arg(project)
				   .toUtf8()
				   .constData());
		return 1;
	}

	printf("%s\n",
		   QCoreApplication::translate("main", "Decompressing project...")
			   .toUtf8()
			   .constData());

	if (!oakengine_serializer_check_compressed(project.toUtf8().constData())) {
		printf("%s\n",
			   QCoreApplication::translate(
				   "main", "Failed to decompress, project may be corrupt")
				   .toUtf8()
				   .constData());
		return 1;
	}

	QByteArray b = project_file.readAll();

	project_file.close();

	QByteArray decompressed = qUncompress(b);

	if (decompressed.isEmpty()) {
		printf("%s\n",
			   QCoreApplication::translate(
				   "main", "Failed to decompress, project may be corrupt")
				   .toUtf8()
				   .constData());
		return 1;
	}

	QFileInfo info(project);

	QString filename;
	QString append;
	int append_num = 0;
	do {
		filename =
			info.dir().filePath(info.completeBaseName().append(append).append(
				QStringLiteral(".ovexml")));
		append_num++;
		append = QStringLiteral("-%1").arg(append_num);
	} while (QFileInfo::exists(filename));

	printf("%s\n",
		   QCoreApplication::translate("main", "Outputting to file \"%1\"")
			   .arg(filename)
			   .toUtf8()
			   .constData());

	QFile out(filename);
	if (!out.open(QFile::WriteOnly)) {
		printf("%s\n", QCoreApplication::translate(
						   "main", "Failed to open output file \"%1\"")
						   .arg(filename)
						   .toUtf8()
						   .constData());
		return 1;
	}

	out.write(decompressed);
	out.close();

	printf("%s\n",
		   QCoreApplication::translate("main", "Decompressed successfully")
			   .toUtf8()
			   .constData());
	return 0;
}

int main(int argc, char *argv[])
{
	// Set up debug handler
	qInstallMessageHandler(olive::debug_handler);

	// Ignore SIGPIPE so that writing to a render-worker process that has
	// already crashed/closed does not terminate the main application. QProcess
	// will report the failure through its normal error path instead.
#if !defined(_WIN32)
	signal(SIGPIPE, SIG_IGN);
#endif

	// Set application metadata
	QCoreApplication::setOrganizationName("oakvideoeditor.org");
	QCoreApplication::setOrganizationDomain("oakvideoeditor.org");
	QCoreApplication::setApplicationName("Oak Video Editor");
	QGuiApplication::setDesktopFileName("org.oakvideoeditor.Oak");
	QCoreApplication::setApplicationVersion(olive::k_app_version_long);

	//
	// Parse command line arguments
	//

	QVector<QString> args;
#if defined(_WIN32) && defined(UNICODE)
	int wargc;
	LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
	args.resize(wargc);
	for (int i = 0; i < wargc; i++) {
		args[i] = QString::fromWCharArray(wargv[i]);
	}
	LocalFree(wargv);
#else
	args.resize(argc);
	for (int i = 0; i < argc; i++) {
		args[i] = QString::fromLocal8Bit(argv[i]);
	}
#endif

	OakEngineAppParams startup_params;
	memset(&startup_params, 0, sizeof(startup_params));
	startup_params.run_mode = OAKENGINE_APP_RUN_NORMAL;

	CommandLineParser parser;

	// Our options
	auto help_option = parser.add_option(
		{ QStringLiteral("h"), QStringLiteral("-help") },
		QCoreApplication::translate("main", "Show this help text"));

	auto version_option = parser.add_option(
		{ QStringLiteral("v"), QStringLiteral("-version") },
		QCoreApplication::translate("main", "Show application version"));

	auto fullscreen_option = parser.add_option(
		{ QStringLiteral("f"), QStringLiteral("-fullscreen") },
		QCoreApplication::translate("main", "Start in full-screen mode"));

	auto export_option = parser.add_option(
		{ QStringLiteral("x"), QStringLiteral("-export") },
		QCoreApplication::translate("main", "Export only (No GUI)"));

	auto ts_option = parser.add_option(
		{ QStringLiteral("-ts") },
		QCoreApplication::translate("main", "Override language with file"),
		true, QCoreApplication::translate("main", "qm-file"));

	auto decompress_option =
		parser.add_option({ QStringLiteral("d"), QStringLiteral("-decompress") },
						 QCoreApplication::translate(
							 "main", "Decompress project file (No GUI)"));

#ifdef _WIN32
	auto console_option = parser.AddOption(
		{ QStringLiteral("c"), QStringLiteral("-console") },
		QCoreApplication::translate("main", "Launch with debug console"));
#endif // _WIN32

	auto project_argument = parser.add_positional_argument(
		QStringLiteral("project"),
		QCoreApplication::translate("main", "Project to open on startup"));

	auto no_plugin = parser.add_option(
		{ QStringLiteral("-no-plugin") },
		QCoreApplication::translate("main", "Don't load plugins"));

	// Qt options re-implemented (add to this as necessary)
	//
	// Because we don't use QCommandLineParser, we must filter out Qt's arguments ourselves. Here,
	// we create them so they're recognized, but never use and also hide them in the "help" text.
	parser.add_option({ QStringLiteral("platform") }, QString(), true, QString(),
					 true);
	parser.add_option({ QStringLiteral("platformpluginpath") }, QString(), true,
					 QString(), true);
	parser.add_option({ QStringLiteral("platformtheme") }, QString(), true,
					 QString(), true);
	parser.add_option({ QStringLiteral("plugin") }, QString(), true, QString(),
					 true);
	parser.add_option({ QStringLiteral("qmljsdebugger") }, QString(), true,
					 QString(), true);
	parser.add_option({ QStringLiteral("qwindowgeometry") }, QString(), true,
					 QString(), true);
	parser.add_option({ QStringLiteral("qwindowicon") }, QString(), true,
					 QString(), true);
	parser.add_option({ QStringLiteral("qwindowtitle") }, QString(), true,
					 QString(), true);
	parser.add_option({ QStringLiteral("reverse") }, QString(), false, QString(),
					 true);
	parser.add_option({ QStringLiteral("session") }, QString(), true, QString(),
					 true);
	parser.add_option({ QStringLiteral("style") }, QString(), true, QString(),
					 true);
	parser.add_option({ QStringLiteral("stylesheet") }, QString(), true,
					 QString(), true);
	parser.add_option({ QStringLiteral("widgetcount") }, QString(), false,
					 QString(), true);

	// Hidden crash option for debugging the crash handling
	auto crash_option = parser.add_option({ QStringLiteral("-crash") },
										 QString(), true, QString(), true);

	parser.process(args);

	if (help_option->is_set()) {
		// Show help
		parser.print_help(argv[0]);
		return 0;
	}

	if (version_option->is_set()) {
		// Print version
		printf("%s\n",
			   QCoreApplication::applicationVersion().toUtf8().constData());
		return 0;
	}

	if (decompress_option->is_set()) {
		return decompress_project(project_argument->get_setting());
	}

	if (export_option->is_set()) {
		startup_params.run_mode = OAKENGINE_APP_RUN_HEADLESS_EXPORT;
	}

	if (ts_option->is_set()) {
		if (ts_option->get_setting().isEmpty()) {
			qWarning() << "--ts was set but no translation file was provided";
		} else {
			QByteArray sl_utf = ts_option->get_setting().toUtf8();
			startup_params.startup_language = sl_utf.constData();
		}
	}

	const bool load_plugins = !no_plugin->is_set();

	if (crash_option->is_set()) {
		startup_params.crash_on_startup = 1;
	}

	startup_params.fullscreen = fullscreen_option->is_set() ? 1 : 0;

	{
		QByteArray sp_utf = project_argument->get_setting().toUtf8();
		if (!sp_utf.isEmpty()) {
			startup_params.startup_project = sp_utf.constData();
		}
	}

	// Set OpenGL display profile. Oak's render pipeline still uses OpenGL
	// internally even when Vulkan is requested as the Qt graphics backend.
	QSurfaceFormat format;

	// Tries to cover all bases. If drivers don't support 3.2, they should fallback to the closest
	// alternative. Unfortunately Qt doesn't support 3.0-3.1 without DeprecatedFunctions, so we
	// declare that too. We also force Qt to not use ANGLE because I've had a lot of problems with it
	// so far.
	//
	// https://bugreports.qt.io/browse/QTBUG-46140
	QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
	format.setVersion(3, 2);
	format.setProfile(QSurfaceFormat::CoreProfile);

	format.setDepthBufferSize(24);
	QSurfaceFormat::setDefaultFormat(format);

	// Enable application automatically using higher resolution images from icons
	QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

	QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

	// Create application instance
	std::unique_ptr<QCoreApplication> a;

	if (startup_params.run_mode == OAKENGINE_APP_RUN_NORMAL) {
#ifdef _WIN32
		// Since Oak Video Editor is linked with the console subsystem (for better POSIX compatibility), a console
		// is created by default. If the user didn't request one, we free it here.
		if (!console_option->IsSet()) {
			FreeConsole();
		}
#endif // _WIN32

		a.reset(new QApplication(argc, argv));
	} else {
		a.reset(new QCoreApplication(argc, argv));
	}

	// Configuration errors are reported through a UI handler so the engine
	// layer (config) never has to know about dialogs
	oakengine_config_set_error_handler(config_error_handler, NULL);

	oakengine_config_load();
	char backend_buf[64];
	int backend_len = oakengine_config_get_string(
			"GraphicsBackend", backend_buf, sizeof(backend_buf));
	const QString graphics_backend =
		backend_len > 0 ? QString::fromUtf8(backend_buf).toLower() :
						  QStringLiteral("opengl");
	qputenv("QSG_RHI_BACKEND", graphics_backend == QStringLiteral("vulkan") ?
								   QByteArrayLiteral("vulkan") :
								   QByteArrayLiteral("opengl"));

	if (auto *gui_app = qobject_cast<QGuiApplication *>(a.get())) {
		gui_app->setWindowIcon(
			QIcon(QStringLiteral(":/graphics/oak-logo.png")));
	}

	if (load_plugins) {
		oakengine_plugin_load_plugins("plugins");
	}

#ifdef _WIN32
	// On Windows, users seem to frequently run into a crash caused by their graphics driver not
	// supporting framebuffers, which we require. I personally have only been able to recreate this
	// when no driver is installed (e.g. when using the Microsoft Basic Display Adapter). Whether
	// that's true for all users or not is still up in the air, but what we do know is it's a driver
	// issue and users should know what to do rather than simply receive a cryptic crash report.
	QOpenGLContext ctx;
	ctx.create();
	QOffscreenSurface surface;
	surface.create();
	ctx.makeCurrent(&surface);
	bool has_proc_address = wglGetProcAddress("glGenFramebuffers");
	std::string gpu_vendor =
		reinterpret_cast<const char *>(ctx.functions()->glGetString(GL_VENDOR));
	std::string gpu_renderer = reinterpret_cast<const char *>(
		ctx.functions()->glGetString(GL_RENDERER));
	std::string gpu_version = reinterpret_cast<const char *>(
		ctx.functions()->glGetString(GL_VERSION));
	ctx.doneCurrent();
	surface.destroy();

	if (!has_proc_address) {
		QString msg =
			QCoreApplication::translate(
				"main",
				"Your computer's graphics driver does not appear to support framebuffers. "
				"This most likely means either your graphics driver is not up-to-date or your graphics card is too old to run Oak Video Editor.\n\n"
				"Please update your graphics driver to the latest version and try again.\n\n"
				"Current driver information: %1 %2 %3")
				.arg(QString::fromStdString(gpu_vendor),
					 QString::fromStdString(gpu_renderer),
					 QString::fromStdString(gpu_version));

		if (dynamic_cast<QGuiApplication *>(a.get())) {
			QMessageBox::critical(nullptr, QString(), msg);
		} else {
			qCritical().noquote() << msg;
		}

		return 1;
	}
#endif

	// Enable Google Crashpad if compiled with it
#ifdef USE_CRASHPAD
	if (!InitializeCrashpad()) {
		qWarning() << "Failed to initialize Crashpad handler";
	}
#endif // USE_CRASHPAD

	// Start core
	olive::Core c(&startup_params);

	c.start();

	int ret = a->exec();

	// Clear core memory
	c.stop();

	return ret;
}
