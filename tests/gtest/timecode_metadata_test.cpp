#include <gtest/gtest.h>

#include <QDir>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "codec/timecodemetadata.h"
#include "node/project/footage/footage.h"
#include "node/project/footage/footagedescription.h"

TEST(TimecodeMetadata, ParsesNonDropFrameTimecode)
{
	const olive::TimecodeMetadata::SourceTime parsed =
		olive::TimecodeMetadata::from_timecode_string(
			QStringLiteral("01:02:03:12"), olive::core::Rational(1, 24));

	ASSERT_TRUE(parsed.valid);
	EXPECT_EQ(parsed.source, QStringLiteral("timecode"));
	EXPECT_EQ(parsed.time, olive::core::Rational(1 * 3600 + 2 * 60 + 3, 1) +
							   olive::core::Rational(12, 24));
}

TEST(TimecodeMetadata, ParsesDropFrameTimecode)
{
	const olive::TimecodeMetadata::SourceTime parsed =
		olive::TimecodeMetadata::from_timecode_string(
			QStringLiteral("00:01:00;02"), olive::core::Rational(1001, 30000));

	ASSERT_TRUE(parsed.valid);
	EXPECT_EQ(parsed.source, QStringLiteral("timecode"));
	EXPECT_GT(parsed.time, olive::core::Rational(59));
	EXPECT_LT(parsed.time, olive::core::Rational(61));
}

TEST(TimecodeMetadata, ParsesBwfTimeReference)
{
	const olive::TimecodeMetadata::SourceTime parsed =
		olive::TimecodeMetadata::from_bwf_time_reference(QStringLiteral("96000"),
													  48000);

	ASSERT_TRUE(parsed.valid);
	EXPECT_EQ(parsed.source, QStringLiteral("bwf_time_reference"));
	EXPECT_EQ(parsed.time, olive::core::Rational(2));
}

TEST(TimecodeMetadata, ParsesLargeBwfTimeReferenceWithoutTruncation)
{
	const olive::TimecodeMetadata::SourceTime parsed =
		olive::TimecodeMetadata::from_bwf_time_reference(
			QStringLiteral("4294967296"), 48000);

	ASSERT_TRUE(parsed.valid);
	EXPECT_GT(parsed.time, olive::core::Rational(89478));
	EXPECT_LT(parsed.time, olive::core::Rational(89479));
}

TEST(TimecodeMetadata, RejectsInvalidMetadata)
{
	EXPECT_FALSE(olive::TimecodeMetadata::from_timecode_string(
					 QString(), olive::core::Rational(1, 24))
					 .valid);
	EXPECT_FALSE(olive::TimecodeMetadata::from_bwf_time_reference(
					 QStringLiteral("not-a-number"), 48000)
					 .valid);
	EXPECT_FALSE(
		olive::TimecodeMetadata::from_bwf_time_reference(QStringLiteral("123"), 0)
			.valid);
	EXPECT_FALSE(
		olive::TimecodeMetadata::from_timecode_string(
			QStringLiteral("not-a-timecode"), olive::core::Rational(1, 24))
			.valid);
}

TEST(TimecodeMetadata, FromBwfTimeReferenceZeroSampleRateIsInvalid)
{
	EXPECT_FALSE(
		olive::TimecodeMetadata::from_bwf_time_reference(QStringLiteral("0"), 0)
			.valid);
}

TEST(TimecodeMetadata, FootageDescriptionWithoutSourceStartTime)
{
	olive::FootageDescription desc(QStringLiteral("ffmpeg"));
	EXPECT_FALSE(desc.has_source_start_time());
}

TEST(TimecodeMetadata, FootageDescriptionCachesSourceStartTime)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path =
		QDir(dir.path()).filePath(QStringLiteral("footage-cache.xml"));

	olive::FootageDescription desc(QStringLiteral("ffmpeg"));
	desc.set_source_start_time(olive::core::Rational(96000, 48000),
							QStringLiteral("bwf_time_reference"));
	ASSERT_TRUE(desc.save(path));

	olive::FootageDescription loaded;
	ASSERT_TRUE(loaded.load(path));
	ASSERT_TRUE(loaded.has_source_start_time());
	EXPECT_EQ(loaded.source_start_time(), olive::core::Rational(2));
	EXPECT_EQ(loaded.source_start_time_source(),
			  QStringLiteral("bwf_time_reference"));
}

TEST(TimecodeMetadata, FootagePersistsSourceStartTime)
{
	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("custom"));
	writer.writeTextElement(QStringLiteral("timestamp"), QStringLiteral("0"));
	writer.writeStartElement(QStringLiteral("sourcestarttime"));
	writer.writeAttribute(QStringLiteral("source"), QStringLiteral("timecode"));
	writer.writeCharacters(QStringLiteral("3600/1"));
	writer.writeEndElement();
	writer.writeEndElement();
	writer.writeEndDocument();

	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name(), QStringLiteral("custom"));

	olive::Footage footage;
	ASSERT_TRUE(footage.load_custom(&reader, nullptr));
	ASSERT_TRUE(footage.has_source_start_time());
	EXPECT_EQ(footage.source_start_time(), olive::core::Rational(3600));
	EXPECT_EQ(footage.source_start_time_source(), QStringLiteral("timecode"));
}

TEST(TimecodeMetadata, FootageClearSourceStartTime)
{
	olive::Footage footage;
	footage.set_source_start_time(olive::core::Rational(3600),
							   QStringLiteral("manual"));
	ASSERT_TRUE(footage.has_source_start_time());

	footage.clear_source_start_time();

	EXPECT_FALSE(footage.has_source_start_time());
	EXPECT_EQ(footage.source_start_time(), olive::core::Rational());
	EXPECT_TRUE(footage.source_start_time_source().isEmpty());
}

TEST(TimecodeMetadata, FootageSetSourceStartTimeOverridesPreviousValue)
{
	olive::Footage footage;
	footage.set_source_start_time(olive::core::Rational(3600),
							   QStringLiteral("timecode"));

	// A manual edit replaces both the value and the recorded source
	footage.set_source_start_time(olive::core::Rational(1800),
							   QStringLiteral("manual"));

	EXPECT_TRUE(footage.has_source_start_time());
	EXPECT_EQ(footage.source_start_time(), olive::core::Rational(1800));
	EXPECT_EQ(footage.source_start_time_source(), QStringLiteral("manual"));
}
