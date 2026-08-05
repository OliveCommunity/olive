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

#include <thread>
#include <vector>

#include "../src/threadsafemap.h"

TEST(ThreadSafeMap, InsertAndGet)
{
	ThreadSafeMap<int, int> map;

	map.insert(1, 42);

	int value = 0;
	ASSERT_TRUE(map.get(1, &value));
	EXPECT_EQ(value, 42);
	EXPECT_TRUE(map.contains(1));
	EXPECT_EQ(map.size(), 1u);
}

TEST(ThreadSafeMap, InsertOverwritesExistingKey)
{
	ThreadSafeMap<int, int> map;

	map.insert(1, 42);
	map.insert(1, 7);

	int value = 0;
	ASSERT_TRUE(map.get(1, &value));
	EXPECT_EQ(value, 7);
	EXPECT_EQ(map.size(), 1u);
}

TEST(ThreadSafeMap, MissingKeyReturnsFalse)
{
	ThreadSafeMap<int, int> map;

	EXPECT_FALSE(map.contains(99));

	int value = 1234;
	EXPECT_FALSE(map.get(99, &value));
	// `out` must be left untouched on failure
	EXPECT_EQ(value, 1234);
}

TEST(ThreadSafeMap, ConcurrentInserts)
{
	ThreadSafeMap<int, int> map;
	const int k_threads = 8;
	const int k_inserts_per_thread = 500;

	std::vector<std::thread> threads;
	for (int t = 0; t < k_threads; t++) {
		threads.emplace_back([&map, t, k_inserts_per_thread]() {
			for (int i = 0; i < k_inserts_per_thread; i++) {
				map.insert(t * k_inserts_per_thread + i, i);
			}
		});
	}
	for (std::thread &th : threads) {
		th.join();
	}

	EXPECT_EQ(map.size(), size_t(k_threads * k_inserts_per_thread));

	int value = 0;
	ASSERT_TRUE(map.get(3 * k_inserts_per_thread + 123, &value));
	EXPECT_EQ(value, 123);
}
