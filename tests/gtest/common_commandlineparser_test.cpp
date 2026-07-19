#include <gtest/gtest.h>

#include <QDebug>

#include "common/commandlineparser.h"

namespace
{

// Collects qDebug/qWarning/qCritical output for inspection
QStringList g_captured_messages;

void CaptureMessageHandler(QtMsgType, const QMessageLogContext &,
						   const QString &msg)
{
	g_captured_messages.append(msg);
}

} // namespace

TEST(CommonCommandLineParser, OptionWithoutArgument)
{
	CommandLineParser parser;
	const CommandLineParser::Option *opt =
		parser.AddOption({ QStringLiteral("help"), QStringLiteral("h") },
						 QStringLiteral("Show help"), false);

	parser.Process({ QStringLiteral("app"), QStringLiteral("-help") });

	EXPECT_TRUE(opt->IsSet());
}

TEST(CommonCommandLineParser, ShortOption)
{
	CommandLineParser parser;
	const CommandLineParser::Option *opt =
		parser.AddOption({ QStringLiteral("help"), QStringLiteral("h") },
						 QStringLiteral("Show help"), false);

	parser.Process({ QStringLiteral("app"), QStringLiteral("-h") });

	EXPECT_TRUE(opt->IsSet());
}

TEST(CommonCommandLineParser, OptionWithArgument)
{
	CommandLineParser parser;
	const CommandLineParser::Option *opt = parser.AddOption(
		{ QStringLiteral("project") }, QStringLiteral("Project file"), true,
		QStringLiteral("file"));

	parser.Process({ QStringLiteral("app"), QStringLiteral("-project"),
					 QStringLiteral("test.ove") });

	EXPECT_TRUE(opt->IsSet());
	EXPECT_EQ(opt->GetSetting(), QStringLiteral("test.ove"));
}

TEST(CommonCommandLineParser, PositionalArgument)
{
	CommandLineParser parser;
	const CommandLineParser::PositionalArgument *arg =
		parser.AddPositionalArgument(QStringLiteral("filename"),
									 QStringLiteral("Project file"), true);

	parser.Process({ QStringLiteral("app"), QStringLiteral("test.ove") });

	EXPECT_EQ(arg->GetSetting(), QStringLiteral("test.ove"));
}

TEST(CommonCommandLineParser, UnknownOptionWarning)
{
	CommandLineParser parser;
	parser.AddOption({ QStringLiteral("known") }, QStringLiteral("Known"));

	g_captured_messages.clear();
	QtMessageHandler old = qInstallMessageHandler(CaptureMessageHandler);
	parser.Process({ QStringLiteral("app"), QStringLiteral("-unknown") });
	qInstallMessageHandler(old);

	// The warning must name the offending option
	ASSERT_EQ(g_captured_messages.size(), 1);
	EXPECT_TRUE(g_captured_messages.first().contains(
		QStringLiteral("Unknown parameter:")));
	EXPECT_TRUE(
		g_captured_messages.first().contains(QStringLiteral("-unknown")));
}

TEST(CommonCommandLineParser, UnknownPositionalWarning)
{
	CommandLineParser parser;

	g_captured_messages.clear();
	QtMessageHandler old = qInstallMessageHandler(CaptureMessageHandler);
	parser.Process({ QStringLiteral("app"), QStringLiteral("extra") });
	qInstallMessageHandler(old);

	// The warning must name the offending positional argument
	ASSERT_EQ(g_captured_messages.size(), 1);
	EXPECT_TRUE(g_captured_messages.first().contains(
		QStringLiteral("Unknown parameter:")));
	EXPECT_TRUE(
		g_captured_messages.first().contains(QStringLiteral("extra")));
}

TEST(CommonCommandLineParser, HiddenOptionExcludedFromHelp)
{
	CommandLineParser parser;
	parser.AddOption({ QStringLiteral("visible") }, QStringLiteral("Visible"));
	parser.AddOption({ QStringLiteral("hidden") }, QStringLiteral("Hidden"),
					 false, QString(), true);
	parser.AddPositionalArgument(QStringLiteral("file"),
								 QStringLiteral("Input file"));

	// PrintHelp writes to stdout via printf
	testing::internal::CaptureStdout();
	parser.PrintHelp("/usr/bin/app");
	std::string help = testing::internal::GetCapturedStdout();

	// Visible option and positional argument must be listed
	EXPECT_NE(help.find("-visible"), std::string::npos);
	EXPECT_NE(help.find("Visible"), std::string::npos);
	EXPECT_NE(help.find("[file]"), std::string::npos);

	// Hidden option must not appear anywhere in the help text
	EXPECT_EQ(help.find("-hidden"), std::string::npos);
	EXPECT_EQ(help.find("Hidden"), std::string::npos);
}
