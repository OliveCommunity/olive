#include <gtest/gtest.h>

#include <memory>

#include <QMatrix4x4>
#include <QSignalSpy>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

#include "node/keyframe.h"
#include "node/math/math/math.h"
#include "node/color/colormanager/colormanager.h"
#include "node/project.h"
#include "node/value.h"
#include "olive/core/render/audioparams.h"
#include "olive/core/render/samplebuffer.h"
#include "olive/core/render/sampleformat.h"

TEST(NodeValueExtended, VectorAccessors)
{
	olive::NodeValue v2(olive::NodeValue::k_vec2, QVector2D(1.5f, -2.5f));
	EXPECT_FLOAT_EQ(v2.to_vec2().x(), 1.5f);
	EXPECT_FLOAT_EQ(v2.to_vec2().y(), -2.5f);

	olive::NodeValue v3(olive::NodeValue::k_vec3, QVector3D(1.0f, 2.0f, 3.0f));
	EXPECT_FLOAT_EQ(v3.to_vec3().x(), 1.0f);
	EXPECT_FLOAT_EQ(v3.to_vec3().y(), 2.0f);
	EXPECT_FLOAT_EQ(v3.to_vec3().z(), 3.0f);

	olive::NodeValue v4(olive::NodeValue::k_vec4,
						QVector4D(1.0f, 2.0f, 3.0f, 4.0f));
	EXPECT_FLOAT_EQ(v4.to_vec4().x(), 1.0f);
	EXPECT_FLOAT_EQ(v4.to_vec4().y(), 2.0f);
	EXPECT_FLOAT_EQ(v4.to_vec4().z(), 3.0f);
	EXPECT_FLOAT_EQ(v4.to_vec4().w(), 4.0f);
}

TEST(NodeValueExtended, ColorMatrixBezierAccessors)
{
	olive::NodeValue color(olive::NodeValue::k_color,
						   olive::core::Color(0.1f, 0.2f, 0.3f, 0.4f));
	EXPECT_FLOAT_EQ(color.to_color().red(), 0.1f);
	EXPECT_FLOAT_EQ(color.to_color().green(), 0.2f);
	EXPECT_FLOAT_EQ(color.to_color().blue(), 0.3f);
	EXPECT_FLOAT_EQ(color.to_color().alpha(), 0.4f);

	QMatrix4x4 matrix;
	matrix.scale(2.0f, 3.0f, 4.0f);
	olive::NodeValue mat(olive::NodeValue::k_matrix, matrix);
	EXPECT_FLOAT_EQ(mat.to_matrix()(0, 0), 2.0f);
	EXPECT_FLOAT_EQ(mat.to_matrix()(1, 1), 3.0f);
	EXPECT_FLOAT_EQ(mat.to_matrix()(2, 2), 4.0f);

	olive::NodeValue bezier(olive::NodeValue::k_bezier,
							olive::core::Bezier(1.0, 2.0, 3.0, 4.0, 5.0, 6.0));
	EXPECT_DOUBLE_EQ(bezier.to_bezier().x(), 1.0);
	EXPECT_DOUBLE_EQ(bezier.to_bezier().y(), 2.0);
	EXPECT_DOUBLE_EQ(bezier.to_bezier().cp1_x(), 3.0);
	EXPECT_DOUBLE_EQ(bezier.to_bezier().cp1_y(), 4.0);
	EXPECT_DOUBLE_EQ(bezier.to_bezier().cp2_x(), 5.0);
	EXPECT_DOUBLE_EQ(bezier.to_bezier().cp2_y(), 6.0);
}

TEST(NodeValueExtended, ScalarAccessors)
{
	olive::NodeValue bool_val(olive::NodeValue::k_boolean, true);
	EXPECT_TRUE(bool_val.to_bool());

	olive::NodeValue floating(olive::NodeValue::k_float, 2.75);
	EXPECT_DOUBLE_EQ(floating.to_double(), 2.75);

	olive::NodeValue text(olive::NodeValue::k_text, QStringLiteral("oak"));
	EXPECT_EQ(text.to_string(), QStringLiteral("oak"));

	olive::NodeValue rational_value(olive::NodeValue::k_rational,
									olive::core::Rational(3, 4));
	EXPECT_EQ(rational_value.to_rational(), olive::core::Rational(3, 4));

	olive::NodeValue audio(
		olive::NodeValue::k_audio_params,
		olive::core::AudioParams(48000, olive::core::k_channel_layout_stereo,
								 olive::core::SampleFormat::f32_p));
	EXPECT_EQ(audio.to_audio_params().sample_rate(), 48000);
}

TEST(NodeValueExtended, SamplesAccessorRoundTripsBuffer)
{
	olive::core::AudioParams params(48000, olive::core::k_channel_layout_mono,
									olive::core::SampleFormat::f32_p);
	olive::core::SampleBuffer buffer(params, size_t(4));
	for (int i = 0; i < 4; i++) {
		buffer.data(0)[i] = 0.25f * float(i + 1);
	}

	olive::NodeValue value(olive::NodeValue::k_samples, buffer);
	olive::core::SampleBuffer out = value.to_samples();
	ASSERT_EQ(out.sample_count(), size_t(4));
	EXPECT_FLOAT_EQ(out.data(0)[0], 0.25f);
	EXPECT_FLOAT_EQ(out.data(0)[1], 0.5f);
	EXPECT_FLOAT_EQ(out.data(0)[2], 0.75f);
	EXPECT_FLOAT_EQ(out.data(0)[3], 1.0f);
}

TEST(NodeValueExtended, MismatchedTypeAccessorsReturnDefaults)
{
	olive::NodeValue text(olive::NodeValue::k_text, QStringLiteral("hello"));

	// Accessors do not validate the stored type; failed QVariant conversions
	// produce default-constructed values
	EXPECT_EQ(text.to_texture(), nullptr);
	EXPECT_FALSE(text.to_samples().is_allocated());
	EXPECT_TRUE(text.to_vec4().isNull());
	EXPECT_TRUE(text.to_matrix().isIdentity());

	const olive::core::Color c = text.to_color();
	EXPECT_FLOAT_EQ(c.red(), 0.0f);
	EXPECT_FLOAT_EQ(c.green(), 0.0f);
	EXPECT_FLOAT_EQ(c.blue(), 0.0f);
	EXPECT_FLOAT_EQ(c.alpha(), 0.0f);
}

