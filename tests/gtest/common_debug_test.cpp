#include <gtest/gtest.h>

#include "common/debug.h"

TEST(CommonDebug, DebugHandlerFormatsAllLevels)
{
	// DebugHandler writes "[LEVEL] message" lines to stderr
	testing::internal::CaptureStderr();
	QtMessageHandler old = qInstallMessageHandler(olive::debug_handler);

	qDebug() << "debug message";
	qInfo() << "info message";
	qWarning() << "warning message";
	qCritical() << "critical message";

	qInstallMessageHandler(old);
	std::string out = testing::internal::GetCapturedStderr();

	// Each level must get its own tag, paired with its message on one line
	QString output = QString::fromStdString(out);
	const QStringList lines = output.split('\n');
	auto has_line = [&lines](const char *tag, const char *text) {
		for (const QString &line : lines) {
			if (line.contains(QString::fromLatin1(tag)) &&
				line.contains(QString::fromLatin1(text))) {
				return true;
			}
		}
		return false;
	};

	EXPECT_TRUE(has_line("[DEBUG]", "debug message"));
	EXPECT_TRUE(has_line("[INFO]", "info message"));
	EXPECT_TRUE(has_line("[WARNING]", "warning message"));
	EXPECT_TRUE(has_line("[ERROR]", "critical message"));
}
