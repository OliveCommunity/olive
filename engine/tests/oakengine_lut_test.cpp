/***

  Oak - Non-Linear Video Editor
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

// Pure C ABI tests for the liboakengine LUT library facade (oakengine/lut.h).
// Runs headless; no GPU required.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "oakengine/init.h"
#include "oakengine/lut.h"

static void test_counts_after_init(void)
{
    assert(oakengine_lut_directory_count() >= 0);
    assert(oakengine_lut_file_count() >= 0);

    // Out-of-range index returns an error.
    char buf[256];
    assert(oakengine_lut_directory_at(-1, buf, sizeof(buf)) < 0);
    assert(oakengine_lut_file_at(-1, buf, sizeof(buf)) < 0);
}

static void test_set_directories_round_trip(void)
{
    const char *dirs[] = { "/tmp/oak_lut_a", "/tmp/oak_lut_b" };

    assert(oakengine_lut_set_directories(dirs, 2) == OAKENGINE_OK);
    assert(oakengine_lut_directory_count() == 2);

    char buf[256];
    assert(oakengine_lut_directory_at(0, buf, sizeof(buf)) > 0);
    assert(strcmp(buf, "/tmp/oak_lut_a") == 0);
    assert(oakengine_lut_directory_at(1, buf, sizeof(buf)) > 0);
    assert(strcmp(buf, "/tmp/oak_lut_b") == 0);

    // Clearing the library.
    assert(oakengine_lut_set_directories(NULL, 0) == OAKENGINE_OK);
    assert(oakengine_lut_directory_count() == 0);
}

int main(void)
{
    assert(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

    test_counts_after_init();
    test_set_directories_round_trip();

    assert(oakengine_shutdown() == OAKENGINE_OK);
    return 0;
}
