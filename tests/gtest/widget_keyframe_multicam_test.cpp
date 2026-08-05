#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QTreeWidget>

#include <olive/core/render/audioparams.h>
#include <olive/core/render/samplebuffer.h>

#include "core.h"
#include "dialog/configbase/configdialogbasetab.h"
#include "dialog/otioproperties/otiopropertiesdialog.h"
#include "node/color/colormanager/colormanager.h"
#include "node/factory.h"
#include "node/input/multicam/multicamnode.h"
#include "node/keyframe.h"
#include "node/math/math/math.h"
#include "node/nodeundo.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "oakengine/node.h"
#include "render/diskmanager.h"
#include "widget/audiomonitor/audiomonitor.h"
#include "widget/keyframeview/keyframehandle.h"
#include "widget/keyframeview/keyframeview.h"
#include "widget/keyframeview/keyframeviewinputconnection.h"
#include "widget/multicam/multicamdisplay.h"
#include "widget/nodecombobox/nodecombobox.h"

using namespace olive;

namespace
{

// Keyframe views and the facade event subscriptions need the application
// singletons; create them once and leak them (same pattern as the other
// widget suites)
void ensure_app_singletons()
{
	if (!olive::Core::instance()) {
		new olive::Core(); // intentionally leaked
	}
	if (!olive::DiskManager::instance()) {
		olive::DiskManager::create_instance();
	}
}

NodeKeyframe *insert_keyframe(Node *node, const QString &input,
							  const Rational &time, const QVariant &value,
							  int track = 0)
{
	auto *key =
		new NodeKeyframe(time, value, NodeKeyframe::k_linear, track, -1, input);
	NodeParamInsertKeyframeCommand(node, key).redo_now();
	return key;
}

oak::KeyframeTrackRef track_ref(Node *n, const QString &input, int track)
{
	return oak::KeyframeTrackRef(
		oak::Input(reinterpret_cast<OakEngineNode *>(n), input), track);
}

OakEngineKeyframe *handle_of(NodeKeyframe *key)
{
	return reinterpret_cast<OakEngineKeyframe *>(key);
}

// Minimal concrete tab: the base class only provides a default validate()
class RecordingTab : public olive::ConfigDialogBaseTab {
public:
	virtual void accept(void *parent) override
	{
		accepted_with = parent;
	}

	void *accepted_with = nullptr;
};

} // namespace

//
// keyframehandle.h: facade accessors over OakEngineKeyframe identity handles
//
class KeyframeHandleTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();
		ensure_app_singletons();

		project_ = std::make_unique<Project>();
		project_->initialize();

		node_ = new MathNode();
		node_->setParent(project_.get());
	}

	std::unique_ptr<Project> project_;
	MathNode *node_ = nullptr;
};

TEST_F(KeyframeHandleTest, AccessorsReadEngineState)
{
	NodeKeyframe *key = insert_keyframe(node_, MathNode::k_param_a_in,
										Rational(1, 2), 2.5);
	const OakEngineKeyframe *handle = handle_of(key);

	EXPECT_EQ(key_node(handle), reinterpret_cast<OakEngineNode *>(node_));
	EXPECT_EQ(key_time(handle), Rational(1, 2));
	EXPECT_EQ(key_easing(handle), 0); // facade order: 0 = linear
	EXPECT_EQ(key_input_id(handle), MathNode::k_param_a_in);
	EXPECT_EQ(key_track(handle), 0);
	EXPECT_EQ(key_element(handle), -1);
}

TEST_F(KeyframeHandleTest, ValueAsDoubleConvertsNumericTypes)
{
	NodeKeyframe *float_key = insert_keyframe(node_, MathNode::k_param_a_in,
										  Rational(0), 2.5);
	const oak_node_value float_value = key_value(handle_of(float_key));
	EXPECT_EQ(float_value.type, OAK_NODE_VALUE_FLOAT);
	EXPECT_DOUBLE_EQ(key_value_as_double(handle_of(float_key)), 2.5);

	// The base-class enabled input carries boolean keyframes
	NodeKeyframe *bool_key = insert_keyframe(node_, Node::k_enabled_input,
										 Rational(0), true);
	const oak_node_value bool_value = key_value(handle_of(bool_key));
	EXPECT_EQ(bool_value.type, OAK_NODE_VALUE_BOOL);
	EXPECT_DOUBLE_EQ(key_value_as_double(handle_of(bool_key)), 1.0);

	NodeKeyframe *false_key = insert_keyframe(node_, Node::k_enabled_input,
										  Rational(1), false);
	EXPECT_DOUBLE_EQ(key_value_as_double(handle_of(false_key)), 0.0);
}

