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

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "node/colormanager.h"
#include "node/node.h"
#include "node/error.h"

// Project C API belongs to another family; the tests drive the native
// object directly as scaffolding (borrowed OakNodeProject handle).
#include "project.h"

#ifndef OAK_OCIO_TEST_CONFIG
#define OAK_OCIO_TEST_CONFIG ""
#endif

namespace
{

/**
 * @brief Fixture ensuring the process-wide default OCIO config resolves
 *
 * olive::Project's constructor initializes its own ColorManager, which
 * builds the default config; point OCIO at the repository's bundled
 * config so that succeeds without the Qt resource extraction path.
 */
class ColorManagerTest : public ::testing::Test {
protected:
	static void SetUpTestSuite()
	{
		if (std::strlen(OAK_OCIO_TEST_CONFIG) > 0) {
			setenv("OCIO", OAK_OCIO_TEST_CONFIG, 1);
		}
	}

	static std::string get_string(int (*fn)(OakNodeColorManager *, char *,
											int),
								  OakNodeColorManager *m)
	{
		int needed = fn(m, nullptr, 0);
		EXPECT_GT(needed, 0);
		std::vector<char> buf(needed);
		EXPECT_EQ(fn(m, buf.data(), needed), needed);
		return std::string(buf.data());
	}
};

} // namespace

TEST_F(ColorManagerTest, InitFree)
{
	olive::Project project;

	int base = oaknode_debug_alive_count();
	OakNodeColorManager *m =
		oaknode_colormanager_init(reinterpret_cast<OakNodeProject *>(&project));
	ASSERT_NE(m, nullptr);
	EXPECT_EQ(oaknode_debug_alive_count(), base + 1);
	oaknode_colormanager_free(m);
	EXPECT_EQ(oaknode_debug_alive_count(), base);

	EXPECT_EQ(oaknode_colormanager_init(nullptr), nullptr);
	oaknode_colormanager_free(nullptr);
}

TEST_F(ColorManagerTest, NullHandleReturnsInvalid)
{
	char buf[64];
	int count;
	EXPECT_EQ(oaknode_colormanager_get_config_filename(nullptr, buf,
													   sizeof(buf)),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_colormanager_initialize(nullptr), OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_colormanager_get_display_count(nullptr, &count),
			  OAKNODE_E_INVALID);
	EXPECT_EQ(oaknode_colormanager_set_config_filename(nullptr, "x"),
			  OAKNODE_E_INVALID);
}

TEST_F(ColorManagerTest, ConfigFilenameRoundTrip)
{
	olive::Project project;
	OakNodeColorManager *m =
		oaknode_colormanager_init(reinterpret_cast<OakNodeProject *>(&project));
	ASSERT_NE(m, nullptr);

	ASSERT_EQ(oaknode_colormanager_set_config_filename(m, "/tmp/myconfig.ocio"),
			  OAKNODE_OK);
	EXPECT_EQ(get_string(oaknode_colormanager_get_config_filename, m),
			  "/tmp/myconfig.ocio");

	EXPECT_EQ(oaknode_colormanager_set_config_filename(m, nullptr),
			  OAKNODE_E_INVALID);

	oaknode_colormanager_free(m);
}

TEST_F(ColorManagerTest, ProjectBackedColorSpaces)
{
	olive::Project project;
	OakNodeColorManager *m =
		oaknode_colormanager_init(reinterpret_cast<OakNodeProject *>(&project));
	ASSERT_NE(m, nullptr);

	// Stored on the project: works even before a config is attached
	ASSERT_EQ(oaknode_colormanager_set_default_input_color_space(m, "Linear"),
			  OAKNODE_OK);
	EXPECT_EQ(get_string(oaknode_colormanager_get_default_input_color_space,
						 m),
			  "Linear");

	// The Project constructor initialized its own manager with the scene
	// linear role as reference space
	std::string ref =
		get_string(oaknode_colormanager_get_reference_color_space, m);
	EXPECT_FALSE(ref.empty());

	oaknode_colormanager_free(m);
}

TEST_F(ColorManagerTest, ConfigDependentCallsRequireConfig)
{
	olive::Project project;
	OakNodeColorManager *m =
		oaknode_colormanager_init(reinterpret_cast<OakNodeProject *>(&project));
	ASSERT_NE(m, nullptr);

	// This manager never got initialize(): no config attached
	int count;
	char buf[64];
	double rgb[3];
	EXPECT_EQ(oaknode_colormanager_get_colorspace_count(m, &count),
			  OAKNODE_E_STATE);
	EXPECT_EQ(oaknode_colormanager_get_display_count(m, &count),
			  OAKNODE_E_STATE);
	EXPECT_EQ(oaknode_colormanager_get_default_display(m, buf, sizeof(buf)),
			  OAKNODE_E_STATE);
	EXPECT_EQ(oaknode_colormanager_get_default_luma_coefs(m, rgb),
			  OAKNODE_E_STATE);
	EXPECT_EQ(oaknode_colormanager_get_compliant_color_space(m, "x", buf,
															 sizeof(buf)),
			  OAKNODE_E_STATE);
	EXPECT_EQ(oaknode_colormanager_get_colorspace_for_ffmpeg_tags(m, 1, 1, buf,
																  sizeof(buf)),
			  OAKNODE_E_STATE);

	oaknode_colormanager_free(m);
}

