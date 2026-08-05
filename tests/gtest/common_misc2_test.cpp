#include <gtest/gtest.h>

#include <QBuffer>
#include <QDataStream>
#include <QSet>
#include <QVariant>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

#include "common/configwrapper.h"
#include "common/debugapp.h"
#include "common/nodedatatypes.h"
#include "common/nodevaluehandle.h"
#include "common/oakvaluehelper.h"
#include "common/projecttypes.h"
#include "common/subtitleapp.h"
#include "common/tooltypes.h"
#include "common/trackreferencehandle.h"
#include "common/undowrapper.h"
#include "oakengine/undo.h"
#include "oakutil/qtutils.h"

namespace
{

// OakConfigValue reads/writes go through the process-wide engine config, so
// snapshot and restore each key the tests touch
class ScopedConfigKey {
public:
	explicit ScopedConfigKey(const QString &key)
		: key_(key)
		, old_value_(oakengine_config_string(key))
	{
	}

	~ScopedConfigKey()
	{
		oakengine_config_set_string(key_.toUtf8().constData(),
									old_value_.toUtf8().constData());
	}

private:
	static QString oakengine_config_string(const QString &key)
	{
		char buf[1024];
		const int len = oakengine_config_get_string(key.toUtf8().constData(),
													buf, sizeof(buf));
		return QString::fromUtf8(buf, len);
	}

	QString key_;
	QString old_value_;
};

// App-side undo command for wrap_app_undo_command(): counts invocations and
// reports its own destruction so ownership transfer can be observed
struct CountingCmd {
	int *redo_count;
	int *undo_count;
	bool *destroyed;

	void redo()
	{
		++*redo_count;
	}
	void undo()
	{
		++*undo_count;
	}
	~CountingCmd()
	{
		*destroyed = true;
	}
};

} // namespace

TEST(ToolTypes, AddableObjectNamesCoverAllValues)
{
	EXPECT_EQ(olive::Tool::get_addable_object_name(olive::Tool::k_addable_empty),
			  QStringLiteral("Empty"));
	EXPECT_EQ(olive::Tool::get_addable_object_name(olive::Tool::k_addable_bars),
			  QStringLiteral("Bars"));
	EXPECT_EQ(olive::Tool::get_addable_object_name(olive::Tool::k_addable_shape),
			  QStringLiteral("Shape"));
	EXPECT_EQ(olive::Tool::get_addable_object_name(olive::Tool::k_addable_solid),
			  QStringLiteral("Solid"));
	EXPECT_EQ(olive::Tool::get_addable_object_name(olive::Tool::k_addable_title),
			  QStringLiteral("Title"));
	EXPECT_EQ(olive::Tool::get_addable_object_name(olive::Tool::k_addable_tone),
			  QStringLiteral("Tone"));
	EXPECT_EQ(
		olive::Tool::get_addable_object_name(olive::Tool::k_addable_subtitle),
		QStringLiteral("Subtitle"));

	// Out-of-range values fall through to "Unknown"
	EXPECT_EQ(olive::Tool::get_addable_object_name(olive::Tool::k_addable_count),
			  QStringLiteral("Unknown"));
	EXPECT_EQ(olive::Tool::get_addable_object_name(
				  static_cast<olive::Tool::AddableObject>(-1)),
			  QStringLiteral("Unknown"));
}

TEST(ToolTypes, AddableObjectIdsCoverAllValues)
{
	EXPECT_EQ(olive::Tool::get_addable_object_id(olive::Tool::k_addable_empty),
			  QStringLiteral("empty"));
	EXPECT_EQ(olive::Tool::get_addable_object_id(olive::Tool::k_addable_bars),
			  QStringLiteral("bars"));
	EXPECT_EQ(olive::Tool::get_addable_object_id(olive::Tool::k_addable_shape),
			  QStringLiteral("shape"));
	EXPECT_EQ(olive::Tool::get_addable_object_id(olive::Tool::k_addable_solid),
			  QStringLiteral("solid"));
	EXPECT_EQ(olive::Tool::get_addable_object_id(olive::Tool::k_addable_title),
			  QStringLiteral("title"));
	EXPECT_EQ(olive::Tool::get_addable_object_id(olive::Tool::k_addable_tone),
			  QStringLiteral("tone"));
	EXPECT_EQ(olive::Tool::get_addable_object_id(olive::Tool::k_addable_subtitle),
			  QStringLiteral("subtitle"));

	// Every real addable has a unique non-empty id
	QSet<QString> ids;
	for (int i = 0; i < olive::Tool::k_addable_count; i++) {
		const QString id = olive::Tool::get_addable_object_id(
			static_cast<olive::Tool::AddableObject>(i));
		EXPECT_FALSE(id.isEmpty());
		EXPECT_FALSE(ids.contains(id)) << id.toStdString();
		ids.insert(id);
	}

	// Out-of-range values return an empty id
	EXPECT_TRUE(
		olive::Tool::get_addable_object_id(olive::Tool::k_addable_count).isEmpty());
	EXPECT_TRUE(olive::Tool::get_addable_object_id(
					static_cast<olive::Tool::AddableObject>(-1)).isEmpty());
}

