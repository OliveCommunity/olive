#include <gtest/gtest.h>

#include <QSignalSpy>

#include "node/generator/solid/solid.h"
#include "node/project.h"
#include "widget/nodeview/nodeview.h"

using namespace olive;

namespace
{

// Helper: wrap an engine Node* as oak::Node for the C ABI widget interface
inline oak::Node to_oak(olive::Node *n)
{
	return oak::Node(reinterpret_cast<OakEngineNode *>(n));
}

} // namespace

class NodeViewTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();

		project_ = std::make_unique<Project>();
		project_->initialize();
	}

	std::unique_ptr<Project> project_;
};

TEST_F(NodeViewTest, ConstructionCreatesEmptyView)
{
	NodeView view;
	EXPECT_TRUE(view.get_contexts().isEmpty());
	EXPECT_FALSE(view.is_group_overlay());
}

TEST_F(NodeViewTest, SetContextsUpdatesContextList)
{
	auto *solid = new SolidGenerator();
	solid->setParent(project_.get());

	NodeView view;
	view.set_contexts({ to_oak(solid) });

	EXPECT_EQ(view.get_contexts().size(), 1);
	EXPECT_EQ(view.get_contexts().first(), to_oak(solid));
}

TEST_F(NodeViewTest, ShowSelectedNodeInParamEditorNoSelectionIsNoOp)
{
	NodeView view;

	QSignalSpy changed_with_ctx_spy(
		&view, &NodeView::node_selection_changed_with_contexts);

	// The action must exist; silently skipping the trigger would make this
	// test pass without exercising anything
	QAction *show_params_action = nullptr;
	foreach (QAction *action, view.actions()) {
		if (action->property("id").toString() ==
			QStringLiteral("shownodeparams")) {
			show_params_action = action;
			break;
		}
	}
	ASSERT_NE(show_params_action, nullptr)
		<< "NodeView has no action with id 'shownodeparams'";

	// With nothing selected, triggering it must not emit a selection change
	show_params_action->trigger();
	EXPECT_EQ(changed_with_ctx_spy.count(), 0);
}

TEST_F(NodeViewTest, ClearGraphRemovesContexts)
{
	auto *solid = new SolidGenerator();
	solid->setParent(project_.get());

	NodeView view;
	view.set_contexts({ to_oak(solid) });
	EXPECT_FALSE(view.get_contexts().isEmpty());

	view.clear_graph();
	EXPECT_TRUE(view.get_contexts().isEmpty());
}
