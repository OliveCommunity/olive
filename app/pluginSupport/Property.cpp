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
#include "node/plugins/Plugin.h"

#include <ofxProperty.h>

OfxStatus propSetPointer(OfxPropertySetHandle properties,
	const char *property, int index, void *value)
{
	olive::plugin::Value val;
	if (properties->plugin) {
		OfxStatus status= properties->plugin->getProp(property, index, val);
		if (val.isInt()) {
			value=calloc(1,sizeof(long));
			*(int64_t*)value=val.getInt();
			properties->plugin->addMem(value);
		}
		else if (val.isFloat()) {
			value=calloc(1,sizeof(double));
			*(double*)value=val.getFloat();
			properties->plugin->addMem(value);
		}
		else if (val.isString()) {
			QString str=val.getString();
			value=(void *)calloc(str.size()+1, sizeof(char));
			memcpy(value, str.data(), str.size());
			properties->plugin->addMem(value);
		}
		return status;
	}
	return kOfxStatErrBadHandle;

}