TEST(NodeValueExtended, SourceArrayFlagAndEquality)
{
	olive::MathNode node; // any Node works; only the pointer value is observed
	olive::NodeValue value(olive::NodeValue::k_float, 1.5, &node, true,
						   QStringLiteral("tag"));
	EXPECT_EQ(value.source(), static_cast<const olive::Node *>(&node));
	EXPECT_TRUE(value.array());
	EXPECT_EQ(value.tag(), QStringLiteral("tag"));
	EXPECT_EQ(value.type(), olive::NodeValue::k_float);
	EXPECT_TRUE(value);

	// A default-constructed value carries no data
	olive::NodeValue empty;
	EXPECT_EQ(empty.type(), olive::NodeValue::k_none);
	EXPECT_EQ(empty.source(), nullptr);
	EXPECT_FALSE(empty.array());
	EXPECT_TRUE(empty.data().isNull());
	EXPECT_FALSE(empty);

	// Equality compares type, tag, and data; the source pointer and the array
	// flag are ignored
	olive::NodeValue same(olive::NodeValue::k_float, 1.5, nullptr, false,
						  QStringLiteral("tag"));
	EXPECT_TRUE(value == same);

	olive::NodeValue different_tag(olive::NodeValue::k_float, 1.5, &node, true,
								   QStringLiteral("other"));
	EXPECT_FALSE(value == different_tag);

	olive::NodeValue different_data(olive::NodeValue::k_float, 2.5, &node, true,
									QStringLiteral("tag"));
	EXPECT_FALSE(value == different_data);

	olive::NodeValue different_type(olive::NodeValue::k_int, int64_t(1), &node,
									true, QStringLiteral("tag"));
	EXPECT_FALSE(value == different_type);
}

TEST(NodeValueExtended, CanConvertReflectsStoredData)
{
	olive::NodeValue integer(olive::NodeValue::k_int, int64_t(7));
	EXPECT_TRUE(integer.canConvert<int64_t>());

	olive::NodeValue text(olive::NodeValue::k_text, QStringLiteral("hello"));
	EXPECT_TRUE(text.canConvert<QString>());

	olive::NodeValue vec(olive::NodeValue::k_vec2, QVector2D(1.0f, 2.0f));
	EXPECT_TRUE(vec.canConvert<QVector2D>());
	EXPECT_FALSE(vec.canConvert<olive::core::Color>());
}

TEST(NodeValueExtended, ColorStringRoundTrip)
{
	const olive::core::Color c(0.25f, 0.5f, 0.75f, 1.0f);
	QString encoded = olive::NodeValue::value_to_string(
		olive::NodeValue::k_color, QVariant::fromValue(c), false);
	QVariant decoded = olive::NodeValue::string_to_value(
		olive::NodeValue::k_color, encoded, false);
	const olive::core::Color out = decoded.value<olive::core::Color>();
	EXPECT_FLOAT_EQ(out.red(), c.red());
	EXPECT_FLOAT_EQ(out.green(), c.green());
	EXPECT_FLOAT_EQ(out.blue(), c.blue());
	EXPECT_FLOAT_EQ(out.alpha(), c.alpha());
}

