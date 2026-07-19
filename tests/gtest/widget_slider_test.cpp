#include <gtest/gtest.h>

#include <QLabel>

#include "common/decibel.h"
#include "widget/slider/base/decimalsliderbase.h"
#include "widget/slider/floatslider.h"
#include "widget/slider/integerslider.h"

namespace
{

// Exposes the protected mapping/parsing entry points so they can be tested
// without simulating mouse drags
class ExposedFloatSlider : public olive::FloatSlider {
public:
	QString value_to_string_public(const QVariant &v) const
	{
		return value_to_string(v);
	}

	QVariant string_to_value_public(const QString &s, bool *ok) const
	{
		return string_to_value(s, ok);
	}

	QVariant adjust_drag_public(const QVariant &start, const double &drag) const
	{
		return adjust_drag_distance_internal(start, drag);
	}
};

class ExposedIntegerSlider : public olive::IntegerSlider {
public:
	QString value_to_string_public(const QVariant &v) const
	{
		return value_to_string(v);
	}

	QVariant string_to_value_public(const QString &s, bool *ok) const
	{
		return string_to_value(s, ok);
	}

	QVariant adjust_drag_public(const QVariant &start, const double &drag) const
	{
		return adjust_drag_distance_internal(start, drag);
	}
};

} // namespace

TEST(WidgetSlider, FloatToStringFormatsAndTrims)
{
	using olive::DecimalSliderBase;

	EXPECT_EQ(DecimalSliderBase::float_to_string(1.5, 2, false),
			  QStringLiteral("1.50"));
	EXPECT_EQ(DecimalSliderBase::float_to_string(1.5, 2, true),
			  QStringLiteral("1.5"));

	// Trimming always leaves at least one decimal digit
	EXPECT_EQ(DecimalSliderBase::float_to_string(2.0, 2, true),
			  QStringLiteral("2.0"));
	EXPECT_EQ(DecimalSliderBase::float_to_string(0.0, 3, true),
			  QStringLiteral("0.0"));

	EXPECT_EQ(DecimalSliderBase::float_to_string(-3.5, 1, false),
			  QStringLiteral("-3.5"));
	EXPECT_EQ(DecimalSliderBase::float_to_string(1.234, 2, false),
			  QStringLiteral("1.23"));
}

TEST(WidgetSlider, FloatSetValueClampsToRange)
{
	olive::FloatSlider s;
	EXPECT_DOUBLE_EQ(s.get_value(), 0.0);

	s.set_value(1.25);
	EXPECT_DOUBLE_EQ(s.get_value(), 1.25);

	s.set_minimum(0.0);
	s.set_maximum(2.0);

	s.set_value(-5.0);
	EXPECT_DOUBLE_EQ(s.get_value(), 0.0);

	s.set_value(10.0);
	EXPECT_DOUBLE_EQ(s.get_value(), 2.0);
}

TEST(WidgetSlider, FloatRangeChangeClampsExistingValue)
{
	olive::FloatSlider s;

	s.set_value(-3.0);
	s.set_minimum(0.0);
	EXPECT_DOUBLE_EQ(s.get_value(), 0.0);

	s.set_value(5.0);
	s.set_maximum(1.0);
	EXPECT_DOUBLE_EQ(s.get_value(), 1.0);
}

TEST(WidgetSlider, FloatDisplayTransformRoundTrips)
{
	EXPECT_DOUBLE_EQ(
		olive::FloatSlider::transform_value_to_display(0.5, olive::FloatSlider::k_percentage),
		50.0);
	EXPECT_DOUBLE_EQ(
		olive::FloatSlider::transform_display_to_value(50.0, olive::FloatSlider::k_percentage),
		0.5);

	EXPECT_DOUBLE_EQ(
		olive::FloatSlider::transform_value_to_display(1.0, olive::FloatSlider::k_decibel),
		0.0);
	EXPECT_NEAR(
		olive::FloatSlider::transform_value_to_display(0.5, olive::FloatSlider::k_decibel),
		-6.0206, 0.001);
	EXPECT_NEAR(
		olive::FloatSlider::transform_display_to_value(
			olive::FloatSlider::transform_value_to_display(0.75, olive::FloatSlider::k_decibel),
			olive::FloatSlider::k_decibel),
		0.75, 1e-12);

	EXPECT_DOUBLE_EQ(
		olive::FloatSlider::transform_value_to_display(3.5, olive::FloatSlider::k_normal),
		3.5);
	EXPECT_DOUBLE_EQ(
		olive::FloatSlider::transform_display_to_value(3.5, olive::FloatSlider::k_normal),
		3.5);
}

TEST(WidgetSlider, FloatStaticValueToStringRespectsDisplayType)
{
	// Zero volume in decibel mode displays as an infinity symbol (U+221E)
	EXPECT_EQ(olive::FloatSlider::value_to_string(0.0, olive::FloatSlider::k_decibel, 2,
												false),
			  QString(QChar(0x221E)));

	EXPECT_EQ(olive::FloatSlider::value_to_string(0.5, olive::FloatSlider::k_percentage,
												1, false),
			  QStringLiteral("50.0"));
	EXPECT_EQ(olive::FloatSlider::value_to_string(1.234, olive::FloatSlider::k_normal,
												2, false),
			  QStringLiteral("1.23"));
}

