#pragma once
#include <string>
#include "variant.h"
#ifndef QStringLiteral
#define QStringLiteral(x) x
#endif
namespace olive {
class ConfigValue : public olive::Variant {
public:
	ConfigValue() = default;
	bool toBool() const { return false; }
	int toInt() const { return 0; }
};
class Config {
public:
	static Config &current() { static Config c; return c; }
	ConfigValue operator[](const std::string &) const { return ConfigValue(); }
};
}
#ifndef OAK_CONFIG
#define OAK_CONFIG(x) Config::current()[x]
#endif
#ifndef OAK_CONFIG_STR
#define OAK_CONFIG_STR(x) Config::current()[x]
#endif
