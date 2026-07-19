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

#include <QMatrix4x4>
#include <QVector>

#include "acceleratedjob.h"
#include "render/texture.h"

namespace olive
{

class ShaderJob : public AcceleratedJob {
public:
	ShaderJob()
	{
		iterations_ = 1;
		iterative_input_ = nullptr;
	}

	ShaderJob(const NodeValueRow &row)
		: ShaderJob()
	{
		insert(row);
	}

	const QString &get_shader_id() const
	{
		return shader_id_;
	}

	void set_shader_id(const QString &id)
	{
		shader_id_ = id;
	}

	void set_iterations(int iterations, const NodeInput &iterative_input)
	{
		set_iterations(iterations, iterative_input.input());
	}

	void set_iterations(int iterations, const QString &iterative_input)
	{
		iterations_ = iterations;
		iterative_input_ = iterative_input;
	}

	int get_iteration_count() const
	{
		return iterations_;
	}

	const QString &get_iterative_input() const
	{
		return iterative_input_;
	}

	Texture::Interpolation get_interpolation(const QString &id) const
	{
		return interpolation_.value(id, Texture::k_default_interpolation);
	}

	const QHash<QString, Texture::Interpolation> &get_interpolation_map() const
	{
		return interpolation_;
	}

	void set_interpolation(const NodeInput &input, Texture::Interpolation interp)
	{
		interpolation_.insert(input.input(), interp);
	}

	void set_interpolation(const QString &id, Texture::Interpolation interp)
	{
		interpolation_.insert(id, interp);
	}

	void set_vertex_coordinates(const QVector<float> &vertex_coords)
	{
		vertex_overrides_ = vertex_coords;
	}

	const QVector<float> &get_vertex_coordinates()
	{
		return vertex_overrides_;
	}

private:
	QString shader_id_;

	int iterations_;

	QString iterative_input_;

	QHash<QString, Texture::Interpolation> interpolation_;

	QVector<float> vertex_overrides_;
};

}

#endif // OAK_SHADERJOB_H
