#include <gtest/gtest.h>

#include <QBuffer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "timeline/timelineworkarea.h"

TEST(TimelineWorkArea, DefaultsAndSetters)
{
	olive::TimelineWorkArea workarea;
	EXPECT_FALSE(workarea.enabled());

	olive::core::TimeRange range(olive::core::Rational(5, 1),
								 olive::core::Rational(10, 1));
	workarea.set_enabled(true);
	workarea.set_range(range);

	EXPECT_TRUE(workarea.enabled());
	EXPECT_EQ(workarea.range(), range);
	EXPECT_EQ(workarea.in(), range.in());
	EXPECT_EQ(workarea.out(), range.out());
	EXPECT_EQ(workarea.length(), range.length());
}

TEST(TimelineWorkArea, SaveLoadRoundTrip)
{
	olive::TimelineWorkArea workarea;
	workarea.set_enabled(true);
	workarea.set_range(olive::core::TimeRange(olive::core::Rational(2, 1),
											  olive::core::Rational(6, 1)));

	QByteArray xml;
	QBuffer buffer(&xml);
	ASSERT_TRUE(buffer.open(QIODevice::WriteOnly));
	QXmlStreamWriter writer(&buffer);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("workarea"));
	workarea.save(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();
	buffer.close();

	olive::TimelineWorkArea loaded;
	QBuffer read_buffer(&xml);
	ASSERT_TRUE(read_buffer.open(QIODevice::ReadOnly));
	QXmlStreamReader reader(&read_buffer);
	ASSERT_TRUE(reader.readNextStartElement());
	EXPECT_TRUE(loaded.load(&reader));

	EXPECT_TRUE(loaded.enabled());
	EXPECT_EQ(loaded.range(),
			  olive::core::TimeRange(olive::core::Rational(2, 1),
									 olive::core::Rational(6, 1)));
}

TEST(TimelineWorkArea, DisabledWorkAreaRoundTrip)
{
	olive::TimelineWorkArea workarea;
	workarea.set_enabled(false);
	workarea.set_range(olive::core::TimeRange(olive::core::Rational(0, 1),
											  olive::core::Rational(10, 1)));

	QByteArray xml;
	QBuffer buffer(&xml);
	ASSERT_TRUE(buffer.open(QIODevice::WriteOnly));
	QXmlStreamWriter writer(&buffer);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("workarea"));
	workarea.save(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();
	buffer.close();

	olive::TimelineWorkArea loaded;
	QBuffer read_buffer(&xml);
	ASSERT_TRUE(read_buffer.open(QIODevice::ReadOnly));
	QXmlStreamReader reader(&read_buffer);
	ASSERT_TRUE(reader.readNextStartElement());
	EXPECT_TRUE(loaded.load(&reader));

	EXPECT_FALSE(loaded.enabled());
	EXPECT_EQ(loaded.range(),
			  olive::core::TimeRange(olive::core::Rational(0, 1),
									 olive::core::Rational(10, 1)));
}

TEST(TimelineWorkArea, SetRangeUpdatesInOut)
{
	olive::TimelineWorkArea workarea;
	workarea.set_range(olive::core::TimeRange(olive::core::Rational(3, 1),
											  olive::core::Rational(8, 1)));

	EXPECT_EQ(workarea.in(), olive::core::Rational(3, 1));
	EXPECT_EQ(workarea.out(), olive::core::Rational(8, 1));
	EXPECT_EQ(workarea.length(), olive::core::Rational(5, 1));
}
