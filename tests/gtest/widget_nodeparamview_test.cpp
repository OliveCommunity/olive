#include <gtest/gtest.h>

#include <memory>

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

#include "core.h"
#include "node/color/colormanager/colormanager.h"
#include "node/generator/text/textv3.h"
#include "node/globals.h"
#include "node/math/math/math.h"
#include "node/project.h"
#include "node/project/folder/folder.h"
#include "oakengine/app.h"
#include "undo/undostack.h"
#include "widget/clickablelabel/clickablelabel.h"
#include "widget/collapsebutton/collapsebutton.h"
#include "widget/nodeparamview/nodeparambutton.h"
#include "widget/nodeparamview/nodeparamviewarraywidget.h"
#include "widget/nodeparamview/nodeparamviewconnectedlabel.h"
#include "widget/nodeparamview/nodeparamviewcontext.h"
#include "widget/nodeparamview/nodeparamviewitem.h"
#include "widget/nodeparamview/nodeparamviewitemtitlebar.h"
#include "widget/nodeparamview/nodeparamviewkeyframecontrol.h"
#include "widget/nodeparamview/nodeparamviewtextedit.h"

namespace
{

// Widgets that connect to Core::instance() at construction require the
// application singleton, but not a MainWindow
void ensure_core()
{
	if (!olive::Core::instance()) {
		new olive::Core(); // intentionally leaked
	}
}

// Helper: wrap an engine Node* as oak::Node for the C ABI widget interface
inline oak::Node to_oak(olive::Node *n)
{
	return oak::Node(reinterpret_cast<OakEngineNode *>(n));
}

// Helper: build an oak::Input value from an engine Node* + input id
inline oak::Input to_oak_input(olive::Node *n, const QString &input,
							   int element = -1)
{
	return oak::Input(reinterpret_cast<OakEngineNode *>(n), input, element);
}

// The process-wide undo stack previously reached via Core::undo_stack();
// facade calls below (array insert/remove, keyframing, edge disconnect) push
// commands onto it, so it must be cleared before the owning Project dies
inline olive::UndoStack *app_undo_stack()
{
	return static_cast<olive::UndoStack *>(oakengine_app_undo_stack());
}

// Minimal node with a plain float output so it can feed MathNode's
// parameter inputs in connection tests
class FloatSourceNode : public olive::Node {
public:
	FloatSourceNode() = default;

	NODE_DEFAULT_FUNCTIONS(FloatSourceNode)

	virtual QString name() const override
	{
		return QStringLiteral("Test Float Source");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.oak.test.floatsource");
	}

	virtual QVector<CategoryID> category() const override
	{
		return { k_category_math };
	}

	virtual void value(const olive::NodeValueRow &value,
					   const olive::NodeGlobals &globals,
					   olive::NodeValueTable *table) const override
	{
		Q_UNUSED(value)
		Q_UNUSED(globals)

		table->push(olive::NodeValue::k_float, QVariant(1.5), this);
	}
};

// Sends a mouse event straight to the widget, bypassing child hit-testing
// and cursor tracking (deterministic under the offscreen QPA)
void send_mouse_event(QWidget *w, QEvent::Type type, const QPointF &pos)
{
	QMouseEvent ev(type, pos, pos, pos, Qt::LeftButton, Qt::LeftButton,
				   Qt::NoModifier);
	QApplication::sendEvent(w, &ev);
}

} // namespace

// ---------------------------------------------------------------------------
// NodeParamButton (global namespace, plain QPushButton with a stored name)
// ---------------------------------------------------------------------------

TEST(WidgetNodeParamButton, ClickEmitsStoredName)
{
	NodeParamButton btn(QStringLiteral("volume"));

	// The name is stored separately; it is not used as the button text
	EXPECT_TRUE(btn.text().isEmpty());

	QSignalSpy spy(&btn, &NodeParamButton::on_pressed);
	btn.click();

	ASSERT_EQ(spy.count(), 1);
	EXPECT_EQ(spy.first().first().toString(), QStringLiteral("volume"));
}

