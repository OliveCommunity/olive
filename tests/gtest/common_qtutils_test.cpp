#include <gtest/gtest.h>

#include <QComboBox>
#include <QFontMetrics>
#include <QFont>
#include <QLabel>

#include "common/qtutils.h"

TEST(CommonQtUtils, PtrToValueAndBack)
{
	int value = 42;
	void *ptr = &value;
	QVariant v = olive::QtUtils::ptr_to_value(ptr);
	EXPECT_EQ(olive::QtUtils::value_to_ptr<int>(v), &value);
}

TEST(CommonQtUtils, GetParentOfType)
{
	QWidget root;
	QLabel *child = new QLabel(&root);

	EXPECT_EQ(olive::QtUtils::get_parent_of_type<QLabel>(child), nullptr);
	EXPECT_EQ(olive::QtUtils::get_parent_of_type<QWidget>(child), &root);
}

TEST(CommonQtUtils, FlipControlAndShiftModifiers)
{
	// NOTE: The early return for "both modifiers present" uses a broken condition
	// (Qt::ControlModifier & Qt::ShiftModifier is always zero), so the function
	// always swaps Control and Shift. This test documents current behavior.
	Qt::KeyboardModifiers both = Qt::ControlModifier | Qt::ShiftModifier;
	Qt::KeyboardModifiers flipped =
		olive::QtUtils::flip_control_and_shift_modifiers(both);
	EXPECT_TRUE(flipped & Qt::ControlModifier);
	EXPECT_FALSE(flipped & Qt::ShiftModifier);

	Qt::KeyboardModifiers only_shift = Qt::ShiftModifier | Qt::AltModifier;
	flipped = olive::QtUtils::flip_control_and_shift_modifiers(only_shift);
	EXPECT_TRUE(flipped & Qt::ControlModifier);
	EXPECT_FALSE(flipped & Qt::ShiftModifier);
	EXPECT_TRUE(flipped & Qt::AltModifier);

	Qt::KeyboardModifiers only_ctrl = Qt::ControlModifier | Qt::AltModifier;
	flipped = olive::QtUtils::flip_control_and_shift_modifiers(only_ctrl);
	EXPECT_FALSE(flipped & Qt::ControlModifier);
	EXPECT_TRUE(flipped & Qt::ShiftModifier);
	EXPECT_TRUE(flipped & Qt::AltModifier);

	Qt::KeyboardModifiers none;
	EXPECT_EQ(olive::QtUtils::flip_control_and_shift_modifiers(none), none);
}

TEST(CommonQtUtils, SetComboBoxDataByInt)
{
	QComboBox cb;
	cb.addItem(QStringLiteral("A"), 1);
	cb.addItem(QStringLiteral("B"), 2);
	cb.addItem(QStringLiteral("C"), 3);

	olive::QtUtils::set_combo_box_data(&cb, 2);
	EXPECT_EQ(cb.currentData().toInt(), 2);
	EXPECT_EQ(cb.currentText(), QStringLiteral("B"));

	olive::QtUtils::set_combo_box_data(&cb, 42);
	EXPECT_EQ(cb.currentData().toInt(), 2);
}

TEST(CommonQtUtils, SetComboBoxDataByString)
{
	QComboBox cb;
	cb.addItem(QStringLiteral("A"), QStringLiteral("alpha"));
	cb.addItem(QStringLiteral("B"), QStringLiteral("beta"));

	olive::QtUtils::set_combo_box_data(&cb, QStringLiteral("beta"));
	EXPECT_EQ(cb.currentData().toString(), QStringLiteral("beta"));

	olive::QtUtils::set_combo_box_data(&cb, QStringLiteral("missing"));
	EXPECT_EQ(cb.currentData().toString(), QStringLiteral("beta"));
}

TEST(CommonQtUtils, QFontMetricsWidth)
{
	QFont font;
	QFontMetrics fm(font);
	QString text = QStringLiteral("Olive");

	// Thin wrapper: must forward to QFontMetrics::horizontalAdvance exactly
	EXPECT_EQ(olive::QtUtils::q_font_metrics_width(fm, text),
			  fm.horizontalAdvance(text));
}

