//
// Created by katherinesolar on 25-8-3.
//

#ifndef PLUGIN_H
#define PLUGIN_H
#include "olive_plugin.h"

#include <QMap>
#include <QString>
#include <vector>

extern struct olive_node_ctx_functions node_ctx_functions;
namespace olive
{
typedef int(*button_callback_t)();
typedef int(*node_callback_t)();
namespace plugin
{
struct Button {
	QString name;
	button_callback_t callback;
};

struct Menu {
	QString name;
	QMap<QString, QList<Button>> buttons;
};
struct Node {
	QString name;
	QString shortName;
	QString id;
	QString description;
	int categoryId;
	QString categoryName;

};
struct Plugin {
	QString name;
	QString version;
	QString author;
	QList<Menu> menus;
	QList<Node> nodes;
};
}
class Plugin {
private:
	std::vector<plugin::Plugin> plugins_;
};
}


#endif //PLUGIN_H