TEST(ToolTypes, ItemOrdinalsMatchEngineMirror)
{
	// The C ABI transports these as ints, so the ordinals are the contract
	EXPECT_EQ(olive::Tool::k_none, 0);
	EXPECT_EQ(olive::Tool::k_pointer, 1);
	EXPECT_EQ(olive::Tool::k_track_select, 13);
	EXPECT_EQ(olive::Tool::k_count, 14);
	EXPECT_EQ(olive::Tool::k_addable_count, 7);
}

TEST(ProjectTypes, CacheSettingOrdinals)
{
	EXPECT_EQ(olive::Project::k_cache_use_default_location, 0);
	EXPECT_EQ(olive::Project::k_cache_store_alongside_project, 1);
	EXPECT_EQ(olive::Project::k_cache_custom_path, 2);
}

TEST(NodeDataTypes, OrdinalsMatchEngineMirror)
{
	EXPECT_EQ(olive::k_node_data_icon, 0);
	EXPECT_EQ(olive::k_node_data_duration, 1);
	EXPECT_EQ(olive::k_node_data_created_time, 2);
	EXPECT_EQ(olive::k_node_data_modified_time, 3);
	EXPECT_EQ(olive::k_node_data_frequency_rate, 4);
	EXPECT_EQ(olive::k_node_data_tooltip, 5);
}

TEST(NodeValueHandle, TypeOrdinalsMatchEngineMirror)
{
	EXPECT_EQ(olive::NodeValueType::k_none, 0);
	EXPECT_EQ(olive::NodeValueType::k_boolean, 4);
	EXPECT_EQ(olive::NodeValueType::k_vec2, 12);
	EXPECT_EQ(olive::NodeValueType::k_data_type_count, 23);
}

TEST(NodeValueHandle, KeyframeTypeOrdinalsMatchEngineMirror)
{
	EXPECT_EQ(olive::NodeKeyframeType::k_invalid, -1);
	EXPECT_EQ(olive::NodeKeyframeType::k_linear, 0);
	EXPECT_EQ(olive::NodeKeyframeType::k_hold, 1);
	EXPECT_EQ(olive::NodeKeyframeType::k_bezier, 2);
}

TEST(NodeValueHandle, ToCMappingsDifferFromPlainCast)
{
	// The two enums do NOT share ordinals; the helper must remap each one
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_int),
			  OAK_NODE_VALUE_INT);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_float),
			  OAK_NODE_VALUE_FLOAT);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_boolean),
			  OAK_NODE_VALUE_BOOL);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_rational),
			  OAK_NODE_VALUE_RATIONAL);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_color),
			  OAK_NODE_VALUE_COLOR);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_vec2),
			  OAK_NODE_VALUE_VEC2);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_vec3),
			  OAK_NODE_VALUE_VEC3);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_vec4),
			  OAK_NODE_VALUE_VEC4);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_combo),
			  OAK_NODE_VALUE_COMBO);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_file),
			  OAK_NODE_VALUE_STRING);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_text),
			  OAK_NODE_VALUE_TEXT);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_font),
			  OAK_NODE_VALUE_FONT);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_str_combo),
			  OAK_NODE_VALUE_STR_COMBO);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_binary),
			  OAK_NODE_VALUE_BINARY);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_bezier),
			  OAK_NODE_VALUE_BEZIER);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_texture),
			  OAK_NODE_VALUE_TEXTURE);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_samples),
			  OAK_NODE_VALUE_SAMPLES);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_video_params),
			  OAK_NODE_VALUE_VIDEO_PARAMS);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_audio_params),
			  OAK_NODE_VALUE_AUDIO_PARAMS);

	// k_boolean=4 vs OAK_NODE_VALUE_BOOL=3: a plain cast would be wrong
	EXPECT_NE(olive::NodeValueType::k_boolean, OAK_NODE_VALUE_BOOL);
}