TEST_F(KeyframeHandleTest, LiveValueSetUpdatesFacadeReads)
{
	NodeKeyframe *key = insert_keyframe(node_, MathNode::k_param_a_in,
										Rational(0), 1.0);

	oak_node_value v;
	std::memset(&v, 0, sizeof(v));
	v.type = OAK_NODE_VALUE_FLOAT;
	v.f[0] = 7.25;
	key_set_value_live(handle_of(key), v);

	EXPECT_DOUBLE_EQ(key_value_as_double(handle_of(key)), 7.25);
	EXPECT_DOUBLE_EQ(key->value().toDouble(), 7.25);
}

TEST_F(KeyframeHandleTest, LiveTimeSetMovesKeyframe)
{
	NodeKeyframe *key = insert_keyframe(node_, MathNode::k_param_a_in,
										Rational(0), 0.0);

	key_set_time_live(handle_of(key), Rational(2));
	EXPECT_EQ(key_time(handle_of(key)), Rational(2));
	EXPECT_EQ(key->time(), Rational(2));

	// The selection ADL wrappers route through the same accessors
	EXPECT_EQ(selection_time(handle_of(key)), Rational(2));
	selection_set_time(handle_of(key), Rational(4));
	EXPECT_EQ(key->time(), Rational(4));
}

TEST_F(KeyframeHandleTest, BezierPointRoundTrip)
{
	NodeKeyframe *key = insert_keyframe(node_, MathNode::k_param_a_in,
										Rational(0), 0.0);
	key->set_type(NodeKeyframe::k_bezier);
	ASSERT_EQ(key_easing(handle_of(key)), 1); // facade order: 1 = bezier

	key_set_bezier_point_live(handle_of(key), 0, QPointF(0.25, 0.75));
	key_set_bezier_point_live(handle_of(key), 1, QPointF(-0.5, 1.5));

	EXPECT_EQ(key_bezier_point(handle_of(key), 0), QPointF(0.25, 0.75));
	EXPECT_EQ(key_bezier_point(handle_of(key), 1), QPointF(-0.5, 1.5));

	// For a bezier keyframe the "valid" points are the actual points
	EXPECT_EQ(key_valid_bezier_point(handle_of(key), 0), QPointF(0.25, 0.75));
	EXPECT_EQ(key_valid_bezier_point(handle_of(key), 1), QPointF(-0.5, 1.5));
}

TEST_F(KeyframeHandleTest, HasSiblingAtTimeExcludesSelf)
{
	// The sibling lookup goes through Node::get_keyframe_at_time_on_track(),
	// which only searches tracks that are not on the standard value, so
	// keyframing must be enabled on the input first
	node_->set_input_is_keyframing(MathNode::k_param_a_in, true);

	NodeKeyframe *key_a = insert_keyframe(node_, MathNode::k_param_a_in,
										  Rational(0), 0.0);
	NodeKeyframe *key_b = insert_keyframe(node_, MathNode::k_param_a_in,
										  Rational(1), 1.0);

	// Another keyframe sits at t=1
	EXPECT_TRUE(key_has_sibling_at_time(handle_of(key_a), Rational(1)));
	// t=0 resolves to key_a itself, which does not count as a sibling
	EXPECT_FALSE(key_has_sibling_at_time(handle_of(key_a), Rational(0)));
	// Nothing at t=5
	EXPECT_FALSE(key_has_sibling_at_time(handle_of(key_a), Rational(5)));

	EXPECT_TRUE(selection_has_sibling_at_time(handle_of(key_b), Rational(0)));
	EXPECT_EQ(selection_time_target_parent(handle_of(key_a)),
			  reinterpret_cast<OakEngineNode *>(node_));
}

