#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QSignalSpy>

#include "common/configwrapper.h"
#include "widget/focusablelineedit/focusablelineedit.h"
#include "widget/slider/base/numericsliderbase.h"
#include "widget/slider/base/sliderlabel.h"
#include "widget/slider/base/sliderladder.h"
#include "widget/slider/sliderdisplaytypeapp.h"

namespace
{

// NumericSliderBase is abstract (SliderBase's value_to_string /
// string_to_value / value_signal_event are pure), so test it through a
// minimal concrete probe that also exposes the protected entry points
class ProbeNumericSlider : public olive::NumericSliderBase {
public:
	void set_value(const QVariant &v)
	{
		set_value_internal(v);
	}

	QVariant get_value() const
	{
		return get_value_internal();
	}

	QVariant get_offset_public() const
	{
		return get_offset();
	}

	QVariant adjust_drag_public(const QVariant &start, const double &drag) const
	{
		return adjust_drag_distance_internal(start, drag);
	}

	bool greater_public(const QVariant &lhs, const QVariant &rhs) const
	{
		return value_greater_than(lhs, rhs);
	}

	bool less_public(const QVariant &lhs, const QVariant &rhs) const
	{
		return value_less_than(lhs, rhs);
	}

	bool can_set_value_public() const
	{
		return can_set_value();
	}

	void set_minimum_public(const QVariant &v)
	{
		set_minimum_internal(v);
	}

	void set_maximum_public(const QVariant &v)
	{
		set_maximum_internal(v);
	}

	QVector<QVariant> signaled;

protected:
	virtual QString value_to_string(const QVariant &v) const override
	{
		return QString::number(v.toDouble());
	}

	virtual QVariant string_to_value(const QString &s, bool *ok) const override
	{
		return s.toDouble(ok);
	}

	virtual void value_signal_event(const QVariant &value) override
	{
		signaled.append(value);
	}
};

// Forces ladder mode on for the duration of a test. Ladder mode keeps the
// SliderLadder away from the macOS cursor-grab path
// (CGAssociateMouseAndMouseCursorPosition), which would otherwise decouple
// the host's real cursor while a ladder widget exists
struct LadderConfigGuard {
	LadderConfigGuard()
		: old_(OAK_CONFIG("UseSliderLadders").toBool())
	{
		OAK_CONFIG("UseSliderLadders") = true;
	}

	~LadderConfigGuard()
	{
		OAK_CONFIG("UseSliderLadders") = old_;
	}

	bool old_;
};

// The drag ladder is an orphan top-level popup; dig it out of the
// application's top-level widget list
olive::SliderLadder *find_ladder()
{
	const QWidgetList tops = QApplication::topLevelWidgets();
	for (QWidget *w : tops) {
		if (auto *l = qobject_cast<olive::SliderLadder *>(w)) {
			return l;
		}
	}
	return nullptr;
}

// Starts a ladder drag on the slider by emitting its label's press signal
void press_label(ProbeNumericSlider &s)
{
	olive::SliderLabel *label = s.findChild<olive::SliderLabel *>();
	ASSERT_NE(label, nullptr);
	ASSERT_TRUE(QMetaObject::invokeMethod(label, "label_pressed"));
}

} // namespace

TEST(SliderDisplayType, OrdinalValuesAreAbiStable)
{
	// These cross the engine C ABI as ints inside node input properties; the
	// ordinals must match engine node/sliderdisplaytype.h
	EXPECT_EQ(int(olive::slider::k_normal), 0);
	EXPECT_EQ(int(olive::slider::k_decibel), 1);
	EXPECT_EQ(int(olive::slider::k_percentage), 2);

	EXPECT_EQ(int(olive::slider::k_time), 0);
	EXPECT_EQ(int(olive::slider::k_float), 1);
	EXPECT_EQ(int(olive::slider::k_rational), 2);
}

