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

#include "node/dragger.h"

#include <string>

#include "inputdragger.h"
#include "node.h"
#include "param.h"
#include "undocommand.h"

#include "nodehandle.h"
#include "valueconvert.h"

using oaknode_c_api::delete_as;
using oaknode_c_api::make_handle;
using oaknode_c_api::to_native;

namespace
{

/**
 * @brief The object behind an OakNodeDragger handle: the wrapped
 * NodeInputDragger plus the node/input the drag targets (needed before
 * start() to validate the input and to map POD values to the input's
 * declared type during drag()).
 */
struct DraggerImpl {
	DraggerImpl(olive::Node *node, const std::string &input_id, int element)
		: dragger()
		, node(node)
		, input_id(input_id)
		, element(element)
	{
	}

	olive::NodeInputDragger dragger;
	olive::Node *node;
	std::string input_id;
	int element;
};

}

OakNodeDragger oaknode_dragger_create(OakNodeNode node, const char *input_id,
									  int element, int track)
{
	olive::Node *n = to_native<olive::Node>(node);
	(void)track; // start() establishes the actual drag track
	if (!n || !input_id || !n->has_input_with_id(input_id)) {
		return OakNodeDragger{};
	}

	try {
		auto *impl = new (std::nothrow) DraggerImpl(n, input_id, element);
		return make_handle<OakNodeDragger>(
			impl, true, delete_as<DraggerImpl>);
	} catch (...) {
		return OakNodeDragger{};
	}
}

int oaknode_dragger_start(OakNodeDragger dragger, int64_t time_num,
						  int64_t time_den, int track,
						  int insert_on_all_tracks)
{
	DraggerImpl *d = to_native<DraggerImpl>(dragger);
	if (!d) {
		return OAKNODE_E_INVALID;
	}
	if (d->dragger.is_started()) {
		return OAKNODE_E_STATE;
	}
	if (track < 0) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::NodeKeyframeTrackReference reference(
			olive::NodeInput(d->node, d->input_id, d->element), track);
		d->dragger.start(reference,
						 olive::core::Rational(static_cast<int>(time_num),
											   static_cast<int>(time_den)),
						 insert_on_all_tracks != 0);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_dragger_drag(OakNodeDragger dragger, const oaknode_value *value)
{
	DraggerImpl *d = to_native<DraggerImpl>(dragger);
	if (!d || !value) {
		return OAKNODE_E_INVALID;
	}
	if (!d->dragger.is_started()) {
		return OAKNODE_E_STATE;
	}

	try {
		const olive::NodeValue::Type declared =
			d->node->get_input_data_type(d->input_id);
		olive::Variant variant;
		if (!oaknode_c_api::component_from_value(value, declared, 0,
												 &variant)) {
			return OAKNODE_E_INVALID;
		}
		d->dragger.drag(variant);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_dragger_end(OakNodeDragger dragger, OakUndoCommand *out_command)
{
	DraggerImpl *d = to_native<DraggerImpl>(dragger);
	if (!d || !out_command) {
		return OAKNODE_E_INVALID;
	}
	if (!d->dragger.is_started()) {
		return OAKNODE_E_STATE;
	}

	try {
		auto *multi = new olive::MultiUndoCommand();
		d->dragger.end(multi);
		*out_command = oaknode_c_api::wrap_command(multi);
		return out_command->ctx ? OAKNODE_OK : OAKNODE_E_NOMEM;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_dragger_is_started(OakNodeDragger dragger, int *out_started)
{
	DraggerImpl *d = to_native<DraggerImpl>(dragger);
	if (!d || !out_started) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_started = d->dragger.is_started() ? 1 : 0;
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

void oaknode_dragger_free(OakNodeDragger *dragger)
{
	try {
		oaknode_c_api::free_handle(dragger);
	} catch (...) {
	}
}
