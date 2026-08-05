#include <gtest/gtest.h>

#include <memory>

#include <QAction>
#include <QSignalSpy>
#include <QTest>

#include "config/config.h"
#include "node/color/colormanager/colormanager.h"
#include "node/project.h"
#include "widget/colorwheel/colorswatchwidget.h"
#include "widget/manageddisplay/manageddisplay.h"
#include "widget/menu/menu.h"
#include "widget/scope/histogram/histogram.h"
#include "widget/scope/scopebase/scopebase.h"
#include "widget/scope/vectorscope/vectorscope.h"
#include "widget/scope/waveform/waveform.h"

namespace
{

// ManagedDisplayWidget is abstract (on_paint), so a minimal concrete probe is
// needed even for tests that only exercise the base-class behavior
class ProbeDisplayWidget : public olive::ManagedDisplayWidget {
public:
	using olive::ManagedDisplayWidget::ManagedDisplayWidget;

	void *pub_renderer() const
	{
		return renderer();
	}
	bool pub_backend_neutral() const
	{
		return is_backend_neutral();
	}

	int processor_changed_events = 0;

protected:
	virtual void on_paint() override
	{
	}

	virtual void color_processor_changed_event() override
	{
		processor_changed_events++;
		olive::ManagedDisplayWidget::color_processor_changed_event();
	}
};

// Exposes the protected renderer state of the concrete scopes so tests can
// verify construction wiring without a GL context
class ProbeHistogramScope : public olive::HistogramScope {
public:
	void *pub_renderer() const
	{
		return renderer();
	}
	bool pub_backend_neutral() const
	{
		return is_backend_neutral();
	}
};

class ProbeWaveformScope : public olive::WaveformScope {
public:
	void *pub_renderer() const
	{
		return renderer();
	}
	bool pub_backend_neutral() const
	{
		return is_backend_neutral();
	}
	oak_video_params pub_viewport_params() const
	{
		return get_viewport_params();
	}
};

class ProbeVectorscopeScope : public olive::VectorscopeScope {
public:
	void *pub_renderer() const
	{
		return renderer();
	}
	bool pub_backend_neutral() const
	{
		return is_backend_neutral();
	}
};

// ColorSwatchWidget is abstract (get_color_from_screen_pos); this probe maps
// screen position deterministically to a color and records the change events
class ProbeSwatchWidget : public olive::ColorSwatchWidget {
public:
	olive::Color pub_managed_color(const olive::Color &c) const
	{
		return get_managed_color(c);
	}
	Qt::GlobalColor pub_selector_color() const
	{
		return get_ui_selector_color();
	}

	int changed_events = 0;
	bool last_external = false;

protected:
	virtual olive::Color get_color_from_screen_pos(const QPoint &p) const override
	{
		return olive::Color(p.x() / 100.0, p.y() / 100.0, 0.5);
	}

	virtual void SelectedColorChangedEvent(const olive::Color &c,
										   bool external) override
	{
		changed_events++;
		last_external = external;
		olive::ColorSwatchWidget::SelectedColorChangedEvent(c, external);
	}
};

// Returns the index of the first action whose data differs from the current
// transform component, or -1 when the menu offers no alternative
int alternative_action_index(olive::Menu *menu, const QString &current)
{
	const QList<QAction *> acts = menu->actions();
	for (int i = 0; i < acts.size(); i++) {
		if (acts.at(i)->data().toString() != current) {
			return i;
		}
	}
	return -1;
}

} // namespace

TEST(WidgetManagedDisplay, DefaultConstructionState)
{
	ProbeDisplayWidget w;

	EXPECT_EQ(w.color_manager(), nullptr);
	EXPECT_NE(w.pub_renderer(), nullptr);

	// No transform has been chosen yet
	const oak::ColorTransform &t = w.get_color_transform();
	EXPECT_FALSE(t.is_display());
	EXPECT_TRUE(t.output().isEmpty());
	EXPECT_TRUE(t.view().isEmpty());
	EXPECT_TRUE(t.look().isEmpty());

	EXPECT_EQ(w.processor_changed_events, 0);
}