TEST(NumericSliderBase, DefaultStateIsIdle)
{
	ProbeNumericSlider s;
	EXPECT_FALSE(s.is_dragging());
	EXPECT_TRUE(s.can_set_value_public());
	EXPECT_EQ(s.cursor().shape(), Qt::SizeHorCursor);
}

TEST(NumericSliderBase, OffsetRoundTrips)
{
	ProbeNumericSlider s;
	EXPECT_FALSE(s.get_offset_public().isValid());

	s.set_offset(7);
	EXPECT_EQ(s.get_offset_public().toInt(), 7);

	s.set_offset(-2.5);
	EXPECT_DOUBLE_EQ(s.get_offset_public().toDouble(), -2.5);
}

TEST(NumericSliderBase, DragDistanceDefaultsToAddition)
{
	ProbeNumericSlider s;
	EXPECT_DOUBLE_EQ(s.adjust_drag_public(1.5, 2.25).toDouble(), 3.75);
	EXPECT_DOUBLE_EQ(s.adjust_drag_public(1.5, -0.5).toDouble(), 1.0);
}

TEST(NumericSliderBase, ValueComparisonUsesNumericValues)
{
	ProbeNumericSlider s;
	EXPECT_TRUE(s.greater_public(2.5, 2.4));
	EXPECT_FALSE(s.greater_public(1.0, 1.0));
	EXPECT_TRUE(s.less_public(1, 2));

	// String variants compare by their numeric value, not lexicographically
	EXPECT_TRUE(s.less_public(QStringLiteral("9"), QStringLiteral("10")));
}

TEST(NumericSliderBase, SetMinimumClampsExistingValue)
{
	ProbeNumericSlider s;

	s.set_value(5.0);
	s.set_minimum_public(10.0);
	EXPECT_DOUBLE_EQ(s.get_value().toDouble(), 10.0);

	// New values below the minimum clamp on the way in; programmatic sets do
	// not emit the value-changed signal
	s.set_value(3.0);
	EXPECT_DOUBLE_EQ(s.get_value().toDouble(), 10.0);
	EXPECT_TRUE(s.signaled.isEmpty());

	s.set_value(42.0);
	EXPECT_DOUBLE_EQ(s.get_value().toDouble(), 42.0);
}

TEST(NumericSliderBase, SetMaximumClampsExistingValue)
{
	ProbeNumericSlider s;

	s.set_value(5.0);
	s.set_maximum_public(2.0);
	EXPECT_DOUBLE_EQ(s.get_value().toDouble(), 2.0);

	s.set_value(99.0);
	EXPECT_DOUBLE_EQ(s.get_value().toDouble(), 2.0);

	s.set_value(-7.0);
	EXPECT_DOUBLE_EQ(s.get_value().toDouble(), -7.0);
}

TEST(NumericSliderBase, LadderDragUpdatesValueAndSignals)
{
	LadderConfigGuard guard;

	{
		ProbeNumericSlider s;
		s.set_ladder_element_count(2);
		s.set_value(10.0);

		press_label(s);
		ASSERT_TRUE(s.is_dragging());
		EXPECT_FALSE(s.can_set_value_public());

		olive::SliderLadder *ladder = find_ladder();
		ASSERT_NE(ladder, nullptr);

		// Two outer values either side plus the center entry
		EXPECT_EQ(ladder->findChildren<olive::SliderLadderElement *>().size(), 5);

		// External sets are blocked while the drag owns the value
		s.set_value(50.0);
		EXPECT_DOUBLE_EQ(s.get_value().toDouble(), 10.0);
		EXPECT_TRUE(s.signaled.isEmpty());

		// value * multiplier accumulates onto the drag start value
		ASSERT_TRUE(QMetaObject::invokeMethod(
			ladder, "dragged_by_value", Q_ARG(int, 3), Q_ARG(double, 2.0)));
		EXPECT_DOUBLE_EQ(s.get_value().toDouble(), 16.0);
		ASSERT_EQ(s.signaled.size(), 1);
		EXPECT_DOUBLE_EQ(s.signaled.last().toDouble(), 16.0);

		ASSERT_TRUE(QMetaObject::invokeMethod(
			ladder, "dragged_by_value", Q_ARG(int, 1), Q_ARG(double, 1.0)));
		EXPECT_DOUBLE_EQ(s.get_value().toDouble(), 17.0);
		ASSERT_EQ(s.signaled.size(), 2);

		// Releasing after a drag re-signals the final value and ends the drag
		ladder->close();
		EXPECT_FALSE(s.is_dragging());
		EXPECT_TRUE(s.can_set_value_public());
		ASSERT_EQ(s.signaled.size(), 3);
		EXPECT_DOUBLE_EQ(s.signaled.last().toDouble(), 17.0);

		// Programmatic sets are accepted again
		s.set_value(3.0);
		EXPECT_DOUBLE_EQ(s.get_value().toDouble(), 3.0);
	}

	// Flush the ladder's deleteLater and its queued drag-timer start only
	// after the slider is gone, so a stray timer tick can't reach a ladder
	// whose owner no longer exists
	QCoreApplication::processEvents();
}