TEST(NodeValueExtended, BezierStringRoundTrip)
{
	const olive::core::Bezier b(1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
	QString encoded = olive::NodeValue::value_to_string(
		olive::NodeValue::k_bezier, QVariant::fromValue(b), false);
	QVariant decoded = olive::NodeValue::string_to_value(
		olive::NodeValue::k_bezier, encoded, false);
	const olive::core::Bezier out = decoded.value<olive::core::Bezier>();
	EXPECT_DOUBLE_EQ(out.x(), b.x());
	EXPECT_DOUBLE_EQ(out.y(), b.y());
	EXPECT_DOUBLE_EQ(out.cp1_x(), b.cp1_x());
	EXPECT_DOUBLE_EQ(out.cp1_y(), b.cp1_y());
	EXPECT_DOUBLE_EQ(out.cp2_x(), b.cp2_x());
	EXPECT_DOUBLE_EQ(out.cp2_y(), b.cp2_y());
}

TEST(NodeValueExtended, RationalStringRoundTrip)
{
	const olive::core::Rational r(1, 24);
	QString encoded = olive::NodeValue::value_to_string(
		olive::NodeValue::k_rational, QVariant::fromValue(r), false);
	EXPECT_EQ(encoded, QStringLiteral("1/24"));
	QVariant decoded = olive::NodeValue::string_to_value(
		olive::NodeValue::k_rational, encoded, false);
	EXPECT_EQ(decoded.value<olive::core::Rational>(), r);

	// The Rational path applies to key track values too
	EXPECT_EQ(olive::NodeValue::value_to_string(olive::NodeValue::k_rational,
											  QVariant::fromValue(r), true),
			  QStringLiteral("1/24"));
}

TEST(NodeValueExtended, IntStringRoundTrip)
{
	const int64_t big = INT64_C(9223372036854775807);
	QString encoded = olive::NodeValue::value_to_string(
		olive::NodeValue::k_int, QVariant::fromValue(big), false);
	EXPECT_EQ(encoded, QStringLiteral("9223372036854775807"));
	QVariant decoded = olive::NodeValue::string_to_value(
		olive::NodeValue::k_int, encoded, false);
	EXPECT_EQ(decoded.value<int64_t>(), big);

	const int64_t small = -big - 1;
	encoded = olive::NodeValue::value_to_string(
		olive::NodeValue::k_int, QVariant::fromValue(small), false);
	decoded = olive::NodeValue::string_to_value(olive::NodeValue::k_int, encoded,
											  false);
	EXPECT_EQ(decoded.value<int64_t>(), small);
}

TEST(NodeValueExtended, BufferAndNoneTypesSerializeToEmptyString)
{
	// Textures, samples, and empty values have no XML representation
	EXPECT_TRUE(olive::NodeValue::value_to_string(
					olive::NodeValue::k_texture,
					QVariant::fromValue(olive::TexturePtr()), false)
					.isEmpty());
	EXPECT_TRUE(olive::NodeValue::value_to_string(
					olive::NodeValue::k_samples,
					QVariant::fromValue(olive::core::SampleBuffer()), false)
					.isEmpty());
	EXPECT_TRUE(olive::NodeValue::value_to_string(olive::NodeValue::k_none,
												QVariant(), false)
					.isEmpty());
}

TEST(NodeValueExtended, KeyTrackFlagFallsBackToPlainString)
{
	// With the key-track flag set, values without a dedicated serialization
	// fall back to plain string conversion
	EXPECT_EQ(olive::NodeValue::value_to_string(olive::NodeValue::k_text,
											  QStringLiteral("hello"), true),
			  QStringLiteral("hello"));
	EXPECT_EQ(olive::NodeValue::value_to_string(olive::NodeValue::k_float, 2.5,
											  true),
			  QStringLiteral("2.5"));

	// StringToValue() likewise leaves key-track values as raw strings
	QVariant decoded = olive::NodeValue::string_to_value(
		olive::NodeValue::k_float, QStringLiteral("2.5"), true);
	EXPECT_EQ(decoded.toString(), QStringLiteral("2.5"));
}

TEST(NodeValueExtended, ShortVectorStringIsZeroPadded)
{
	QVariant decoded = olive::NodeValue::string_to_value(
		olive::NodeValue::k_vec3, QStringLiteral("5:7"), false);
	const QVector3D vec = decoded.value<QVector3D>();
	EXPECT_FLOAT_EQ(vec.x(), 5.0f);
	EXPECT_FLOAT_EQ(vec.y(), 7.0f);
	EXPECT_FLOAT_EQ(vec.z(), 0.0f);

	// Even an empty string yields a zero vector rather than crashing
	decoded = olive::NodeValue::string_to_value(olive::NodeValue::k_vec2,
											  QString(), false);
	const QVector2D vec2 = decoded.value<QVector2D>();
	EXPECT_FLOAT_EQ(vec2.x(), 0.0f);
	EXPECT_FLOAT_EQ(vec2.y(), 0.0f);
}

TEST(NodeValueExtended, KeyframeTrackCounts)
{
	EXPECT_EQ(olive::NodeValue::get_number_of_keyframe_tracks(
				  olive::NodeValue::k_vec2),
			  2);
	EXPECT_EQ(olive::NodeValue::get_number_of_keyframe_tracks(
				  olive::NodeValue::k_vec3),
			  3);
	EXPECT_EQ(olive::NodeValue::get_number_of_keyframe_tracks(
				  olive::NodeValue::k_vec4),
			  4);
	EXPECT_EQ(olive::NodeValue::get_number_of_keyframe_tracks(
				  olive::NodeValue::k_color),
			  4);
	EXPECT_EQ(olive::NodeValue::get_number_of_keyframe_tracks(
				  olive::NodeValue::k_bezier),
			  6);

	// All scalar types live on a single track
	EXPECT_EQ(olive::NodeValue::get_number_of_keyframe_tracks(
				  olive::NodeValue::k_float),
			  1);
	EXPECT_EQ(olive::NodeValue::get_number_of_keyframe_tracks(
				  olive::NodeValue::k_int),
			  1);
	EXPECT_EQ(olive::NodeValue::get_number_of_keyframe_tracks(
				  olive::NodeValue::k_text),
			  1);
	EXPECT_EQ(olive::NodeValue::get_number_of_keyframe_tracks(
				  olive::NodeValue::k_rational),
			  1);
	EXPECT_EQ(olive::NodeValue::get_number_of_keyframe_tracks(
				  olive::NodeValue::k_none),
			  1);
}

TEST(NodeValueExtended, SplitVectorIntoTrackValues)
{
	olive::NodeValue value(olive::NodeValue::k_vec3,
						   QVector3D(1.0f, 2.0f, 3.0f));
	const olive::SplitValue split = value.to_split_value();
	ASSERT_EQ(split.size(), 3);
	EXPECT_FLOAT_EQ(split.at(0).toFloat(), 1.0f);
	EXPECT_FLOAT_EQ(split.at(1).toFloat(), 2.0f);
	EXPECT_FLOAT_EQ(split.at(2).toFloat(), 3.0f);

	// to_split_value() matches the underlying static helper
	const QVector<QVariant> manual =
		olive::NodeValue::split_normal_value_into_track_values(
			olive::NodeValue::k_vec3,
			QVariant::fromValue(QVector3D(1.0f, 2.0f, 3.0f)));
	ASSERT_EQ(manual.size(), 3);
	EXPECT_FLOAT_EQ(manual.at(2).toFloat(), 3.0f);
}

TEST(NodeValueExtended, SplitColorAndBezierIntoTrackValues)
{
	olive::NodeValue color(olive::NodeValue::k_color,
						   olive::core::Color(0.1f, 0.2f, 0.3f, 0.4f));
	olive::SplitValue split = color.to_split_value();
	ASSERT_EQ(split.size(), 4);
	EXPECT_FLOAT_EQ(split.at(0).toFloat(), 0.1f);
	EXPECT_FLOAT_EQ(split.at(1).toFloat(), 0.2f);
	EXPECT_FLOAT_EQ(split.at(2).toFloat(), 0.3f);
	EXPECT_FLOAT_EQ(split.at(3).toFloat(), 0.4f);

	olive::NodeValue bezier(olive::NodeValue::k_bezier,
							olive::core::Bezier(1.0, 2.0, 3.0, 4.0, 5.0, 6.0));
	split = bezier.to_split_value();
	ASSERT_EQ(split.size(), 6);
	EXPECT_DOUBLE_EQ(split.at(0).toDouble(), 1.0);
	EXPECT_DOUBLE_EQ(split.at(1).toDouble(), 2.0);
	EXPECT_DOUBLE_EQ(split.at(2).toDouble(), 3.0);
	EXPECT_DOUBLE_EQ(split.at(3).toDouble(), 4.0);
	EXPECT_DOUBLE_EQ(split.at(4).toDouble(), 5.0);
	EXPECT_DOUBLE_EQ(split.at(5).toDouble(), 6.0);
}

TEST(NodeValueExtended, SplitScalarStaysSingleValue)
{
	olive::NodeValue value(olive::NodeValue::k_float, 4.75);
	const olive::SplitValue split = value.to_split_value();
	ASSERT_EQ(split.size(), 1);
	EXPECT_DOUBLE_EQ(split.at(0).toDouble(), 4.75);
}

TEST(NodeValueExtended, CombineTrackValuesRebuildsValue)
{
	// Round trip through split/combine restores the original value
	const olive::core::Color c(0.25f, 0.5f, 0.75f, 1.0f);
	olive::SplitValue split =
		olive::NodeValue(olive::NodeValue::k_color, c).to_split_value();
	QVariant combined = olive::NodeValue::combine_track_values_into_normal_value(
		olive::NodeValue::k_color, split);
	const olive::core::Color color_out = combined.value<olive::core::Color>();
	EXPECT_FLOAT_EQ(color_out.red(), c.red());
	EXPECT_FLOAT_EQ(color_out.green(), c.green());
	EXPECT_FLOAT_EQ(color_out.blue(), c.blue());
	EXPECT_FLOAT_EQ(color_out.alpha(), c.alpha());

	const QVector2D vec(1.5f, -2.5f);
	split = olive::NodeValue(olive::NodeValue::k_vec2, vec).to_split_value();
	combined = olive::NodeValue::combine_track_values_into_normal_value(
		olive::NodeValue::k_vec2, split);
	const QVector2D vec_out = combined.value<QVector2D>();
	EXPECT_FLOAT_EQ(vec_out.x(), vec.x());
	EXPECT_FLOAT_EQ(vec_out.y(), vec.y());

	// Scalar types return the first (only) track value
	QVariant scalar = olive::NodeValue::combine_track_values_into_normal_value(
		olive::NodeValue::k_float, { 4.5 });
	EXPECT_DOUBLE_EQ(scalar.toDouble(), 4.5);

	// An empty split combines to a null variant
	EXPECT_TRUE(olive::NodeValue::combine_track_values_into_normal_value(
					olive::NodeValue::k_vec2, {})
					.isNull());
}

TEST(NodeValueExtended, PrettyDataTypeNames)
{
	for (int i = olive::NodeValue::k_none; i < olive::NodeValue::k_data_type_count;
		 i++) {
		const auto type = static_cast<olive::NodeValue::Type>(i);
		EXPECT_FALSE(olive::NodeValue::get_pretty_data_type_name(type).isEmpty())
			<< "type " << i;
	}
	EXPECT_EQ(olive::NodeValue::get_pretty_data_type_name(
				  olive::NodeValue::k_data_type_count),
			  QStringLiteral("Unknown"));

	// kStrCombo and kPushButton have dedicated pretty names like every other
	// type
	EXPECT_EQ(
		olive::NodeValue::get_pretty_data_type_name(olive::NodeValue::k_str_combo),
		QStringLiteral("String Combo"));
	EXPECT_EQ(
		olive::NodeValue::get_pretty_data_type_name(olive::NodeValue::k_push_button),
		QStringLiteral("Push Button"));
}

TEST(NodeValueExtended, TypeClassificationRemainingCases)
{
	EXPECT_TRUE(
		olive::NodeValue::type_can_be_interpolated(olive::NodeValue::k_vec2));
	EXPECT_TRUE(
		olive::NodeValue::type_can_be_interpolated(olive::NodeValue::k_vec3));
	EXPECT_TRUE(
		olive::NodeValue::type_can_be_interpolated(olive::NodeValue::k_vec4));
	EXPECT_TRUE(
		olive::NodeValue::type_can_be_interpolated(olive::NodeValue::k_bezier));
	EXPECT_TRUE(
		olive::NodeValue::type_can_be_interpolated(olive::NodeValue::k_rational));
	EXPECT_FALSE(
		olive::NodeValue::type_can_be_interpolated(olive::NodeValue::k_none));
	EXPECT_FALSE(
		olive::NodeValue::type_can_be_interpolated(olive::NodeValue::k_text));
	EXPECT_FALSE(
		olive::NodeValue::type_can_be_interpolated(olive::NodeValue::k_texture));
	EXPECT_FALSE(
		olive::NodeValue::type_can_be_interpolated(olive::NodeValue::k_samples));
	EXPECT_FALSE(
		olive::NodeValue::type_can_be_interpolated(olive::NodeValue::k_boolean));

	EXPECT_TRUE(olive::NodeValue::type_is_numeric(olive::NodeValue::k_rational));
	EXPECT_FALSE(olive::NodeValue::type_is_numeric(olive::NodeValue::k_vec2));
	EXPECT_FALSE(olive::NodeValue::type_is_numeric(olive::NodeValue::k_color));
	EXPECT_FALSE(olive::NodeValue::type_is_numeric(olive::NodeValue::k_boolean));
	EXPECT_FALSE(olive::NodeValue::type_is_numeric(olive::NodeValue::k_none));

	EXPECT_TRUE(olive::NodeValue::type_is_vector(olive::NodeValue::k_vec4));
	EXPECT_FALSE(olive::NodeValue::type_is_vector(olive::NodeValue::k_color));
	EXPECT_FALSE(olive::NodeValue::type_is_vector(olive::NodeValue::k_text));

	EXPECT_FALSE(olive::NodeValue::type_is_buffer(olive::NodeValue::k_none));
	EXPECT_FALSE(olive::NodeValue::type_is_buffer(olive::NodeValue::k_float));
}

TEST(NodeValueExtended, AllRealTypesHaveDataTypeNames)
{
	EXPECT_EQ(olive::NodeValue::get_data_type_name(olive::NodeValue::k_str_combo),
			  QStringLiteral("strcombo"));
	EXPECT_EQ(olive::NodeValue::get_data_type_name(olive::NodeValue::k_push_button),
			  QStringLiteral("pushbutton"));
	EXPECT_TRUE(
		olive::NodeValue::get_data_type_name(olive::NodeValue::k_data_type_count)
			.isEmpty());

	EXPECT_EQ(olive::NodeValue::get_data_type_from_name(
				  QStringLiteral("not-a-type")),
			  olive::NodeValue::k_none);

	// An empty name matches no type
	EXPECT_EQ(olive::NodeValue::get_data_type_from_name(QString()),
			  olive::NodeValue::k_none);

	// The newly named types round-trip
	EXPECT_EQ(
		olive::NodeValue::get_data_type_from_name(QStringLiteral("strcombo")),
		olive::NodeValue::k_str_combo);
	EXPECT_EQ(
		olive::NodeValue::get_data_type_from_name(QStringLiteral("pushbutton")),
		olive::NodeValue::k_push_button);
}

TEST(NodeValueExtended, ArrayValuesRoundTrip)
{
	olive::NodeValueArray array;
	array[0] = olive::NodeValue(olive::NodeValue::k_int, int64_t(4));
	array[5] = olive::NodeValue(olive::NodeValue::k_text,
								QStringLiteral("five"));

	olive::NodeValue value(olive::NodeValue::k_int, array, nullptr, true);
	EXPECT_TRUE(value.array());

	const olive::NodeValueArray round_trip = value.to_array();
	ASSERT_EQ(round_trip.size(), size_t(2));
	EXPECT_EQ(round_trip.at(0).to_int(), 4);
	EXPECT_EQ(round_trip.at(5).to_string(), QStringLiteral("five"));
}

TEST(NodeValueTableExtended, GetReturnsNewestMatchingValue)
{
	olive::NodeValueTable table;
	table.push(olive::NodeValue(olive::NodeValue::k_float, 1.0));
	table.push(olive::NodeValue(olive::NodeValue::k_float, 2.0));

	// Get() scans from the back, so the newest value wins and nothing is
	// removed
	EXPECT_DOUBLE_EQ(table.get(olive::NodeValue::k_float).to_double(), 2.0);
	EXPECT_EQ(table.count(), 2);
}

TEST(NodeValueTableExtended, GetWithTagSelectsMatchingValue)
{
	olive::NodeValueTable table;
	table.push(olive::NodeValue(olive::NodeValue::k_float, 1.0, nullptr,
								QStringLiteral("a")));
	table.push(olive::NodeValue(olive::NodeValue::k_float, 2.0, nullptr,
								QStringLiteral("b")));
	table.push(olive::NodeValue(olive::NodeValue::k_float, 3.0));

	EXPECT_DOUBLE_EQ(
		table.get(olive::NodeValue::k_float, QStringLiteral("a")).to_double(),
		1.0);
	EXPECT_DOUBLE_EQ(
		table.get(olive::NodeValue::k_float, QStringLiteral("b")).to_double(),
		2.0);

	// Without a tag the newest value wins
	EXPECT_DOUBLE_EQ(table.get(olive::NodeValue::k_float).to_double(), 3.0);
	EXPECT_EQ(table.get_value_index({ olive::NodeValue::k_float },
								  QStringLiteral("b")),
			  1);

	// An unknown tag yields an empty value; the fallback to the oldest value
	// of the type only applies when no tag is requested
	olive::NodeValue missing =
		table.get(olive::NodeValue::k_float, QStringLiteral("missing"));
	EXPECT_EQ(missing.type(), olive::NodeValue::k_none);
	EXPECT_EQ(table.get_value_index({ olive::NodeValue::k_float },
								  QStringLiteral("missing")),
			  -1);
}

TEST(NodeValueTableExtended, GetWithMultipleTypes)
{
	olive::NodeValueTable table;
	table.push(
		olive::NodeValue(olive::NodeValue::k_text, QStringLiteral("s")));
	table.push(olive::NodeValue(olive::NodeValue::k_float, 2.5));

	olive::NodeValue newest =
		table.get({ olive::NodeValue::k_vec2, olive::NodeValue::k_float });
	EXPECT_DOUBLE_EQ(newest.to_double(), 2.5);

	olive::NodeValue text =
		table.get({ olive::NodeValue::k_vec2, olive::NodeValue::k_text });
	EXPECT_EQ(text.to_string(), QStringLiteral("s"));

	// A type that was never pushed produces an empty (kNone) value
	olive::NodeValue missing = table.get(olive::NodeValue::k_color);
	EXPECT_EQ(missing.type(), olive::NodeValue::k_none);
	EXPECT_FALSE(missing);
}

TEST(NodeValueTableExtended, PrependAddsValueToFront)
{
	olive::NodeValueTable table;
	table.push(olive::NodeValue(olive::NodeValue::k_float, 1.0));
	table.prepend(olive::NodeValue(olive::NodeValue::k_float, 2.0));
	table.prepend(olive::NodeValue::k_text, QStringLiteral("t"), nullptr,
				  QStringLiteral("tag"));

	ASSERT_EQ(table.count(), 3);
	EXPECT_EQ(table.at(0).type(), olive::NodeValue::k_text);
	EXPECT_EQ(table.at(0).tag(), QStringLiteral("tag"));
	EXPECT_DOUBLE_EQ(table.at(1).to_double(), 2.0);
	EXPECT_DOUBLE_EQ(table.at(2).to_double(), 1.0);

	// Get() scans from the back, so prepended values are the lowest priority
	EXPECT_DOUBLE_EQ(table.get(olive::NodeValue::k_float).to_double(), 1.0);
}

TEST(NodeValueTableExtended, TakeWithTagAndMissingType)
{
	olive::NodeValueTable table;
	table.push(olive::NodeValue(olive::NodeValue::k_text, QStringLiteral("a"),
								nullptr, QStringLiteral("x")));
	table.push(olive::NodeValue(olive::NodeValue::k_text, QStringLiteral("b"),
								nullptr, QStringLiteral("y")));

	olive::NodeValue taken =
		table.take(olive::NodeValue::k_text, QStringLiteral("x"));
	EXPECT_EQ(taken.to_string(), QStringLiteral("a"));
	EXPECT_EQ(table.count(), 1);

	// Taking a type that is not present returns an empty value and leaves the
	// table unchanged
	olive::NodeValue absent = table.take(olive::NodeValue::k_color);
	EXPECT_EQ(absent.type(), olive::NodeValue::k_none);
	EXPECT_EQ(table.count(), 1);

	// Taking with an unmatched tag returns an empty value and leaves the table
	// unchanged; the oldest value of the type is not used as a fallback
	olive::NodeValue fallback =
		table.take(olive::NodeValue::k_text, QStringLiteral("missing"));
	EXPECT_EQ(fallback.type(), olive::NodeValue::k_none);
	ASSERT_EQ(table.count(), 1);
	EXPECT_EQ(table.at(0).to_string(), QStringLiteral("b"));
}

TEST(NodeValueTableExtended, TakeWithMultipleTypes)
{
	olive::NodeValueTable table;
	table.push(
		olive::NodeValue(olive::NodeValue::k_text, QStringLiteral("s")));
	table.push(olive::NodeValue(olive::NodeValue::k_float, 1.5));

	olive::NodeValue taken =
		table.take({ olive::NodeValue::k_vec2, olive::NodeValue::k_float });
	EXPECT_DOUBLE_EQ(taken.to_double(), 1.5);
	ASSERT_EQ(table.count(), 1);
	EXPECT_EQ(table.at(0).type(), olive::NodeValue::k_text);
}

TEST(NodeValueTableExtended, TakeAtRemovesByIndex)
{
	olive::NodeValueTable table;
	table.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(1)));
	table.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(2)));
	table.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(3)));

	olive::NodeValue taken = table.take_at(1);
	EXPECT_EQ(taken.to_int(), 2);
	ASSERT_EQ(table.count(), 2);
	EXPECT_EQ(table.at(0).to_int(), 1);
	EXPECT_EQ(table.at(1).to_int(), 3);
}