TEST(WidgetManagedDisplay, SetColorTransformWithoutManagerRoundTrips)
{
	ProbeDisplayWidget w;

	// ColorProcessorHandlePtr is not a registered metatype, so count the
	// signal through a direct lambda connection instead of QSignalSpy
	int signal_count = 0;
	olive::ColorProcessorHandlePtr last_processor;
	QObject::connect(&w, &olive::ManagedDisplayWidget::color_processor_changed,
					 [&signal_count, &last_processor](
						 olive::ColorProcessorHandlePtr p) {
						 signal_count++;
						 last_processor = p;
					 });

	w.set_color_transform(
		oak::ColorTransform(QStringLiteral("MyDisplay"),
							QStringLiteral("MyView"), QStringLiteral("MyLook")));

	const oak::ColorTransform &t = w.get_color_transform();
	EXPECT_TRUE(t.is_display());
	EXPECT_EQ(t.display(), QStringLiteral("MyDisplay"));
	EXPECT_EQ(t.view(), QStringLiteral("MyView"));
	EXPECT_EQ(t.look(), QStringLiteral("MyLook"));

	// With no color manager connected, the processor is cleared and the
	// change is signalled once
	EXPECT_EQ(signal_count, 1);
	EXPECT_EQ(last_processor, nullptr);
	EXPECT_EQ(w.processor_changed_events, 1);

	// A plain colorspace transform stores only the output name
	w.set_color_transform(oak::ColorTransform(QStringLiteral("ACEScg")));
	EXPECT_FALSE(w.get_color_transform().is_display());
	EXPECT_EQ(w.get_color_transform().output(), QStringLiteral("ACEScg"));
	EXPECT_EQ(signal_count, 2);
}

TEST(WidgetManagedDisplay, ConnectColorManagerDefaultsToDisplayViewTransform)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	ProbeDisplayWidget w;

	int manager_signals = 0;
	OakEngineColorManager *last_manager = nullptr;
	QObject::connect(&w, &olive::ManagedDisplayWidget::color_manager_changed,
					 [&manager_signals,
					  &last_manager](OakEngineColorManager *m) {
						 manager_signals++;
						 last_manager = m;
					 });

	w.connect_color_manager(olive::oak_color_manager(project.color_manager()));

	EXPECT_EQ(w.color_manager(),
			  olive::oak_color_manager(project.color_manager()));
	EXPECT_EQ(manager_signals, 1);
	EXPECT_EQ(last_manager, olive::oak_color_manager(project.color_manager()));

	// An empty transform is conformed to the config's default display/view so
	// the widget would show a sensible image
	const oak::ColorTransform &t = w.get_color_transform();
	EXPECT_TRUE(t.is_display());
	EXPECT_FALSE(t.display().isEmpty());
	EXPECT_FALSE(t.view().isEmpty());

	// Connecting the same manager again is a no-op
	w.connect_color_manager(olive::oak_color_manager(project.color_manager()));
	EXPECT_EQ(manager_signals, 1);
}

TEST(WidgetManagedDisplay, DisconnectColorManagerClearsState)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	ProbeDisplayWidget w;
	w.connect_color_manager(olive::oak_color_manager(project.color_manager()));

	int manager_signals = 0;
	OakEngineColorManager *last_manager =
		olive::oak_color_manager(project.color_manager());
	QObject::connect(&w, &olive::ManagedDisplayWidget::color_manager_changed,
					 [&manager_signals,
					  &last_manager](OakEngineColorManager *m) {
						 manager_signals++;
						 last_manager = m;
					 });

	w.disconnect_color_manager();

	EXPECT_EQ(w.color_manager(), nullptr);
	EXPECT_EQ(manager_signals, 1);
	EXPECT_EQ(last_manager, nullptr);
}