TEST(NumericSliderBase, LadderDragClampsToRange)
{
	LadderConfigGuard guard;

	{
		ProbeNumericSlider s;
		s.set_ladder_element_count(2);
		s.set_minimum_public(0.0);
		s.set_maximum_public(8.0);
		s.set_value(5.0);

		press_label(s);
		olive::SliderLadder *ladder = find_ladder();
		ASSERT_NE(ladder, nullptr);

		ASSERT_TRUE(QMetaObject::invokeMethod(
			ladder, "dragged_by_value", Q_ARG(int, -10), Q_ARG(double, 1.0)));
		EXPECT_DOUBLE_EQ(s.get_value().toDouble(), 0.0);

		ASSERT_TRUE(QMetaObject::invokeMethod(
			ladder, "dragged_by_value", Q_ARG(int, 100), Q_ARG(double, 1.0)));
		EXPECT_DOUBLE_EQ(s.get_value().toDouble(), 8.0);

		ladder->close();
	}

	QCoreApplication::processEvents();
}

TEST(NumericSliderBase, ClickWithoutDragShowsEditor)
{
	LadderConfigGuard guard;

	{
		ProbeNumericSlider s;
		s.set_ladder_element_count(2);
		s.set_value(4.0);

		press_label(s);
		olive::SliderLadder *ladder = find_ladder();
		ASSERT_NE(ladder, nullptr);

		// A press/release with no drag in between is treated as a click and
		// opens the line editor instead of changing the value
		ladder->close();
		EXPECT_FALSE(s.is_dragging());
		EXPECT_TRUE(s.signaled.isEmpty());

		olive::FocusableLineEdit *editor =
			s.findChild<olive::FocusableLineEdit *>();
		ASSERT_NE(editor, nullptr);
		EXPECT_EQ(s.currentWidget(), editor);
		EXPECT_EQ(editor->text(), QStringLiteral("4"));
		EXPECT_DOUBLE_EQ(s.get_value().toDouble(), 4.0);
	}

	QCoreApplication::processEvents();
}

TEST(SliderLadder, BuildsElementPerPowerOfTen)
{
	LadderConfigGuard guard;

	olive::SliderLadder ladder(1.0, 2, QStringLiteral("9999"));
	EXPECT_TRUE(ladder.windowFlags() & Qt::Popup);

	const auto elements = ladder.findChildren<olive::SliderLadderElement *>();
	ASSERT_EQ(elements.size(), 5);

	// Coarse multipliers on top, the unit entry in the middle, fine below
	EXPECT_DOUBLE_EQ(elements.at(0)->get_multiplier(), 100.0);
	EXPECT_DOUBLE_EQ(elements.at(1)->get_multiplier(), 10.0);
	EXPECT_DOUBLE_EQ(elements.at(2)->get_multiplier(), 1.0);
	EXPECT_DOUBLE_EQ(elements.at(3)->get_multiplier(), 0.1);
	EXPECT_DOUBLE_EQ(elements.at(4)->get_multiplier(), 0.01);

	// Only the center entry starts highlighted
	for (int i = 0; i < elements.size(); i++) {
		const QPalette::ColorRole expected =
			(i == 2) ? QPalette::Highlight : QPalette::Window;
		EXPECT_EQ(elements.at(i)->backgroundRole(), expected) << i;
	}
}