TEST(WidgetNodeParamButton, InstancesEmitTheirOwnNames)
{
	NodeParamButton a(QStringLiteral("alpha"));
	NodeParamButton b(QStringLiteral("beta"));

	QStringList received;
	QObject::connect(&a, &NodeParamButton::on_pressed,
					 [&received](const QString &n) { received.append(n); });
	QObject::connect(&b, &NodeParamButton::on_pressed,
					 [&received](const QString &n) { received.append(n); });

	b.click();
	a.click();
	b.click();

	ASSERT_EQ(received.size(), 3);
	EXPECT_EQ(received.at(0), QStringLiteral("beta"));
	EXPECT_EQ(received.at(1), QStringLiteral("alpha"));
	EXPECT_EQ(received.at(2), QStringLiteral("beta"));
}

// ---------------------------------------------------------------------------
// NodeParamViewItemTitleBar
// ---------------------------------------------------------------------------

TEST(WidgetNodeParamViewItemTitleBar, DefaultsAreExpandedWithHiddenButtons)
{
	olive::NodeParamViewItemTitleBar bar;

	// CollapseButton defaults to checked, so the bar starts expanded
	EXPECT_TRUE(bar.is_expanded());

	auto *collapse = bar.findChild<olive::CollapseButton *>();
	ASSERT_NE(collapse, nullptr);
	EXPECT_TRUE(collapse->isChecked());
	EXPECT_TRUE(collapse->isVisibleTo(&bar));

	// Pin / add-effect / enabled-checkbox all start hidden
	QPushButton *pin = nullptr;
	QPushButton *add_fx = nullptr;
	for (QPushButton *b : bar.findChildren<QPushButton *>()) {
		if (b == collapse) {
			continue;
		}
		if (b->text() == QStringLiteral("P")) {
			pin = b;
		} else {
			add_fx = b;
		}
	}
	ASSERT_NE(pin, nullptr);
	ASSERT_NE(add_fx, nullptr);
	EXPECT_TRUE(pin->isCheckable());
	EXPECT_FALSE(pin->isVisibleTo(&bar));
	EXPECT_FALSE(add_fx->isVisibleTo(&bar));

	auto *checkbox = bar.findChild<QCheckBox *>();
	ASSERT_NE(checkbox, nullptr);
	EXPECT_FALSE(checkbox->isVisibleTo(&bar));
}

TEST(WidgetNodeParamViewItemTitleBar, SetExpandedRoundTripsWithoutSignal)
{
	olive::NodeParamViewItemTitleBar bar;
	QSignalSpy spy(&bar,
				   &olive::NodeParamViewItemTitleBar::expanded_state_changed);

	bar.set_expanded(false);
	EXPECT_FALSE(bar.is_expanded());

	bar.set_expanded(true);
	EXPECT_TRUE(bar.is_expanded());

	// Programmatic expansion changes do not count as user interaction
	EXPECT_EQ(spy.count(), 0);
}

TEST(WidgetNodeParamViewItemTitleBar, SetTextUpdatesLabelAndTooltip)
{
	olive::NodeParamViewItemTitleBar bar;
	bar.set_text(QStringLiteral("My Node"));

	auto *lbl = bar.findChild<QLabel *>();
	ASSERT_NE(lbl, nullptr);
	EXPECT_EQ(lbl->text(), QStringLiteral("My Node"));
	EXPECT_EQ(lbl->toolTip(), QStringLiteral("My Node"));
}

TEST(WidgetNodeParamViewItemTitleBar, VisibilitySettersToggleChildren)
{
	olive::NodeParamViewItemTitleBar bar;

	QPushButton *pin = nullptr;
	QPushButton *add_fx = nullptr;
	for (QPushButton *b : bar.findChildren<QPushButton *>()) {
		if (qobject_cast<olive::CollapseButton *>(b)) {
			continue;
		}
		if (b->text() == QStringLiteral("P")) {
			pin = b;
		} else {
			add_fx = b;
		}
	}
	auto *checkbox = bar.findChild<QCheckBox *>();
	ASSERT_NE(pin, nullptr);
	ASSERT_NE(add_fx, nullptr);
	ASSERT_NE(checkbox, nullptr);

	bar.set_pin_button_visible(true);
	EXPECT_TRUE(pin->isVisibleTo(&bar));
	EXPECT_FALSE(add_fx->isVisibleTo(&bar));

	bar.set_add_effect_button_visible(true);
	EXPECT_TRUE(add_fx->isVisibleTo(&bar));

	bar.set_enabled_check_box_visible(true);
	EXPECT_TRUE(checkbox->isVisibleTo(&bar));

	bar.set_pin_button_visible(false);
	EXPECT_FALSE(pin->isVisibleTo(&bar));
}

