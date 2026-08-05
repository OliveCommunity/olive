#include <gtest/gtest.h>

#include <vector>

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QSignalSpy>

#include "render/videoparams.h"
#include "ui/humanstrings.h"
#include "widget/standardcombos/standardcombos.h"

// These tests complement widget_combos_test.cpp, which already covers item
// counts and basic value round-trips. Here we cover item text/data contents,
// signal emissions, and the stateful edge cases (custom entries, restore
// behavior, language changes).

namespace
{

// QComboBox::currentIndexChanged is overloaded; spy on the int form
QSignalSpy *index_spy(QComboBox *combo)
{
	return new QSignalSpy(combo,
						  static_cast<void (QComboBox::*)(int)>(
							  &QComboBox::currentIndexChanged));
}

} // namespace

TEST(WidgetStandardCombos, SampleRateTextsAndDataMatchHumanStrings)
{
	olive::SampleRateComboBox combo;

	ASSERT_EQ(combo.count(),
			  int(olive::AudioParams::k_supported_sample_rates.size()));

	for (int i = 0; i < combo.count(); i++) {
		const int rate = olive::AudioParams::k_supported_sample_rates.at(i);
		EXPECT_EQ(combo.itemData(i).toInt(), rate);
		EXPECT_EQ(combo.itemText(i),
				  olive::HumanStrings::sample_rate_to_string(rate));
		EXPECT_FALSE(combo.itemText(i).isEmpty());
	}
}

TEST(WidgetStandardCombos, SampleRateSetEmitsIndexChanged)
{
	olive::SampleRateComboBox combo;
	combo.set_sample_rate(olive::AudioParams::k_supported_sample_rates.back());

	QSignalSpy *spy = index_spy(&combo);
	combo.set_sample_rate(olive::AudioParams::k_supported_sample_rates.front());

	ASSERT_EQ(spy->count(), 1);
	EXPECT_EQ(spy->first().first().toInt(), 0);
	delete spy;
}

TEST(WidgetStandardCombos, SampleRateUnknownRateLeavesSelection)
{
	olive::SampleRateComboBox combo;
	combo.set_sample_rate(olive::AudioParams::k_supported_sample_rates.back());

	int sentinel = 7;
	while (combo.findData(sentinel) != -1) {
		sentinel++;
	}

	const int old_index = combo.currentIndex();
	combo.set_sample_rate(sentinel);

	EXPECT_EQ(combo.currentIndex(), old_index);
	EXPECT_EQ(combo.get_sample_rate(),
			  olive::AudioParams::k_supported_sample_rates.back());
}

TEST(WidgetStandardCombos, ChannelLayoutTextsAndDataMatchHumanStrings)
{
	olive::ChannelLayoutComboBox combo;

	ASSERT_EQ(combo.count(),
			  int(olive::AudioParams::k_supported_channel_layouts.size()));

	for (int i = 0; i < combo.count(); i++) {
		const uint64_t layout =
			olive::AudioParams::k_supported_channel_layouts.at(i);
		EXPECT_EQ(combo.itemData(i).toULongLong(), layout);
		EXPECT_EQ(combo.itemText(i),
				  olive::HumanStrings::channel_layout_to_string(layout));
		EXPECT_FALSE(combo.itemText(i).isEmpty());
	}
}

TEST(WidgetStandardCombos, ChannelLayoutSetEmitsIndexChanged)
{
	olive::ChannelLayoutComboBox combo;
	combo.set_channel_layout(
		olive::AudioParams::k_supported_channel_layouts.back());

	QSignalSpy *spy = index_spy(&combo);
	combo.set_channel_layout(
		olive::AudioParams::k_supported_channel_layouts.front());

	ASSERT_EQ(spy->count(), 1);
	EXPECT_EQ(spy->first().first().toInt(), 0);
	delete spy;
}

