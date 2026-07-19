#include <gtest/gtest.h>

#include <QBuffer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "render/videoparams.h"

TEST(RenderVideoParams, SaveLoadRoundTrip)
{
	olive::VideoParams params;
	params.set_width(1280);
	params.set_height(720);
	params.set_frame_rate(olive::core::Rational(24, 1));
	params.set_pixel_aspect_ratio(olive::core::Rational(1, 1));
	params.set_colorspace(QStringLiteral("test"));
	params.set_color_primaries(9);
	params.set_color_transfer(16);

	QByteArray xml;
	QBuffer buffer(&xml);
	buffer.open(QIODevice::WriteOnly);
	QXmlStreamWriter writer(&buffer);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("videoparams"));
	params.save(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();
	buffer.close();

	olive::VideoParams loaded;
	QBuffer read_buffer(&xml);
	read_buffer.open(QIODevice::ReadOnly);
	QXmlStreamReader reader(&read_buffer);
	EXPECT_TRUE(reader.readNextStartElement());
	EXPECT_EQ(reader.name().toString(), QStringLiteral("videoparams"));
	loaded.load(&reader);

	EXPECT_EQ(loaded.width(), 1280);
	EXPECT_EQ(loaded.height(), 720);
	EXPECT_EQ(loaded.frame_rate(), olive::core::Rational(24, 1));
	EXPECT_EQ(loaded.pixel_aspect_ratio(), olive::core::Rational(1, 1));
	EXPECT_EQ(loaded.colorspace(), QStringLiteral("test"));
	EXPECT_EQ(loaded.color_primaries(), 9);
	EXPECT_EQ(loaded.color_transfer(), 16);
}