TEST(SliderLadder, DragMultiplierScalesElements)
{
	LadderConfigGuard guard;

	olive::SliderLadder ladder(2.5, 1, QStringLiteral("9999"));

	const auto elements = ladder.findChildren<olive::SliderLadderElement *>();
	ASSERT_EQ(elements.size(), 3);
	EXPECT_DOUBLE_EQ(elements.at(0)->get_multiplier(), 25.0);
	EXPECT_DOUBLE_EQ(elements.at(1)->get_multiplier(), 2.5);
	EXPECT_DOUBLE_EQ(elements.at(2)->get_multiplier(), 0.25);
}

TEST(SliderLadder, SetValueShowsOnHighlightedElementOnly)
{
	LadderConfigGuard guard;

	olive::SliderLadder ladder(1.0, 1, QStringLiteral("9999"));
	ladder.set_value(QStringLiteral("42"));

	const auto elements = ladder.findChildren<olive::SliderLadderElement *>();
	ASSERT_EQ(elements.size(), 3);

	for (int i = 0; i < elements.size(); i++) {
		QLabel *label = elements.at(i)->findChild<QLabel *>();
		ASSERT_NE(label, nullptr);
		if (i == 1) {
			EXPECT_EQ(label->text(), QStringLiteral("1\n42"));
		} else {
			EXPECT_FALSE(label->text().contains(QStringLiteral("42")));
		}
	}
}

TEST(SliderLadder, CloseEmitsReleased)
{
	LadderConfigGuard guard;

	olive::SliderLadder ladder(1.0, 1, QStringLiteral("9999"));
	ladder.show();

	QSignalSpy spy(&ladder, &olive::SliderLadder::released);
	ladder.close();
	EXPECT_EQ(spy.count(), 1);
}

TEST(SliderLadder, SingleElementHidesMultiplier)
{
	LadderConfigGuard guard;

	// With no outer values the ladder is a single readout, and its multiplier
	// is hidden since there's nothing to switch between
	olive::SliderLadder ladder(1.0, 0, QStringLiteral("9999"));

	const auto elements = ladder.findChildren<olive::SliderLadderElement *>();
	ASSERT_EQ(elements.size(), 1);

	ladder.set_value(QStringLiteral("7"));

	QLabel *label = elements.first()->findChild<QLabel *>();
	ASSERT_NE(label, nullptr);
	EXPECT_EQ(label->text(), QStringLiteral("7"));
}

TEST(SliderLadderElement, LabelReflectsHighlightAndMultiplierVisibility)
{
	olive::SliderLadderElement e(2.5, QStringLiteral("9999"));
	QLabel *label = e.findChild<QLabel *>();
	ASSERT_NE(label, nullptr);

	// Unhighlighted: only the multiplier shows, the value slot stays empty
	EXPECT_EQ(label->text(), QStringLiteral("2.5\n"));
	EXPECT_EQ(e.backgroundRole(), QPalette::Window);

	e.set_value(QStringLiteral("9"));
	EXPECT_EQ(label->text(), QStringLiteral("2.5\n"));

	e.set_highlighted(true);
	EXPECT_EQ(label->text(), QStringLiteral("2.5\n9"));
	EXPECT_EQ(e.backgroundRole(), QPalette::Highlight);

	e.set_highlighted(false);
	EXPECT_EQ(e.backgroundRole(), QPalette::Window);

	e.set_multiplier_visible(false);
	EXPECT_EQ(label->text(), QStringLiteral("9"));
}
