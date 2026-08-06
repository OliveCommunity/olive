/***

  Oak Video Editor - Non-Linear Video Editor
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

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "common/xmlutils.h"

namespace
{

std::string read_string(int (*fn)(OakXmlReader, char *, int),
						OakXmlReader reader)
{
	int needed = fn(reader, nullptr, 0);
	EXPECT_GT(needed, 0);
	std::vector<char> buf(needed);
	EXPECT_EQ(fn(reader, buf.data(), needed), needed);
	return std::string(buf.data());
}

} // namespace

TEST(CommonXmlUtilsCApi, ReaderInitNullData)
{
	EXPECT_EQ(oakcommon_xml_reader_init(nullptr).ctx, nullptr);
}

TEST(CommonXmlUtilsCApi, ReaderFreeNull)
{
	oakcommon_xml_reader_free(nullptr);
}

TEST(CommonXmlUtilsCApi, ReadNextStartElement)
{
	OakXmlReader r = oakcommon_xml_reader_init("<root><child>value</child></root>");
	ASSERT_NE(r.ctx, nullptr);

	int found = 0;
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 1);
	EXPECT_EQ(read_string(oakcommon_xml_reader_name, r), "root");

	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 1);
	EXPECT_EQ(read_string(oakcommon_xml_reader_name, r), "child");

	oakcommon_xml_reader_free(&r);
}

TEST(CommonXmlUtilsCApi, ReadNextStartElementNullHandle)
{
	int found = 0;
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(OakXmlReader{}, &found),
			  OAKCOMMON_E_INVALID);

	OakXmlReader r = oakcommon_xml_reader_init("<root/>");
	ASSERT_NE(r.ctx, nullptr);
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, nullptr),
			  OAKCOMMON_E_INVALID);
	oakcommon_xml_reader_free(&r);
}

TEST(CommonXmlUtilsCApi, ReadNextStartElementReturnsFalseAtEnd)
{
	OakXmlReader r = oakcommon_xml_reader_init("<root/>");
	ASSERT_NE(r.ctx, nullptr);

	int found = -1;
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 1);
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 0);

	oakcommon_xml_reader_free(&r);
}

TEST(CommonXmlUtilsCApi, NameNullHandle)
{
	char buf[16];
	EXPECT_EQ(oakcommon_xml_reader_name(OakXmlReader{}, buf, sizeof(buf)),
			  OAKCOMMON_E_INVALID);
}

TEST(CommonXmlUtilsCApi, ReadElementText)
{
	OakXmlReader r =
		oakcommon_xml_reader_init("<root><child>a &amp; b</child></root>");
	ASSERT_NE(r.ctx, nullptr);

	int found = 0;
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 1);
	EXPECT_EQ(read_string(oakcommon_xml_reader_read_element_text, r),
			  "a & b");

	oakcommon_xml_reader_free(&r);
}

TEST(CommonXmlUtilsCApi, ReadElementTextNullHandle)
{
	char buf[16];
	EXPECT_EQ(oakcommon_xml_reader_read_element_text(OakXmlReader{}, buf,
													 sizeof(buf)),
			  OAKCOMMON_E_INVALID);
}

TEST(CommonXmlUtilsCApi, SkipCurrentElement)
{
	OakXmlReader r = oakcommon_xml_reader_init(
		"<root><unknown><nested/></unknown><known/></root>");
	ASSERT_NE(r.ctx, nullptr);

	int found = 0;
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(read_string(oakcommon_xml_reader_name, r), "unknown");

	EXPECT_EQ(oakcommon_xml_reader_skip_current_element(r), OAKCOMMON_OK);

	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 1);
	EXPECT_EQ(read_string(oakcommon_xml_reader_name, r), "known");

	oakcommon_xml_reader_free(&r);
}

TEST(CommonXmlUtilsCApi, SkipCurrentElementNullHandle)
{
	EXPECT_EQ(oakcommon_xml_reader_skip_current_element(OakXmlReader{}),
			  OAKCOMMON_E_INVALID);
}

TEST(CommonXmlUtilsCApi, Attributes)
{
	OakXmlReader r = oakcommon_xml_reader_init(
		"<root><item id=\"7\" name=\"a&quot;b\"/></root>");
	ASSERT_NE(r.ctx, nullptr);

	int found = 0;
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 1);

	int count = 0;
	EXPECT_EQ(oakcommon_xml_reader_attribute_count(r, &count), OAKCOMMON_OK);
	ASSERT_EQ(count, 2);

	char buf[32];
	EXPECT_GT(oakcommon_xml_reader_attribute_name(r, 0, buf, sizeof(buf)), 0);
	EXPECT_STREQ(buf, "id");
	EXPECT_GT(oakcommon_xml_reader_attribute_value(r, 0, buf, sizeof(buf)),
			  0);
	EXPECT_STREQ(buf, "7");
	EXPECT_GT(oakcommon_xml_reader_attribute_name(r, 1, buf, sizeof(buf)), 0);
	EXPECT_STREQ(buf, "name");
	EXPECT_GT(oakcommon_xml_reader_attribute_value(r, 1, buf, sizeof(buf)),
			  0);
	EXPECT_STREQ(buf, "a\"b");

	oakcommon_xml_reader_free(&r);
}

TEST(CommonXmlUtilsCApi, AttributeErrorPaths)
{
	EXPECT_EQ(oakcommon_xml_reader_attribute_count(OakXmlReader{}, nullptr),
			  OAKCOMMON_E_INVALID);

	OakXmlReader r = oakcommon_xml_reader_init("<root a=\"1\"/>");
	ASSERT_NE(r.ctx, nullptr);
	int found = 0;
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);

	char buf[16];
	EXPECT_EQ(oakcommon_xml_reader_attribute_name(r, 5, buf, sizeof(buf)),
			  OAKCOMMON_E_NOT_FOUND);
	EXPECT_EQ(oakcommon_xml_reader_attribute_value(r, -1, buf, sizeof(buf)),
			  OAKCOMMON_E_NOT_FOUND);
	EXPECT_EQ(oakcommon_xml_reader_attribute_name(OakXmlReader{}, 0, buf,
												  sizeof(buf)),
			  OAKCOMMON_E_INVALID);

	oakcommon_xml_reader_free(&r);
}

TEST(CommonXmlUtilsCApi, HasError)
{
	OakXmlReader bad = oakcommon_xml_reader_init("<root><unclosed></root>");
	ASSERT_NE(bad.ctx, nullptr);
	int has_error = 0;
	EXPECT_EQ(oakcommon_xml_reader_has_error(bad, &has_error), OAKCOMMON_OK);
	EXPECT_EQ(has_error, 1);
	oakcommon_xml_reader_free(&bad);

	OakXmlReader good = oakcommon_xml_reader_init("<root/>");
	ASSERT_NE(good.ctx, nullptr);
	EXPECT_EQ(oakcommon_xml_reader_has_error(good, &has_error), OAKCOMMON_OK);
	EXPECT_EQ(has_error, 0);
	EXPECT_EQ(oakcommon_xml_reader_has_error(OakXmlReader{}, &has_error),
			  OAKCOMMON_E_INVALID);
	oakcommon_xml_reader_free(&good);
}

TEST(CommonXmlUtilsCApi, WriterFreeNull)
{
	oakcommon_xml_writer_free(nullptr);
}

TEST(CommonXmlUtilsCApi, WriterNullHandleAndArgs)
{
	char buf[16];
	EXPECT_EQ(oakcommon_xml_writer_write_start_element(OakXmlWriter{}, "a"),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_xml_writer_write_end_element(OakXmlWriter{}),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_xml_writer_write_end_document(OakXmlWriter{}),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_xml_writer_output(OakXmlWriter{}, buf, sizeof(buf)),
			  OAKCOMMON_E_INVALID);

	OakXmlWriter w = oakcommon_xml_writer_init();
	ASSERT_NE(w.ctx, nullptr);
	EXPECT_EQ(oakcommon_xml_writer_write_start_element(w, nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_xml_writer_write_attribute(w, "a", nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_xml_writer_write_characters(w, nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_xml_writer_write_text_element(w, nullptr, "x"),
			  OAKCOMMON_E_INVALID);
	oakcommon_xml_writer_free(&w);
}

TEST(CommonXmlUtilsCApi, WriterRoundTrip)
{
	OakXmlWriter w = oakcommon_xml_writer_init();
	ASSERT_NE(w.ctx, nullptr);

	EXPECT_EQ(oakcommon_xml_writer_write_start_element(w, "root"),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_xml_writer_write_attribute(w, "version", "1"),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_xml_writer_write_text_element(w, "child",
													  "a & b"),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_xml_writer_write_start_element(w, "empty"),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_xml_writer_write_end_element(w), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_xml_writer_write_end_document(w), OAKCOMMON_OK);

	int needed = oakcommon_xml_writer_output(w, nullptr, 0);
	ASSERT_GT(needed, 0);
	std::vector<char> buf(needed);
	EXPECT_EQ(oakcommon_xml_writer_output(w, buf.data(), needed), needed);
	oakcommon_xml_writer_free(&w);

	// Read the produced document back.
	OakXmlReader r = oakcommon_xml_reader_init(buf.data());
	ASSERT_NE(r.ctx, nullptr);
	int has_error = 1;
	EXPECT_EQ(oakcommon_xml_reader_has_error(r, &has_error), OAKCOMMON_OK);
	EXPECT_EQ(has_error, 0);

	int found = 0;
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 1);
	EXPECT_EQ(read_string(oakcommon_xml_reader_name, r), "root");

	int count = 0;
	EXPECT_EQ(oakcommon_xml_reader_attribute_count(r, &count), OAKCOMMON_OK);
	ASSERT_EQ(count, 1);
	char abuf[32];
	EXPECT_GT(oakcommon_xml_reader_attribute_value(r, 0, abuf, sizeof(abuf)),
			  0);
	EXPECT_STREQ(abuf, "1");

	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 1);
	EXPECT_EQ(read_string(oakcommon_xml_reader_name, r), "child");
	EXPECT_EQ(read_string(oakcommon_xml_reader_read_element_text, r),
			  "a & b");

	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 1);
	EXPECT_EQ(read_string(oakcommon_xml_reader_name, r), "empty");

	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 0);

	oakcommon_xml_reader_free(&r);
}
