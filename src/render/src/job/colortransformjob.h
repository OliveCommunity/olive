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

#ifndef OAK_COLORTRANSFORMJOB_H
#define OAK_COLORTRANSFORMJOB_H

#include <cassert>
#include <string>

#include "acceleratedjob.h"
#include "alphaassoc.h"
#include "colorprocessor.h"
#include "mathtypes.h"
#include "texture.h"

namespace olive
{

class Node;

class ColorTransformJob : public AcceleratedJob {
public:
	ColorTransformJob()
	{
		processor_ = nullptr;
		custom_shader_src_ = nullptr;
		input_alpha_association_ = k_alpha_none;
		clear_destination_ = true;
		force_opaque_ = false;
	}

	ColorTransformJob(const NodeValueRow &row)
		: ColorTransformJob()
	{
		insert(row);
	}

	std::string id() const
	{
		if (id_.empty()) {
			return processor_->id();
		} else {
			return id_;
		}
	}

	void set_override_id(const std::string &id)
	{
		id_ = id;
	}

	const NodeValue &get_input_texture() const
	{
		return input_texture_;
	}
	void set_input_texture(const NodeValue &tex)
	{
		input_texture_ = tex;
	}
	void set_input_texture(TexturePtr tex)
	{
		assert(!tex->is_dummy());
		input_texture_ = NodeValue(NodeValue::k_texture, tex);
	}

	ColorProcessorPtr get_color_processor() const
	{
		return processor_;
	}
	void set_color_processor(ColorProcessorPtr p)
	{
		processor_ = p;
	}

	const AlphaAssociated &get_input_alpha_association() const
	{
		return input_alpha_association_;
	}
	void set_input_alpha_association(const AlphaAssociated &e)
	{
		input_alpha_association_ = e;
	}

	const Node *custom_shader_source() const
	{
		return custom_shader_src_;
	}
	const std::string &custom_shader_id() const
	{
		return custom_shader_id_;
	}
	void set_needs_custom_shader(const Node *node,
							   const std::string &id = std::string())
	{
		custom_shader_src_ = node;
		custom_shader_id_ = id;
	}

	bool is_clear_destination_enabled() const
	{
		return clear_destination_;
	}
	void set_clear_destination_enabled(bool e)
	{
		clear_destination_ = e;
	}

	const Matrix4x4 &get_transform_matrix() const
	{
		return matrix_;
	}
	void set_transform_matrix(const Matrix4x4 &m)
	{
		matrix_ = m;
	}

	const Matrix4x4 &get_crop_matrix() const
	{
		return crop_matrix_;
	}
	void set_crop_matrix(const Matrix4x4 &m)
	{
		crop_matrix_ = m;
	}

	const std::string &get_function_name() const
	{
		return function_name_;
	}
	void set_function_name(const std::string &function_name = std::string())
	{
		function_name_ = function_name;
	};

	bool get_force_opaque() const
	{
		return force_opaque_;
	}
	void set_force_opaque(bool e)
	{
		force_opaque_ = e;
	}

private:
	ColorProcessorPtr processor_;
	std::string id_;

	NodeValue input_texture_;

	const Node *custom_shader_src_;
	std::string custom_shader_id_;

	AlphaAssociated input_alpha_association_;

	bool clear_destination_;

	Matrix4x4 matrix_;

	Matrix4x4 crop_matrix_;

	std::string function_name_;

	bool force_opaque_;
};

}

#endif // OAK_COLORTRANSFORMJOB_H