TEST(WidgetStandardCombos, ChannelLayoutUnknownLayoutLeavesSelection)
{
	olive::ChannelLayoutComboBox combo;
	combo.set_channel_layout(
		olive::AudioParams::k_supported_channel_layouts.back());

	const uint64_t sentinel = ~uint64_t(0);
	for (int i = 0; i < combo.count(); i++) {
		ASSERT_NE(combo.itemData(i).toULongLong(), sentinel);
	}

	const int old_index = combo.currentIndex();
	combo.set_channel_layout(sentinel);

	EXPECT_EQ(combo.currentIndex(), old_index);
	EXPECT_EQ(combo.get_channel_layout(),
			  olive::AudioParams::k_supported_channel_layouts.back());
}

TEST(WidgetStandardCombos, InterlacedTextsMatchEnumOrder)
{
	olive::InterlacedComboBox combo;

	ASSERT_EQ(combo.count(), 3);
	EXPECT_EQ(combo.itemText(int(olive::VideoParams::k_interlace_none)),
			  QStringLiteral("None (Progressive)"));
	EXPECT_EQ(combo.itemText(int(olive::VideoParams::k_interlaced_top_first)),
			  QStringLiteral("Top-Field First"));
	EXPECT_EQ(
		combo.itemText(int(olive::VideoParams::k_interlaced_bottom_first)),
		QStringLiteral("Bottom-Field First"));
}

TEST(WidgetStandardCombos, InterlacedSetEmitsIndexChanged)
{
	olive::InterlacedComboBox combo;

	QSignalSpy *spy = index_spy(&combo);
	combo.set_interlace_mode(olive::VideoParams::k_interlaced_bottom_first);

	ASSERT_EQ(spy->count(), 1);
	EXPECT_EQ(spy->first().first().toInt(),
			  int(olive::VideoParams::k_interlaced_bottom_first));
	delete spy;
}

TEST(WidgetStandardCombos, PixelAspectRatioEntriesCarryNamesAndRatios)
{
	olive::PixelAspectRatioComboBox combo;

	const QVector<olive::Rational> &standards =
		olive::VideoParams::k_standard_pixel_aspects;
	const QStringList names =
		olive::VideoParams::get_standard_pixel_aspect_ratio_names();
	ASSERT_EQ(combo.count(), standards.size() + 1);
	ASSERT_EQ(names.size(), standards.size());

	for (int i = 0; i < standards.size(); i++) {
		EXPECT_EQ(combo.itemData(i).value<olive::Rational>(), standards.at(i));
		EXPECT_EQ(combo.itemText(i), names.at(i));
	}

	// Default selection is the first standard ratio
	EXPECT_EQ(combo.currentIndex(), 0);
	EXPECT_EQ(combo.get_pixel_aspect_ratio(), standards.first());
}

TEST(WidgetStandardCombos, PixelAspectRatioCustomEntryDefaultsToSquare)
{
	olive::PixelAspectRatioComboBox combo;

	const int last = combo.count() - 1;
	EXPECT_EQ(combo.itemText(last), QStringLiteral("Custom..."));

	// A null custom ratio is backed by 1:1 so it can never produce a 0 PAR
	const olive::Rational data =
		combo.itemData(last).value<olive::Rational>();
	EXPECT_DOUBLE_EQ(data.to_double(), 1.0);
}

TEST(WidgetStandardCombos, PixelAspectRatioCustomSetRenamesLastEntry)
{
	olive::PixelAspectRatioComboBox combo;

	const QVector<olive::Rational> &standards =
		olive::VideoParams::k_standard_pixel_aspects;
	const int last = combo.count() - 1;

	const olive::Rational custom(17, 13);
	combo.set_pixel_aspect_ratio(custom);

	EXPECT_EQ(combo.currentIndex(), last);
	EXPECT_TRUE(
		combo.itemText(last).startsWith(QStringLiteral("Custom (")));
	EXPECT_EQ(combo.itemData(last).value<olive::Rational>(), custom);

	// Returning to a standard ratio selects it but keeps the custom label
	combo.set_pixel_aspect_ratio(standards.first());
	EXPECT_EQ(combo.currentIndex(), 0);
	EXPECT_EQ(combo.get_pixel_aspect_ratio(), standards.first());
	EXPECT_TRUE(
		combo.itemText(last).startsWith(QStringLiteral("Custom (")));
}