TEST(WidgetNodeParamViewItemTitleBar, EnabledCheckBoxCheckedRoundTrips)
{
	olive::NodeParamViewItemTitleBar bar;
	auto *checkbox = bar.findChild<QCheckBox *>();
	ASSERT_NE(checkbox, nullptr);
	EXPECT_FALSE(checkbox->isChecked());

	bar.set_enabled_check_box_checked(true);
	EXPECT_TRUE(checkbox->isChecked());

	bar.set_enabled_check_box_checked(false);
	EXPECT_FALSE(checkbox->isChecked());
}

TEST(WidgetNodeParamViewItemTitleBar, CollapseClickEmitsExpandedState)
{
	olive::NodeParamViewItemTitleBar bar;
	auto *collapse = bar.findChild<olive::CollapseButton *>();
	ASSERT_NE(collapse, nullptr);

	QSignalSpy spy(&bar,
				   &olive::NodeParamViewItemTitleBar::expanded_state_changed);

	// Starts expanded; clicking collapses
	collapse->click();
	ASSERT_EQ(spy.count(), 1);
	EXPECT_FALSE(spy.first().first().toBool());

	collapse->click();
	ASSERT_EQ(spy.count(), 2);
	EXPECT_TRUE(spy.at(1).first().toBool());
}

TEST(WidgetNodeParamViewItemTitleBar, ButtonsForwardTheirSignals)
{
	olive::NodeParamViewItemTitleBar bar;

	QPushButton *pin = nullptr;
	QPushButton *add_fx = nullptr;
	for (QPushButton *b : bar.findChildren<QPushButton *>()) {
		if (qobject_cast<olive::CollapseButton *>(b)) {
			continue;
		}
		if (b->text() == QStringLiteral("P")) {
			pin = b;
		} else {
			add_fx = b;
		}
	}
	auto *checkbox = bar.findChild<QCheckBox *>();
	ASSERT_NE(pin, nullptr);
	ASSERT_NE(add_fx, nullptr);
	ASSERT_NE(checkbox, nullptr);

	QSignalSpy pin_spy(&bar, &olive::NodeParamViewItemTitleBar::pin_toggled);
	QSignalSpy add_spy(
		&bar, &olive::NodeParamViewItemTitleBar::add_effect_button_clicked);
	QSignalSpy enabled_spy(
		&bar, &olive::NodeParamViewItemTitleBar::enabled_check_box_clicked);

	pin->click();
	ASSERT_EQ(pin_spy.count(), 1);
	EXPECT_TRUE(pin_spy.first().first().toBool());

	add_fx->click();
	EXPECT_EQ(add_spy.count(), 1);

	checkbox->click();
	ASSERT_EQ(enabled_spy.count(), 1);
	EXPECT_TRUE(enabled_spy.first().first().toBool());
}

TEST(WidgetNodeParamViewItemTitleBar, MousePressEmitsClicked)
{
	olive::NodeParamViewItemTitleBar bar;
	QSignalSpy spy(&bar, &olive::NodeParamViewItemTitleBar::clicked);

	send_mouse_event(&bar, QEvent::MouseButtonPress, QPointF(5, 5));
	EXPECT_EQ(spy.count(), 1);
}

TEST(WidgetNodeParamViewItemTitleBar, DoubleClickTogglesExpansion)
{
	olive::NodeParamViewItemTitleBar bar;
	ASSERT_TRUE(bar.is_expanded());

	QSignalSpy spy(&bar,
				   &olive::NodeParamViewItemTitleBar::expanded_state_changed);

	send_mouse_event(&bar, QEvent::MouseButtonDblClick, QPointF(5, 5));
	EXPECT_FALSE(bar.is_expanded());
	ASSERT_EQ(spy.count(), 1);
	EXPECT_FALSE(spy.first().first().toBool());

	send_mouse_event(&bar, QEvent::MouseButtonDblClick, QPointF(5, 5));
	EXPECT_TRUE(bar.is_expanded());
	EXPECT_EQ(spy.count(), 2);
}

