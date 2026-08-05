#pragma once
// Transitional stub for engine/config/config.h (still Qt-based, config 未拆分).
// Union of the src/node/transition stub and the render-facing surface:
// keeps the OAK_CONFIG* call shape; values are inert defaults until the
// config milestone wires the real store. operator[] returns a stored
// reference so writes (lutlibrary) compile; nothing is persisted. 只增不删。
#include <map>
#include <string>
#include "variant.h"
#ifndef QStringLiteral
#define QStringLiteral(x) x
#endif
namespace olive {
class ConfigValue : public olive::Variant {
public:
	ConfigValue() = default;
	ConfigValue(const std::string &s) : olive::Variant(s) {}
	ConfigValue &operator=(const std::string &s)
	{
		olive::Variant::operator=(olive::Variant(s));
		return *this;
	}
	bool toBool() const { return to_bool(); }
	int toInt() const { return to_int(); }
	uint64_t toULongLong() const { return to_u_long_long(); }
	// rendermanager reads the graphics backend name through this
	std::string toString() const { return to_string(); }
	std::vector<std::string> toStringList() const { return to_string_list(); }
};
class Config {
public:
	static Config &current() { static Config c; return c; }
	ConfigValue &operator[](const std::string &key) { return values_[key]; }
private:
	std::map<std::string, ConfigValue> values_;
};
}
#ifndef OAK_CONFIG
#define OAK_CONFIG(x) Config::current()[x]
#endif
#ifndef OAK_CONFIG_STR
#define OAK_CONFIG_STR(x) Config::current()[x]
#endif
