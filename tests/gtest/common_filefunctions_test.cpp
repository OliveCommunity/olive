#include <gtest/gtest.h>

#include <QDebug>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include "common/filefunctions.h"

namespace
{

// Collects qDebug/qWarning/qCritical output for inspection
QStringList g_captured_messages;

void capture_message_handler(QtMsgType, const QMessageLogContext &,
						   const QString &msg)
{
	g_captured_messages.append(msg);
}

} // namespace

TEST(CommonFileFunctions, EnsureFilenameExtension)
{
	EXPECT_EQ(olive::FileFunctions::ensure_filename_extension(
				  QStringLiteral("project"), QStringLiteral("ove")),
			  QStringLiteral("project.ove"));
	EXPECT_EQ(olive::FileFunctions::ensure_filename_extension(
				  QStringLiteral("project.ove"), QStringLiteral("ove")),
			  QStringLiteral("project.ove"));
	EXPECT_EQ(olive::FileFunctions::ensure_filename_extension(
				  QStringLiteral("PROJECT"), QStringLiteral("ove")),
			  QStringLiteral("PROJECT.ove"));
	EXPECT_TRUE(olive::FileFunctions::ensure_filename_extension(
					QString(), QStringLiteral("ove"))
					.isEmpty());
	EXPECT_EQ(olive::FileFunctions::ensure_filename_extension(
				  QStringLiteral("project"), QString()),
			  QStringLiteral("project"));
}

TEST(CommonFileFunctions, GetSafeTemporaryFilename)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());

	QString base = dir.filePath(QStringLiteral("test.ove"));
	QString first = olive::FileFunctions::get_safe_temporary_filename(base);
	EXPECT_FALSE(QFileInfo::exists(first));
	EXPECT_TRUE(first.contains(QStringLiteral(".tmp0.")));

	QFile f(first);
	(void)f.open(QIODevice::WriteOnly);
	f.close();

	QString second = olive::FileFunctions::get_safe_temporary_filename(base);
	EXPECT_NE(first, second);
	EXPECT_TRUE(second.contains(QStringLiteral(".tmp1.")));
}

TEST(CommonFileFunctions, DirectoryIsValid)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());

	EXPECT_TRUE(
		olive::FileFunctions::directory_is_valid(QDir(dir.path()), false));

	QDir nonexistent(dir.filePath(QStringLiteral("subdir/nested")));
	EXPECT_TRUE(olive::FileFunctions::directory_is_valid(nonexistent, true));
	EXPECT_TRUE(nonexistent.exists());
}

TEST(CommonFileFunctions, RenameFileAllowOverwrite)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());

	QString from = dir.filePath(QStringLiteral("from.txt"));
	QString to = dir.filePath(QStringLiteral("to.txt"));

	QFile f(from);
	(void)f.open(QIODevice::WriteOnly);
	f.write("source");
	f.close();

	QFile t(to);
	(void)t.open(QIODevice::WriteOnly);
	t.write("existing");
	t.close();

	EXPECT_TRUE(olive::FileFunctions::rename_file_allow_overwrite(from, to));
	EXPECT_FALSE(QFileInfo::exists(from));
	QFile result(to);
	(void)result.open(QIODevice::ReadOnly);
	EXPECT_EQ(result.readAll(), QByteArray("source"));
}

TEST(CommonFileFunctions, CanCopyDirectoryWithoutOverwriting)
{
	QTemporaryDir src;
	QTemporaryDir dst;
	ASSERT_TRUE(src.isValid());
	ASSERT_TRUE(dst.isValid());

	QString src_file = QDir(src.path()).filePath(QStringLiteral("file.txt"));
	QFile f(src_file);
	(void)f.open(QIODevice::WriteOnly);
	f.close();

	EXPECT_TRUE(olive::FileFunctions::can_copy_directory_without_overwriting(
		src.path(), dst.path()));

	QString dst_file = QDir(dst.path()).filePath(QStringLiteral("file.txt"));
	QFile g(dst_file);
	(void)g.open(QIODevice::WriteOnly);
	g.close();

	EXPECT_FALSE(olive::FileFunctions::can_copy_directory_without_overwriting(
		src.path(), dst.path()));
}

