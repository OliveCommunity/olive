#include <QApplication>
#include <QDir>
#include <QFile>
#include <gtest/gtest.h>

int main(int argc, char **argv)
{
	Q_INIT_RESOURCE(ocioconf);
	if (qEnvironmentVariableIsEmpty("OCIO")) {
		qputenv("OCIO", QFile::encodeName(
						 QDir(QStringLiteral(OAK_TEST_SOURCE_DIR))
							 .filePath(QStringLiteral(
								 "app/render/ocioconf/config.ocio"))));
	}
	QApplication app(argc, argv);
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