TEST(WidgetStandardCombos, PixelAspectRatioSelectingStandardDoesNotPrompt)
{
	olive::PixelAspectRatioComboBox combo;

	const QVector<olive::Rational> &standards =
		olive::VideoParams::k_standard_pixel_aspects;
	ASSERT_GE(standards.size(), 2);

	// A user-style index change to a standard entry must not open the
	// custom-ratio dialog (selecting the last entry would, so it is
	// intentionally not exercised under the offscreen platform)
	combo.setCurrentIndex(1);

	EXPECT_EQ(combo.get_pixel_aspect_ratio(), standards.at(1));
	EXPECT_EQ(combo.itemText(combo.count() - 1),
			  QStringLiteral("Custom..."));
}

TEST(WidgetStandardCombos, PixelFormatEntriesOrderedWithNames)
{
	olive::PixelFormatComboBox combo(false);

	// Preview formats u8..f32 are added in enum order
	ASSERT_EQ(combo.count(), int(olive::core::PixelFormat::count));
	for (int i = 0; i < combo.count(); i++) {
		EXPECT_EQ(combo.itemData(i).toInt(), i);
		const auto fmt = static_cast<olive::core::PixelFormat::Format>(i);
		EXPECT_EQ(combo.itemText(i),
				  olive::VideoParams::get_format_name(fmt));
		EXPECT_FALSE(combo.itemText(i).isEmpty());
	}

	// Default selection is the first (u8) format
	EXPECT_EQ(static_cast<olive::core::PixelFormat::Format>(
				  combo.get_pixel_format()),
			  olive::core::PixelFormat::u8);
}

TEST(WidgetStandardCombos, PixelFormatSetEmitsIndexChanged)
{
	olive::PixelFormatComboBox combo(false);

	QSignalSpy *spy = index_spy(&combo);
	combo.set_pixel_format(olive::core::PixelFormat::f32);

	ASSERT_EQ(spy->count(), 1);
	EXPECT_EQ(spy->first().first().toInt(),
			  int(olive::core::PixelFormat::f32));
	delete spy;
}

TEST(WidgetStandardCombos, PixelFormatFloatOnlyIgnoresIntegerFormat)
{
	olive::PixelFormatComboBox combo(true);
	ASSERT_GT(combo.count(), 0);

	// u8 is not a float format, so it cannot be selected here
	combo.set_pixel_format(olive::core::PixelFormat::u8);

	olive::core::PixelFormat selected =
		static_cast<olive::core::PixelFormat::Format>(
			combo.get_pixel_format());
	EXPECT_TRUE(selected.is_float());
	EXPECT_EQ(combo.currentIndex(), 0);
}

TEST(WidgetStandardCombos, SampleFormatStartsEmpty)
{
	olive::SampleFormatComboBox combo;
	EXPECT_EQ(combo.count(), 0);
}

TEST(WidgetStandardCombos, SampleFormatAvailableFormatsPopulateInOrder)
{
	using SampleFormat = olive::core::SampleFormat;

	olive::SampleFormatComboBox combo;

	const std::vector<SampleFormat> formats = {
		SampleFormat::u8, SampleFormat::s16, SampleFormat::f32
	};
	combo.set_available_formats(formats);

	ASSERT_EQ(combo.count(), int(formats.size()));
	for (int i = 0; i < combo.count(); i++) {
		EXPECT_EQ(combo.itemData(i).toInt(),
				  int(static_cast<SampleFormat::Format>(formats.at(i))));
		EXPECT_EQ(combo.itemText(i),
				  olive::HumanStrings::format_to_string(formats.at(i)));
	}

	// The first entry is selected by default
	EXPECT_EQ(static_cast<SampleFormat::Format>(combo.get_sample_format()),
			  SampleFormat::u8);
}