TEST(WidgetManagedDisplay, DisplayMenuReflectsCurrentTransform)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	ProbeDisplayWidget w;
	w.connect_color_manager(olive::oak_color_manager(project.color_manager()));

	std::unique_ptr<olive::Menu> menu(w.get_display_menu(nullptr));
	ASSERT_FALSE(menu->actions().isEmpty());

	// Exactly the current display is checked
	int checked = 0;
	for (QAction *a : menu->actions()) {
		EXPECT_TRUE(a->isCheckable());
		EXPECT_EQ(a->data().toString(), a->text());
		if (a->isChecked()) {
			checked++;
			EXPECT_EQ(a->data().toString(), w.get_color_transform().display());
		}
	}
	EXPECT_EQ(checked, 1);

	// Picking another display (if the config has one) retargets the transform
	const int alt =
		alternative_action_index(menu.get(), w.get_color_transform().display());
	if (alt >= 0) {
		const QString target = menu->actions().at(alt)->data().toString();
		menu->actions().at(alt)->trigger();
		EXPECT_EQ(w.get_color_transform().display(), target);
	}
}

TEST(WidgetManagedDisplay, ViewMenuReflectsCurrentTransform)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	ProbeDisplayWidget w;
	w.connect_color_manager(olive::oak_color_manager(project.color_manager()));

	std::unique_ptr<olive::Menu> menu(w.get_view_menu(nullptr));
	ASSERT_FALSE(menu->actions().isEmpty());

	int checked = 0;
	for (QAction *a : menu->actions()) {
		EXPECT_TRUE(a->isCheckable());
		if (a->isChecked()) {
			checked++;
			EXPECT_EQ(a->data().toString(), w.get_color_transform().view());
		}
	}
	EXPECT_EQ(checked, 1);

	const int alt =
		alternative_action_index(menu.get(), w.get_color_transform().view());
	if (alt >= 0) {
		const QString target = menu->actions().at(alt)->data().toString();
		menu->actions().at(alt)->trigger();
		EXPECT_EQ(w.get_color_transform().view(), target);
		// The display component survives a view change
		EXPECT_FALSE(w.get_color_transform().display().isEmpty());
	}
}

TEST(WidgetManagedDisplay, LookMenuStartsWithNoLookEntry)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	ProbeDisplayWidget w;
	w.connect_color_manager(olive::oak_color_manager(project.color_manager()));

	std::unique_ptr<olive::Menu> menu(w.get_look_menu(nullptr));
	ASSERT_FALSE(menu->actions().isEmpty());

	// The first entry is always the "(None)" entry carrying empty data
	QAction *none = menu->actions().first();
	EXPECT_TRUE(none->isCheckable());
	EXPECT_EQ(none->data().toString(), QString());

	// The default transform has no look, so "(None)" is checked
	EXPECT_TRUE(w.get_color_transform().look().isEmpty());
	EXPECT_TRUE(none->isChecked());

	for (int i = 1; i < menu->actions().size(); i++) {
		EXPECT_TRUE(menu->actions().at(i)->isCheckable());
		EXPECT_EQ(menu->actions().at(i)->data().toString(),
				  menu->actions().at(i)->text());
	}
}

TEST(WidgetManagedDisplay, ColorSpaceMenuListsConfigSpaces)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	ProbeDisplayWidget w;
	w.connect_color_manager(olive::oak_color_manager(project.color_manager()));

	const QStringList spaces =
		project.color_manager()->list_available_colorspaces();
	ASSERT_GE(spaces.size(), 2);

	std::unique_ptr<olive::Menu> menu(w.get_color_space_menu(nullptr));
	ASSERT_EQ(menu->actions().size(), spaces.size());

	for (int i = 0; i < spaces.size(); i++) {
		EXPECT_EQ(menu->actions().at(i)->data().toString(), spaces.at(i));
	}

	// Selecting a colorspace switches to a non-display transform
	const int alt =
		alternative_action_index(menu.get(), w.get_color_transform().output());
	if (alt >= 0) {
		const QString target = menu->actions().at(alt)->data().toString();
		menu->actions().at(alt)->trigger();
		EXPECT_EQ(w.get_color_transform().output(), target);
	}
}