TEST(WidgetSlider, FloatLabelShowsFormattedValue)
{
	olive::FloatSlider s;
	QLabel *label = s.findChild<QLabel *>();
	ASSERT_NE(label, nullptr);

	s.set_value(0.5);
	EXPECT_EQ(label->text(), QStringLiteral("0.50"));

	s.set_display_type(olive::FloatSlider::k_percentage);
	EXPECT_EQ(label->text(), QStringLiteral("50.00%"));

	s.set_format(QStringLiteral("%1 px"));
	EXPECT_EQ(label->text(), QStringLiteral("50.00 px"));

	s.clear_format();
	s.set_display_type(olive::FloatSlider::k_normal);
	EXPECT_EQ(label->text(), QStringLiteral("0.50"));
}

TEST(WidgetSlider, FloatOffsetAppliesToDisplayAndParse)
{
	ExposedFloatSlider s;
	s.set_offset(10.0);

	EXPECT_EQ(s.value_to_string_public(2.0), QStringLiteral("12.00"));

	bool ok = false;
	QVariant v = s.string_to_value_public(QStringLiteral("12.5"), &ok);
	EXPECT_TRUE(ok);
	EXPECT_DOUBLE_EQ(v.toDouble(), 2.5);
}

TEST(WidgetSlider, FloatStringToValueRejectsGarbage)
{
	ExposedFloatSlider s;

	bool ok = true;
	s.string_to_value_public(QStringLiteral("not a number"), &ok);
	EXPECT_FALSE(ok);
}

TEST(WidgetSlider, FloatStringToValueRespectsDisplayType)
{
	ExposedFloatSlider s;
	s.set_display_type(olive::FloatSlider::k_percentage);

	bool ok = false;
	QVariant v = s.string_to_value_public(QStringLiteral("50"), &ok);
	EXPECT_TRUE(ok);
	EXPECT_DOUBLE_EQ(v.toDouble(), 0.5);
}

TEST(WidgetSlider, FloatDragDistanceRespectsDisplayType)
{
	ExposedFloatSlider s;

	// Normal: plain addition
	EXPECT_DOUBLE_EQ(s.adjust_drag_public(1.0, 2.5).toDouble(), 3.5);

	// Percentage: drag is scaled by 1/100
	s.set_display_type(olive::FloatSlider::k_percentage);
	EXPECT_DOUBLE_EQ(s.adjust_drag_public(0.5, 10.0).toDouble(), 0.6);

	// Decibel: drag happens in dB space
	s.set_display_type(olive::FloatSlider::k_decibel);
	const double expected =
		olive::Decibel::to_linear(olive::Decibel::from_linear(1.0) + 6.0);
	EXPECT_DOUBLE_EQ(s.adjust_drag_public(1.0, 6.0).toDouble(), expected);
}

TEST(WidgetSlider, TristateShowsDashesUntilValueSet)
{
	olive::FloatSlider s;
	QLabel *label = s.findChild<QLabel *>();
	ASSERT_NE(label, nullptr);

	s.set_value(1.0);
	s.set_tristate();
	EXPECT_TRUE(s.is_tristate());
	EXPECT_EQ(label->text(), QStringLiteral("---"));

	// Setting a value clears the tristate display
	s.set_value(2.0);
	EXPECT_FALSE(s.is_tristate());
	EXPECT_EQ(label->text(), QStringLiteral("2.00"));
}

TEST(WidgetSlider, LabelSubstitutionOverridesText)
{
	olive::FloatSlider s;
	QLabel *label = s.findChild<QLabel *>();
	ASSERT_NE(label, nullptr);

	s.set_value(0.0);
	s.insert_label_substitution(0.0, QStringLiteral("Zero"));
	EXPECT_EQ(label->text(), QStringLiteral("Zero"));

	s.set_value(1.0);
	EXPECT_EQ(label->text(), QStringLiteral("1.00"));
}

TEST(WidgetSlider, IntegerSetValueClampsToRange)
{
	olive::IntegerSlider s;
	EXPECT_EQ(s.get_value(), 0);

	s.set_minimum(0);
	s.set_maximum(10);

	s.set_value(-3);
	EXPECT_EQ(s.get_value(), 0);

	s.set_value(42);
	EXPECT_EQ(s.get_value(), 10);

	s.set_value(7);
	EXPECT_EQ(s.get_value(), 7);
}

TEST(WidgetSlider, IntegerStringToValueRounds)
{
	ExposedIntegerSlider s;

	bool ok = false;
	EXPECT_EQ(s.string_to_value_public(QStringLiteral("3.6"), &ok).toLongLong(), 4);
	EXPECT_TRUE(ok);
	EXPECT_EQ(s.string_to_value_public(QStringLiteral("-2.4"), &ok).toLongLong(), -2);
	EXPECT_TRUE(ok);

	ok = true;
	s.string_to_value_public(QStringLiteral("junk"), &ok);
	EXPECT_FALSE(ok);
}

TEST(WidgetSlider, IntegerOffsetAppliesToDisplayAndParse)
{
	ExposedIntegerSlider s;
	s.set_offset(10);

	EXPECT_EQ(s.value_to_string_public(2), QStringLiteral("12"));

	bool ok = false;
	EXPECT_EQ(s.string_to_value_public(QStringLiteral("12"), &ok).toLongLong(), 2);
	EXPECT_TRUE(ok);
}

TEST(WidgetSlider, IntegerDragRoundsToWhole)
{
	ExposedIntegerSlider s;

	EXPECT_EQ(s.adjust_drag_public(2, 1.4).toLongLong(), 3);
	EXPECT_EQ(s.adjust_drag_public(2, -1.4).toLongLong(), 1);
}