TEST(WidgetStandardCombos, SampleFormatRestoreDisabledReselectsFirst)
{
	using Format = olive::core::SampleFormat::Format;

	olive::SampleFormatComboBox combo;
	combo.set_attempt_to_restore_format(false);

	combo.set_packed_formats();
	combo.set_sample_format(olive::core::SampleFormat::f32);
	ASSERT_EQ(static_cast<Format>(combo.get_sample_format()),
			  olive::core::SampleFormat::f32);

	// With restore disabled, repopulating falls back to the first entry
	combo.set_packed_formats();
	EXPECT_EQ(combo.currentIndex(), 0);
	EXPECT_EQ(static_cast<Format>(combo.get_sample_format()),
			  static_cast<Format>(olive::core::SampleFormat::packed_start));
}

TEST(WidgetStandardCombos, SampleFormatSetEmitsIndexChanged)
{
	olive::SampleFormatComboBox combo;
	combo.set_packed_formats();

	QSignalSpy *spy = index_spy(&combo);
	combo.set_sample_format(olive::core::SampleFormat::f32);

	ASSERT_EQ(spy->count(), 1);
	EXPECT_EQ(spy->first().first().toInt(),
			  int(olive::core::SampleFormat::f32) -
				  int(olive::core::SampleFormat::packed_start));
	delete spy;
}

TEST(WidgetStandardCombos, VideoDividerTextsAndDataMatch)
{
	olive::VideoDividerComboBox combo;

	ASSERT_EQ(combo.count(), olive::VideoParams::k_supported_dividers.size());

	for (int i = 0; i < combo.count(); i++) {
		const int divider = olive::VideoParams::k_supported_dividers.at(i);
		EXPECT_EQ(combo.itemData(i).toInt(), divider);
		EXPECT_EQ(combo.itemText(i),
				  olive::VideoParams::get_name_for_divider(divider));
		EXPECT_FALSE(combo.itemText(i).isEmpty());
	}
}

TEST(WidgetStandardCombos, VideoDividerSetEmitsIndexChanged)
{
	olive::VideoDividerComboBox combo;
	combo.set_divider(olive::VideoParams::k_supported_dividers.last());

	QSignalSpy *spy = index_spy(&combo);
	combo.set_divider(olive::VideoParams::k_supported_dividers.first());

	ASSERT_EQ(spy->count(), 1);
	EXPECT_EQ(spy->first().first().toInt(), 0);
	delete spy;
}

TEST(WidgetStandardCombos, VideoDividerUnknownDividerLeavesSelection)
{
	olive::VideoDividerComboBox combo;
	combo.set_divider(olive::VideoParams::k_supported_dividers.last());

	int sentinel = 3;
	while (combo.findData(sentinel) != -1) {
		sentinel += 2;
	}

	const int old_index = combo.currentIndex();
	combo.set_divider(sentinel);

	EXPECT_EQ(combo.currentIndex(), old_index);
	EXPECT_EQ(combo.get_divider(),
			  olive::VideoParams::k_supported_dividers.last());
}

TEST(WidgetStandardCombos, FrameRateHasTrailingCustomEntry)
{
	olive::FrameRateComboBox combo;

	QComboBox *inner = combo.findChild<QComboBox *>();
	ASSERT_NE(inner, nullptr);

	const QVector<olive::Rational> &standards =
		olive::VideoParams::k_supported_frame_rates;
	ASSERT_EQ(inner->count(), standards.size() + 1);

	for (int i = 0; i < standards.size(); i++) {
		EXPECT_EQ(inner->itemData(i).value<olive::Rational>(),
				  standards.at(i));
		EXPECT_EQ(inner->itemText(i),
				  olive::VideoParams::frame_rate_to_string(standards.at(i)));
	}

	EXPECT_EQ(inner->itemText(standards.size()),
			  QStringLiteral("Custom..."));
}