TEST(NodeValueTableExtended, RemoveDeletesNewestEqualValue)
{
	olive::NodeValueTable table;
	table.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(1)));
	table.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(2)));
	table.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(1)));

	// Remove() scans from the back and drops the newest equal value
	table.remove(olive::NodeValue(olive::NodeValue::k_int, int64_t(1)));
	ASSERT_EQ(table.count(), 2);
	EXPECT_EQ(table.at(0).to_int(), 1);
	EXPECT_EQ(table.at(1).to_int(), 2);

	// Removing a value that is not present is a no-op
	table.remove(olive::NodeValue(olive::NodeValue::k_int, int64_t(99)));
	EXPECT_EQ(table.count(), 2);
}

TEST(NodeValueTableExtended, HasUsesExactTypeMatch)
{
	olive::NodeValueTable table;
	table.push(olive::NodeValue(olive::NodeValue::k_float, 1.0));
	EXPECT_TRUE(table.has(olive::NodeValue::k_float));
	EXPECT_FALSE(table.has(olive::NodeValue::k_int));

	// Type is a sequential enum, so no other type aliases kFloat
	EXPECT_FALSE(table.has(olive::NodeValue::k_rational));
	EXPECT_FALSE(table.has(olive::NodeValue::k_text));

	// A table holding a kNone value reports it too
	olive::NodeValueTable none_table;
	none_table.push(olive::NodeValue());
	EXPECT_TRUE(none_table.has(olive::NodeValue::k_none));
}

