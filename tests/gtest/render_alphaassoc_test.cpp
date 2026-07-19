#include <gtest/gtest.h>

#include "render/alphaassoc.h"

TEST(AlphaAssociated, ValuesAreDistinct)
{
	EXPECT_NE(olive::k_alpha_none, olive::k_alpha_unassociated);
	EXPECT_NE(olive::k_alpha_none, olive::k_alpha_associated);
	EXPECT_NE(olive::k_alpha_unassociated, olive::k_alpha_associated);
}
