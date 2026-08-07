/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#ifndef OAK_NODE_TESTS_TESTNODE_H
#define OAK_NODE_TESTS_TESTNODE_H

// Shared test fixture node: a minimal concrete olive::Node subclass with
// one input per POD-carrying value type, so the C API tests can exercise
// value mapping without depending on NodeFactory.

#include "../c_api/nodehandle.h"
#include "../src/node.h"

namespace oaknode_test
{

class TestNode : public olive::Node {
public:
	static const char *k_id;

	TestNode()
	{
		add_input("float_in", olive::NodeValue::k_float, 0.0);
		add_input("int_in", olive::NodeValue::k_int, int64_t(0));
		add_input("text_in", olive::NodeValue::k_text, std::string());
		add_input("color_in", olive::NodeValue::k_color, olive::core::Color());
		add_input("vec2_in", olive::NodeValue::k_vec2, olive::Vector2D());
		add_input("rational_in", olive::NodeValue::k_rational,
				  olive::Variant::from_value(olive::core::Rational(0, 1)));
	}

	virtual ~TestNode() override
	{
		disconnect_all();
	}

	virtual olive::Node *copy() const override
	{
		return new TestNode();
	}

	virtual std::string name() const override
	{
		return "Test Node";
	}

	virtual std::string id() const override
	{
		return k_id;
	}

	virtual std::vector<CategoryID> category() const override
	{
		return { k_category_filter };
	}
};

inline const char *TestNode::k_id = "org.oak.TestNode";

/**
 * @brief Borrowed handle to a test-owned node; releasing it only releases
 * the handle, never the node.
 */
inline OakNodeNode as_handle(olive::Node *node)
{
	return oaknode_c_api::make_handle<OakNodeNode>(
		node, false, &oaknode_c_api::delete_as<olive::Node>);
}

/**
 * @brief Identity comparison for handles: two handles refer to the same
 * node when they wrap the same native object.
 */
inline bool same_node(OakNodeNode a, olive::Node *node)
{
	return oaknode_c_api::to_native<olive::Node>(a) == node;
}

}

#endif // OAK_NODE_TESTS_TESTNODE_H
