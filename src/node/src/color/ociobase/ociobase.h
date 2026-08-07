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

#ifndef OAK_OCIOBASENODE_H
#define OAK_OCIOBASENODE_H

#include "node.h"
#include "render/color.h"
#include "render/job/colortransformjob.h"

namespace olive
{

class OCIOBaseNode : public Node {
public:
	OCIOBaseNode();
	virtual ~OCIOBaseNode() override;

	virtual void AddedToGraphEvent(Project *p) override;
	virtual void RemovedFromGraphEvent(Project *p) override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	static const std::string k_texture_input;

protected:
	// Called when the OCIO config changes. The former ColorManager::config_changed
	// signal connection is gone with Qt; the facade/event layer invokes this.
	virtual void config_changed() = 0;

protected:
	ColorManager *manager() const
	{
		return manager_;
	}

	/**
	 * @brief Borrowed copy of the processor handle owned by this node.
	 *        Callers must NOT free it.
	 */
	const OakColorProcessor &processor() const
	{
		return processor_;
	}

	/**
	 * @brief Take ownership of a processor handle (the old one is
	 *        released).
	 */
	void set_processor(OakColorProcessor p)
	{
		oakrender_color_processor_free(&processor_);
		processor_ = p;
	}

private:
	ColorManager *manager_;

	OakColorProcessor processor_ = {};
};

}

#endif // OAK_OCIOBASENODE_H
