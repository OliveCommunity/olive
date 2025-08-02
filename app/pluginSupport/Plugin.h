//
// Created by katherinesolar on 25-8-3.
//

#ifndef PLUGIN_H
#define PLUGIN_H
#include <map>
#include <string>
#include <vector>
namespace olive
{
typedef int(*button_callback_t)();
namespace plugin
{
struct Button {
	std::string name;
	button_callback_t callback;
};

struct Menu {
	std::string name;
	std::map<std::string, std::vector<Button>> buttons;
};
struct Plugin {
	std::string name;
	std::string version;
	std::string author;
	std::vector<Menu> menus;
};
}
class Plugin {
private:
	std::vector<plugin::Plugin> plugins_;
};
}


#endif //PLUGIN_H
