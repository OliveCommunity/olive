/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef OAK_NODEGROUP_H
#define OAK_NODEGROUP_H

#include <string>
#include <utility>
#include <vector>

#include "node.h"

namespace olive
{

class NodeGroup : public Node {
public:
	NodeGroup();

	NODE_DEFAULT_FUNCTIONS(NodeGroup)

	virtual std::string name() const override;
	virtual std::string id() const override;
	virtual std::vector<CategoryID> category() const override;
	virtual std::string description() const override;

	virtual void retranslate() override;

	virtual bool load_custom(XmlStreamReader *reader,
							SerializedData *data) override;
	virtual void save_custom(XmlStreamWriter *writer) const override;
	virtual void PostLoadEvent(SerializedData *data) override;

	std::string add_input_passthrough(const NodeInput &input,
								const std::string &force_id = std::string());

	void remove_input_passthrough(const NodeInput &input);

	Node *get_output_passthrough() const
	{
		return output_passthrough_;
	}

	void set_output_passthrough(Node *node);

	using InputPassthrough = std::pair<std::string, NodeInput>;
	using InputPassthroughs = std::vector<InputPassthrough>;
	const InputPassthroughs &get_input_passthroughs() const
	{
		return input_passthroughs_;
	}

	bool contains_input_passthrough(const NodeInput &input) const;

	virtual std::string get_input_name(const std::string &id) const override;

	static NodeInput resolve_input(NodeInput input);
	static bool get_inner(NodeInput *input);

	std::string get_id_of_passthrough(const NodeInput &input) const
	{
		for (auto it = input_passthroughs_.cbegin();
			 it != input_passthroughs_.cend(); it++) {
			if (it->second == input) {
				return it->first;
			}
		}
		return std::string();
	}

	NodeInput get_input_from_id(const std::string &id) const
	{
		for (auto it = input_passthroughs_.cbegin();
			 it != input_passthroughs_.cend(); it++) {
			if (it->first == id) {
				return it->second;
			}
		}
		return NodeInput();
	}

private:
	InputPassthroughs input_passthroughs_;

	Node *output_passthrough_;
};

class NodeGroupAddInputPassthrough : public UndoCommand {
public:
	NodeGroupAddInputPassthrough(NodeGroup *group, const NodeInput &input,
								 const std::string &force_id = std::string())
		: group_(group)
		, input_(input)
		, actually_added_(false)
	{
		// NOTE: upstream Olive never stored force_id here (force_id_ stays
		// empty); preserved as-is for behavior neutrality.
		(void) force_id;
	}

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	NodeGroup *group_;

	NodeInput input_;

	std::string force_id_;

	bool actually_added_;
};

class NodeGroupSetOutputPassthrough : public UndoCommand {
public:
	NodeGroupSetOutputPassthrough(NodeGroup *group, Node *output)
		: group_(group)
		, new_output_(output)
	{
	}

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	NodeGroup *group_;

	Node *new_output_;
	Node *old_output_;
};

}

#endif // OAK_NODEGROUP_H