// ---------------------------------------------------------------------------
// NodeParamViewTextEdit
// ---------------------------------------------------------------------------

TEST(WidgetNodeParamViewTextEdit, SetTextRoundTripsWithoutSignal)
{
	olive::NodeParamViewTextEdit w;
	QSignalSpy spy(&w, &olive::NodeParamViewTextEdit::text_edited);

	w.setText(QStringLiteral("hello"));
	EXPECT_EQ(w.text(), QStringLiteral("hello"));

	// Programmatic changes don't count as user edits
	EXPECT_EQ(spy.count(), 0);
}

TEST(WidgetNodeParamViewTextEdit, TypingEmitsTextEdited)
{
	olive::NodeParamViewTextEdit w;
	auto *edit = w.findChild<QPlainTextEdit *>();
	ASSERT_NE(edit, nullptr);

	QSignalSpy spy(&w, &olive::NodeParamViewTextEdit::text_edited);
	QTest::keyClicks(edit, QStringLiteral("hi"));

	ASSERT_EQ(spy.count(), 2);
	EXPECT_EQ(spy.first().first().toString(), QStringLiteral("h"));
	EXPECT_EQ(spy.at(1).first().toString(), QStringLiteral("hi"));
	EXPECT_EQ(w.text(), QStringLiteral("hi"));
}

TEST(WidgetNodeParamViewTextEdit, SetTextPreservingCursorKeepsPosition)
{
	olive::NodeParamViewTextEdit w;
	auto *edit = w.findChild<QPlainTextEdit *>();
	ASSERT_NE(edit, nullptr);

	w.setText(QStringLiteral("hello"));
	QTextCursor c = edit->textCursor();
	c.setPosition(5);
	edit->setTextCursor(c);

	w.setTextPreservingCursor(QStringLiteral("hello world"));

	EXPECT_EQ(w.text(), QStringLiteral("hello world"));
	EXPECT_EQ(edit->textCursor().position(), 5);
}

TEST(WidgetNodeParamViewTextEdit, ViewerOnlyModeSwapsVisibleChildren)
{
	olive::NodeParamViewTextEdit w;
	auto *edit = w.findChild<QPlainTextEdit *>();
	ASSERT_NE(edit, nullptr);

	QPushButton *viewer_btn = nullptr;
	QPushButton *edit_btn = nullptr;
	for (QPushButton *b : w.findChildren<QPushButton *>()) {
		if (b->text() == QStringLiteral("Edit In Viewer")) {
			viewer_btn = b;
		} else {
			edit_btn = b;
		}
	}
	ASSERT_NE(viewer_btn, nullptr);
	ASSERT_NE(edit_btn, nullptr);

	// Default mode: plain-text edit + dialog button, no viewer button
	EXPECT_TRUE(edit->isVisibleTo(&w));
	EXPECT_TRUE(edit_btn->isVisibleTo(&w));
	EXPECT_FALSE(viewer_btn->isVisibleTo(&w));

	w.set_edit_in_viewer_only_mode(true);
	EXPECT_FALSE(edit->isVisibleTo(&w));
	EXPECT_FALSE(edit_btn->isVisibleTo(&w));
	EXPECT_TRUE(viewer_btn->isVisibleTo(&w));

	w.set_edit_in_viewer_only_mode(false);
	EXPECT_TRUE(edit->isVisibleTo(&w));
	EXPECT_TRUE(edit_btn->isVisibleTo(&w));
	EXPECT_FALSE(viewer_btn->isVisibleTo(&w));
}