TEST(WidgetScopeBase, ViewportParamsTrackWidgetGeometry)
{
	ProbeWaveformScope w;
	w.resize(640, 360);

	const oak_video_params vp = w.pub_viewport_params();
	EXPECT_EQ(vp.width, int(640 * w.devicePixelRatioF()));
	EXPECT_EQ(vp.height, int(360 * w.devicePixelRatioF()));
	EXPECT_EQ(vp.format,
			  olive::Config::current()[QStringLiteral("OfflinePixelFormat")]
				  .toInt());
}

TEST(WidgetHistogramScope, ConstructionCreatesRendererWithoutTexture)
{
	ProbeHistogramScope w;

	// The renderer abstraction exists right after construction, but nothing
	// color-related is connected yet
	EXPECT_NE(w.pub_renderer(), nullptr);
	EXPECT_EQ(w.color_manager(), nullptr);
	EXPECT_TRUE(w.get_color_transform().output().isEmpty());

	// A null buffer is accepted and simply schedules a repaint; there is no
	// texture to retain
	w.set_buffer(nullptr);
	w.set_buffer(nullptr);

	EXPECT_EQ(w.color_manager(), nullptr);
}

TEST(WidgetHistogramScope, ConnectColorManagerDefaultsTransform)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	ProbeHistogramScope w;
	w.connect_color_manager(olive::oak_color_manager(project.color_manager()));

	EXPECT_EQ(w.color_manager(),
			  olive::oak_color_manager(project.color_manager()));
	EXPECT_TRUE(w.get_color_transform().is_display());
	EXPECT_FALSE(w.get_color_transform().display().isEmpty());
	EXPECT_FALSE(w.get_color_transform().view().isEmpty());
}

TEST(WidgetVectorscopeScope, ConstructionCreatesRenderer)
{
	ProbeVectorscopeScope w;

	EXPECT_NE(w.pub_renderer(), nullptr);
	EXPECT_EQ(w.color_manager(), nullptr);

	// Null buffers are tolerated without a connected manager
	w.set_buffer(nullptr);
}

TEST(WidgetVectorscopeScope, ConnectColorManagerDefaultsTransform)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	ProbeVectorscopeScope w;
	w.connect_color_manager(olive::oak_color_manager(project.color_manager()));

	EXPECT_TRUE(w.get_color_transform().is_display());
	EXPECT_FALSE(w.get_color_transform().display().isEmpty());
}

TEST(WidgetWaveformScope, ParadeModeFollowsConfig)
{
	const QVariant old =
		olive::Config::current()[QStringLiteral("WaveformRgbParade")];

	olive::Config::current()[QStringLiteral("WaveformRgbParade")] = true;
	{
		ProbeWaveformScope w;
		EXPECT_TRUE(w.parade_mode());
		EXPECT_NE(w.pub_renderer(), nullptr);
	}

	olive::Config::current()[QStringLiteral("WaveformRgbParade")] = false;
	{
		ProbeWaveformScope w;
		EXPECT_FALSE(w.parade_mode());
	}

	olive::Config::current()[QStringLiteral("WaveformRgbParade")] = old;
}

TEST(WidgetWaveformScope, SetParadeModePersistsToConfig)
{
	const QVariant old =
		olive::Config::current()[QStringLiteral("WaveformRgbParade")];

	{
		ProbeWaveformScope w;

		w.set_parade_mode(true);
		EXPECT_TRUE(w.parade_mode());
		EXPECT_TRUE(
			olive::Config::current()[QStringLiteral("WaveformRgbParade")]
				.toBool());

		w.set_parade_mode(false);
		EXPECT_FALSE(w.parade_mode());
		EXPECT_FALSE(
			olive::Config::current()[QStringLiteral("WaveformRgbParade")]
				.toBool());
	}

	// A newly constructed scope picks the persisted value up
	{
		ProbeWaveformScope w;
		EXPECT_FALSE(w.parade_mode());
	}

	olive::Config::current()[QStringLiteral("WaveformRgbParade")] = old;
}