TEST(NodeValueHandle, ToCReturnsMinusOneForUnrepresentable)
{
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_none), -1);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_matrix), -1);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_subtitle_params),
			  -1);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_push_button), -1);
	EXPECT_EQ(olive::node_value_type_to_c(olive::NodeValueType::k_data_type_count),
			  -1);
	EXPECT_EQ(olive::node_value_type_to_c(-7), -1);
}

TEST(TrackReferenceHandle, DefaultIsInvalidNone)
{
	const olive::TrackReference ref;
	EXPECT_EQ(ref.type(), olive::TrackReference::k_none);
	EXPECT_EQ(ref.index(), -1);
	EXPECT_FALSE(ref.is_valid());
	EXPECT_TRUE(ref.to_string().isEmpty());
}

TEST(TrackReferenceHandle, ConstructionAndValidity)
{
	const olive::TrackReference video(olive::TrackReference::k_video, 3);
	EXPECT_EQ(video.type(), olive::TrackReference::k_video);
	EXPECT_EQ(video.index(), 3);
	EXPECT_TRUE(video.is_valid());

	// A known type with a negative index is not valid
	EXPECT_FALSE(
		olive::TrackReference(olive::TrackReference::k_video, -1).is_valid());
	// k_none and k_count are never valid, even with a non-negative index
	EXPECT_FALSE(olive::TrackReference(olive::TrackReference::k_none, 5).is_valid());
	EXPECT_FALSE(
		olive::TrackReference(olive::TrackReference::k_count, 0).is_valid());
	// Index zero is a valid track
	EXPECT_TRUE(
		olive::TrackReference(olive::TrackReference::k_audio, 0).is_valid());
}

TEST(TrackReferenceHandle, ComparisonOperators)
{
	const olive::TrackReference a(olive::TrackReference::k_audio, 1);
	const olive::TrackReference a2(olive::TrackReference::k_audio, 1);
	const olive::TrackReference a3(olive::TrackReference::k_audio, 2);
	const olive::TrackReference v0(olive::TrackReference::k_video, 0);

	EXPECT_TRUE(a == a2);
	EXPECT_FALSE(a != a2);
	EXPECT_TRUE(a != a3);
	EXPECT_TRUE(a != v0);

	// Ordering is by type first (k_video=0 < k_audio=1), then index
	EXPECT_TRUE(v0 < a);
	EXPECT_FALSE(a < v0);
	EXPECT_TRUE(a < a3);
	EXPECT_FALSE(a3 < a);
	EXPECT_FALSE(a < a2);
}

TEST(TrackReferenceHandle, TypeStringMappings)
{
	EXPECT_EQ(olive::TrackReference::type_to_string(olive::TrackReference::k_video),
			  QStringLiteral("v"));
	EXPECT_EQ(olive::TrackReference::type_to_string(olive::TrackReference::k_audio),
			  QStringLiteral("a"));
	EXPECT_EQ(
		olive::TrackReference::type_to_string(olive::TrackReference::k_subtitle),
		QStringLiteral("s"));
	EXPECT_TRUE(
		olive::TrackReference::type_to_string(olive::TrackReference::k_none)
			.isEmpty());
	EXPECT_TRUE(
		olive::TrackReference::type_to_string(olive::TrackReference::k_count)
			.isEmpty());

	EXPECT_EQ(olive::TrackReference::type_to_translated_string(
				  olive::TrackReference::k_video),
			  QStringLiteral("V"));
	EXPECT_EQ(olive::TrackReference::type_to_translated_string(
				  olive::TrackReference::k_audio),
			  QStringLiteral("A"));
	EXPECT_EQ(olive::TrackReference::type_to_translated_string(
				  olive::TrackReference::k_subtitle),
			  QStringLiteral("S"));
	EXPECT_TRUE(olive::TrackReference::type_to_translated_string(
					olive::TrackReference::k_none).isEmpty());
	EXPECT_TRUE(olive::TrackReference::type_to_translated_string(
					olive::TrackReference::k_count).isEmpty());
}

TEST(TrackReferenceHandle, StringRoundTrip)
{
	const olive::TrackReference ref(olive::TrackReference::k_video, 12);
	EXPECT_EQ(ref.to_string(), QStringLiteral("v:12"));

	const olive::TrackReference parsed =
		olive::TrackReference::from_string(QStringLiteral("v:12"));
	EXPECT_EQ(parsed, ref);
	EXPECT_TRUE(parsed.is_valid());

	EXPECT_EQ(olive::TrackReference(olive::TrackReference::k_audio, 0).to_string(),
			  QStringLiteral("a:0"));
	EXPECT_EQ(
		olive::TrackReference(olive::TrackReference::k_subtitle, 7).to_string(),
		QStringLiteral("s:7"));

	// An invalid reference serializes to an empty string
	EXPECT_TRUE(olive::TrackReference().to_string().isEmpty());
}