TEST(WidgetNodeParamViewTextEdit, EditInViewerButtonEmitsRequest)
{
	olive::NodeParamViewTextEdit w;

	QPushButton *viewer_btn = nullptr;
	for (QPushButton *b : w.findChildren<QPushButton *>()) {
		if (b->text() == QStringLiteral("Edit In Viewer")) {
			viewer_btn = b;
			break;
		}
	}
	ASSERT_NE(viewer_btn, nullptr);

	QSignalSpy spy(&w, &olive::NodeParamViewTextEdit::request_edit_in_viewer);
	viewer_btn->click();
	EXPECT_EQ(spy.count(), 1);
}

// NOTE: the other push button opens a modal TextDialog (exec()); not
// testable under the offscreen QPA.

// ---------------------------------------------------------------------------
// Shared fixture for the engine-backed widgets
// ---------------------------------------------------------------------------

class NodeParamViewTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		olive::ColorManager::set_up_default_config();
		ensure_core();

		project_ = std::make_unique<olive::Project>();
		project_->initialize();
	}

	void TearDown() override
	{
		// Undo commands pushed by facade calls reference project nodes
		app_undo_stack()->clear();
		project_.reset();
	}

	template <typename T> T *add_node()
	{
		auto *node = new T();
		node->setParent(project_.get());
		return node;
	}

	std::unique_ptr<olive::Project> project_;
};

// ---------------------------------------------------------------------------
// NodeParamViewConnectedLabel
// ---------------------------------------------------------------------------

TEST_F(NodeParamViewTest, ConnectedLabelDisconnectedInputShowsNothing)
{
	auto *math = add_node<olive::MathNode>();

	olive::NodeParamViewConnectedLabel w(
		to_oak_input(math, olive::MathNode::k_param_a_in));

	auto *lbl = w.findChild<olive::ClickableLabel *>();
	ASSERT_NE(lbl, nullptr);
	EXPECT_EQ(lbl->text(), QStringLiteral("Nothing"));
}

TEST_F(NodeParamViewTest, ConnectedLabelShowsSourceNameWhenConnected)
{
	auto *source = add_node<FloatSourceNode>();
	auto *math = add_node<olive::MathNode>();

	olive::Node::connect_edge(
		source, olive::NodeInput(math, olive::MathNode::k_param_a_in));

	olive::NodeParamViewConnectedLabel w(
		to_oak_input(math, olive::MathNode::k_param_a_in));

	auto *lbl = w.findChild<olive::ClickableLabel *>();
	ASSERT_NE(lbl, nullptr);
	EXPECT_EQ(lbl->text(), QStringLiteral("Test Float Source"));
}

TEST_F(NodeParamViewTest, ConnectedLabelTracksLiveConnectAndDisconnect)
{
	auto *source = add_node<FloatSourceNode>();
	auto *math = add_node<olive::MathNode>();

	olive::NodeParamViewConnectedLabel w(
		to_oak_input(math, olive::MathNode::k_param_a_in));
	auto *lbl = w.findChild<olive::ClickableLabel *>();
	ASSERT_NE(lbl, nullptr);
	EXPECT_EQ(lbl->text(), QStringLiteral("Nothing"));

	// The widget subscribes to the engine's edge events via its bridge
	OakEngineNode *src_handle = reinterpret_cast<OakEngineNode *>(source);
	OakEngineNode *math_handle = reinterpret_cast<OakEngineNode *>(math);
	const QByteArray input_id = olive::MathNode::k_param_a_in.toUtf8();

	ASSERT_EQ(oakengine_node_connect(src_handle, math_handle,
									 input_id.constData()),
			  OAKENGINE_OK);
	EXPECT_EQ(lbl->text(), QStringLiteral("Test Float Source"));

	ASSERT_EQ(oakengine_node_disconnect(math_handle, input_id.constData()),
			  OAKENGINE_OK);
	EXPECT_EQ(lbl->text(), QStringLiteral("Nothing"));
}

