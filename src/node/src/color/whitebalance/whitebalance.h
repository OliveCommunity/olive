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

#ifndef OAK_WHITEBALANCENODE_H
#define OAK_WHITEBALANCENODE_H

#include "mathtypes.h"
#include "node.h"

namespace olive
{

/**
 * @brief White balance correction by color temperature and tint
 *
 * Converts a scene illuminant temperature (in Kelvin) into per-channel RGB
 * gains using the Tanner Helland blackbody approximation, normalized so the
 * green channel is preserved (no exposure shift). Tint shifts the image
 * along the green-magenta axis.
 */
class WhiteBalanceNode : public Node {
public:
	WhiteBalanceNode();

	NODE_DEFAULT_FUNCTIONS(WhiteBalanceNode)

	virtual std::string name() const override;
	virtual std::string id() const override;
	virtual std::vector<CategoryID> category() const override;
	virtual std::string description() const override;

	virtual void retranslate() override;

	virtual ShaderCode get_shader_code(const ShaderRequest &request) const override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	/**
	 * @brief RGB gains for a given illuminant temperature and tint
	 *
	 * Extracted for testability. Kelvin is clamped to [1000, 40000]; the
	 * result is normalized so the green channel gain is 1.0 at tint 0.
	 */
	static Vector3D get_gain_for_temperature(double kelvin, double tint);

	static const std::string k_texture_input;
	static const std::string k_temperature_input;
	static const std::string k_tint_input;
	static const std::string k_gain_input;
};

} // olive

#endif
