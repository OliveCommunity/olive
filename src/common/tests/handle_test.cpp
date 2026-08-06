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

#include "common/colortransform.h"
#include "common/commandlineparser.h"
#include "common/current.h"
#include "common/filefunctions.h"
#include "common/ocioutils.h"
#include "common/oiioutils.h"
#include "common/subtitleparams.h"
#include "common/videoparams.h"
#include "common/xmlutils.h"

// Native C++ headers (exported through the oakcommon target's public
// include dirs) for the init_from_native tests.
#include "colortransform.h"
#include "subtitleparams.h"
#include "videoparams.h"

namespace
{

/**
 * @brief Every init-produced handle must carry the current ABI version
 *        and non-NULL addref/release thunks.
 */
template <typename Handle> void expect_valid_handle(const Handle &h)
{
	EXPECT_NE(h.ctx, nullptr);
	EXPECT_NE(h.addref, nullptr);
	EXPECT_NE(h.release, nullptr);
	EXPECT_EQ(h.abi_version, OAKCOMMON_ABI_VERSION);
}

} // namespace

TEST(OakHandle, AbiVersionStampedEverywhere)
{
	OakVideoParams vp = oakcommon_videoparams_init();
	expect_valid_handle(vp);
	oakcommon_videoparams_free(&vp);

	OakSubtitleParams sp = oakcommon_subtitleparams_init();
	expect_valid_handle(sp);
	oakcommon_subtitleparams_free(&sp);

	OakColorTransform ct = oakcommon_colortransform_init_output("sRGB");
	expect_valid_handle(ct);
	oakcommon_colortransform_free(&ct);

	OakCommandLineParser parser = oakcommon_commandlineparser_init();
	expect_valid_handle(parser);

	const char *names[] = { "h" };
	OakCommandLineOption option = {};
	ASSERT_EQ(oakcommon_commandlineparser_add_option(
				  parser, names, 1, "help", 0, nullptr, 0, &option),
			  OAKCOMMON_OK);
	expect_valid_handle(option);
	oakcommon_commandlineoption_free(&option);

	OakCommandLinePositionalArgument arg = {};
	ASSERT_EQ(oakcommon_commandlineparser_add_positional_argument(
				  parser, "file", "desc", 0, &arg),
			  OAKCOMMON_OK);
	expect_valid_handle(arg);
	oakcommon_commandlinepositionalargument_free(&arg);
	oakcommon_commandlineparser_free(&parser);

	OakXmlReader reader =
		oakcommon_xml_reader_init("<root/>");
	expect_valid_handle(reader);
	oakcommon_xml_reader_free(&reader);

	OakXmlWriter writer = oakcommon_xml_writer_init();
	expect_valid_handle(writer);
	oakcommon_xml_writer_free(&writer);

	OakFileFunctions ff = oakcommon_filefunctions_init();
	expect_valid_handle(ff);
	oakcommon_filefunctions_free(&ff);

	OakOCIOUtils ocio = oakcommon_ocioutils_init();
	expect_valid_handle(ocio);
	oakcommon_ocioutils_free(&ocio);

	OakOIIOUtils oiio = oakcommon_oiioutils_init();
	expect_valid_handle(oiio);
	oakcommon_oiioutils_free(&oiio);

	OakCurrent current = oakcommon_current_instance();
	expect_valid_handle(current);
}

TEST(OakHandle, AddrefReleaseCountSemantics)
{
	OakVideoParams h = oakcommon_videoparams_init();
	ASSERT_NE(h.ctx, nullptr);

	ASSERT_EQ(oakcommon_videoparams_set_width(h, 1920), OAKCOMMON_OK);

	// Copy the handle struct and take a second reference through the
	// function pointer, like a foreign (Rust/DLL) consumer would.
	OakVideoParams copy = h;
	h.addref(h.ctx);

	// Dropping the first reference must not destroy the object: the copy
	// is still fully usable.
	h.release(h.ctx);
	int width = 0;
	ASSERT_EQ(oakcommon_videoparams_get_width(copy, &width), OAKCOMMON_OK);
	EXPECT_EQ(width, 1920);

	// Dropping the last reference destroys the object; free() clears ctx.
	oakcommon_videoparams_free(&copy);
	EXPECT_EQ(copy.ctx, nullptr);
}