TEST(NodeValueTableExtended, PushTableAppendsAllValues)
{
	olive::NodeValueTable first;
	first.push(olive::NodeValue(olive::NodeValue::k_float, 1.0));
	first.push(
		olive::NodeValue(olive::NodeValue::k_text, QStringLiteral("a")));

	olive::NodeValueTable second;
	second.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(7)));

	first.push(second);
	ASSERT_EQ(first.count(), 3);
	EXPECT_EQ(first.at(2).to_int(), 7);
}

TEST(NodeValueTableExtended, MergeSlipstreamsTables)
{
	// A single table is returned as-is
	olive::NodeValueTable single;
	single.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(9)));
	olive::NodeValueTable merged_single =
		olive::NodeValueTable::merge({ single });
	ASSERT_EQ(merged_single.count(), 1);
	EXPECT_EQ(merged_single.at(0).to_int(), 9);

	// Merging no tables yields an empty table
	EXPECT_TRUE(olive::NodeValueTable::merge({}).isEmpty());

	// Rows are slipstreamed together from the back of each input table
	olive::NodeValueTable a;
	a.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(1)));
	a.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(3)));
	olive::NodeValueTable b;
	b.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(2)));
	b.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(4)));

	olive::NodeValueTable merged = olive::NodeValueTable::merge({ a, b });
	ASSERT_EQ(merged.count(), 4);
	EXPECT_EQ(merged.at(0).to_int(), 2);
	EXPECT_EQ(merged.at(1).to_int(), 1);
	EXPECT_EQ(merged.at(2).to_int(), 4);
	EXPECT_EQ(merged.at(3).to_int(), 3);

	// A longer table's excess rows end up at the front
	olive::NodeValueTable c;
	c.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(5)));
	c.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(6)));
	c.push(olive::NodeValue(olive::NodeValue::k_int, int64_t(7)));

	olive::NodeValueTable merged_long = olive::NodeValueTable::merge({ a, c });
	ASSERT_EQ(merged_long.count(), 5);
	EXPECT_EQ(merged_long.at(0).to_int(), 5);
	EXPECT_EQ(merged_long.at(1).to_int(), 6);
	EXPECT_EQ(merged_long.at(2).to_int(), 1);
	EXPECT_EQ(merged_long.at(3).to_int(), 7);
	EXPECT_EQ(merged_long.at(4).to_int(), 3);
}

