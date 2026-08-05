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

#ifndef OAK_VOLUMENODE_H
#define OAK_VOLUMENODE_H

#include "math/math/mathbase.h"

namespace olive
{

class VolumeNode : public MathNodeBase {
public:
	VolumeNode();

	NODE_DEFAULT_FUNCTIONS(VolumeNode)

	virtual std::string name() const override;
	virtual std::string id() const override;
	virtual std::vector<CategoryID> category() const override;
	virtual std::string description() const override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual void process_samples(const NodeValueRow &values,
								const SampleBuffer &input, SampleBuffer &output,
								int index) const override;

	virtual void retranslate() override;

	static const std::string k_samples_input;
	static const std::string k_volume_input;
};

}

#endif // OAK_VOLUMENODE_H
