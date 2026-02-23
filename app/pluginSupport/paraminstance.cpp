/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

*/

#include "paraminstance.h"

#include "OlivePluginInstance.h"

namespace olive
{
namespace plugin
{
void SubmitUndoCommand(const std::shared_ptr<PluginNode> &node,
					   UndoCommand *command, const QString &label)
{
	if (!command) {
		return;
	}

	if (node) {
		auto *instance = node->getPluginInstance();
		auto *olive_instance =
			dynamic_cast<OlivePluginInstance *>(instance);
		if (olive_instance) {
			olive_instance->SubmitUndoCommand(command, label);
			return;
		}
	}

	Core::instance()->undo_stack()->push(command, label);
}
}
}