TEST(NodeKeyframeExtended, FullConstructorInitializesFields)
{
	olive::NodeKeyframe key(olive::core::Rational(1, 24), 42.0,
							olive::NodeKeyframe::k_bezier, 2, 3,
							QStringLiteral("input_name"));
	EXPECT_EQ(key.time(), olive::core::Rational(1, 24));
	EXPECT_DOUBLE_EQ(key.value().toDouble(), 42.0);
	EXPECT_EQ(key.type(), olive::NodeKeyframe::k_bezier);
	EXPECT_EQ(key.track(), 2);
	EXPECT_EQ(key.element(), 3);
	EXPECT_EQ(key.input(), QStringLiteral("input_name"));
	EXPECT_TRUE(key.bezier_control_in().isNull());
	EXPECT_TRUE(key.bezier_control_out().isNull());
	EXPECT_EQ(key.previous(), nullptr);
	EXPECT_EQ(key.next(), nullptr);
	EXPECT_EQ(key.parent(), nullptr);

	EXPECT_EQ(olive::NodeKeyframe::k_default_type, olive::NodeKeyframe::k_linear);
}

TEST(NodeKeyframeExtended, CopyDuplicatesAllFields)
{
	olive::NodeKeyframe key(olive::core::Rational(1, 24), 3.5,
							olive::NodeKeyframe::k_bezier, 1, 2,
							QStringLiteral("in"));
	key.set_bezier_control_in(QPointF(0.1, 0.2));
	key.set_bezier_control_out(QPointF(0.3, 0.4));

	std::unique_ptr<olive::NodeKeyframe> copy(key.copy());
	EXPECT_EQ(copy->time(), key.time());
	EXPECT_DOUBLE_EQ(copy->value().toDouble(), 3.5);
	EXPECT_EQ(copy->type(), olive::NodeKeyframe::k_bezier);
	EXPECT_EQ(copy->track(), 1);
	EXPECT_EQ(copy->element(), 2);
	EXPECT_EQ(copy->input(), QStringLiteral("in"));
	EXPECT_EQ(copy->bezier_control_in(), QPointF(0.1, 0.2));
	EXPECT_EQ(copy->bezier_control_out(), QPointF(0.3, 0.4));
	EXPECT_EQ(copy->parent(), nullptr);

	// copy(element) overrides the element
	std::unique_ptr<olive::NodeKeyframe> moved(key.copy(7));
	EXPECT_EQ(moved->element(), 7);

	// The copy is independent of the original
	key.set_value(9.0);
	key.set_bezier_control_in(QPointF(1.0, 1.0));
	EXPECT_DOUBLE_EQ(copy->value().toDouble(), 3.5);
	EXPECT_EQ(copy->bezier_control_in(), QPointF(0.1, 0.2));
}