TEST(WidgetColorSwatchWidget, SetSelectedColorRoundTrips)
{
	ProbeSwatchWidget w;

	w.set_selected_color(olive::Color(0.2, 0.4, 0.6));

	EXPECT_FLOAT_EQ(w.get_selected_color().red(), 0.2f);
	EXPECT_FLOAT_EQ(w.get_selected_color().green(), 0.4f);
	EXPECT_FLOAT_EQ(w.get_selected_color().blue(), 0.6f);

	// Programmatic changes count as external
	EXPECT_EQ(w.changed_events, 1);
	EXPECT_TRUE(w.last_external);
}

TEST(WidgetColorSwatchWidget, MousePressPicksColorAndEmits)
{
	ProbeSwatchWidget w;
	w.resize(100, 100);

	// olive::Color is not a registered metatype in this harness, so capture
	// the signal payload through a lambda
	QVector<olive::Color> received;
	QObject::connect(&w, &olive::ColorSwatchWidget::selected_color_changed,
					 [&received](const olive::Color &c) { received.append(c); });

	QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, QPoint(10, 20));

	ASSERT_EQ(received.size(), 1);
	EXPECT_NEAR(received.first().red(), 0.10, 1e-6);
	EXPECT_NEAR(received.first().green(), 0.20, 1e-6);
	EXPECT_NEAR(received.first().blue(), 0.5, 1e-6);

	// User interaction is reported as non-external
	EXPECT_EQ(w.changed_events, 1);
	EXPECT_FALSE(w.last_external);

	EXPECT_NEAR(w.get_selected_color().red(), 0.10, 1e-6);
	EXPECT_NEAR(w.get_selected_color().green(), 0.20, 1e-6);
}

TEST(WidgetColorSwatchWidget, DragEmitsForEachMove)
{
	ProbeSwatchWidget w;
	w.resize(100, 100);

	QVector<olive::Color> received;
	QObject::connect(&w, &olive::ColorSwatchWidget::selected_color_changed,
					 [&received](const olive::Color &c) { received.append(c); });

	QTest::mousePress(&w, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
	ASSERT_EQ(received.size(), 1);

	// A move while the left button is held updates the color...
	QTest::mouseMove(&w, QPoint(30, 40));
	ASSERT_EQ(received.size(), 2);
	EXPECT_NEAR(received.at(1).red(), 0.30, 1e-6);
	EXPECT_NEAR(received.at(1).green(), 0.40, 1e-6);

	QTest::mouseRelease(&w, Qt::LeftButton, Qt::NoModifier, QPoint(30, 40));
	EXPECT_EQ(received.size(), 2);

	// ...but a bare move without a pressed button is ignored
	QTest::mouseMove(&w, QPoint(50, 50));
	EXPECT_EQ(received.size(), 2);
}

TEST(WidgetColorSwatchWidget, ManagedColorPassesThroughWithoutProcessors)
{
	ProbeSwatchWidget w;

	// With no color processors installed, the managed color is the input
	const olive::Color in(0.1, 0.2, 0.3, 1.0);
	const olive::Color out = w.pub_managed_color(in);
	EXPECT_FLOAT_EQ(out.red(), 0.1f);
	EXPECT_FLOAT_EQ(out.green(), 0.2f);
	EXPECT_FLOAT_EQ(out.blue(), 0.3f);

	// Setting null processors still forces a full external refresh
	w.set_color_processor(nullptr, nullptr);
	EXPECT_TRUE(w.last_external);
}

TEST(WidgetColorSwatchWidget, SelectorColorFollowsLuminance)
{
	ProbeSwatchWidget w;

	// Bright swatches get a black selector, dark swatches a white one
	w.set_selected_color(olive::Color(1.0, 1.0, 1.0));
	EXPECT_EQ(w.pub_selector_color(), Qt::black);

	w.set_selected_color(olive::Color(0.0, 0.0, 0.0));
	EXPECT_EQ(w.pub_selector_color(), Qt::white);
}
