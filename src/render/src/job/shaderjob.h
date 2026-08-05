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

#ifndef OAK_SHADERJOB_H
#define OAK_SHADERJOB_H

#include <map>
#include <string>
#include <vector>

#include "acceleratedjob.h"
#include "texture.h"

namespace olive
{

class ShaderJob : public AcceleratedJob {
public:
	ShaderJob()
	{
		iterations_ = 1;
	}

	ShaderJob(const NodeValueRow &row)
		: ShaderJob()
	{
		insert(row);
	}

	const std::string &get_shader_id() const
	{
		return shader_id_;
	}

	void set_shader_id(const std::string &id)
	{
		shader_id_ = id;
	}

	void set_iterations(int iterations, const NodeInput &iterative_input)
	{
		set_iterations(iterations, iterative_input.input());
	}

	void set_iterations(int iterations, const std::string &iterative_input)
	{
		iterations_ = iterations;
		iterative_input_ = iterative_input;
	}

	int get_iteration_count() const
	{
		return iterations_;
	}

	const std::string &get_iterative_input() const
	{
		return iterative_input_;
	}

	Texture::Interpolation get_interpolation(const std::string &id) const
	{
		auto it = interpolation_.find(id);
		return it == interpolation_.end() ? Texture::k_default_interpolation :
											it->second;
	}

	const std::map<std::string, Texture::Interpolation> &
	get_interpolation_map() const
	{
		return interpolation_;
	}

	void set_interpolation(const NodeInput &input, Texture::Interpolation interp)
	{
		interpolation_[input.input()] = interp;
	}

	void set_interpolation(const std::string &id, Texture::Interpolation interp)
	{
		interpolation_[id] = interp;
	}

	void set_vertex_coordinates(const std::vector<float> &vertex_coords)
	{
		vertex_overrides_ = vertex_coords;
	}

	const std::vector<float> &get_vertex_coordinates()
	{
		return vertex_overrides_;
	}

private:
	std::string shader_id_;

	int iterations_;

	std::string iterative_input_;

	std::map<std::string, Texture::Interpolation> interpolation_;

	std::vector<float> vertex_overrides_;
};

}

#endif // OAK_SHADERJOB_H