TEST(NodeKeyframeExtended, SettersEmitSignals)
{
	// QSignalSpy resolves signal argument types at runtime; the app normally
	// registers these in Core::Start(), which the test harness does not call
	qRegisterMetaType<olive::core::Rational>();
	qRegisterMetaType<olive::NodeKeyframe::Type>();

	olive::NodeKeyframe key;

	QSignalSpy time_spy(&key, &olive::NodeKeyframe::time_changed);
	key.set_time(olive::core::Rational(1, 2));
	ASSERT_EQ(time_spy.count(), 1);
	EXPECT_EQ(time_spy.first().at(0).value<olive::core::Rational>(),
			  olive::core::Rational(1, 2));

	QSignalSpy value_spy(&key, &olive::NodeKeyframe::value_changed);
	key.set_value(3.5);
	ASSERT_EQ(value_spy.count(), 1);
	EXPECT_DOUBLE_EQ(value_spy.first().at(0).toDouble(), 3.5);

	QSignalSpy type_spy(&key, &olive::NodeKeyframe::type_changed);
	key.set_type(olive::NodeKeyframe::k_hold);
	ASSERT_EQ(type_spy.count(), 1);
	EXPECT_EQ(type_spy.first().at(0).value<olive::NodeKeyframe::Type>(),
			  olive::NodeKeyframe::k_hold);

	// Setting the same type again does not re-emit
	key.set_type(olive::NodeKeyframe::k_hold);
	EXPECT_EQ(type_spy.count(), 1);

	QSignalSpy in_spy(&key, &olive::NodeKeyframe::bezier_control_in_changed);
	key.set_bezier_control_in(QPointF(0.25, -0.5));
	ASSERT_EQ(in_spy.count(), 1);
	EXPECT_EQ(in_spy.first().at(0).toPointF(), QPointF(0.25, -0.5));

	QSignalSpy out_spy(&key, &olive::NodeKeyframe::bezier_control_out_changed);
	key.set_bezier_control_out(QPointF(-0.25, 0.5));
	ASSERT_EQ(out_spy.count(), 1);
	EXPECT_EQ(out_spy.first().at(0).toPointF(), QPointF(-0.25, 0.5));
}

TEST(NodeKeyframeExtended, SetTypeToBezierInitializesHandles)
{
	// Without neighbors the handles default to one second either way
	olive::NodeKeyframe lone;
	lone.set_time(olive::core::Rational(2));
	lone.set_type(olive::NodeKeyframe::k_bezier);
	EXPECT_DOUBLE_EQ(lone.bezier_control_in().x(), -1.0);
	EXPECT_DOUBLE_EQ(lone.bezier_control_in().y(), 0.0);
	EXPECT_DOUBLE_EQ(lone.bezier_control_out().x(), 1.0);
	EXPECT_DOUBLE_EQ(lone.bezier_control_out().y(), 0.0);

	// With neighbors the handles default to halfway to each neighbor's time
	olive::NodeKeyframe previous;
	previous.set_time(olive::core::Rational(-4));
	olive::NodeKeyframe next;
	next.set_time(olive::core::Rational(8));
	olive::NodeKeyframe key;
	key.set_time(olive::core::Rational(2));
	key.set_previous(&previous);
	key.set_next(&next);
	key.set_type(olive::NodeKeyframe::k_bezier);
	EXPECT_DOUBLE_EQ(key.bezier_control_in().x(), -3.0);
	EXPECT_DOUBLE_EQ(key.bezier_control_in().y(), 0.0);
	EXPECT_DOUBLE_EQ(key.bezier_control_out().x(), 3.0);
	EXPECT_DOUBLE_EQ(key.bezier_control_out().y(), 0.0);

	// Handles that are already set are preserved
	olive::NodeKeyframe preset;
	preset.set_bezier_control_in(QPointF(-0.25, 0.5));
	preset.set_bezier_control_out(QPointF(0.75, -0.5));
	preset.set_type(olive::NodeKeyframe::k_bezier);
	EXPECT_EQ(preset.bezier_control_in(), QPointF(-0.25, 0.5));
	EXPECT_EQ(preset.bezier_control_out(), QPointF(0.75, -0.5));
}

