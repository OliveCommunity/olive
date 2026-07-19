#include <gtest/gtest.h>

#include "tool/tool.h"

TEST(ModuleSmoke, ToolAddableObjectNames)
{
	EXPECT_FALSE(
		olive::Tool::get_addable_object_name(olive::Tool::k_addable_empty).isEmpty());
	EXPECT_FALSE(
		olive::Tool::get_addable_object_name(olive::Tool::k_addable_bars).isEmpty());
	EXPECT_FALSE(
		olive::Tool::get_addable_object_name(olive::Tool::k_addable_shape).isEmpty());
	EXPECT_FALSE(
		olive::Tool::get_addable_object_name(olive::Tool::k_addable_solid).isEmpty());
	EXPECT_FALSE(
		olive::Tool::get_addable_object_name(olive::Tool::k_addable_title).isEmpty());
	EXPECT_FALSE(
		olive::Tool::get_addable_object_name(olive::Tool::k_addable_tone).isEmpty());
	EXPECT_FALSE(
		olive::Tool::get_addable_object_name(olive::Tool::k_addable_subtitle)
			.isEmpty());
}

TEST(ModuleSmoke, ToolAddableObjectIds)
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
}
