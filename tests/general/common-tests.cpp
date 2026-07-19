/***

  Olive - Non-Linear video Editor
  Copyright (c) 2022 Olive Team
  Modifications Copyright (c) 2025 mikesolar

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

#include "testutil.h"

#include "common/digit.h"

namespace olive
{

OAK_ADD_TEST(DigitTest)
{
	OAK_ASSERT(get_digit_count(1) == 1);
	OAK_ASSERT(get_digit_count(69) == 2);
	OAK_ASSERT(get_digit_count(420) == 3);
	OAK_ASSERT(get_digit_count(1337) == 4);
	OAK_ASSERT(get_digit_count(80085) == 5);
	OAK_ASSERT(get_digit_count(555555) == 6);
	OAK_ASSERT(get_digit_count(8675309) == 7);
	OAK_ASSERT(get_digit_count(78956423) == 8);
	OAK_ASSERT(get_digit_count(148497523) == 9);
	OAK_ASSERT(get_digit_count(4845821233) == 10);
	OAK_ASSERT(get_digit_count(18002738255) == 11);
	OAK_ASSERT(get_digit_count(180027382556) == 12);
	OAK_ASSERT(get_digit_count(1800273825568) == 13);
	OAK_ASSERT(get_digit_count(18002738255685) == 14);
	OAK_ASSERT(get_digit_count(180027382556857) == 15);
	OAK_ASSERT(get_digit_count(1800273825564857) == 16);

	OAK_TEST_END;
}

}