TEST(CommonFileFunctions, CopyDirectory)
{
	QTemporaryDir src;
	QTemporaryDir dst;
	ASSERT_TRUE(src.isValid());
	ASSERT_TRUE(dst.isValid());

	QString src_file = QDir(src.path()).filePath(QStringLiteral("file.txt"));
	QFile f(src_file);
	(void)f.open(QIODevice::WriteOnly);
	f.write("copied");
	f.close();

	QString dst_dir = QDir(dst.path()).filePath(QStringLiteral("copied"));
	olive::FileFunctions::copy_directory(src.path(), dst_dir, false);

	QFile result(QDir(dst_dir).filePath(QStringLiteral("file.txt")));
	EXPECT_TRUE(result.open(QIODevice::ReadOnly));
	EXPECT_EQ(result.readAll(), QByteArray("copied"));
}

TEST(CommonFileFunctions, ReadFileAsString)
{
	QTemporaryFile f;
	ASSERT_TRUE(f.open());
	f.write("hello world");
	f.close();

	EXPECT_EQ(olive::FileFunctions::read_file_as_string(f.fileName()),
			  QStringLiteral("hello world"));
	EXPECT_TRUE(olive::FileFunctions::read_file_as_string(
					QStringLiteral("/nonexistent/path"))
					.isEmpty());
}

TEST(CommonFileFunctions, GetUniqueFileIdentifier)
{
	QTemporaryFile f;
	ASSERT_TRUE(f.open());
	f.close();

	QString id1 = olive::FileFunctions::get_unique_file_identifier(f.fileName());
	QString id2 = olive::FileFunctions::get_unique_file_identifier(f.fileName());
	EXPECT_FALSE(id1.isEmpty());
	EXPECT_EQ(id1, id2);

	EXPECT_TRUE(olive::FileFunctions::get_unique_file_identifier(
					QStringLiteral("/nonexistent"))
					.isEmpty());
}

TEST(CommonFileFunctions, GetConfigurationLocation)
{
	QString loc = olive::FileFunctions::get_configuration_location();
	EXPECT_FALSE(loc.isEmpty());
	EXPECT_TRUE(QDir(loc).exists());
}

TEST(CommonFileFunctions, GetTempFilePath)
{
	QString temp = olive::FileFunctions::get_temp_file_path();
	EXPECT_FALSE(temp.isEmpty());
	EXPECT_TRUE(QDir(temp).exists());
}

TEST(CommonFileFunctions, GetAutoRecoveryRoot)
{
	QString root = olive::FileFunctions::get_auto_recovery_root();
	EXPECT_FALSE(root.isEmpty());
}

TEST(CommonFileFunctions, DirectoryIsValidExisting)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	EXPECT_TRUE(
		olive::FileFunctions::directory_is_valid(QDir(dir.path()), false));
}

TEST(CommonFileFunctions, CopyDirectoryWithOverwrite)
{
	QTemporaryDir src;
	QTemporaryDir dst;
	ASSERT_TRUE(src.isValid());
	ASSERT_TRUE(dst.isValid());

	QString src_file = QDir(src.path()).filePath(QStringLiteral("file.txt"));
	QFile f(src_file);
	(void)f.open(QIODevice::WriteOnly);
	f.write("new content");
	f.close();

	QString dst_file = QDir(dst.path()).filePath(QStringLiteral("file.txt"));
	QFile g(dst_file);
	(void)g.open(QIODevice::WriteOnly);
	g.write("old content");
	g.close();

	olive::FileFunctions::copy_directory(src.path(), dst.path(), true);

	QFile result(dst_file);
	(void)result.open(QIODevice::ReadOnly);
	EXPECT_EQ(result.readAll(), QByteArray("new content"));
}

TEST(CommonFileFunctions, CopyDirectorySourceMissing)
{
	QTemporaryDir dst;
	ASSERT_TRUE(dst.isValid());

	// A missing source must log a critical error naming the source and
	// leave the destination untouched
	g_captured_messages.clear();
	QtMessageHandler old = qInstallMessageHandler(capture_message_handler);
	olive::FileFunctions::copy_directory(QStringLiteral("/nonexistent/path"),
										dst.path(), false);
	qInstallMessageHandler(old);

	ASSERT_EQ(g_captured_messages.size(), 1);
	EXPECT_TRUE(g_captured_messages.first().contains(
		QStringLiteral("Failed to copy directory")));
	EXPECT_TRUE(g_captured_messages.first().contains(
		QStringLiteral("/nonexistent/path")));
	EXPECT_TRUE(
		QDir(dst.path()).entryList(QDir::NoDotAndDotDot).isEmpty());
}