TEST(TrackReferenceHandle, FromStringRejectsGarbage)
{
	// Bad prefixes and malformed input all parse back to the default ref
	const olive::TrackReference fallback;
	for (const QString &s : { QStringLiteral("x:1"), QStringLiteral("v"),
							  QStringLiteral("v:"), QStringLiteral("v:abc"),
							  QStringLiteral(""), QStringLiteral("a"),
							  QStringLiteral("vv:1") }) {
		EXPECT_EQ(olive::TrackReference::from_string(s), fallback)
			<< s.toStdString();
	}

	EXPECT_EQ(olive::TrackReference::type_from_string(QStringLiteral("v:1")),
			  olive::TrackReference::k_video);
	EXPECT_EQ(olive::TrackReference::type_from_string(QStringLiteral("a:2")),
			  olive::TrackReference::k_audio);
	EXPECT_EQ(olive::TrackReference::type_from_string(QStringLiteral("s:3")),
			  olive::TrackReference::k_subtitle);
	EXPECT_EQ(olive::TrackReference::type_from_string(QStringLiteral("q:3")),
			  olive::TrackReference::k_none);
}

TEST(TrackReferenceHandle, HashAndSetSemantics)
{
	QSet<olive::TrackReference> set;
	set.insert(olive::TrackReference(olive::TrackReference::k_video, 0));
	set.insert(olive::TrackReference(olive::TrackReference::k_video, 0));
	set.insert(olive::TrackReference(olive::TrackReference::k_audio, 0));

	EXPECT_EQ(set.size(), 2);
	EXPECT_TRUE(
		set.contains(olive::TrackReference(olive::TrackReference::k_video, 0)));
	EXPECT_TRUE(
		set.contains(olive::TrackReference(olive::TrackReference::k_audio, 0)));
	EXPECT_FALSE(
		set.contains(olive::TrackReference(olive::TrackReference::k_video, 1)));

	// Equal references hash equally
	EXPECT_EQ(qHash(olive::TrackReference(olive::TrackReference::k_subtitle, 2)),
			  qHash(olive::TrackReference(olive::TrackReference::k_subtitle, 2)));
}

TEST(TrackReferenceHandle, DataStreamRoundTrip)
{
	QByteArray bytes;
	{
		QBuffer buffer(&bytes);
		ASSERT_TRUE(buffer.open(QIODevice::WriteOnly));
		QDataStream out(&buffer);
		out << olive::TrackReference(olive::TrackReference::k_audio, 4)
			<< olive::TrackReference();
	}

	QBuffer buffer(&bytes);
	ASSERT_TRUE(buffer.open(QIODevice::ReadOnly));
	QDataStream in(&buffer);
	olive::TrackReference first, second;
	in >> first >> second;

	EXPECT_EQ(first, olive::TrackReference(olive::TrackReference::k_audio, 4));
	EXPECT_EQ(second, olive::TrackReference());
}

TEST(OakValueHelper, QVariantToOakNodeValueScalars)
{
	oak_node_value out;

	ASSERT_TRUE(olive::QVariantToOakNodeValue(olive::NodeValueType::k_int,
											  QVariant(42), &out));
	EXPECT_EQ(out.type, OAK_NODE_VALUE_INT);
	EXPECT_EQ(out.num, 42);

	// Combos map to the COMBO facade type, not INT
	ASSERT_TRUE(olive::QVariantToOakNodeValue(olive::NodeValueType::k_combo,
											  QVariant(3), &out));
	EXPECT_EQ(out.type, OAK_NODE_VALUE_COMBO);
	EXPECT_EQ(out.num, 3);

	ASSERT_TRUE(olive::QVariantToOakNodeValue(olive::NodeValueType::k_float,
											  QVariant(2.5), &out));
	EXPECT_EQ(out.type, OAK_NODE_VALUE_FLOAT);
	EXPECT_DOUBLE_EQ(out.f[0], 2.5);

	ASSERT_TRUE(olive::QVariantToOakNodeValue(olive::NodeValueType::k_boolean,
											  QVariant(true), &out));
	EXPECT_EQ(out.type, OAK_NODE_VALUE_BOOL);
	EXPECT_EQ(out.num, 1);
	ASSERT_TRUE(olive::QVariantToOakNodeValue(olive::NodeValueType::k_boolean,
											  QVariant(false), &out));
	EXPECT_EQ(out.num, 0);

	ASSERT_TRUE(olive::QVariantToOakNodeValue(
		olive::NodeValueType::k_rational,
		QVariant::fromValue(olive::core::Rational(1001, 30000)), &out));
	EXPECT_EQ(out.type, OAK_NODE_VALUE_RATIONAL);
	EXPECT_EQ(out.num, 1001);
	EXPECT_EQ(out.den, 30000);

	// Types with no POD representation fail
	EXPECT_FALSE(olive::QVariantToOakNodeValue(olive::NodeValueType::k_text,
											   QVariant(QStringLiteral("x")),
											   &out));
	EXPECT_FALSE(olive::QVariantToOakNodeValue(olive::NodeValueType::k_matrix,
											   QVariant(), &out));
}

