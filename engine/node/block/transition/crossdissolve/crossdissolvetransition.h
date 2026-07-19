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

#ifndef OAK_CROSSDISSOLVETRANSITION_H
#define OAK_CROSSDISSOLVETRANSITION_H

#include "node/block/transition/transition.h"

namespace olive
{

class CrossDissolveTransition : public TransitionBlock {
	Q_OBJECT
public:
	CrossDissolveTransition();

	NODE_DEFAULT_FUNCTIONS(CrossDissolveTransition)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	//virtual void Retranslate() override;

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;

protected:
	virtual void SampleJobEvent(const SampleBuffer &from_samples,
								const SampleBuffer &to_samples,
								SampleBuffer &out_samples,
								double time_in) const override;
};

}

#endif // OAK_CROSSDISSOLVETRANSITION_H