TEST_F(ColorManagerTest, InitializeAndQueryConfig)
{
	olive::Project project;
	OakNodeColorManager *m =
		oaknode_colormanager_init(reinterpret_cast<OakNodeProject *>(&project));
	ASSERT_NE(m, nullptr);

	ASSERT_EQ(oaknode_colormanager_initialize(m), OAKNODE_OK);

	int count = 0;
	ASSERT_EQ(oaknode_colormanager_get_colorspace_count(m, &count), OAKNODE_OK);
	ASSERT_GT(count, 0);

	int needed = oaknode_colormanager_get_colorspace_at(m, 0, nullptr, 0);
	ASSERT_GT(needed, 1);
	std::vector<char> buf(needed);
	ASSERT_EQ(oaknode_colormanager_get_colorspace_at(m, 0, buf.data(), needed),
			  needed);
	EXPECT_GT(std::strlen(buf.data()), 0U);
	EXPECT_EQ(oaknode_colormanager_get_colorspace_at(m, count, buf.data(),
													 needed),
			  OAKNODE_E_NOT_FOUND);

	int displays = 0;
	ASSERT_EQ(oaknode_colormanager_get_display_count(m, &displays), OAKNODE_OK);
	ASSERT_GT(displays, 0);
	std::string display =
		get_string(oaknode_colormanager_get_default_display, m);
	EXPECT_FALSE(display.empty());

	int views = 0;
	ASSERT_EQ(oaknode_colormanager_get_view_count(m, display.c_str(), &views),
			  OAKNODE_OK);
	ASSERT_GT(views, 0);
	std::string view;
	{
		int vneeded =
			oaknode_colormanager_get_default_view(m, display.c_str(), nullptr, 0);
		ASSERT_GT(vneeded, 0);
		std::vector<char> vbuf(vneeded);
		ASSERT_EQ(oaknode_colormanager_get_default_view(m, display.c_str(),
														vbuf.data(), vneeded),
				  vneeded);
		view = vbuf.data();
	}
	EXPECT_FALSE(view.empty());

	int looks = -1;
	ASSERT_EQ(oaknode_colormanager_get_look_count(m, &looks), OAKNODE_OK);
	EXPECT_GE(looks, 0);

	double rgb[3] = { 0.0, 0.0, 0.0 };
	ASSERT_EQ(oaknode_colormanager_get_default_luma_coefs(m, rgb), OAKNODE_OK);
	EXPECT_GT(rgb[0], 0.0);
	EXPECT_GT(rgb[1], 0.0);
	EXPECT_GT(rgb[2], 0.0);

	// Unknown tags map to the empty string (required size 1 = just NUL)
	EXPECT_EQ(oaknode_colormanager_get_colorspace_for_ffmpeg_tags(m, 0, 0,
																  nullptr, 0),
			  1);

	// A colorspace the config does not contain falls back to the default
	std::string compliant;
	{
		int cneeded = oaknode_colormanager_get_compliant_color_space(
			m, "NoSuchColorSpace", nullptr, 0);
		ASSERT_GT(cneeded, 1);
		std::vector<char> cbuf(cneeded);
		ASSERT_EQ(oaknode_colormanager_get_compliant_color_space(
					  m, "NoSuchColorSpace", cbuf.data(), cneeded),
				  cneeded);
		compliant = cbuf.data();
	}
	EXPECT_EQ(compliant,
			  get_string(oaknode_colormanager_get_default_input_color_space,
						 m));

	oaknode_colormanager_free(m);
}

TEST_F(ColorManagerTest, CompliantColorTransform)
{
	olive::Project project;
	OakNodeColorManager *m =
		oaknode_colormanager_init(reinterpret_cast<OakNodeProject *>(&project));
	ASSERT_NE(m, nullptr);
	ASSERT_EQ(oaknode_colormanager_initialize(m), OAKNODE_OK);

	std::string display =
		get_string(oaknode_colormanager_get_default_display, m);
	ASSERT_FALSE(display.empty());

	OakCommonColorTransform *t = oakcommon_colortransform_init_display(
		display.c_str(), "No Such View", "");
	ASSERT_NE(t, nullptr);

	OakCommonColorTransform *compliant = nullptr;
	ASSERT_EQ(oaknode_colormanager_get_compliant_color_transform(m, t, 0,
																 &compliant),
			  OAKNODE_OK);
	ASSERT_NE(compliant, nullptr);

	int is_display = 0;
	ASSERT_EQ(oakcommon_colortransform_is_display(compliant, &is_display),
			  OAKCOMMON_OK);
	EXPECT_EQ(is_display, 1);

	// The bogus view was replaced by the config's default view
	int needed = oakcommon_colortransform_get_view(compliant, nullptr, 0);
	ASSERT_GT(needed, 0);
	std::vector<char> buf(needed);
	ASSERT_EQ(oakcommon_colortransform_get_view(compliant, buf.data(), needed),
			  needed);
	EXPECT_STRNE(buf.data(), "No Such View");

	oakcommon_colortransform_free(compliant);
	oakcommon_colortransform_free(t);

	// Error paths
	EXPECT_EQ(oaknode_colormanager_get_compliant_color_transform(m, nullptr, 0,
																 &compliant),
			  OAKNODE_E_INVALID);

	oaknode_colormanager_free(m);
}