TEST(OakValueHelper, QVariantToOakNodeValueColorAndVectors)
{
	oak_node_value out;

	ASSERT_TRUE(olive::QVariantToOakNodeValue(
		olive::NodeValueType::k_color,
		QVariant::fromValue(olive::core::Color(0.25f, 0.5f, 0.75f, 1.0f)),
		&out));
	EXPECT_EQ(out.type, OAK_NODE_VALUE_COLOR);
	EXPECT_DOUBLE_EQ(out.f[0], 0.25);
	EXPECT_DOUBLE_EQ(out.f[1], 0.5);
	EXPECT_DOUBLE_EQ(out.f[2], 0.75);
	EXPECT_DOUBLE_EQ(out.f[3], 1.0);

	ASSERT_TRUE(olive::QVariantToOakNodeValue(
		olive::NodeValueType::k_vec2,
		QVariant::fromValue(QVector2D(1.0f, 2.0f)), &out));
	EXPECT_EQ(out.type, OAK_NODE_VALUE_VEC2);
	EXPECT_DOUBLE_EQ(out.f[0], 1.0);
	EXPECT_DOUBLE_EQ(out.f[1], 2.0);

	ASSERT_TRUE(olive::QVariantToOakNodeValue(
		olive::NodeValueType::k_vec3,
		QVariant::fromValue(QVector3D(1.0f, 2.0f, 3.0f)), &out));
	EXPECT_EQ(out.type, OAK_NODE_VALUE_VEC3);
	EXPECT_DOUBLE_EQ(out.f[2], 3.0);

	ASSERT_TRUE(olive::QVariantToOakNodeValue(
		olive::NodeValueType::k_vec4,
		QVariant::fromValue(QVector4D(1.0f, 2.0f, 3.0f, 4.0f)), &out));
	EXPECT_EQ(out.type, OAK_NODE_VALUE_VEC4);
	EXPECT_DOUBLE_EQ(out.f[3], 4.0);
}

TEST(OakValueHelper, TrackComponentUsesDeclaredTypeWithSingleSlot)
{
	oak_node_value out;

	// Beziers take a FLOAT per track, not the full-bezier POD type
	ASSERT_TRUE(olive::NodeTrackComponentToOakNodeValue(
		olive::NodeValueType::k_bezier, QVariant(0.75), &out));
	EXPECT_EQ(out.type, OAK_NODE_VALUE_FLOAT);
	EXPECT_DOUBLE_EQ(out.f[0], 0.75);

	// A color channel is one float in f[0] but keeps the COLOR type
	ASSERT_TRUE(olive::NodeTrackComponentToOakNodeValue(
		olive::NodeValueType::k_color, QVariant(0.5), &out));
	EXPECT_EQ(out.type, OAK_NODE_VALUE_COLOR);
	EXPECT_DOUBLE_EQ(out.f[0], 0.5);
	EXPECT_DOUBLE_EQ(out.f[1], 0.0);

	ASSERT_TRUE(olive::NodeTrackComponentToOakNodeValue(
		olive::NodeValueType::k_vec4, QVariant(1.5), &out));
	EXPECT_EQ(out.type, OAK_NODE_VALUE_VEC4);
	EXPECT_DOUBLE_EQ(out.f[0], 1.5);

	ASSERT_TRUE(olive::NodeTrackComponentToOakNodeValue(
		olive::NodeValueType::k_combo, QVariant(2), &out));
	EXPECT_EQ(out.type, OAK_NODE_VALUE_COMBO);
	EXPECT_EQ(out.num, 2);

	ASSERT_TRUE(olive::NodeTrackComponentToOakNodeValue(
		olive::NodeValueType::k_rational,
		QVariant::fromValue(olive::core::Rational(1, 24)), &out));
	EXPECT_EQ(out.type, OAK_NODE_VALUE_RATIONAL);
	EXPECT_EQ(out.num, 1);
	EXPECT_EQ(out.den, 24);

	EXPECT_FALSE(olive::NodeTrackComponentToOakNodeValue(
		olive::NodeValueType::k_text, QVariant(QStringLiteral("x")), &out));
}

