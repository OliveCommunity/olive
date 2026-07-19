#include <gtest/gtest.h>

#include "tool/tool.h"

TEST(ModuleSmoke, ToolAddableObjectNames)
{
	EXPECT_FALSE(
		olive::Tool::GetAddableObjectName(olive::Tool::kAddableEmpty).isEmpty());
	EXPECT_FALSE(
		olive::Tool::GetAddableObjectName(olive::Tool::kAddableBars).isEmpty());
	EXPECT_FALSE(
		olive::Tool::GetAddableObjectName(olive::Tool::kAddableShape).isEmpty());
	EXPECT_FALSE(
		olive::Tool::GetAddableObjectName(olive::Tool::kAddableSolid).isEmpty());
	EXPECT_FALSE(
		olive::Tool::GetAddableObjectName(olive::Tool::kAddableTitle).isEmpty());
	EXPECT_FALSE(
		olive::Tool::GetAddableObjectName(olive::Tool::kAddableTone).isEmpty());
	EXPECT_FALSE(
		olive::Tool::GetAddableObjectName(olive::Tool::kAddableSubtitle)
			.isEmpty());
}

TEST(ModuleSmoke, ToolAddableObjectIds)
{
	EXPECT_EQ(olive::Tool::GetAddableObjectID(olive::Tool::kAddableEmpty),
			  QStringLiteral("empty"));
	EXPECT_EQ(olive::Tool::GetAddableObjectID(olive::Tool::kAddableBars),
			  QStringLiteral("bars"));
	EXPECT_EQ(olive::Tool::GetAddableObjectID(olive::Tool::kAddableShape),
			  QStringLiteral("shape"));
	EXPECT_EQ(olive::Tool::GetAddableObjectID(olive::Tool::kAddableSolid),
			  QStringLiteral("solid"));
	EXPECT_EQ(olive::Tool::GetAddableObjectID(olive::Tool::kAddableTitle),
			  QStringLiteral("title"));
	EXPECT_EQ(olive::Tool::GetAddableObjectID(olive::Tool::kAddableTone),
			  QStringLiteral("tone"));
	EXPECT_EQ(olive::Tool::GetAddableObjectID(olive::Tool::kAddableSubtitle),
			  QStringLiteral("subtitle"));
}
