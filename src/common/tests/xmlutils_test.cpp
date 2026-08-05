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

std::string read_string(int (*fn)(OakCommonXmlReader *, char *, int),
						OakCommonXmlReader *reader)
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
	EXPECT_EQ(oakcommon_xml_reader_init(nullptr), nullptr);
}

TEST(CommonXmlUtilsCApi, ReaderFreeNull)
{
	oakcommon_xml_reader_free(nullptr);
}

TEST(CommonXmlUtilsCApi, ReadNextStartElement)
{
	OakCommonXmlReader *r =
		oakcommon_xml_reader_init("<root><child>value</child></root>");
	ASSERT_NE(r, nullptr);

	int found = 0;
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 1);
	EXPECT_EQ(read_string(oakcommon_xml_reader_name, r), "root");

	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 1);
	EXPECT_EQ(read_string(oakcommon_xml_reader_name, r), "child");

	oakcommon_xml_reader_free(r);
}

TEST(CommonXmlUtilsCApi, ReadNextStartElementNullHandle)
{
	int found = 0;
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(nullptr, &found),
			  OAKCOMMON_E_INVALID);

	OakCommonXmlReader *r = oakcommon_xml_reader_init("<root/>");
	ASSERT_NE(r, nullptr);
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, nullptr),
			  OAKCOMMON_E_INVALID);
	oakcommon_xml_reader_free(r);
}

TEST(CommonXmlUtilsCApi, ReadNextStartElementReturnsFalseAtEnd)
{
	OakCommonXmlReader *r = oakcommon_xml_reader_init("<root/>");
	ASSERT_NE(r, nullptr);

	int found = -1;
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 1);
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 0);

	oakcommon_xml_reader_free(r);
}

TEST(CommonXmlUtilsCApi, NameNullHandle)
{
	char buf[16];
	EXPECT_EQ(oakcommon_xml_reader_name(nullptr, buf, sizeof(buf)),
			  OAKCOMMON_E_INVALID);
}

TEST(CommonXmlUtilsCApi, ReadElementText)
{
	OakCommonXmlReader *r =
		oakcommon_xml_reader_init("<root><child>a &amp; b</child></root>");
	ASSERT_NE(r, nullptr);

	int found = 0;
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);
	EXPECT_EQ(found, 1);
	EXPECT_EQ(read_string(oakcommon_xml_reader_read_element_text, r),
			  "a & b");

	oakcommon_xml_reader_free(r);
}

TEST(CommonXmlUtilsCApi, ReadElementTextNullHandle)
{
	char buf[16];
	EXPECT_EQ(oakcommon_xml_reader_read_element_text(nullptr, buf,
													 sizeof(buf)),
			  OAKCOMMON_E_INVALID);
}

TEST(CommonXmlUtilsCApi, SkipCurrentElement)
{
	OakCommonXmlReader *r = oakcommon_xml_reader_init(
		"<root><unknown><nested/></unknown><known/></root>");
	ASSERT_NE(r, nullptr);

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

	oakcommon_xml_reader_free(r);
}

TEST(CommonXmlUtilsCApi, SkipCurrentElementNullHandle)
{
	EXPECT_EQ(oakcommon_xml_reader_skip_current_element(nullptr),
			  OAKCOMMON_E_INVALID);
}

TEST(CommonXmlUtilsCApi, Attributes)
{
	OakCommonXmlReader *r = oakcommon_xml_reader_init(
		"<root><item id=\"7\" name=\"a&quot;b\"/></root>");
	ASSERT_NE(r, nullptr);

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

	oakcommon_xml_reader_free(r);
}

TEST(CommonXmlUtilsCApi, AttributeErrorPaths)
{
	EXPECT_EQ(oakcommon_xml_reader_attribute_count(nullptr, nullptr),
			  OAKCOMMON_E_INVALID);

	OakCommonXmlReader *r = oakcommon_xml_reader_init("<root a=\"1\"/>");
	ASSERT_NE(r, nullptr);
	int found = 0;
	EXPECT_EQ(oakcommon_xml_reader_read_next_start_element(r, &found),
			  OAKCOMMON_OK);

	char buf[16];
	EXPECT_EQ(oakcommon_xml_reader_attribute_name(r, 5, buf, sizeof(buf)),
			  OAKCOMMON_E_NOT_FOUND);
	EXPECT_EQ(oakcommon_xml_reader_attribute_value(r, -1, buf, sizeof(buf)),
			  OAKCOMMON_E_NOT_FOUND);
	EXPECT_EQ(oakcommon_xml_reader_attribute_name(nullptr, 0, buf,
												  sizeof(buf)),
			  OAKCOMMON_E_INVALID);

	oakcommon_xml_reader_free(r);
}

TEST(CommonXmlUtilsCApi, HasError)
{
	OakCommonXmlReader *bad =
		oakcommon_xml_reader_init("<root><unclosed></root>");
	ASSERT_NE(bad, nullptr);
	int has_error = 0;
	EXPECT_EQ(oakcommon_xml_reader_has_error(bad, &has_error), OAKCOMMON_OK);
	EXPECT_EQ(has_error, 1);
	oakcommon_xml_reader_free(bad);

	OakCommonXmlReader *good = oakcommon_xml_reader_init("<root/>");
	ASSERT_NE(good, nullptr);
	EXPECT_EQ(oakcommon_xml_reader_has_error(good, &has_error), OAKCOMMON_OK);
	EXPECT_EQ(has_error, 0);
	EXPECT_EQ(oakcommon_xml_reader_has_error(nullptr, &has_error),
			  OAKCOMMON_E_INVALID);
	oakcommon_xml_reader_free(good);
}

TEST(CommonXmlUtilsCApi, WriterFreeNull)
{
	oakcommon_xml_writer_free(nullptr);
}

TEST(CommonXmlUtilsCApi, WriterNullHandleAndArgs)
{
	char buf[16];
	EXPECT_EQ(oakcommon_xml_writer_write_start_element(nullptr, "a"),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_xml_writer_write_end_element(nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_xml_writer_write_end_document(nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_xml_writer_output(nullptr, buf, sizeof(buf)),
			  OAKCOMMON_E_INVALID);

	OakCommonXmlWriter *w = oakcommon_xml_writer_init();
	ASSERT_NE(w, nullptr);
	EXPECT_EQ(oakcommon_xml_writer_write_start_element(w, nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_xml_writer_write_attribute(w, "a", nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_xml_writer_write_characters(w, nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_xml_writer_write_text_element(w, nullptr, "x"),
			  OAKCOMMON_E_INVALID);
	oakcommon_xml_writer_free(w);
}

TEST(CommonXmlUtilsCApi, WriterRoundTrip)
{
	OakCommonXmlWriter *w = oakcommon_xml_writer_init();
	ASSERT_NE(w, nullptr);

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
	oakcommon_xml_writer_free(w);

	// Read the produced document back.
	OakCommonXmlReader *r = oakcommon_xml_reader_init(buf.data());
	ASSERT_NE(r, nullptr);
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

	oakcommon_xml_reader_free(r);
}
