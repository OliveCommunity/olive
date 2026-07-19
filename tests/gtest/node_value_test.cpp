#include <gtest/gtest.h>

#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

#include "node/value.h"

TEST(NodeValue, VectorRoundTrip)
{
	QVector2D v2(1.5f, -2.0f);
	QString encoded = olive::NodeValue::value_to_string(
		olive::NodeValue::k_vec2, QVariant::fromValue(v2), false);
	QVariant decoded = olive::NodeValue::string_to_value(olive::NodeValue::k_vec2,
													   encoded, false);
	QVector2D v2_out = decoded.value<QVector2D>();
	EXPECT_FLOAT_EQ(v2_out.x(), v2.x());
	EXPECT_FLOAT_EQ(v2_out.y(), v2.y());

	QVector3D v3(1.0f, 2.0f, 3.0f);
	encoded = olive::NodeValue::value_to_string(olive::NodeValue::k_vec3,
											  QVariant::fromValue(v3), false);
	decoded = olive::NodeValue::string_to_value(olive::NodeValue::k_vec3, encoded,
											  false);
	QVector3D v3_out = decoded.value<QVector3D>();
	EXPECT_FLOAT_EQ(v3_out.x(), v3.x());
	EXPECT_FLOAT_EQ(v3_out.y(), v3.y());
	EXPECT_FLOAT_EQ(v3_out.z(), v3.z());

	QVector4D v4(1.0f, 2.0f, 3.0f, 4.0f);
	encoded = olive::NodeValue::value_to_string(olive::NodeValue::k_vec4,
											  QVariant::fromValue(v4), false);
	decoded = olive::NodeValue::string_to_value(olive::NodeValue::k_vec4, encoded,
											  false);
	QVector4D v4_out = decoded.value<QVector4D>();
	EXPECT_FLOAT_EQ(v4_out.x(), v4.x());
	EXPECT_FLOAT_EQ(v4_out.y(), v4.y());
	EXPECT_FLOAT_EQ(v4_out.z(), v4.z());
	EXPECT_FLOAT_EQ(v4_out.w(), v4.w());
}

TEST(NodeValue, BinaryRoundTrip)
{
	QByteArray data("OliveTest");
	QString encoded =
		olive::NodeValue::value_to_string(olive::NodeValue::k_binary, data, false);
	QVariant decoded = olive::NodeValue::string_to_value(
		olive::NodeValue::k_binary, encoded, false);
	EXPECT_EQ(decoded.toByteArray(), data);
}

TEST(NodeValue, TypeClassification)
{
	EXPECT_TRUE(
		olive::NodeValue::type_can_be_interpolated(olive::NodeValue::k_float));
	EXPECT_TRUE(
		olive::NodeValue::type_can_be_interpolated(olive::NodeValue::k_color));
	EXPECT_FALSE(
		olive::NodeValue::type_can_be_interpolated(olive::NodeValue::k_int));

	EXPECT_TRUE(olive::NodeValue::type_is_numeric(olive::NodeValue::k_int));
	EXPECT_TRUE(olive::NodeValue::type_is_numeric(olive::NodeValue::k_float));
	EXPECT_FALSE(olive::NodeValue::type_is_numeric(olive::NodeValue::k_text));

	EXPECT_TRUE(olive::NodeValue::type_is_vector(olive::NodeValue::k_vec2));
	EXPECT_TRUE(olive::NodeValue::type_is_vector(olive::NodeValue::k_vec3));
	EXPECT_FALSE(olive::NodeValue::type_is_vector(olive::NodeValue::k_float));

	EXPECT_TRUE(olive::NodeValue::type_is_buffer(olive::NodeValue::k_texture));
	EXPECT_TRUE(olive::NodeValue::type_is_buffer(olive::NodeValue::k_samples));
	EXPECT_FALSE(olive::NodeValue::type_is_buffer(olive::NodeValue::k_color));
}

TEST(NodeValue, DataTypeNameRoundTrip)
{
	for (int i = olive::NodeValue::k_none; i < olive::NodeValue::k_data_type_count;
		 ++i) {
		auto type = static_cast<olive::NodeValue::Type>(i);
		QString name = olive::NodeValue::get_data_type_name(type);
		if (name.isEmpty()) {
			continue;
		}
		EXPECT_EQ(olive::NodeValue::get_data_type_from_name(name), type)
			<< name.toStdString();
	}
}

TEST(NodeValue, ConstructionAndAccessors)
{
	olive::NodeValue val(olive::NodeValue::k_int, static_cast<int64_t>(42));
	EXPECT_EQ(val.type(), olive::NodeValue::k_int);
	EXPECT_EQ(val.to_int(), 42);
	EXPECT_TRUE(val);

	val.set_tag(QStringLiteral("tag"));
	EXPECT_EQ(val.tag(), QStringLiteral("tag"));
}

TEST(NodeValueTable, PushAndGet)
{
	olive::NodeValueTable table;
	olive::NodeValue v(olive::NodeValue::k_float, 3.14);
	table.push(v);

	EXPECT_EQ(table.count(), 1);
	EXPECT_FALSE(table.isEmpty());
	EXPECT_TRUE(table.has(olive::NodeValue::k_float));

	olive::NodeValue got = table.get(olive::NodeValue::k_float);
	EXPECT_DOUBLE_EQ(got.to_double(), 3.14);
}

TEST(NodeValueTable, TakeRemovesValue)
{
	olive::NodeValueTable table;
	table.push(
		olive::NodeValue(olive::NodeValue::k_int, static_cast<int64_t>(1)));
	table.push(
		olive::NodeValue(olive::NodeValue::k_int, static_cast<int64_t>(2)));

	olive::NodeValue taken = table.take(olive::NodeValue::k_int);
	EXPECT_EQ(taken.to_int(), 2);
	EXPECT_EQ(table.count(), 1);
}

TEST(NodeValueTable, ClearEmptiesTable)
{
	olive::NodeValueTable table;
	table.push(
		olive::NodeValue(olive::NodeValue::k_text, QStringLiteral("hello")));
	table.clear();
	EXPECT_TRUE(table.isEmpty());
	EXPECT_EQ(table.count(), 0);
}
