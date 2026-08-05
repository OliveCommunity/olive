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

#ifndef OAK_SHADERCODE_H
#define OAK_SHADERCODE_H

#include <string>

#include "filefunctions.h"

namespace olive
{

class ShaderCode {
public:
	ShaderCode(const std::string &frag_code = std::string(),
			   const std::string &vert_code = std::string())
		: frag_code_(frag_code)
		, vert_code_(vert_code)
	{
	}

	const std::string &frag_code() const
	{
		return frag_code_;
	}
	void set_frag_code(const std::string &f)
	{
		frag_code_ = f;
	}

	const std::string &vert_code() const
	{
		return vert_code_;
	}
	void set_vert_code(const std::string &v)
	{
		vert_code_ = v;
	}

private:
	std::string frag_code_;

	std::string vert_code_;
};

}

#endif // OAK_SHADERCODE_H
