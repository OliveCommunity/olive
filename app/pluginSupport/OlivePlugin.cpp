/*
 * Olive Community Edition - Non-Linear Video Editor
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
 */
#include "OlivePlugin.h"

#include <utility>
olive::plugin::OlivePlugin::~OlivePlugin()
{
	for (void *p:mems) {
		free(p);
	}
}
void olive::plugin::OlivePlugin::setProp(QString key, int index, Value val)
{
	props[key][index]=std::move(val);
}

OfxStatus olive::plugin::OlivePlugin::getProp(QString key, int index, Value &val)
{
	if (props.contains(key)) {
		if (props[key].contains(index)) {
			val = props[key][index];
			return kOfxStatOK;
		}
		return kOfxStatErrBadIndex;
	}
	return kOfxStatErrUnknown;
}

