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

// Pure C ABI tests for the liboakengine proxy facade (oakengine/proxy.h).
// Runs headless; no GPU required.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "oakengine/init.h"
#include "oakengine/proxy.h"

static void test_instance_lifecycle(void)
{
    assert(oakengine_proxy_create_instance() == OAKENGINE_OK);
    assert(oakengine_proxy_destroy_instance() == OAKENGINE_OK);
    // Destroying again is a no-op.
    assert(oakengine_proxy_destroy_instance() == OAKENGINE_OK);
}

static void test_params_from_config(void)
{
    oak_proxy_params params;
    memset(&params, 0xFF, sizeof(params));

    assert(oakengine_proxy_create_instance() == OAKENGINE_OK);
    assert(oakengine_proxy_params_from_config(&params) == OAKENGINE_OK);

    // Sanity defaults from ProxyManager::proxy_params_from_config().
    assert(params.width > 0);
    assert(params.height > 0);
    assert(params.divider >= 1);
    assert(params.version >= 1);
    assert(params.crf >= 0);
    assert(params.include_audio == 0 || params.include_audio == 1);
    assert(strlen(params.extension) > 0);
    assert(strlen(params.preset) > 0);

    assert(oakengine_proxy_params_from_config(NULL) == OAKENGINE_E_INVALID);

    assert(oakengine_proxy_destroy_instance() == OAKENGINE_OK);
}

static void test_state_string_round_trip(void)
{
    char buf[64];

    assert(oakengine_proxy_state_to_string(OAKENGINE_PROXY_STATE_MISSING, buf,
                                           sizeof(buf)) > 0);
    assert(strlen(buf) > 0);

    assert(oakengine_proxy_state_to_string(OAKENGINE_PROXY_STATE_GENERATING,
                                           buf, sizeof(buf)) > 0);
    assert(strlen(buf) > 0);

    assert(oakengine_proxy_state_to_string(OAKENGINE_PROXY_STATE_READY, buf,
                                           sizeof(buf)) > 0);
    assert(strlen(buf) > 0);

    assert(oakengine_proxy_state_to_string(OAKENGINE_PROXY_STATE_FAILED, buf,
                                           sizeof(buf)) > 0);
    assert(strlen(buf) > 0);

    // Unknown state returns an error.
    assert(oakengine_proxy_state_to_string(999, buf, sizeof(buf)) < 0);
}

static void test_state_query(void)
{
    assert(oakengine_proxy_get_state(NULL) == OAKENGINE_PROXY_STATE_MISSING);
    assert(oakengine_proxy_get_state("") == OAKENGINE_PROXY_STATE_MISSING);
    assert(oakengine_proxy_get_state("/nonexistent/path/proxy.mp4") ==
           OAKENGINE_PROXY_STATE_MISSING);
}

static void test_get_or_start_null(void)
{
    oak_proxy_result result;
    memset(&result, 0xFF, sizeof(result));

    // NULL cache_path should not crash; returns an error.
    assert(oakengine_proxy_get_or_start(NULL, NULL, 0, NULL, &result) !=
           OAKENGINE_OK);
}

static void test_get_working_filename(void)
{
    char buf[1024];
    int len = oakengine_proxy_get_working_filename("/tmp/test.proxy",
                                                    buf, sizeof(buf));
    // Should return a filename derived from input, even if file doesn't exist.
    assert(len > 0);
    assert(strlen(buf) > 0);

    // NULL safety.
    assert(oakengine_proxy_get_working_filename(NULL, buf, sizeof(buf)) < 0);
}

int main(void)
{
    assert(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

    test_instance_lifecycle();
    test_params_from_config();
    test_state_string_round_trip();
    test_state_query();
    test_get_or_start_null();
    test_get_working_filename();

    assert(oakengine_shutdown() == OAKENGINE_OK);
    return 0;
}