TEST_F(NodeParamViewTest, ConnectedLabelClickRequestsSourceSelection)
{
	auto *source = add_node<FloatSourceNode>();
	auto *math = add_node<olive::MathNode>();

	olive::Node::connect_edge(
		source, olive::NodeInput(math, olive::MathNode::k_param_a_in));

	olive::NodeParamViewConnectedLabel w(
		to_oak_input(math, olive::MathNode::k_param_a_in));
	w.resize(400, 40);
	w.show();
	EXPECT_TRUE(QTest::qWaitForWindowExposed(&w));

	auto *lbl = w.findChild<olive::ClickableLabel *>();
	ASSERT_NE(lbl, nullptr);

	// ClickableLabel only emits when the cursor is over it; if the
	// offscreen platform can't track the cursor there's nothing to assert
	QTest::mouseMove(lbl, QPoint(5, 5));
	if (!lbl->underMouse()) {
		GTEST_SKIP() << "Platform does not track cursor position";
	}

	OakEngineNode *received = nullptr;
	QObject::connect(&w, &olive::NodeParamViewConnectedLabel::request_select_node,
					 [&received](OakEngineNode *n) { received = n; });

	QTest::mouseClick(lbl, Qt::LeftButton);
	EXPECT_EQ(received, reinterpret_cast<OakEngineNode *>(source));
}

// ---------------------------------------------------------------------------
// NodeParamViewKeyframeControl
// ---------------------------------------------------------------------------

TEST_F(NodeParamViewTest, KeyframeControlInvalidInputDisablesEverything)
{
	olive::NodeParamViewKeyframeControl control;
	EXPECT_FALSE(control.get_connected_input().is_valid());

	// Both constructor forms build the same four buttons
	olive::NodeParamViewKeyframeControl left_aligned(false, nullptr);
	for (QWidget *c : { static_cast<QWidget *>(&control),
						static_cast<QWidget *>(&left_aligned) }) {
		const auto buttons = c->findChildren<QPushButton *>();
		ASSERT_EQ(buttons.size(), 4);

		int checkable_count = 0;
		for (QPushButton *b : buttons) {
			EXPECT_FALSE(b->isEnabled());
			if (b->isCheckable()) {
				checkable_count++;
			}
		}
		// toggle + enable are checkable, prev/next are not
		EXPECT_EQ(checkable_count, 2);
	}

	// With keyframing off only the enable button is shown
	int visible_count = 0;
	for (QPushButton *b : control.findChildren<QPushButton *>()) {
		if (b->isVisibleTo(&control)) {
			visible_count++;
			EXPECT_TRUE(b->isCheckable());
			EXPECT_FALSE(b->isChecked());
		}
	}
	EXPECT_EQ(visible_count, 1);
}

TEST_F(NodeParamViewTest, KeyframeControlSetInputEnablesButtons)
{
	auto *math = add_node<olive::MathNode>();

	olive::NodeParamViewKeyframeControl control;
	control.set_input(to_oak_input(math, olive::MathNode::k_param_a_in));

	EXPECT_TRUE(control.get_connected_input().is_valid());
	EXPECT_EQ(control.get_connected_input().input_id(),
			  olive::MathNode::k_param_a_in);

	const auto buttons = control.findChildren<QPushButton *>();
	ASSERT_EQ(buttons.size(), 4);
	for (QPushButton *b : buttons) {
		EXPECT_TRUE(b->isEnabled());
	}

	// Input is not keyframing yet: nav buttons stay hidden, enable unchecked
	for (QPushButton *b : buttons) {
		if (b->isCheckable() && b->isVisibleTo(&control)) {
			EXPECT_FALSE(b->isChecked()); // the enable button
		} else {
			EXPECT_FALSE(b->isVisibleTo(&control)); // prev/toggle/next
		}
	}
}