//
// keyframeviewinputconnection.h: per-track connection between engine keyframe
// events and a KeyframeView
//
class KeyframeConnectionTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();
		ensure_app_singletons();

		project_ = std::make_unique<Project>();
		project_->initialize();

		node_ = new MathNode();
		node_->setParent(project_.get());
	}

	oak::KeyframeTrackRef param_a_ref() const
	{
		return track_ref(node_, MathNode::k_param_a_in, 0);
	}

	std::unique_ptr<Project> project_;
	MathNode *node_ = nullptr;
};

TEST_F(KeyframeConnectionTest, DefaultsAndAccessors)
{
	KeyframeView view;
	const oak::KeyframeTrackRef ref = param_a_ref();
	KeyframeViewInputConnection connection(ref, &view);

	EXPECT_EQ(connection.get_keyframe_y(), 0);
	EXPECT_EQ(connection.get_brush().color(), QColor(Qt::white));
	EXPECT_EQ(connection.get_reference(), ref);
	EXPECT_TRUE(connection.get_keyframes().isEmpty());
}

TEST_F(KeyframeConnectionTest, SetKeyframeYEmitsOnlyOnChange)
{
	KeyframeView view;
	KeyframeViewInputConnection connection(param_a_ref(), &view);
	QSignalSpy spy(&connection, &KeyframeViewInputConnection::require_update);

	connection.set_keyframe_y(10);
	EXPECT_EQ(connection.get_keyframe_y(), 10);
	EXPECT_EQ(spy.count(), 1);

	// Same value is a no-op
	connection.set_keyframe_y(10);
	EXPECT_EQ(spy.count(), 1);

	connection.set_keyframe_y(-3);
	EXPECT_EQ(connection.get_keyframe_y(), -3);
	EXPECT_EQ(spy.count(), 2);
}

TEST_F(KeyframeConnectionTest, SetBrushEmitsOnlyOnChange)
{
	KeyframeView view;
	KeyframeViewInputConnection connection(param_a_ref(), &view);
	QSignalSpy spy(&connection, &KeyframeViewInputConnection::require_update);

	// Same as the default brush: no emission
	connection.set_brush(QBrush(Qt::white));
	EXPECT_EQ(spy.count(), 0);

	connection.set_brush(QBrush(Qt::red));
	EXPECT_EQ(connection.get_brush().color(), QColor(Qt::red));
	EXPECT_EQ(spy.count(), 1);
}

TEST_F(KeyframeConnectionTest, SetYBehaviorEmitsOnlyOnChange)
{
	KeyframeView view;
	KeyframeViewInputConnection connection(param_a_ref(), &view);
	QSignalSpy spy(&connection, &KeyframeViewInputConnection::require_update);

	// Default behavior is k_single_row
	connection.set_y_behavior(KeyframeViewInputConnection::k_single_row);
	EXPECT_EQ(spy.count(), 0);

	connection.set_y_behavior(KeyframeViewInputConnection::k_value_is_height);
	EXPECT_EQ(spy.count(), 1);

	connection.set_y_behavior(KeyframeViewInputConnection::k_value_is_height);
	EXPECT_EQ(spy.count(), 1);
}

TEST_F(KeyframeConnectionTest, MatchingKeyframeInsertionEmitsRequireUpdate)
{
	KeyframeView view;
	KeyframeViewInputConnection connection(param_a_ref(), &view);
	QSignalSpy spy(&connection, &KeyframeViewInputConnection::require_update);

	insert_keyframe(node_, MathNode::k_param_a_in, Rational(0), 0.0);
	EXPECT_EQ(spy.count(), 1);
	ASSERT_EQ(connection.get_keyframes().size(), 1);

	// A keyframe on a different input of the same node does not match
	insert_keyframe(node_, MathNode::k_param_b_in, Rational(0), 0.0);
	EXPECT_EQ(spy.count(), 1);
	EXPECT_EQ(connection.get_keyframes().size(), 1);
}

