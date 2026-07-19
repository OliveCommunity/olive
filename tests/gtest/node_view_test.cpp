#include <gtest/gtest.h>

#include <QSignalSpy>

#include "node/generator/solid/solid.h"
#include "node/project.h"
#include "widget/nodeview/nodeview.h"

using namespace olive;

class NodeViewTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::SetUpDefaultConfig();

		project_ = std::make_unique<Project>();
		project_->Initialize();
	}

	std::unique_ptr<Project> project_;
};

TEST_F(NodeViewTest, ConstructionCreatesEmptyView)
{
	NodeView view;
	EXPECT_TRUE(view.GetContexts().isEmpty());
	EXPECT_FALSE(view.IsGroupOverlay());
}

TEST_F(NodeViewTest, SetContextsUpdatesContextList)
{
	auto *solid = new SolidGenerator();
	solid->setParent(project_.get());

	NodeView view;
	view.SetContexts({ solid });

	EXPECT_EQ(view.GetContexts().size(), 1);
	EXPECT_EQ(view.GetContexts().first(), solid);
}

TEST_F(NodeViewTest, ShowSelectedNodeInParamEditorNoSelectionIsNoOp)
{
	NodeView view;

	QSignalSpy changed_with_ctx_spy(
		&view, &NodeView::NodeSelectionChangedWithContexts);

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
	view.SetContexts({ solid });
	EXPECT_FALSE(view.GetContexts().isEmpty());

	view.ClearGraph();
	EXPECT_TRUE(view.GetContexts().isEmpty());
}