TEST_F(NodeParamViewTest, KeyframeControlReflectsKeyframingChanges)
{
	auto *math = add_node<olive::MathNode>();

	olive::NodeParamViewKeyframeControl control;
	control.set_input(to_oak_input(math, olive::MathNode::k_param_a_in));

	// Identify the enable button while it is the only visible checkable one
	QPushButton *enable_btn = nullptr;
	for (QPushButton *b : control.findChildren<QPushButton *>()) {
		if (b->isCheckable() && b->isVisibleTo(&control)) {
			enable_btn = b;
			break;
		}
	}
	ASSERT_NE(enable_btn, nullptr);
	EXPECT_FALSE(enable_btn->isChecked());

	OakEngineNode *handle = reinterpret_cast<OakEngineNode *>(math);
	const QByteArray input_id = olive::MathNode::k_param_a_in.toUtf8();

	// Enabling keyframing through the facade fires the bridge event, which
	// checks the enable button and reveals the navigation buttons
	ASSERT_EQ(oakengine_node_set_input_keyframing(handle, input_id.constData(),
												  -1, 1, 0, 1, nullptr),
			  OAKENGINE_OK);
	EXPECT_TRUE(enable_btn->isChecked());
	for (QPushButton *b : control.findChildren<QPushButton *>()) {
		EXPECT_TRUE(b->isVisibleTo(&control));
	}

	// Disabling hides the navigation buttons again
	ASSERT_EQ(oakengine_node_set_input_keyframing(handle, input_id.constData(),
												  -1, 0, 0, 1, nullptr),
			  OAKENGINE_OK);
	EXPECT_FALSE(enable_btn->isChecked());
	for (QPushButton *b : control.findChildren<QPushButton *>()) {
		EXPECT_EQ(b->isVisibleTo(&control), b == enable_btn);
	}
}

// ---------------------------------------------------------------------------
// NodeParamViewArrayWidget / NodeParamViewArrayButton
// ---------------------------------------------------------------------------

TEST(WidgetNodeParamViewArrayButton, TypeDeterminesText)
{
	olive::NodeParamViewArrayButton add(olive::NodeParamViewArrayButton::k_add);
	EXPECT_EQ(add.text(), QStringLiteral("+"));

	olive::NodeParamViewArrayButton remove(
		olive::NodeParamViewArrayButton::k_remove);
	EXPECT_EQ(remove.text(), QStringLiteral("-"));
}

TEST_F(NodeParamViewTest, ArrayWidgetCounterTracksArraySize)
{
	auto *text = add_node<olive::TextGeneratorV3>();

	olive::NodeParamViewArrayWidget w(to_oak(text),
									  olive::TextGeneratorV3::k_args_input);

	auto *lbl = w.findChild<QLabel *>();
	ASSERT_NE(lbl, nullptr);
	EXPECT_EQ(lbl->text(), QStringLiteral("0 element(s)"));

	OakEngineNode *handle = reinterpret_cast<OakEngineNode *>(text);
	const QByteArray input_id = olive::TextGeneratorV3::k_args_input.toUtf8();

	// The widget subscribes to the engine's array-size event via its bridge
	ASSERT_EQ(oakengine_node_array_insert_at(handle, input_id.constData(), 0),
			  OAKENGINE_OK);
	EXPECT_EQ(lbl->text(), QStringLiteral("1 element(s)"));

	ASSERT_EQ(oakengine_node_array_insert_at(handle, input_id.constData(), 1),
			  OAKENGINE_OK);
	EXPECT_EQ(lbl->text(), QStringLiteral("2 element(s)"));

	ASSERT_EQ(oakengine_node_array_remove_at(handle, input_id.constData(), 0),
			  OAKENGINE_OK);
	EXPECT_EQ(lbl->text(), QStringLiteral("1 element(s)"));
}

TEST_F(NodeParamViewTest, ArrayWidgetDoubleClickEmitsSignal)
{
	auto *text = add_node<olive::TextGeneratorV3>();

	olive::NodeParamViewArrayWidget w(to_oak(text),
									  olive::TextGeneratorV3::k_args_input);

	QSignalSpy spy(&w, &olive::NodeParamViewArrayWidget::double_clicked);
	send_mouse_event(&w, QEvent::MouseButtonDblClick, QPointF(2, 2));
	EXPECT_EQ(spy.count(), 1);
}

// ---------------------------------------------------------------------------
// NodeParamViewContext
// ---------------------------------------------------------------------------

TEST_F(NodeParamViewTest, ContextDefaultConstruction)
{
	olive::NodeParamViewContext ctx;

	EXPECT_NE(ctx.get_dock_area(), nullptr);
	EXPECT_TRUE(ctx.get_contexts().isEmpty());
	EXPECT_TRUE(ctx.get_items().isEmpty());
}

