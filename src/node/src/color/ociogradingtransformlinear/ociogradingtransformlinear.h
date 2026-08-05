/***

  Olive - Non-Linear Video Editor
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

#ifndef OAK_OCIOGRADINGTRANSFORMLINEARNODE_H
#define OAK_OCIOGRADINGTRANSFORMLINEARNODE_H

#include "color/ociobase/ociobase.h"
#include "render/colorprocessor.h"

namespace olive
{

class OCIOGradingTransformLinearNode : public OCIOBaseNode {
public:
	OCIOGradingTransformLinearNode();

	NODE_DEFAULT_FUNCTIONS(OCIOGradingTransformLinearNode)

	virtual std::string name() const override;
	virtual std::string id() const override;
	virtual std::vector<CategoryID> category() const override;
	virtual std::string description() const override;

	virtual void retranslate() override;
	virtual void InputValueChangedEvent(const std::string &input,
										int element) override;
	virtual void InputConnectedEvent(const std::string &input, int element,
									 Node *output) override;
	virtual void InputDisconnectedEvent(const std::string &input, int element,
										Node *output) override;
	void generate_processor();

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	static const std::string k_contrast_input;
	static const std::string k_offset_input;
	static const std::string k_exposure_input;
	static const std::string k_saturation_input;
	static const std::string k_pivot_input;
	static const std::string k_clamp_black_enable_input;
	static const std::string k_clamp_black_input;
	static const std::string k_clamp_white_enable_input;
	static const std::string k_clamp_white_input;

protected:
	virtual void config_changed() override;

private:
	void set_vec4_input_colors(const std::string &input);

	/**
	 * @brief Constrains the white clamp UI minimum to just above the black
	 * clamp, as required by ocio::GradingPrimary::validate
	 *
	 * Only applies while the black clamp is a static value; when it is
	 * keyframed or connected the invariant is enforced per frame in Value()
	 * instead.
	 */
	void update_clamp_white_minimum();
};

} // olive

#endif