TEST_F(KeyframeConnectionTest, LiveValueAndTimeChangesEmitRequireUpdate)
{
	NodeKeyframe *key = insert_keyframe(node_, MathNode::k_param_a_in,
										Rational(0), 0.0);

	KeyframeView view;
	KeyframeViewInputConnection connection(param_a_ref(), &view);
	QSignalSpy spy(&connection, &KeyframeViewInputConnection::require_update);

	oak_node_value v;
	std::memset(&v, 0, sizeof(v));
	v.type = OAK_NODE_VALUE_FLOAT;
	v.f[0] = 3.0;
	key_set_value_live(handle_of(key), v);
	EXPECT_EQ(spy.count(), 1);

	key_set_time_live(handle_of(key), Rational(3));
	EXPECT_EQ(spy.count(), 2);
}

TEST_F(KeyframeConnectionTest, TypeChangeEmitsTypeChangedAndRequireUpdate)
{
	// Node::invalidate_from_keyframe_type_changed() returns early when the
	// track holds a single keyframe (interpolation is a no-op), so the type
	// change signal only reaches the connection with two or more keyframes
	NodeKeyframe *key = insert_keyframe(node_, MathNode::k_param_a_in,
										Rational(0), 0.0);
	insert_keyframe(node_, MathNode::k_param_a_in, Rational(1), 1.0);

	KeyframeView view;
	KeyframeViewInputConnection connection(param_a_ref(), &view);
	QSignalSpy update_spy(&connection,
						  &KeyframeViewInputConnection::require_update);
	QSignalSpy type_spy(&connection,
						&KeyframeViewInputConnection::type_changed);

	key->set_type(NodeKeyframe::k_hold);
	EXPECT_EQ(update_spy.count(), 1);
	EXPECT_EQ(type_spy.count(), 1);

	// Setting the same type again emits nothing
	key->set_type(NodeKeyframe::k_hold);
	EXPECT_EQ(update_spy.count(), 1);
	EXPECT_EQ(type_spy.count(), 1);
}

TEST_F(KeyframeConnectionTest, RemovalEmitsRequireUpdate)
{
	NodeKeyframe *key = insert_keyframe(node_, MathNode::k_param_a_in,
										Rational(0), 0.0);

	KeyframeView view;
	KeyframeViewInputConnection connection(param_a_ref(), &view);
	QSignalSpy spy(&connection, &KeyframeViewInputConnection::require_update);

	NodeParamRemoveKeyframeCommand(key).redo_now();
	EXPECT_EQ(spy.count(), 1);
	EXPECT_TRUE(connection.get_keyframes().isEmpty());
}

//
// multicamdisplay.h: the shader/paint paths need a GL renderer, but the
// widget itself constructs offscreen and its node contract is facade-readable
//
TEST(WidgetMulticamDisplay, ConstructsWithoutNode)
{
	ensure_app_singletons();

	MulticamDisplay display;
	EXPECT_FALSE(display.isVisible());

	// Accepts (and clears) a null node without touching the paint path
	display.set_multicam_node(nullptr);
}

TEST(WidgetMulticamDisplay, MulticamNodeStateMatchesPaintContract)
{
	ensure_app_singletons();

	MulticamDisplay display;

	MultiCamNode node;
	node.input_array_resize(MultiCamNode::k_sources_input, 3);
	node.set_standard_value(MultiCamNode::k_current_input, 1);

	auto *handle = reinterpret_cast<OakEngineNode *>(&node);
	display.set_multicam_node(handle);

	// on_paint() lays the highlight rect out from exactly these facade reads
	EXPECT_EQ(oakengine_multicam_get_source_count(handle), 3);
	EXPECT_EQ(oakengine_multicam_get_current_source(handle), 1);

	int rows = 0, cols = 0;
	oakengine_multicam_get_rows_and_columns(3, &rows, &cols);
	EXPECT_EQ(rows, 2);
	EXPECT_EQ(cols, 2);

	int row = -1, col = -1;
	oakengine_multicam_index_to_row_cols(1, rows, cols, &row, &col);
	EXPECT_EQ(row, 0);
	EXPECT_EQ(col, 1);

	display.set_multicam_node(nullptr);
}