TEST_F(NodeParamViewTest, ContextAddAndRemoveContexts)
{
	auto *math = add_node<olive::MathNode>();
	auto *folder = add_node<olive::Folder>();

	olive::NodeParamViewContext ctx;
	ctx.add_context(to_oak(math));
	ctx.add_context(to_oak(folder));

	ASSERT_EQ(ctx.get_contexts().size(), 2);
	EXPECT_EQ(ctx.get_contexts().at(0), to_oak(math));
	EXPECT_EQ(ctx.get_contexts().at(1), to_oak(folder));

	ctx.remove_context(to_oak(math));
	ASSERT_EQ(ctx.get_contexts().size(), 1);
	EXPECT_EQ(ctx.get_contexts().first(), to_oak(folder));

	// Removing something that isn't there is a no-op
	ctx.remove_context(to_oak(math));
	EXPECT_EQ(ctx.get_contexts().size(), 1);
}

TEST_F(NodeParamViewTest, ContextAddGetAndRemoveNodeItems)
{
	auto *math = add_node<olive::MathNode>();
	auto *folder = add_node<olive::Folder>();

	olive::NodeParamViewContext ctx;

	auto *item = new olive::NodeParamViewItem(to_oak(math),
											  olive::k_no_check_boxes, &ctx);
	item->set_context(to_oak(folder));

	ctx.add_node(item);
	ASSERT_EQ(ctx.get_items().size(), 1);
	EXPECT_EQ(ctx.get_items().first(), item);

	// Lookup matches on (node, context) pair only
	EXPECT_EQ(ctx.get_item(to_oak(math), to_oak(folder)), item);
	EXPECT_EQ(ctx.get_item(to_oak(folder), to_oak(math)), nullptr);
	EXPECT_EQ(ctx.get_item(to_oak(math), to_oak(math)), nullptr);

	olive::NodeParamViewItem *about_to_delete = nullptr;
	QObject::connect(
		&ctx, &olive::NodeParamViewContext::about_to_delete_item,
		[&about_to_delete](olive::NodeParamViewItem *i) {
			about_to_delete = i;
		});

	// Removing a non-matching pair leaves the item alone
	ctx.remove_node(to_oak(math), to_oak(math));
	EXPECT_EQ(ctx.get_items().size(), 1);
	EXPECT_EQ(about_to_delete, nullptr);

	ctx.remove_node(to_oak(math), to_oak(folder));
	EXPECT_EQ(about_to_delete, item);
	EXPECT_TRUE(ctx.get_items().isEmpty());
	EXPECT_EQ(ctx.get_item(to_oak(math), to_oak(folder)), nullptr);
}

TEST_F(NodeParamViewTest, ContextRemoveNodesWithContextRemovesOnlyMatches)
{
	auto *math_a = add_node<olive::MathNode>();
	auto *math_b = add_node<olive::MathNode>();
	auto *folder_a = add_node<olive::Folder>();
	auto *folder_b = add_node<olive::Folder>();

	olive::NodeParamViewContext ctx;

	auto *item_a = new olive::NodeParamViewItem(to_oak(math_a),
												olive::k_no_check_boxes, &ctx);
	item_a->set_context(to_oak(folder_a));
	auto *item_b = new olive::NodeParamViewItem(to_oak(math_b),
												olive::k_no_check_boxes, &ctx);
	item_b->set_context(to_oak(folder_b));

	ctx.add_node(item_a);
	ctx.add_node(item_b);
	ASSERT_EQ(ctx.get_items().size(), 2);

	QVector<olive::NodeParamViewItem *> deleted;
	QObject::connect(&ctx, &olive::NodeParamViewContext::about_to_delete_item,
					 [&deleted](olive::NodeParamViewItem *i) {
						 deleted.append(i);
					 });

	ctx.remove_nodes_with_context(to_oak(folder_a));

	ASSERT_EQ(deleted.size(), 1);
	EXPECT_EQ(deleted.first(), item_a);

	ASSERT_EQ(ctx.get_items().size(), 1);
	EXPECT_EQ(ctx.get_items().first(), item_b);
	EXPECT_EQ(ctx.get_item(to_oak(math_a), to_oak(folder_a)), nullptr);
	EXPECT_EQ(ctx.get_item(to_oak(math_b), to_oak(folder_b)), item_b);
}
