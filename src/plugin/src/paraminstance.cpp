/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "paraminstance.h"

#include <map>

#include "oliveplugininstance.h"

namespace olive
{
namespace plugin
{
namespace
{

/**
 * @brief Node-identity → plugin instance registry, maintained by
 *        OlivePluginInstance::set_node_handle()
 */
std::map<uintptr_t, OlivePluginInstance *> &instance_registry()
{
	static std::map<uintptr_t, OlivePluginInstance *> registry;
	return registry;
}

} // namespace

void register_node_instance(uintptr_t identity, OlivePluginInstance *instance)
{
	if (instance) {
		instance_registry()[identity] = instance;
	} else {
		instance_registry().erase(identity);
	}
}

void submit_undo_command(OakNodeNode node, OakUndoCommand command,
						 const std::string &label)
{
	if (!command.ctx) {
		return;
	}

	if (node.ctx) {
		auto it = instance_registry().find(oaknode_node_identity(node));
		if (it != instance_registry().end() && it->second) {
			it->second->submit_undo_command(command, label);
			return;
		}
	}

	// No instance bound: run immediately and release
	oakundo_command_redo_now(command);
	oakundo_command_free(&command);
}

}
}
