/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include <gtest/gtest.h>

#include <string.h>

#include "../src/memorypool.h"

namespace
{

/**
 * @brief Test pool with a fixed 16-byte element size
 */
class TestPool : public olive::MemoryPool {
public:
	TestPool(int element_count) : MemoryPool(element_count)
	{
	}

protected:
	size_t get_element_size() override
	{
		return 16;
	}
};

} // namespace

TEST(MemoryPool, AllocateReturnsWritableElement)
{
	TestPool pool(4);

	auto e = pool.get();
	ASSERT_NE(e, nullptr);
	ASSERT_NE(e->data(), nullptr);

	// The full element must be writable
	memset(e->data(), 0xAB, 16);
	EXPECT_EQ(e->data()[0], 0xAB);
	EXPECT_EQ(e->data()[15], 0xAB);

	EXPECT_TRUE(pool.is_allocated());
	EXPECT_EQ(pool.get_arena_count(), 1);
}

TEST(MemoryPool, ReleasedElementIsReused)
{
	TestPool pool(4);

	uint8_t *first = nullptr;
	{
		auto e = pool.get();
		ASSERT_NE(e, nullptr);
		first = e->data();
	}

	// After the ElementPtr went out of scope the chunk must be recycled
	auto e2 = pool.get();
	ASSERT_NE(e2, nullptr);
	EXPECT_EQ(e2->data(), first);
	EXPECT_EQ(pool.get_arena_count(), 1);
}

TEST(MemoryPool, FullArenaGrowsNewArena)
{
	TestPool pool(2);

	auto e1 = pool.get();
	auto e2 = pool.get();
	ASSERT_NE(e1, nullptr);
	ASSERT_NE(e2, nullptr);
	EXPECT_EQ(pool.get_arena_count(), 1);

	// Both slots lent out, next get() must allocate a second arena
	auto e3 = pool.get();
	ASSERT_NE(e3, nullptr);
	EXPECT_EQ(pool.get_arena_count(), 2);
}

TEST(MemoryPool, ClearFreesAllArenas)
{
	TestPool pool(4);

	auto e = pool.get();
	ASSERT_NE(e, nullptr);
	e->release();
	e.reset();

	pool.clear();
	EXPECT_FALSE(pool.is_allocated());
	EXPECT_EQ(pool.get_arena_count(), 0);
}

TEST(MemoryPool, InvalidElementCountReturnsNull)
{
	TestPool pool(0);
	EXPECT_EQ(pool.get(), nullptr);

	TestPool negative(-1);
	EXPECT_EQ(negative.get(), nullptr);
}

TEST(MemoryPool, ClearEmptyArenasKeepsRecentlyEmptiedArena)
{
	TestPool pool(2);

	{
		auto e = pool.get();
		ASSERT_NE(e, nullptr);
	}

	// Arena was emptied just now, well within kMaxEmptyArenaLife, so it must survive
	pool.clear_empty_arenas();
	EXPECT_EQ(pool.get_arena_count(), 1);
}

TEST(MemoryPool, ClearEmptyArenasKeepsArenaInUse)
{
	TestPool pool(2);

	auto e = pool.get();
	ASSERT_NE(e, nullptr);

	pool.clear_empty_arenas();
	EXPECT_EQ(pool.get_arena_count(), 1);
}
