//
// Created by katherinesolar on 25-8-3.
//

#ifndef PLUGIN_H
#define PLUGIN_H
#include "node/node.h"
#include "pluginSupport/Plugin.h"
#include "pluginSupport/olive_plugin.h"
namespace olive
{
namespace plugin
{

class PluginNode : public olive::Node{
public:
	PluginNode():Node()
	{
		memset(&methods, 0, sizeof(olive_node_methods));
	};
	explicit PluginNode(struct olive_node_methods methods):Node()
	{
		this->methods=methods;
	}
	void setMethods(struct olive_node_methods methods)
	{
		this->methods=methods;
	}
	QString Name() const override;
	QString id() const override;
	QVector<CategoryID> Category() const override;
	QString Description() const override;
	void Value(const NodeValueRow &value,
		const NodeGlobals &globals, NodeValueTable *table) const override;
private:
	struct olive_node_methods methods;

};

}
}


#endif //PLUGIN_H
