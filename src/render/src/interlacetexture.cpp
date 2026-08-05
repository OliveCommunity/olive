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

#include "renderer.h"

#include "filefunctions.h"
#include "value.h"
#include "render/job/shaderjob.h"

namespace olive
{

TexturePtr Renderer::interlace_texture(TexturePtr top, TexturePtr bottom,
									  const VideoParams &params)
{
	color_cache_mutex_.lock();
	if (interlace_texture_.is_null()) {
		interlace_texture_ =
			create_native_shader(ShaderCode(FileFunctions::read_file_as_string(
				":/shaders/interlace.frag")));
	}
	color_cache_mutex_.unlock();

	ShaderJob job;
	job.insert("top_tex_in",
			   NodeValue(NodeValue::k_texture, Variant::from_value(top)));
	job.insert("bottom_tex_in",
			   NodeValue(NodeValue::k_texture, Variant::from_value(bottom)));
	job.insert("resolution_in",
			   NodeValue(NodeValue::k_vec2,
						 Vector2D(params.effective_width(),
								  params.effective_height())));

	TexturePtr output = create_texture(params);

	blit_to_texture(interlace_texture_, job, output.get());

	return output;
}

}