TEST(OakValueHelper, PodBackToQVariantRoundTrips)
{
	oak_node_value pod;

	ASSERT_TRUE(olive::QVariantToOakNodeValue(olive::NodeValueType::k_int,
											  QVariant(17), &pod));
	EXPECT_EQ(olive::OakNodeValueToQVariant(pod).toLongLong(), 17);

	ASSERT_TRUE(olive::QVariantToOakNodeValue(olive::NodeValueType::k_float,
											  QVariant(3.25), &pod));
	EXPECT_DOUBLE_EQ(olive::OakNodeValueToQVariant(pod).toDouble(), 3.25);

	ASSERT_TRUE(olive::QVariantToOakNodeValue(olive::NodeValueType::k_boolean,
											  QVariant(true), &pod));
	EXPECT_TRUE(olive::OakNodeValueToQVariant(pod).toBool());

	ASSERT_TRUE(olive::QVariantToOakNodeValue(
		olive::NodeValueType::k_rational,
		QVariant::fromValue(olive::core::Rational(24, 1)), &pod));
	EXPECT_EQ(olive::OakNodeValueToQVariant(pod).value<olive::core::Rational>(),
			  olive::core::Rational(24, 1));

	ASSERT_TRUE(olive::QVariantToOakNodeValue(
		olive::NodeValueType::k_color,
		QVariant::fromValue(olive::core::Color(0.1f, 0.2f, 0.3f, 0.4f)), &pod));
	const olive::core::Color c =
		olive::OakNodeValueToQVariant(pod).value<olive::core::Color>();
	EXPECT_FLOAT_EQ(c.red(), 0.1f);
	EXPECT_FLOAT_EQ(c.green(), 0.2f);
	EXPECT_FLOAT_EQ(c.blue(), 0.3f);
	EXPECT_FLOAT_EQ(c.alpha(), 0.4f);

	ASSERT_TRUE(olive::QVariantToOakNodeValue(
		olive::NodeValueType::k_vec3,
		QVariant::fromValue(QVector3D(4.0f, 5.0f, 6.0f)), &pod));
	EXPECT_EQ(olive::OakNodeValueToQVariant(pod).value<QVector3D>(),
			  QVector3D(4.0f, 5.0f, 6.0f));

	// Combos come back as plain ints
	ASSERT_TRUE(olive::QVariantToOakNodeValue(olive::NodeValueType::k_combo,
											  QVariant(5), &pod));
	EXPECT_EQ(olive::OakNodeValueToQVariant(pod).toInt(), 5);

	// Types with no POD representation produce an invalid QVariant
	pod.type = OAK_NODE_VALUE_STRING;
	EXPECT_FALSE(olive::OakNodeValueToQVariant(pod).isValid());
	pod.type = OAK_NODE_VALUE_BINARY;
	EXPECT_FALSE(olive::OakNodeValueToQVariant(pod).isValid());
	pod.type = OAK_NODE_VALUE_NONE;
	EXPECT_FALSE(olive::OakNodeValueToQVariant(pod).isValid());
}

TEST(OakValueHelper, KeyframeTypeToFacadeRenumbering)
{
	// Facade easing: 0=linear, 1=bezier, 2=hold — NOT the engine ordinals
	EXPECT_EQ(olive::NodeKeyframeTypeToFacade(olive::NodeKeyframeType::k_linear),
			  0);
	EXPECT_EQ(olive::NodeKeyframeTypeToFacade(olive::NodeKeyframeType::k_bezier),
			  1);
	EXPECT_EQ(olive::NodeKeyframeTypeToFacade(olive::NodeKeyframeType::k_hold), 2);

	// Anything else (including k_invalid) falls back to linear
	EXPECT_EQ(olive::NodeKeyframeTypeToFacade(olive::NodeKeyframeType::k_invalid),
			  0);
	EXPECT_EQ(olive::NodeKeyframeTypeToFacade(99), 0);
}

