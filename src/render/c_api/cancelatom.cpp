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

#include "../../../include/render/cancelatom.h"

#include <atomic>
#include <cstdint>

#include "alivecount.h"

#include "cancelatom.h"

namespace
{

/**
 * @brief Heap box behind every OakCancelAtom's ctx pointer.
 *
 * Holds the wrapped CancelAtom plus its atomic reference count. addref
 * and release are emitted in this translation unit so the function
 * pointers stored in a handle always run code from the DLL that created
 * the object.
 */
struct CancelAtomBox {
	olive::CancelAtom impl;
	std::atomic<uint32_t> refs;

	CancelAtomBox()
		: refs(1)
	{
	}
};

CancelAtomBox *box(OakCancelAtom atom)
{
	return static_cast<CancelAtomBox *>(atom.ctx);
}

olive::CancelAtom *impl(OakCancelAtom atom)
{
	auto *b = box(atom);
	return b ? &b->impl : nullptr;
}

/**
 * @brief Handle addref thunk: atomically increments the count.
 */
void cancel_atom_addref(void *ctx)
{
	auto *b = static_cast<CancelAtomBox *>(ctx);
	if (b)
		b->refs.fetch_add(1, std::memory_order_relaxed);
}

/**
 * @brief Handle release thunk: decrements the count, destroys at zero.
 */
void cancel_atom_release(void *ctx)
{
	auto *b = static_cast<CancelAtomBox *>(ctx);
	if (b && b->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		delete b;
		oakrender_c_api::alive_dec();
	}
}

} // namespace

OakCancelAtom oakrender_cancelatom_init(void)
{
	OakCancelAtom h = {};
	try {
		h.ctx = new CancelAtomBox();
	} catch (...) {
		h.ctx = nullptr;
	}
	h.addref = &cancel_atom_addref;
	h.release = &cancel_atom_release;
	h.abi_version = OAKRENDER_ABI_VERSION;
	if (h.ctx)
		oakrender_c_api::alive_inc();
	return h;
}

void oakrender_cancelatom_free(OakCancelAtom *atom)
{
	if (!atom || !atom->ctx || !atom->release)
		return;
	atom->release(atom->ctx);
	atom->ctx = nullptr;
}

int oakrender_cancelatom_cancel(OakCancelAtom atom)
{
	auto *c = impl(atom);
	if (!c)
		return OAKRENDER_E_INVALID;
	c->cancel();
	return OAKRENDER_OK;
}

int oakrender_cancelatom_is_cancelled(OakCancelAtom atom, int *cancelled)
{
	auto *c = impl(atom);
	if (!c || !cancelled)
		return OAKRENDER_E_INVALID;
	*cancelled = c->is_cancelled() ? 1 : 0;
	return OAKRENDER_OK;
}

int oakrender_cancelatom_heard_cancel(OakCancelAtom atom, int *heard)
{
	auto *c = impl(atom);
	if (!c || !heard)
		return OAKRENDER_E_INVALID;
	*heard = c->heard_cancel() ? 1 : 0;
	return OAKRENDER_OK;
}