TEST(NodeKeyframeExtended, SetTypeNoBezierAdjLeavesHandlesUntouched)
{
	olive::NodeKeyframe key;
	key.set_bezier_control_in(QPointF(0.5, 0.5));
	key.set_bezier_control_out(QPointF(-0.5, -0.5));
	key.set_type_no_bezier_adj(olive::NodeKeyframe::k_bezier);
	EXPECT_EQ(key.type(), olive::NodeKeyframe::k_bezier);
	EXPECT_EQ(key.bezier_control_in(), QPointF(0.5, 0.5));
	EXPECT_EQ(key.bezier_control_out(), QPointF(-0.5, -0.5));

	// Handles stay null when none were set
	olive::NodeKeyframe other;
	other.set_type_no_bezier_adj(olive::NodeKeyframe::k_bezier);
	EXPECT_TRUE(other.bezier_control_in().isNull());
	EXPECT_TRUE(other.bezier_control_out().isNull());
}

TEST(NodeKeyframeExtended, BezierControlAccessorsByHandleType)
{
	olive::NodeKeyframe key;
	key.set_bezier_control(olive::NodeKeyframe::k_in_handle,
						   QPointF(-0.5, 0.25));
	key.set_bezier_control(olive::NodeKeyframe::k_out_handle,
						   QPointF(0.5, -0.25));

	EXPECT_EQ(key.bezier_control_in(), QPointF(-0.5, 0.25));
	EXPECT_EQ(key.bezier_control_out(), QPointF(0.5, -0.25));
	EXPECT_EQ(key.bezier_control(olive::NodeKeyframe::k_in_handle),
			  key.bezier_control_in());
	EXPECT_EQ(key.bezier_control(olive::NodeKeyframe::k_out_handle),
			  key.bezier_control_out());

	EXPECT_EQ(olive::NodeKeyframe::get_opposing_bezier_type(
				  olive::NodeKeyframe::k_in_handle),
			  olive::NodeKeyframe::k_out_handle);
	EXPECT_EQ(olive::NodeKeyframe::get_opposing_bezier_type(
				  olive::NodeKeyframe::k_out_handle),
			  olive::NodeKeyframe::k_in_handle);
}

TEST(NodeKeyframeExtended, ValidBezierControlsClampToNeighbors)
{
	olive::NodeKeyframe key;
	key.set_time(olive::core::Rational(2));
	key.set_bezier_control_in(QPointF(-5.0, 0.5));
	key.set_bezier_control_out(QPointF(5.0, -0.25));

	// Without neighbors the handles pass through unchanged
	EXPECT_EQ(key.valid_bezier_control_in(), QPointF(-5.0, 0.5));
	EXPECT_EQ(key.valid_bezier_control_out(), QPointF(5.0, -0.25));

	olive::NodeKeyframe previous;
	previous.set_time(olive::core::Rational(1));
	olive::NodeKeyframe next;
	next.set_time(olive::core::Rational(3));
	key.set_previous(&previous);
	key.set_next(&next);

	EXPECT_EQ(key.previous(), &previous);
	EXPECT_EQ(key.next(), &next);

	// The clamped handles may not cross the neighboring keyframe's time
	EXPECT_EQ(key.valid_bezier_control_in(), QPointF(-1.0, 0.5));
	EXPECT_EQ(key.valid_bezier_control_out(), QPointF(1.0, -0.25));
}

TEST(NodeKeyframeExtended, KeyTrackRefReflectsInputTrackElement)
{
	olive::NodeKeyframe key(olive::core::Rational(1, 24), 2.0,
							olive::NodeKeyframe::k_linear, 2, 3,
							QStringLiteral("my_input"));

	const olive::NodeKeyframeTrackReference ref = key.key_track_ref();
	EXPECT_EQ(ref.track(), 2);
	EXPECT_EQ(ref.input().input(), QStringLiteral("my_input"));
	EXPECT_EQ(ref.input().element(), 3);
	EXPECT_EQ(ref.input().node(), nullptr);
	EXPECT_FALSE(ref.is_valid());
}

TEST(NodeKeyframeExtended, HasSiblingAtTimeDetectsOtherKeyframes)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *math = new olive::MathNode();
	math->setParent(&project);

	// Keyframe lookups only apply to tracks with keyframing enabled
	math->set_input_is_keyframing(olive::MathNode::k_param_a_in, true);

	auto *first = new olive::NodeKeyframe(
		olive::core::Rational(0), 1.0, olive::NodeKeyframe::k_linear, 0, -1,
		olive::MathNode::k_param_a_in, math);
	auto *second = new olive::NodeKeyframe(
		olive::core::Rational(1), 2.0, olive::NodeKeyframe::k_linear, 0, -1,
		olive::MathNode::k_param_a_in, math);

	// Parenting inserts the keyframes in time order and links the track
	EXPECT_EQ(first->next(), second);
	EXPECT_EQ(second->previous(), first);
	EXPECT_EQ(first->previous(), nullptr);
	EXPECT_EQ(second->next(), nullptr);

	// A sibling exists wherever another keyframe holds the time
	EXPECT_TRUE(second->has_sibling_at_time(olive::core::Rational(0)));
	EXPECT_FALSE(second->has_sibling_at_time(olive::core::Rational(1)));
	EXPECT_FALSE(first->has_sibling_at_time(olive::core::Rational(2)));

	// Inserting out of order keeps the track sorted and relinks neighbors
	auto *middle = new olive::NodeKeyframe(
		olive::core::Rational(1, 2), 1.5, olive::NodeKeyframe::k_linear, 0, -1,
		olive::MathNode::k_param_a_in, math);
	EXPECT_EQ(first->next(), middle);
	EXPECT_EQ(middle->previous(), first);
	EXPECT_EQ(middle->next(), second);
	EXPECT_EQ(second->previous(), middle);
	EXPECT_TRUE(middle->has_sibling_at_time(olive::core::Rational(0)));
}
