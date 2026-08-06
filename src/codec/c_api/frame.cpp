/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#include "codec/frame.h"

#include <atomic>

#include "frame.h"
#include "refcounted.h"

namespace
{

// Every OakFrame box holds an olive::FramePtr: frames created here own a
// fresh olive::Frame, decoder-produced frames alias the decoder's
// shared_ptr. Unifying the box type keeps the addref/release thunks and
// the impl recovery symmetric across all OakFrame handles.
olive::Frame *impl(void *ctx)
{
	auto *p = oakcodec::handle_impl<olive::FramePtr>(ctx);
	return p ? p->get() : nullptr;
}

} // namespace

namespace oakcodec
{

std::atomic<int> g_alive_count{0};

void alive_inc()
{
	g_alive_count.fetch_add(1, std::memory_order_relaxed);
}

void alive_dec()
{
	g_alive_count.fetch_sub(1, std::memory_order_relaxed);
}

} // namespace oakcodec

int oakcodec_debug_alive_count(void)
{
	return oakcodec::g_alive_count.load(std::memory_order_relaxed);
}

OakFrame oakcodec_frame_init(void)
{
	return oakcodec::make_handle<OakFrame>(olive::Frame::create());
}

OakFrame oakcodec_frame_init_with_params(OakVideoParams params)
{
	OakFrame h = oakcodec_frame_init();
	if (h.ctx) {
		impl(h.ctx)->set_video_params(params);
	}
	return h;
}

void oakcodec_frame_free(OakFrame *frame)
{
	oakcodec::free_handle(frame);
}

int oakcodec_frame_get_params(OakFrame frame, OakVideoParams *out)
{
	if (!frame.ctx || !out)
		return OAKCODEC_E_INVALID;
	*out = impl(frame.ctx)->video_params();
	return OAKCODEC_OK;
}

int oakcodec_frame_set_params(OakFrame frame, OakVideoParams params)
{
	if (!frame.ctx)
		return OAKCODEC_E_INVALID;
	impl(frame.ctx)->set_video_params(params);
	return OAKCODEC_OK;
}

int oakcodec_frame_allocate(OakFrame frame)
{
	if (!frame.ctx)
		return OAKCODEC_E_INVALID;
	if (!impl(frame.ctx)->allocate())
		return OAKCODEC_E_STATE;
	return OAKCODEC_OK;
}

int oakcodec_frame_is_allocated(OakFrame frame)
{
	if (!frame.ctx)
		return 0;
	return impl(frame.ctx)->is_allocated() ? 1 : 0;
}

void *oakcodec_frame_data(OakFrame frame)
{
	if (!frame.ctx)
		return nullptr;
	return impl(frame.ctx)->data();
}

const void *oakcodec_frame_const_data(OakFrame frame)
{
	if (!frame.ctx)
		return nullptr;
	return impl(frame.ctx)->const_data();
}

int oakcodec_frame_allocated_size(OakFrame frame)
{
	if (!frame.ctx)
		return 0;
	return impl(frame.ctx)->allocated_size();
}

int oakcodec_frame_linesize_bytes(OakFrame frame)
{
	if (!frame.ctx)
		return 0;
	return impl(frame.ctx)->linesize_bytes();
}

int oakcodec_frame_linesize_pixels(OakFrame frame)
{
	if (!frame.ctx)
		return 0;
	return impl(frame.ctx)->linesize_pixels();
}

int oakcodec_frame_width(OakFrame frame)
{
	if (!frame.ctx)
		return 0;
	return impl(frame.ctx)->width();
}

int oakcodec_frame_height(OakFrame frame)
{
	if (!frame.ctx)
		return 0;
	return impl(frame.ctx)->height();
}

int oakcodec_frame_format(OakFrame frame)
{
	if (!frame.ctx)
		return OAKCOMMON_PIXEL_FORMAT_INVALID;
	return impl(frame.ctx)->format();
}

int oakcodec_frame_channel_count(OakFrame frame)
{
	if (!frame.ctx)
		return 0;
	return impl(frame.ctx)->channel_count();
}

int oakcodec_frame_get_timestamp(OakFrame frame, int *numerator,
								 int *denominator)
{
	if (!frame.ctx || !numerator || !denominator)
		return OAKCODEC_E_INVALID;
	const olive::core::Rational &ts = impl(frame.ctx)->timestamp();
	*numerator = ts.numerator();
	*denominator = ts.denominator();
	return OAKCODEC_OK;
}

int oakcodec_frame_set_timestamp(OakFrame frame, int numerator,
								 int denominator)
{
	if (!frame.ctx)
		return OAKCODEC_E_INVALID;
	impl(frame.ctx)->set_timestamp(
		olive::core::Rational(numerator, denominator));
	return OAKCODEC_OK;
}
