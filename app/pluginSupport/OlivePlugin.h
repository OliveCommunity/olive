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

#ifndef OLIVE_PLUGIN_H
#define OLIVE_PLUGIN_H
#include "ofxCore.h"

#include <cstdint>
#include <QString>
#include <QMap>
#include <list>
namespace olive
{
namespace plugin
{
class Value {
private:
	enum Type {
		FLOAT, STRING, INT, NONE
	};
	int64_t valueInt=0;
	double valueDouble=0;
	QString valueString;
	Type type=NONE;

public:

	Value()=default;
	Value(int64_t val)
	{
		valueInt=val;
		type=INT;
	}
	Value(double val)
	{
		valueDouble=val;
		type=FLOAT;
	}
	Value(QString val)
	{
		valueString=val;
		type=STRING;
	}
	void setValue(int64_t val)
	{
		valueInt=val;
		type=INT;
	}
	void setValue(double val)
	{
		valueDouble=val;
		type=FLOAT;
	}
	void setValue(QString val)
	{
		valueString=val;
		type=STRING;
	}
	void setValue(nullptr_t val)
	{
		valueInt=0;
		valueDouble=0;
		valueString=QString();
		type=NONE;
	}
	int64_t getInt() const
	{
		return valueInt;
	}
	double getFloat() const
	{
		return valueDouble;
	}
	QString getString()
	{
		return valueString;
	}
	bool isInt()
	{
		return type==INT;
	}
	bool isFloat()
	{
		return type==FLOAT;
	}
	bool isString()
	{
		return type==STRING;
	}
	bool isNull()
	{
		return type==NONE;
	}
};
struct plugin_mem_ptr {
	void *mem;
	int32_t count=0;
};

class OlivePlugin {
public:
	OlivePlugin()=default;
	~OlivePlugin();
	void setProp(QString key, int index, Value val);
	OfxStatus getProp(QString key, int index, Value &val);
	void addMem(void *mem)
	{
		mems.emplace_back(mem);
	}
private:
	uint64_t id;
	QString name;
	QString version;
	QMap<QString, QMap<int, Value>> props;
	std::list<void *> mems;
};
}
}
struct OfxPropertySetStruct {
	olive::plugin::OlivePlugin* plugin;
};
#endif //OLIVE_PLUGIN_H