TEST(UndoWrapper, WrapsRedoUndoAndOwnership)
{
	int redo_count = 0;
	int undo_count = 0;
	bool destroyed = false;

	void *cmd = olive::wrap_app_undo_command(
		"GTest counting command",
		new CountingCmd{ &redo_count, &undo_count, &destroyed });
	ASSERT_NE(cmd, nullptr);
	EXPECT_FALSE(destroyed);

	// redo_now/undo_now forward to the wrapped object
	EXPECT_EQ(oakengine_undo_command_redo_now(cmd), 0);
	EXPECT_EQ(redo_count, 1);
	EXPECT_EQ(undo_count, 0);

	EXPECT_EQ(oakengine_undo_command_undo_now(cmd), 0);
	EXPECT_EQ(undo_count, 1);

	EXPECT_EQ(oakengine_undo_command_redo_now(cmd), 0);
	EXPECT_EQ(redo_count, 2);

	// Freeing the engine command deletes the wrapped object exactly once
	oakengine_undo_command_free(cmd);
	EXPECT_TRUE(destroyed);
}

TEST(ConfigWrapper, IntRoundTripAndComparisons)
{
	const ScopedConfigKey guard(QStringLiteral("GTestConfigWrapperInt"));

	olive::OakConfigValue value(QStringLiteral("GTestConfigWrapperInt"));

	value = 42;
	EXPECT_EQ(value.toInt(), 42);
	EXPECT_EQ(static_cast<qint64>(value), 42);
	EXPECT_TRUE(value == 42);
	EXPECT_FALSE(value != 42);
	EXPECT_TRUE(value == qint64(42));
	EXPECT_TRUE(value.toBool()); // non-zero reads as true

	value = 0;
	EXPECT_EQ(value.toInt(), 0);
	EXPECT_FALSE(value.toBool());

	// Large values survive the int64 round trip
	value = qint64(5000000000LL);
	EXPECT_EQ(value.toLongLong(), 5000000000LL);
}

TEST(ConfigWrapper, StringRoundTripAndComparisons)
{
	const ScopedConfigKey guard(QStringLiteral("GTestConfigWrapperString"));

	olive::OakConfigValue value(QStringLiteral("GTestConfigWrapperString"));

	value = QStringLiteral("hello world");
	EXPECT_EQ(value.toString(), QStringLiteral("hello world"));
	EXPECT_TRUE(value == QStringLiteral("hello world"));
	EXPECT_TRUE(value == "hello world");
	EXPECT_FALSE(value != "hello world");
	EXPECT_TRUE(value != "goodbye");

	// const char* assignment, including UTF-8 payloads
	value = "caf\u00E9";
	EXPECT_EQ(value.toString(), QStringLiteral("caf\u00E9"));

	// The QVariant conversion yields a string variant
	const QVariant as_variant = static_cast<QVariant>(value);
	EXPECT_EQ(as_variant.typeId(), QMetaType::QString);
	EXPECT_EQ(as_variant.toString(), QStringLiteral("caf\u00E9"));
}

TEST(ConfigWrapper, QVariantAssignmentDispatchesByType)
{
	const ScopedConfigKey guard(QStringLiteral("GTestConfigWrapperVariant"));

	olive::OakConfigValue value(QStringLiteral("GTestConfigWrapperVariant"));

	value = QVariant(true);
	EXPECT_TRUE(value.toBool());

	value = QVariant(7);
	EXPECT_EQ(value.toInt(), 7);

	// Floating point variants are truncated to int64 by design
	value = QVariant(3.9);
	EXPECT_EQ(value.toLongLong(), 3);

	// Everything else falls back to its string form
	value = QVariant(QStringLiteral("plain"));
	EXPECT_EQ(value.toString(), QStringLiteral("plain"));
}

TEST(ConfigWrapper, RationalValueParsesStoredString)
{
	const ScopedConfigKey guard(QStringLiteral("GTestConfigWrapperRational"));

	olive::OakConfigValue value(QStringLiteral("GTestConfigWrapperRational"));

	value = QStringLiteral("1001/30000");
	const olive::core::Rational r = value.value<olive::core::Rational>();
	EXPECT_EQ(r, olive::core::Rational(1001, 30000));
	EXPECT_EQ(r.numerator(), 1001);
	EXPECT_EQ(r.denominator(), 30000);
}

TEST(ConfigWrapper, MissingKeyReadsDefaults)
{
	// No guard: this key must never exist, so there is nothing to restore
	olive::OakConfigValue value(
		QStringLiteral("GTestConfigWrapperDefinitelyMissing"));

	EXPECT_FALSE(value.toBool());
	EXPECT_EQ(value.toInt(), 0);
	EXPECT_EQ(value.toLongLong(), 0);
	EXPECT_TRUE(value.toString().isEmpty());
}