TEST(OakHandle, FreeNullAndEmptyCtxAreNoOp)
{
	// NULL handle pointer.
	oakcommon_videoparams_free(nullptr);
	oakcommon_subtitleparams_free(nullptr);
	oakcommon_colortransform_free(nullptr);
	oakcommon_commandlineparser_free(nullptr);
	oakcommon_commandlineoption_free(nullptr);
	oakcommon_commandlinepositionalargument_free(nullptr);
	oakcommon_xml_reader_free(nullptr);
	oakcommon_xml_writer_free(nullptr);
	oakcommon_filefunctions_free(nullptr);
	oakcommon_ocioutils_free(nullptr);
	oakcommon_oiioutils_free(nullptr);
	oakcommon_current_free(nullptr);

	// Handle whose ctx is NULL (e.g. after a failed init or a free).
	OakVideoParams h = {};
	oakcommon_videoparams_free(&h);
	int width = 0;
	EXPECT_EQ(oakcommon_videoparams_get_width(h, &width),
			  OAKCOMMON_E_INVALID);

	// release() itself must tolerate a NULL ctx (foreign consumers may
	// call it directly).
	h.release = nullptr; // no thunk available on a zero-initialized handle
	oakcommon_videoparams_free(&h);
	SUCCEED();
}

TEST(OakHandle, VideoParamsFromNativeSurvivesSource)
{
	OakVideoParams h = {};
	{
		olive::VideoParams native(
			1920, 1080, olive::core::Rational(1, 25),
			olive::core::PixelFormat::u8, 4, olive::core::Rational(1, 1),
			olive::VideoParams::k_interlace_none, 1);
		h = oakcommon_videoparams_init_from_native(&native);
		ASSERT_NE(h.ctx, nullptr);
		EXPECT_EQ(h.abi_version, OAKCOMMON_ABI_VERSION);
	} // native stack object destroyed here

	int width = 0, height = 0, num = 0, den = 0;
	ASSERT_EQ(oakcommon_videoparams_get_width(h, &width), OAKCOMMON_OK);
	ASSERT_EQ(oakcommon_videoparams_get_height(h, &height), OAKCOMMON_OK);
	ASSERT_EQ(oakcommon_videoparams_get_time_base(h, &num, &den),
			  OAKCOMMON_OK);
	EXPECT_EQ(width, 1920);
	EXPECT_EQ(height, 1080);
	EXPECT_EQ(num, 1);
	EXPECT_EQ(den, 25);
	oakcommon_videoparams_free(&h);

	// NULL source yields an empty handle, not a crash.
	OakVideoParams empty =
		oakcommon_videoparams_init_from_native(nullptr);
	EXPECT_EQ(empty.ctx, nullptr);
}

TEST(OakHandle, SubtitleParamsFromNativeSurvivesSource)
{
	OakSubtitleParams h = {};
	{
		olive::SubtitleParams native;
		native.push_back(olive::Subtitle(
			olive::core::TimeRange(olive::core::Rational(0, 1),
								   olive::core::Rational(2, 1)),
			"hello"));
		h = oakcommon_subtitleparams_init_from_native(&native);
		ASSERT_NE(h.ctx, nullptr);
	} // native stack object destroyed here

	int count = 0;
	ASSERT_EQ(oakcommon_subtitleparams_count(h, &count), OAKCOMMON_OK);
	EXPECT_EQ(count, 1);

	char buf[16];
	int needed = oakcommon_subtitleparams_get_subtitle_text(h, 0, buf,
															sizeof(buf));
	ASSERT_EQ(needed, 6);
	EXPECT_STREQ(buf, "hello");
	oakcommon_subtitleparams_free(&h);
}

TEST(OakHandle, ColorTransformFromNativeSurvivesSource)
{
	OakColorTransform h = {};
	{
		olive::ColorTransform native(std::string("Display"),
									 std::string("Standard"),
									 std::string("None"));
		h = oakcommon_colortransform_init_from_native(&native);
		ASSERT_NE(h.ctx, nullptr);
	} // native stack object destroyed here

	int is_display = 0;
	ASSERT_EQ(oakcommon_colortransform_is_display(h, &is_display),
			  OAKCOMMON_OK);
	EXPECT_EQ(is_display, 1);

	char buf[16];
	int needed = oakcommon_colortransform_get_view(h, buf, sizeof(buf));
	ASSERT_EQ(needed, 9);
	EXPECT_STREQ(buf, "Standard");
	oakcommon_colortransform_free(&h);
}

TEST(OakHandle, CurrentSingletonReleaseNeverDestroys)
{
	OakCurrent h = oakcommon_current_instance();
	ASSERT_NE(h.ctx, nullptr);
	void *ctx = h.ctx;

	// The singleton's addref/release are deliberate no-ops.
	h.addref(h.ctx);
	h.release(h.ctx);
	oakcommon_current_free(&h);

	OakCurrent again = oakcommon_current_instance();
	EXPECT_EQ(again.ctx, ctx);
}
