#include <QApplication>
#include <QSurfaceFormat>
#include <gtest/gtest.h>

int main(int argc, char **argv)
{
	// Match the OpenGL setup from the real application's main.cpp so that
	// any test that creates an OpenGLRenderer (e.g. via RenderManager) can
	// initialise a context without crashing.
	QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
	QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

	QSurfaceFormat format;
	format.setVersion(3, 2);
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setDepthBufferSize(24);
	QSurfaceFormat::setDefaultFormat(format);

	QApplication app(argc, argv);
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
