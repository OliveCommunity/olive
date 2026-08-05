/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#ifndef OAK_DECIBEL_H
#define OAK_DECIBEL_H

#include <cmath>
#include <limits>

namespace olive
{

class Decibel {
public:
	// In basically all circumstances, this should calculate to 0.0 linear
	static constexpr double minimum = -200.0;

	static double from_linear(double linear)
	{
		double v = double(20.0) * std::log10(linear);
		if (std::isinf(v)) {
			return minimum;
		}
		return v;
	}

	static double to_linear(double decibel)
	{
		double to_linear = std::pow(double(10.0), decibel / double(20.0));

		// Minimum threshold that we figure is close enough to 0 that we may as well just return 0
		if (to_linear < 0.000001) {
			return 0;
		} else {
			return to_linear;
		}
	}

	static double from_logarithmic(double logarithmic)
	{
		if (logarithmic < 0.001)
			return minimum;
		else if (logarithmic > 0.99)
			return 0;
		else
			return 20.0 * std::log10(-std::log(1 - logarithmic) / lo_g100);
	}

	static double to_logarithmic(double decibel)
	{
		if (std::abs(decibel) <= 1e-12) {
			return 1;
		} else {
			return 1 - std::exp(-std::pow(10.0, decibel / 20.0) * lo_g100);
		}
	}

	static double linear_to_logarithmic(double linear)
	{
		return 1 - std::exp(-linear * lo_g100);
	}

	static double logarithmic_to_linear(double logarithmic)
	{
		if (logarithmic > 0.99) {
			return 1;
		} else {
			return -std::log(1 - logarithmic) / lo_g100;
		}
	}

private:
	static constexpr double lo_g100 = 4.60517018599;
};

}

#endif // OAK_DECIBEL_H
