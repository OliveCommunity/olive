#include <gtest/gtest.h>

#include <QFile>
#include <QIcon>
#include <QVector>

#include "ui/icons/icons.h"

TEST(UIIcons, ThemeResourcesAreRegistered)
{
	// Both themes compile their icons into the binary via AUTORCC resources
	EXPECT_TRUE(
		QFile::exists(QStringLiteral(":/style/olive-dark/png/play.16.png")));
	EXPECT_TRUE(
		QFile::exists(QStringLiteral(":/style/olive-light/png/play.16.png")));
	EXPECT_TRUE(QFile::exists(QStringLiteral(":/style/olive-dark/palette.ini")));
	EXPECT_TRUE(QFile::exists(QStringLiteral(":/style/olive-dark/style.css")));
}

TEST(UIIcons, CreateLoadsAllSizes)
{
	QIcon icon = olive::icon::create(QStringLiteral(":/style/olive-dark"),
									 QStringLiteral("play"));
	ASSERT_FALSE(icon.isNull());

	// genicons.sh generates 16/32/64/128 px variants of every icon
	const QList<QSize> sizes = icon.availableSizes();
	for (int sz : { 16, 32, 64, 128 }) {
		EXPECT_TRUE(sizes.contains(QSize(sz, sz))) << "Missing size " << sz;
	}

	EXPECT_FALSE(icon.pixmap(QSize(32, 32)).isNull());
}

TEST(UIIcons, CreateWithUnknownNameYieldsNoUsableIcon)
{
	QIcon icon = olive::icon::create(QStringLiteral(":/style/olive-dark"),
									 QStringLiteral("no-such-icon"));

	// QIcon::addFile() differs across Qt builds in whether entries for
	// nonexistent files keep the icon "null". What matters is that no usable
	// pixmap can be produced for an unknown icon name.
	EXPECT_TRUE(icon.availableSizes().isEmpty());
	EXPECT_TRUE(icon.pixmap(QSize(16, 16)).isNull());
	EXPECT_TRUE(icon.pixmap(32, 32, QIcon::Disabled).isNull());
}

TEST(UIIcons, LoadAllPopulatesGlobalIcons)
{
	olive::icon::load_all(QStringLiteral(":/style/olive-dark"));

	const QVector<QIcon *> all = { &olive::icon::go_to_start,
								   &olive::icon::prev_frame,
								   &olive::icon::play,
								   &olive::icon::pause,
								   &olive::icon::next_frame,
								   &olive::icon::go_to_end,
								   &olive::icon::New,
								   &olive::icon::open,
								   &olive::icon::save,
								   &olive::icon::undo,
								   &olive::icon::redo,
								   &olive::icon::tree_view,
								   &olive::icon::list_view,
								   &olive::icon::icon_view,
								   &olive::icon::tool_pointer,
								   &olive::icon::tool_edit,
								   &olive::icon::tool_ripple,
								   &olive::icon::tool_rolling,
								   &olive::icon::tool_razor,
								   &olive::icon::tool_slip,
								   &olive::icon::tool_slide,
								   &olive::icon::tool_hand,
								   &olive::icon::tool_transition,
								   &olive::icon::tool_track_select,
								   &olive::icon::folder,
								   &olive::icon::sequence,
								   &olive::icon::video,
								   &olive::icon::audio,
								   &olive::icon::image,
								   &olive::icon::mini_map,
								   &olive::icon::tri_up,
								   &olive::icon::tri_left,
								   &olive::icon::tri_down,
								   &olive::icon::tri_right,
								   &olive::icon::text_bold,
								   &olive::icon::text_italic,
								   &olive::icon::text_underline,
								   &olive::icon::text_strikethrough,
								   &olive::icon::text_small_caps,
								   &olive::icon::text_align_left,
								   &olive::icon::text_align_right,
								   &olive::icon::text_align_center,
								   &olive::icon::text_align_justify,
								   &olive::icon::text_align_top,
								   &olive::icon::text_align_bottom,
								   &olive::icon::text_align_middle,
								   &olive::icon::snapping,
								   &olive::icon::zoom_in,
								   &olive::icon::zoom_out,
								   &olive::icon::record,
								   &olive::icon::add,
								   &olive::icon::error,
								   &olive::icon::dir_up,
								   &olive::icon::clock,
								   &olive::icon::diamond,
								   &olive::icon::plus,
								   &olive::icon::minus,
								   &olive::icon::add_effect,
								   &olive::icon::eye_opened,
								   &olive::icon::eye_closed,
								   &olive::icon::lock_opened,
								   &olive::icon::lock_closed,
								   &olive::icon::pencil,
								   &olive::icon::subtitles,
								   &olive::icon::color_picker };

	for (const QIcon *icon : all) {
		EXPECT_FALSE(icon->isNull());
	}
}