TEST(CommonQtUtils, CreateHorizontalLine)
{
	QFrame *line = olive::QtUtils::create_horizontal_line();
	ASSERT_NE(line, nullptr);
	EXPECT_EQ(line->frameShape(), QFrame::HLine);
	delete line;
}

TEST(CommonQtUtils, CreateVerticalLine)
{
	QFrame *line = olive::QtUtils::create_vertical_line();
	ASSERT_NE(line, nullptr);
	EXPECT_EQ(line->frameShape(), QFrame::VLine);
	delete line;
}

TEST(CommonQtUtils, ToQColor)
{
	olive::core::Color c(0.1f, 0.2f, 0.3f, 0.4f);
	QColor qc = olive::QtUtils::to_q_color(c);
	EXPECT_NEAR(qc.redF(), 0.1, 0.001);
	EXPECT_NEAR(qc.greenF(), 0.2, 0.001);
	EXPECT_NEAR(qc.blueF(), 0.3, 0.001);
	EXPECT_NEAR(qc.alphaF(), 0.4, 0.001);
}

TEST(CommonQtUtils, GetFormattedDateTime)
{
	QDateTime dt = QDateTime::fromString(QStringLiteral("2025-01-15T10:30:00"),
										 Qt::ISODate);

	// Qt::TextDate renders "ddd MMM d HH:mm:ss yyyy" in the C locale
	EXPECT_EQ(olive::QtUtils::get_formatted_date_time(dt),
			  QStringLiteral("Wed Jan 15 10:30:00 2025"));
}

TEST(CommonQtUtils, WordWrapString)
{
	QFont font;
	QFontMetrics fm(font);

	// A string wider than the bounding width must be split into
	// multiple lines
	QStringList wrapped = olive::QtUtils::word_wrap_string(
		QStringLiteral("hello world foo bar"), fm, 40);
	EXPECT_GT(wrapped.size(), 1);

	// A string that fits stays on a single line, untouched
	wrapped = olive::QtUtils::word_wrap_string(
		QStringLiteral("hello world foo bar"), fm, 100000);
	EXPECT_EQ(wrapped.size(), 1);
	EXPECT_EQ(wrapped.first(), QStringLiteral("hello world foo bar"));

	// Should preserve manual newlines
	wrapped = olive::QtUtils::word_wrap_string(QStringLiteral("line1\nline2"), fm,
											 1000);
	EXPECT_EQ(wrapped.size(), 2);
	EXPECT_EQ(wrapped.at(0), QStringLiteral("line1"));
	EXPECT_EQ(wrapped.at(1), QStringLiteral("line2"));
}

TEST(CommonQtUtils, ToQColorClampsValues)
{
	olive::core::Color c(2.0f, -1.0f, 0.5f, 1.5f);
	QColor qc = olive::QtUtils::to_q_color(c);
	EXPECT_NEAR(qc.redF(), 1.0, 0.001);
	EXPECT_NEAR(qc.greenF(), 0.0, 0.001);
	EXPECT_NEAR(qc.blueF(), 0.5, 0.001);
	EXPECT_NEAR(qc.alphaF(), 1.0, 0.001);
}

TEST(CommonQtUtils, qHashRational)
{
	using olive::core::Rational;

	// Hash contract: equal rationals must hash equally
	EXPECT_EQ(qHash(Rational(3, 4)), qHash(Rational(3, 4)));
	EXPECT_EQ(qHash(Rational(3, 4)), qHash(Rational(6, 8)));

	// Distinct values must hash differently
	EXPECT_NE(qHash(Rational(3, 4)), qHash(Rational(1, 2)));
	EXPECT_NE(qHash(Rational(1, 3)), qHash(Rational(2, 3)));
}

TEST(CommonQtUtils, qHashTimeRange)
{
	using olive::core::Rational;
	using olive::core::TimeRange;

	// Hash contract: equal ranges must hash equally
	EXPECT_EQ(qHash(TimeRange(Rational(1), Rational(5))),
			  qHash(TimeRange(Rational(1), Rational(5))));

	// Ranges differing in their in- or out-point must hash differently
	EXPECT_NE(qHash(TimeRange(Rational(1), Rational(5))),
			  qHash(TimeRange(Rational(2), Rational(5))));
	EXPECT_NE(qHash(TimeRange(Rational(1), Rational(5))),
			  qHash(TimeRange(Rational(1), Rational(6))));
}
