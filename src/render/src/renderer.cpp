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

#include <chrono>

namespace olive
{

static int64_t current_msecs_since_epoch()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::system_clock::now().time_since_epoch())
		.count();
}

Renderer::Renderer()
	: lifetime_(std::make_shared<RendererLifetime>())
	, owner_thread_(std::this_thread::get_id())
{
}

Renderer::~Renderer()
{
	destroyed_ = true;
	if (lifetime_) {
		lifetime_->alive = false;
	}
}

TexturePtr Renderer::create_texture(const VideoParams &params, const void *data,
								   int linesize)
{
	Variant v;

	if (use_texture_cache) {
		std::lock_guard<std::mutex> locker(texture_cache_lock_);
		for (auto it = texture_cache_.begin(); it != texture_cache_.end();
			 it++) {
			if (it->width == params.effective_width() &&
				it->height == params.effective_height() &&
				it->depth == params.effective_depth() &&
				it->format == params.format() &&
				it->channel_count == params.channel_count()) {
				v = it->handle;
				texture_cache_.erase(it);
				break;
			}
		}
	}

	if (v.is_null()) {
		v = create_native_texture(params.effective_width(),
								params.effective_height(),
								params.effective_depth(), params.format(),
								params.channel_count(), data, linesize);
	} else if (data) {
		upload_to_texture(v, params, data, linesize);
	} else {
		this->flush();
	}

	return create_texture_from_native_handle(v, params);
}

void Renderer::destroy_texture(Texture *texture)
{
	if (destroyed_) {
		return;
	}
	if (use_texture_cache) {
		// HACK: Dirty, dirty hack. OpenGL uses "contexts" to store all of its data, and each context
		//       can only be used by the thread that created it. However there are also "shared contexts"
		//       where assets from one context can be used in another. We use shared contexts so that
		//       textures rendered in the background can be displayed on the screen, travelling from
		//       a background thread to the main UI thread. However, when that texture is destroyed, it
		//       comes back here to be placed in the texture cache. But that leads to a race condition
		//       because it will call the background thread's renderer in the main thread. Since all
		//       assets are shared, we could technically just get the texture to call "destroy" in the
		//       viewer's renderer instance, but that would mean all textures would end up stranded
		//       there unusable by the background renderer, negating the very advantage of the texture
		//       cache in the first place. Therefore, we simply allow the thread calling to happen, and
		//       use mutexes to prevent race conditions.
		//
		//       Presumably Vulkan would not have this issue because it allows for application-wide
		//       instances and multithreading.
		texture_cache_lock_.lock();
		texture_cache_.push_back(
			{ texture->params().effective_width(),
			  texture->params().effective_height(),
			  texture->params().effective_depth(), texture->params().format(),
			  texture->params().channel_count(), texture->id(),
			  current_msecs_since_epoch() });
		texture_cache_lock_.unlock();

		if (called_on_owner_thread()) {
			clear_old_textures();
		}
	} else {
		destroy_native_texture(texture->id());
	}
}

Variant Renderer::get_default_shader()
{
	std::lock_guard<std::mutex> locker(color_cache_mutex_);

	if (default_shader_.is_null()) {
		default_shader_ = create_native_shader(ShaderCode());
	}

	return default_shader_;
}

void Renderer::destroy()
{
	if (!default_shader_.is_null()) {
		destroy_native_shader(default_shader_);
		default_shader_ = Variant();
	}

	{
		std::lock_guard<std::mutex> locker(color_cache_mutex_);

		// Destroy the cached native shaders explicitly. The LUT textures are
		// TexturePtrs whose destructors call DestroyTexture(), so the cache must
		// be cleared while the renderer is still alive for those to be honored.
		for (auto it = color_cache_.begin(); it != color_cache_.end(); it++) {
			if (!it->second.compiled_shader.is_null()) {
				destroy_native_shader(it->second.compiled_shader);
			}
		}
		color_cache_.clear();
	}

	if (!interlace_texture_.is_null()) {
		destroy_native_shader(interlace_texture_);
		interlace_texture_ = Variant();
	}

	for (auto it = texture_cache_.begin(); it != texture_cache_.end(); it++) {
		destroy_native_texture(it->handle);
	}
	texture_cache_.clear();

	destroyed_ = true;
	if (lifetime_) {
		lifetime_->alive = false;
	}

	destroy_internal();
}

TexturePtr Renderer::create_texture_from_native_handle(const Variant &v,
												   const VideoParams &params)
{
	if (v.is_null()) {
		return nullptr;
	}

	return std::make_shared<Texture>(this, v, params, lifetime_);
}

void Renderer::clear_old_textures()
{
	std::lock_guard<std::mutex> locker(texture_cache_lock_);

	for (auto it = texture_cache_.begin(); it != texture_cache_.end();) {
		if (it->accessed < current_msecs_since_epoch() - max_texture_life) {
			destroy_native_texture(it->handle);
			it = texture_cache_.erase(it);
		} else {
			it++;
		}
	}
}

} // namespace olive