//
// nodecombobox.h: the popup path is modal; the retranslation and factory-name
// paths are not (selection round-trips are covered in widget_combos_test.cpp)
//
TEST(WidgetNodeComboBox, LanguageChangeRetranslatesSelection)
{
	NodeFactory::initialize();

	{
		NodeComboBox combo;
		const QString id = QStringLiteral("org.olivevideoeditor.Olive.math");
		combo.set_node(id);
		ASSERT_EQ(combo.count(), 1);
		const QString before = combo.itemText(0);
		EXPECT_EQ(before, NodeFactory::get_name_from_id(id));
		ASSERT_FALSE(before.isEmpty());

		QEvent lang(QEvent::LanguageChange);
		QApplication::sendEvent(&combo, &lang);

		// The selection survives and the label is re-fetched from the factory
		EXPECT_EQ(combo.get_selected_node(), id);
		ASSERT_EQ(combo.count(), 1);
		EXPECT_EQ(combo.itemText(0), before);
	}

	NodeFactory::destroy();
}

TEST(WidgetNodeComboBox, UnknownIdProducesEmptyText)
{
	NodeFactory::initialize();

	{
		NodeComboBox combo;
		combo.set_node(QStringLiteral("org.example.bogus"));

		// The id is stored even though the factory has no name for it
		EXPECT_EQ(combo.get_selected_node(), QStringLiteral("org.example.bogus"));
		ASSERT_EQ(combo.count(), 1);
		EXPECT_TRUE(combo.itemText(0).isEmpty());
	}

	NodeFactory::destroy();
}

//
// audiomonitor.h: painting needs a GL context; the playback state machine,
// parameter handling and the static instance broadcasters do not
//
TEST(WidgetAudioMonitor, ConstructionDefaults)
{
	AudioMonitor monitor;

	EXPECT_FALSE(monitor.is_playing());
	// The ctor sizes the minimum width to the font height
	EXPECT_EQ(monitor.minimumWidth(), monitor.fontMetrics().height());
}

TEST(WidgetAudioMonitor, StartWaveformWithNullCacheStaysStopped)
{
	AudioMonitor monitor;

	// A null cache reports sample rate 0, so the start request is rejected
	monitor.start_waveform(nullptr, Rational(0), 1);
	EXPECT_FALSE(monitor.is_playing());

	monitor.stop();
	EXPECT_FALSE(monitor.is_playing());
}

TEST(WidgetAudioMonitor, PushSampleBufferWithoutParamsIsIgnored)
{
	AudioMonitor monitor;

	// No params set -> channel_count() is 0 -> early return
	monitor.push_sample_buffer(SampleBuffer());
	EXPECT_FALSE(monitor.is_playing());
}

TEST(WidgetAudioMonitor, PushSampleBufferWithParamsDoesNotStartPlayback)
{
	const olive::core::AudioParams params(48000,
										  olive::core::k_channel_layout_stereo,
										  olive::core::SampleFormat::f32_p);

	AudioMonitor monitor;
	monitor.set_params(params);

	SampleBuffer buffer(params, size_t(64));
	ASSERT_TRUE(buffer.is_allocated());
	float *left = buffer.data(0);
	ASSERT_NE(left, nullptr);
	for (int i = 0; i < 64; i++) {
		left[i] = 0.5f;
	}

	// Level analysis runs, but sample playback is not waveform playback
	monitor.push_sample_buffer(buffer);
	EXPECT_FALSE(monitor.is_playing());

	monitor.stop();
	EXPECT_FALSE(monitor.is_playing());
}

TEST(WidgetAudioMonitor, StaticBroadcastsReachAllInstances)
{
	const olive::core::AudioParams params(48000,
										  olive::core::k_channel_layout_stereo,
										  olive::core::SampleFormat::f32_p);

	AudioMonitor first;
	AudioMonitor second;
	first.set_params(params);
	second.set_params(params);

	// A null waveform is rejected on every registered instance
	AudioMonitor::start_waveform_on_all(nullptr, Rational(0), 1);
	EXPECT_FALSE(first.is_playing());
	EXPECT_FALSE(second.is_playing());

	SampleBuffer buffer(params, size_t(32));
	AudioMonitor::push_sample_buffer_on_all(buffer);
	EXPECT_FALSE(first.is_playing());
	EXPECT_FALSE(second.is_playing());

	AudioMonitor::stop_on_all();
	EXPECT_FALSE(first.is_playing());
	EXPECT_FALSE(second.is_playing());
}