TEST(WidgetStandardCombos, FrameRateProgrammaticSetDoesNotEmit)
{
	olive::FrameRateComboBox combo;

	QSignalSpy spy(&combo, &olive::FrameRateComboBox::frame_rate_changed);

	combo.set_frame_rate(olive::VideoParams::k_supported_frame_rates.at(1));
	combo.set_frame_rate(olive::Rational(27, 2));
	combo.set_frame_rate(olive::VideoParams::k_supported_frame_rates.at(0));

	EXPECT_EQ(spy.count(), 0);
}

TEST(WidgetStandardCombos, FrameRateUserSelectionEmitsFrameRateChanged)
{
	olive::FrameRateComboBox combo;

	QComboBox *inner = combo.findChild<QComboBox *>();
	ASSERT_NE(inner, nullptr);
	ASSERT_GE(olive::VideoParams::k_supported_frame_rates.size(), 3);

	QSignalSpy spy(&combo, &olive::FrameRateComboBox::frame_rate_changed);

	// Standard entries emit directly; only the last "Custom..." entry would
	// open a modal input dialog, which is not exercised here
	inner->setCurrentIndex(2);

	ASSERT_EQ(spy.count(), 1);
	EXPECT_EQ(spy.first().first().value<olive::Rational>(),
			  olive::VideoParams::k_supported_frame_rates.at(2));
	EXPECT_EQ(combo.get_frame_rate(),
			  olive::VideoParams::k_supported_frame_rates.at(2));
}

TEST(WidgetStandardCombos, FrameRateCustomSetRenamesLastEntry)
{
	olive::FrameRateComboBox combo;

	QComboBox *inner = combo.findChild<QComboBox *>();
	ASSERT_NE(inner, nullptr);
	const int last = inner->count() - 1;

	combo.set_frame_rate(olive::Rational(27, 2));
	EXPECT_EQ(inner->currentIndex(), last);
	EXPECT_TRUE(
		inner->itemText(last).startsWith(QStringLiteral("Custom (")));
	EXPECT_EQ(combo.get_frame_rate(), olive::Rational(27, 2));

	// A second custom rate replaces the label's rate
	combo.set_frame_rate(olive::Rational(29, 2));
	EXPECT_EQ(inner->currentIndex(), last);
	EXPECT_EQ(combo.get_frame_rate(), olive::Rational(29, 2));

	// User-selecting a standard rate afterwards emits and returns it
	QSignalSpy spy(&combo, &olive::FrameRateComboBox::frame_rate_changed);
	inner->setCurrentIndex(0);

	ASSERT_EQ(spy.count(), 1);
	EXPECT_EQ(spy.first().first().value<olive::Rational>(),
			  olive::VideoParams::k_supported_frame_rates.first());
	EXPECT_EQ(combo.get_frame_rate(),
			  olive::VideoParams::k_supported_frame_rates.first());
}

TEST(WidgetStandardCombos, FrameRateLanguageChangePreservesCustom)
{
	olive::FrameRateComboBox combo;

	combo.set_frame_rate(olive::Rational(27, 2));

	QEvent ev(QEvent::LanguageChange);
	QApplication::sendEvent(&combo, &ev);

	QComboBox *inner = combo.findChild<QComboBox *>();
	ASSERT_NE(inner, nullptr);

	const QVector<olive::Rational> &standards =
		olive::VideoParams::k_supported_frame_rates;
	ASSERT_EQ(inner->count(), standards.size() + 1);
	EXPECT_EQ(inner->currentIndex(), standards.size());
	EXPECT_EQ(combo.get_frame_rate(), olive::Rational(27, 2));
	EXPECT_TRUE(inner->itemText(standards.size())
					.startsWith(QStringLiteral("Custom (")));
}