TEST(ConfigWrapper, OakConfigMacrosBuildValues)
{
	const ScopedConfigKey guard(QStringLiteral("GTestConfigWrapperMacro"));

	OAK_CONFIG("GTestConfigWrapperMacro") = 9;
	EXPECT_EQ(OAK_CONFIG("GTestConfigWrapperMacro").toInt(), 9);

	OAK_CONFIG_STR(QStringLiteral("GTestConfigWrapperMacro")) =
		QStringLiteral("macro");
	EXPECT_EQ(OAK_CONFIG_STR(QStringLiteral("GTestConfigWrapperMacro")).toString(),
			  QStringLiteral("macro"));
}

TEST(SubtitleApp, DefaultAndParameterizedConstruction)
{
	const olive::SubtitleApp empty;
	EXPECT_TRUE(empty.text().isEmpty());

	const olive::SubtitleApp sub(
		olive::core::TimeRange(olive::core::Rational(1, 24),
							   olive::core::Rational(48, 24)),
		QStringLiteral("Hello"));
	EXPECT_EQ(sub.text(), QStringLiteral("Hello"));
	EXPECT_EQ(sub.time().in(), olive::core::Rational(1, 24));
	EXPECT_EQ(sub.time().out(), olive::core::Rational(48, 24));
	EXPECT_EQ(sub.time().length(), olive::core::Rational(47, 24));
}

TEST(SubtitleApp, SettersRoundTrip)
{
	olive::SubtitleApp sub;

	sub.set_text(QStringLiteral("First"));
	EXPECT_EQ(sub.text(), QStringLiteral("First"));

	sub.set_time(olive::core::TimeRange(olive::core::Rational(0, 1),
										olive::core::Rational(10, 1)));
	EXPECT_EQ(sub.time().in(), olive::core::Rational(0, 1));
	EXPECT_EQ(sub.time().out(), olive::core::Rational(10, 1));

	sub.set_text(QStringLiteral("Second"));
	EXPECT_EQ(sub.text(), QStringLiteral("Second"));
}

TEST(SubtitleApp, MetatypeRoundTrip)
{
	const olive::SubtitleApp sub(
		olive::core::TimeRange(olive::core::Rational(2, 1),
							   olive::core::Rational(5, 1)),
		QStringLiteral("Packed"));

	const QVariant v = QVariant::fromValue(sub);
	EXPECT_EQ(v.typeId(), qMetaTypeId<olive::SubtitleApp>());

	const olive::SubtitleApp out = v.value<olive::SubtitleApp>();
	EXPECT_EQ(out.text(), QStringLiteral("Packed"));
	EXPECT_EQ(out.time(), sub.time());
}

TEST(DebugApp, HandlerFormatsMessageTypeAndContext)
{
	testing::internal::CaptureStderr();
	olive::debug_handler(QtDebugMsg, QMessageLogContext("file.cpp", 12, "func",
													  "category"),
						 QStringLiteral("hello"));
	const std::string out = testing::internal::GetCapturedStderr();

	EXPECT_NE(out.find("Debug: hello"), std::string::npos);
	EXPECT_NE(out.find("file.cpp"), std::string::npos);
	EXPECT_NE(out.find("func"), std::string::npos);

	// A null context file/function is reported as <null>
	testing::internal::CaptureStderr();
	olive::debug_handler(QtInfoMsg, QMessageLogContext(), QStringLiteral("plain"));
	const std::string out2 = testing::internal::GetCapturedStderr();
	EXPECT_NE(out2.find("Info: plain"), std::string::npos);
	EXPECT_NE(out2.find("<null>"), std::string::npos);
}

TEST(DebugApp, HandlerSuppressesFilteredWarnings)
{
	// QXcbIntegration noise is always dropped
	testing::internal::CaptureStderr();
	olive::debug_handler(QtWarningMsg, QMessageLogContext(),
						 QStringLiteral("QXcbIntegration: something"));
	EXPECT_TRUE(testing::internal::GetCapturedStderr().empty());

	// Other warnings depend on the OAK_TESTING environment variable, which the
	// handler samples once; mirror that expectation here
	testing::internal::CaptureStderr();
	olive::debug_handler(QtWarningMsg, QMessageLogContext(),
						 QStringLiteral("ordinary warning"));
	const std::string out = testing::internal::GetCapturedStderr();
	if (qEnvironmentVariableIsSet("OAK_TESTING")) {
		EXPECT_TRUE(out.empty());
	} else {
		EXPECT_NE(out.find("Warning: ordinary warning"), std::string::npos);
	}
}