//
// configdialogbasetab.h
//
TEST(DialogConfigBaseTab, DefaultValidateAccepts)
{
	RecordingTab tab;
	EXPECT_TRUE(tab.validate());
}

TEST(DialogConfigBaseTab, AcceptReceivesParentPointer)
{
	RecordingTab tab;
	int parent_token = 0;

	tab.accept(&parent_token);
	EXPECT_EQ(tab.accepted_with, static_cast<void *>(&parent_token));
}

//
// otiopropertiesdialog.h: the per-sequence Settings button opens a modal
// SequenceDialog (not exercised); construction, listing and OK/Cancel are not
// modal
//
TEST(DialogOTIOProperties, ListsSequencesWithIndexedSettingsButtons)
{
	ColorManager::set_up_default_config();

	Project project;
	project.initialize();

	auto *seq_a = new Sequence();
	seq_a->setParent(&project);
	seq_a->set_label(QStringLiteral("Alpha"));
	auto *seq_b = new Sequence();
	seq_b->setParent(&project);
	seq_b->set_label(QStringLiteral("Beta"));

	const QList<OakEngineSequence *> sequences = {
		reinterpret_cast<OakEngineSequence *>(seq_a),
		reinterpret_cast<OakEngineSequence *>(seq_b),
	};

	OTIOPropertiesDialog dialog(
		sequences, reinterpret_cast<OakEngineProject *>(&project));

	EXPECT_EQ(dialog.windowTitle(), QStringLiteral("Load OpenTimelineIO Project"));

	auto *table = dialog.findChild<QTreeWidget *>();
	ASSERT_NE(table, nullptr);
	EXPECT_EQ(table->columnCount(), 2);
	EXPECT_EQ(table->headerItem()->text(0), QStringLiteral("Sequence"));
	EXPECT_EQ(table->headerItem()->text(1), QStringLiteral("Actions"));
	EXPECT_FALSE(table->rootIsDecorated());

	ASSERT_EQ(table->topLevelItemCount(), 2);
	EXPECT_EQ(table->topLevelItem(0)->text(0), QStringLiteral("Alpha"));
	EXPECT_EQ(table->topLevelItem(1)->text(0), QStringLiteral("Beta"));

	// Each row carries a Settings button whose "index" property is its row
	for (int i = 0; i < 2; i++) {
		QWidget *actions = table->itemWidget(table->topLevelItem(i), 1);
		ASSERT_NE(actions, nullptr) << "row " << i;
		auto *button = actions->findChild<QPushButton *>();
		ASSERT_NE(button, nullptr) << "row " << i;
		EXPECT_EQ(button->text(), QStringLiteral("Settings"));
		EXPECT_EQ(button->property("index").toInt(), i);
	}
}

TEST(DialogOTIOProperties, EmptyListAndButtonBoxResults)
{
	ColorManager::set_up_default_config();

	Project project;
	project.initialize();

	{
		OTIOPropertiesDialog dialog(
			{}, reinterpret_cast<OakEngineProject *>(&project));

		auto *table = dialog.findChild<QTreeWidget *>();
		ASSERT_NE(table, nullptr);
		EXPECT_EQ(table->topLevelItemCount(), 0);

		auto *buttons = dialog.findChild<QDialogButtonBox *>();
		ASSERT_NE(buttons, nullptr);
		QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
		ASSERT_NE(ok, nullptr);
		ok->click();
		EXPECT_EQ(dialog.result(), QDialog::Accepted);
	}

	{
		OTIOPropertiesDialog dialog(
			{}, reinterpret_cast<OakEngineProject *>(&project));

		auto *buttons = dialog.findChild<QDialogButtonBox *>();
		ASSERT_NE(buttons, nullptr);
		QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel);
		ASSERT_NE(cancel, nullptr);
		cancel->click();
		EXPECT_EQ(dialog.result(), QDialog::Rejected);
	}
}
