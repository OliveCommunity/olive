//
// Created by katherinesolar on 25-8-3.
//

#include "Plugin.h"
QString olive::plugin::PluginNode::Name() const
{
	if (methods.name) {
		char buffer[64]={0};
		bool ok=methods.name(buffer, 64);
		if (ok) {
			return QString(buffer);
		}
	}
	return "";
}

QString olive::plugin::PluginNode::Description() const
{
	if (methods.description) {
		char buffer[1024] = { 0 };
		bool ok = methods.description(buffer, 1024);
		if (ok) {
			return QString(buffer);
		}
	}
	return "";
}
void olive::plugin::PluginNode::Value(const NodeValueRow &value,
									  const NodeGlobals &globals,
									  NodeValueTable *table) const
{
	methods.value(&node_ctx_functions);
}

QString olive::plugin::PluginNode::id() const
{
	if (methods.id) {
		char buffer[64]={0};
		bool ok=methods.id(buffer, 64);
		if (ok) {
			return QString(buffer);
		}
	}
	return "";
}