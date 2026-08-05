#include <gtest/gtest.h>

#include <QGraphicsScene>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

#include "core.h"
#include "node/generator/solid/solid.h"
#include "node/math/math/math.h"
#include "node/project.h"
#include "widget/nodeview/nodeviewcommon.h"
#include "widget/nodeview/nodeviewcontext.h"
#include "widget/nodeview/nodeviewedge.h"
#include "widget/nodeview/nodeviewitem.h"
#include "widget/nodeview/nodeviewitemconnector.h"
#include "widget/nodeview/nodeviewminimap.h"
#include "widget/nodeview/nodeviewscene.h"
#include "widget/nodeview/nodeviewtoolbar.h"
#include "widget/nodeview/nodewidget.h"

namespace
{

using NVC = olive::NodeViewCommon;
using NVI = olive::NodeViewItem;

// NodeView (inside NodeWidget) is a HandMovableView, whose constructor
// connects to Core::instance(); the singleton is required but a MainWindow
// is not
void ensure_core()
{
	if (!olive::Core::instance()) {
		new olive::Core(); // intentionally leaked
	}
}

// Wrap an engine Node* as oak::Node for the C ABI widget interface
inline oak::Node to_oak(olive::Node *n)
{
	return oak::Node(reinterpret_cast<OakEngineNode *>(n));
}

// Number of inputs a NodeViewItem would create child items for when expanded
// (mirrors NodeViewItem::is_input_valid for non-plugin nodes)
int count_valid_inputs(olive::Node *node)
{
	oak::Node n = to_oak(node);
	int count = 0;
	const int input_count = n.input_count();
	for (int i = 0; i < input_count; i++) {
		oak::Input in(n.handle(), n.input_id(i));
		if (in.is_connectable() && !in.is_hidden()) {
			count++;
		}
	}
	return count;
}

// The direct child items of `item` that are themselves NodeViewItems (the
// per-input rows); connectors are a different class and are excluded
QList<NVI *> child_node_items(NVI *item)
{
	QList<NVI *> out;
	for (QGraphicsItem *c : item->childItems()) {
		if (auto *nvi = dynamic_cast<NVI *>(c)) {
			out.append(nvi);
		}
	}
	return out;
}

// The input (is_output()==false) or output connector of a NodeViewItem
olive::NodeViewItemConnector *find_connector(NVI *item, bool output)
{
	for (QGraphicsItem *c : item->childItems()) {
		if (auto *conn = dynamic_cast<olive::NodeViewItemConnector *>(c)) {
			if (conn->is_output() == output) {
				return conn;
			}
		}
	}
	return nullptr;
}

QPushButton *find_button_by_tooltip(QWidget *parent, const QString &tooltip)
{
	for (QPushButton *b : parent->findChildren<QPushButton *>()) {
		if (b->toolTip() == tooltip) {
			return b;
		}
	}
	return nullptr;
}

// NodeViewMiniMap is a QGraphicsView; QTest mouse delivery to the scroll-area
// frame is unreliable under the offscreen QPA, so expose the protected
// handlers and feed them crafted events directly (same pattern as
// ProbeHandView in widget_misc_test.cpp)
class ProbeMiniMap : public olive::NodeViewMiniMap {
public:
	explicit ProbeMiniMap(olive::NodeViewScene *scene)
		: olive::NodeViewMiniMap(scene)
	{
	}

	void pub_press(const QPoint &pos)
	{
		QMouseEvent e(QEvent::MouseButtonPress, QPointF(pos), QPointF(pos),
					  QPointF(pos), Qt::LeftButton, Qt::LeftButton,
					  Qt::NoModifier);
		mousePressEvent(&e);
	}

	void pub_release(const QPoint &pos)
	{
		QMouseEvent e(QEvent::MouseButtonRelease, QPointF(pos), QPointF(pos),
					  QPointF(pos), Qt::LeftButton, Qt::NoButton,
					  Qt::NoModifier);
		mouseReleaseEvent(&e);
	}
};

} // namespace

TEST(WidgetNodeViewCommon, FlowOrientationAndPredicates)
{
	EXPECT_EQ(NVC::get_flow_orientation(NVC::k_top_to_bottom), Qt::Vertical);
	EXPECT_EQ(NVC::get_flow_orientation(NVC::k_bottom_to_top), Qt::Vertical);
	EXPECT_EQ(NVC::get_flow_orientation(NVC::k_left_to_right), Qt::Horizontal);
	EXPECT_EQ(NVC::get_flow_orientation(NVC::k_right_to_left), Qt::Horizontal);
	// Anything else falls through to horizontal
	EXPECT_EQ(NVC::get_flow_orientation(NVC::k_invalid_direction), Qt::Horizontal);

	EXPECT_TRUE(NVC::is_flow_vertical(NVC::k_top_to_bottom));
	EXPECT_TRUE(NVC::is_flow_vertical(NVC::k_bottom_to_top));
	EXPECT_FALSE(NVC::is_flow_vertical(NVC::k_left_to_right));
	EXPECT_FALSE(NVC::is_flow_vertical(NVC::k_invalid_direction));

	EXPECT_TRUE(NVC::is_flow_horizontal(NVC::k_left_to_right));
	EXPECT_TRUE(NVC::is_flow_horizontal(NVC::k_right_to_left));
	EXPECT_FALSE(NVC::is_flow_horizontal(NVC::k_top_to_bottom));
	EXPECT_FALSE(NVC::is_flow_horizontal(NVC::k_invalid_direction));
}

TEST(WidgetNodeViewCommon, DirectionsAreOpposing)
{
	EXPECT_TRUE(NVC::directions_are_opposing(NVC::k_left_to_right, NVC::k_right_to_left));
	EXPECT_TRUE(NVC::directions_are_opposing(NVC::k_right_to_left, NVC::k_left_to_right));
	EXPECT_TRUE(NVC::directions_are_opposing(NVC::k_top_to_bottom, NVC::k_bottom_to_top));
	EXPECT_TRUE(NVC::directions_are_opposing(NVC::k_bottom_to_top, NVC::k_top_to_bottom));

	// Same direction, perpendicular, and invalid pairs are not opposing
	EXPECT_FALSE(NVC::directions_are_opposing(NVC::k_left_to_right, NVC::k_left_to_right));
	EXPECT_FALSE(NVC::directions_are_opposing(NVC::k_left_to_right, NVC::k_top_to_bottom));
	EXPECT_FALSE(NVC::directions_are_opposing(NVC::k_invalid_direction, NVC::k_right_to_left));
}

TEST(WidgetNodeViewItem, DefaultMetricsDeriveFromFontMetrics)
{
	const QFont f;
	const QFontMetrics fm(f);
	const int padding = fm.height() / 4;

	EXPECT_EQ(NVI::default_text_padding(), padding);
	EXPECT_EQ(NVI::default_item_height(), fm.height() + padding * 2);
	EXPECT_EQ(NVI::default_item_border(), fm.height() / 12);
	EXPECT_GT(NVI::default_item_width(), 0);
}

TEST(WidgetNodeViewItem, FlowPaddingDependsOnOrientation)
{
	const double w = NVI::default_item_width();
	const double h = NVI::default_item_height();

	EXPECT_DOUBLE_EQ(NVI::default_item_horizontal_padding(NVC::k_left_to_right), w * 1.5);
	EXPECT_DOUBLE_EQ(NVI::default_item_horizontal_padding(NVC::k_top_to_bottom), w * 1.25);
	EXPECT_DOUBLE_EQ(NVI::default_item_vertical_padding(NVC::k_left_to_right), h * 1.5);
	EXPECT_DOUBLE_EQ(NVI::default_item_vertical_padding(NVC::k_top_to_bottom), h * 2.0);
}

TEST(WidgetNodeViewItem, NodeScreenPointMappingPerDirection)
{
	const double hpad_h = NVI::default_item_horizontal_padding(NVC::k_left_to_right);
	const double vpad_h = NVI::default_item_vertical_padding(NVC::k_left_to_right);
	const double hpad_v = NVI::default_item_horizontal_padding(NVC::k_top_to_bottom);
	const double vpad_v = NVI::default_item_vertical_padding(NVC::k_top_to_bottom);

	const QPointF p(1, 2);

	// Left-to-right is the internal representation: no axis shuffling
	QPointF s = NVI::node_to_screen_point(p, NVC::k_left_to_right);
	EXPECT_DOUBLE_EQ(s.x(), hpad_h);
	EXPECT_DOUBLE_EQ(s.y(), 2 * vpad_h);

	// Right-to-left inverts X
	s = NVI::node_to_screen_point(p, NVC::k_right_to_left);
	EXPECT_DOUBLE_EQ(s.x(), -hpad_h);
	EXPECT_DOUBLE_EQ(s.y(), 2 * vpad_h);

	// Top-to-bottom swaps the axes
	s = NVI::node_to_screen_point(p, NVC::k_top_to_bottom);
	EXPECT_DOUBLE_EQ(s.x(), 2 * hpad_v);
	EXPECT_DOUBLE_EQ(s.y(), vpad_v);

	// Bottom-to-top swaps the axes and inverts Y
	s = NVI::node_to_screen_point(p, NVC::k_bottom_to_top);
	EXPECT_DOUBLE_EQ(s.x(), 2 * hpad_v);
	EXPECT_DOUBLE_EQ(s.y(), -vpad_v);
}

TEST(WidgetNodeViewItemConnector, ConstructionSetsOutputFlagAndColors)
{
	olive::NodeViewItemConnector in(false);
	EXPECT_FALSE(in.is_output());

	olive::NodeViewItemConnector out(true);
	EXPECT_TRUE(out.is_output());

	// Pen and brush use the palette text color; pen width is the item border
	const QColor text_color = qApp->palette().text().color();
	EXPECT_EQ(out.pen().color(), text_color);
	EXPECT_EQ(out.brush().color(), text_color);
	EXPECT_EQ(out.pen().width(), NVI::default_item_border());
}

TEST(WidgetNodeViewItemConnector, FlowDirectionShapesTriangle)
{
	const int triangle_sz = QFontMetricsF(QFont()).height() / 2;
	const int half = triangle_sz / 2;
	ASSERT_GT(half, 0);

	olive::NodeViewItemConnector conn(true);

	conn.set_flow_direction(NVC::k_left_to_right);
	QPolygonF p = conn.polygon();
	ASSERT_EQ(p.size(), 3);
	EXPECT_EQ(p.at(0), QPointF(0, -half));
	EXPECT_EQ(p.at(1), QPointF(half, 0)); // tip points right
	EXPECT_EQ(p.at(2), QPointF(0, half));

	conn.set_flow_direction(NVC::k_right_to_left);
	p = conn.polygon();
	ASSERT_EQ(p.size(), 3);
	EXPECT_EQ(p.at(1), QPointF(-half, 0)); // tip points left

	conn.set_flow_direction(NVC::k_top_to_bottom);
	p = conn.polygon();
	ASSERT_EQ(p.size(), 3);
	EXPECT_EQ(p.at(1), QPointF(0, half)); // tip points down

	conn.set_flow_direction(NVC::k_bottom_to_top);
	p = conn.polygon();
	ASSERT_EQ(p.size(), 3);
	EXPECT_EQ(p.at(1), QPointF(0, -half)); // tip points up
}

TEST(WidgetNodeViewItemConnector, BoundingRectExpandsPolygonByRadius)
{
	olive::NodeViewItemConnector conn(true);
	conn.set_flow_direction(NVC::k_left_to_right);

	const int radius = QFontMetrics(QFont()).height() / 2;
	const QRectF poly_bounds = conn.polygon().boundingRect();
	const QRectF bounds = conn.boundingRect();

	EXPECT_TRUE(bounds.contains(poly_bounds));
	EXPECT_DOUBLE_EQ(bounds.width(), poly_bounds.width() + 2 * radius);
	EXPECT_DOUBLE_EQ(bounds.height(), poly_bounds.height() + 2 * radius);
}

TEST(WidgetNodeViewEdge, DefaultConstructionState)
{
	olive::NodeViewEdge edge;

	EXPECT_EQ(edge.from_item(), nullptr);
	EXPECT_EQ(edge.to_item(), nullptr);
	EXPECT_FALSE(edge.is_connected());
	// Drawn behind the node items
	EXPECT_DOUBLE_EQ(edge.zValue(), -1.0);
	EXPECT_TRUE(edge.flags() & QGraphicsItem::ItemIsSelectable);
}

TEST(WidgetNodeViewEdge, StraightLinePathHasTwoEndpoints)
{
	olive::NodeViewEdge edge;
	edge.set_curved(false);
	edge.set_points(QPointF(0, 0), QPointF(100, 50));

	const QPainterPath path = edge.path();
	ASSERT_EQ(path.elementCount(), 2); // moveTo + lineTo
	EXPECT_EQ(path.elementAt(0).type, QPainterPath::MoveToElement);
	EXPECT_EQ(path.elementAt(1).type, QPainterPath::LineToElement);
	EXPECT_DOUBLE_EQ(path.elementAt(1).x, 100.0);
	EXPECT_DOUBLE_EQ(path.elementAt(1).y, 50.0);
}

TEST(WidgetNodeViewEdge, CurvedPathWithoutItemsFallsBackToHorizontal)
{
	// With no attached items the flow direction is unknown and the code falls
	// back to left-to-right: control points share the start/end Y and the
	// midpoint X
	olive::NodeViewEdge edge; // curved by default
	edge.set_points(QPointF(0, 0), QPointF(100, 50));

	const QPainterPath path = edge.path();
	ASSERT_EQ(path.elementCount(), 4); // moveTo + cubicTo (3 elements)
	EXPECT_EQ(path.elementAt(1).type, QPainterPath::CurveToElement);
	EXPECT_DOUBLE_EQ(path.elementAt(1).x, 50.0);
	EXPECT_DOUBLE_EQ(path.elementAt(1).y, 0.0);
	EXPECT_DOUBLE_EQ(path.elementAt(2).x, 50.0);
	EXPECT_DOUBLE_EQ(path.elementAt(2).y, 50.0);
	EXPECT_DOUBLE_EQ(path.elementAt(3).x, 100.0);
	EXPECT_DOUBLE_EQ(path.elementAt(3).y, 50.0);
}

TEST(WidgetNodeViewEdge, CurvedPathFollowsItemFlowDirection)
{
	olive::Project project;
	project.initialize();

	auto *a = new olive::SolidGenerator();
	a->setParent(&project);
	auto *b = new olive::MathNode();
	b->setParent(&project);

	NVI from_item(to_oak(a), oak::Node());
	NVI to_item(to_oak(b), oak::Node());

	// Vertical flow moves the control points onto the start/end X and the
	// midpoint Y instead
	from_item.set_flow_direction(NVC::k_top_to_bottom);
	to_item.set_flow_direction(NVC::k_top_to_bottom);

	olive::NodeViewEdge edge(to_oak(a),
							 oak::Input(to_oak(b).handle(), olive::MathNode::k_param_a_in),
							 &from_item, &to_item);
	edge.set_points(QPointF(0, 0), QPointF(40, 200));

	const QPainterPath path = edge.path();
	ASSERT_EQ(path.elementCount(), 4);
	EXPECT_DOUBLE_EQ(path.elementAt(1).x, 0.0);
	EXPECT_DOUBLE_EQ(path.elementAt(1).y, 100.0);
	EXPECT_DOUBLE_EQ(path.elementAt(2).x, 40.0);
	EXPECT_DOUBLE_EQ(path.elementAt(2).y, 100.0);
	EXPECT_DOUBLE_EQ(path.elementAt(3).x, 40.0);
	EXPECT_DOUBLE_EQ(path.elementAt(3).y, 200.0);

	// The item destructor asserts its edge list is empty; the edge destructor
	// unregisters itself, and the edge is destroyed first on the stack
}

TEST(WidgetNodeViewEdge, FullConstructorRegistersWithItems)
{
	olive::Project project;
	project.initialize();

	auto *a = new olive::SolidGenerator();
	a->setParent(&project);
	auto *b = new olive::MathNode();
	b->setParent(&project);

	NVI from_item(to_oak(a), oak::Node());
	NVI to_item(to_oak(b), oak::Node());

	const oak::Input input(to_oak(b).handle(), olive::MathNode::k_param_a_in);
	auto *edge = new olive::NodeViewEdge(to_oak(a), input, &from_item, &to_item);

	// A real edge is connected by definition
	EXPECT_TRUE(edge->is_connected());
	EXPECT_EQ(edge->from_item(), &from_item);
	EXPECT_EQ(edge->to_item(), &to_item);
	EXPECT_EQ(edge->output(), to_oak(a));
	EXPECT_EQ(edge->input(), input);

	// Both items track the edge
	EXPECT_EQ(from_item.edges().size(), 1);
	EXPECT_EQ(to_item.edges().size(), 1);
	EXPECT_EQ(from_item.edges().first(), edge);

	// Deleting the edge unregisters it from both items (the item destructor
	// asserts on a non-empty edge list)
	delete edge;
	EXPECT_TRUE(from_item.edges().isEmpty());
	EXPECT_TRUE(to_item.edges().isEmpty());
}

TEST(WidgetNodeViewEdge, SetFromToItemMovesRegistration)
{
	olive::Project project;
	project.initialize();

	auto *a = new olive::SolidGenerator();
	a->setParent(&project);
	auto *b = new olive::MathNode();
	b->setParent(&project);
	auto *c = new olive::MathNode();
	c->setParent(&project);

	NVI from_item(to_oak(a), oak::Node());
	NVI other_from(to_oak(c), oak::Node());
	NVI to_item(to_oak(b), oak::Node());

	auto *edge = new olive::NodeViewEdge(
		to_oak(a), oak::Input(to_oak(b).handle(), olive::MathNode::k_param_a_in),
		&from_item, &to_item);

	edge->set_from_item(&other_from);
	EXPECT_EQ(edge->from_item(), &other_from);
	EXPECT_TRUE(from_item.edges().isEmpty());
	EXPECT_EQ(other_from.edges().size(), 1);

	edge->set_to_item(&other_from);
	EXPECT_EQ(edge->to_item(), &other_from);
	EXPECT_TRUE(to_item.edges().isEmpty());
	EXPECT_EQ(other_from.edges().size(), 2);

	// set_connected round-trips and does not affect registration
	edge->set_connected(false);
	EXPECT_FALSE(edge->is_connected());
	edge->set_connected(true);
	EXPECT_TRUE(edge->is_connected());

	delete edge;
	EXPECT_TRUE(other_from.edges().isEmpty());
}

TEST(WidgetNodeViewItem, OutputItemConstructionDefaults)
{
	olive::Project project;
	project.initialize();

	auto *math = new olive::MathNode();
	math->setParent(&project);

	NVI item(to_oak(math), oak::Node());

	EXPECT_TRUE(item.is_output_item());
	EXPECT_EQ(item.get_node(), to_oak(math));
	EXPECT_TRUE(item.get_input().input_id().isEmpty());
	EXPECT_FALSE(item.is_expanded());
	EXPECT_FALSE(item.is_labelled_as_output_of_context());
	// The constructor installs a fallback flow direction
	EXPECT_EQ(item.get_flow_direction(), NVC::k_left_to_right);

	// The rect is one logical unit tall and contains the origin
	EXPECT_DOUBLE_EQ(item.rect().width(), NVI::default_item_width());
	EXPECT_DOUBLE_EQ(item.rect().height(), NVI::default_item_height());
	EXPECT_TRUE(item.rect().contains(QPointF(0, 0)));

	// Output items are interactive
	EXPECT_TRUE(item.flags() & QGraphicsItem::ItemIsMovable);
	EXPECT_TRUE(item.flags() & QGraphicsItem::ItemIsSelectable);
}

TEST(WidgetNodeViewItem, InputItemHidesOutputConnector)
{
	olive::Project project;
	project.initialize();

	auto *math = new olive::MathNode();
	math->setParent(&project);

	NVI item(to_oak(math), olive::MathNode::k_param_a_in, -1, oak::Node());

	EXPECT_FALSE(item.is_output_item());
	EXPECT_EQ(item.get_input().input_id(), olive::MathNode::k_param_a_in);
	EXPECT_EQ(item.get_input().element(), -1);

	olive::NodeViewItemConnector *out_conn = find_connector(&item, true);
	ASSERT_NE(out_conn, nullptr);
	EXPECT_FALSE(out_conn->isVisible());

	// A non-array input can never be expanded
	EXPECT_FALSE(item.can_be_expanded());
	item.set_expanded(true);
	EXPECT_FALSE(item.is_expanded());
}

TEST(WidgetNodeViewItem, SetFlowDirectionRepositionsConnectors)
{
	olive::Project project;
	project.initialize();

	auto *math = new olive::MathNode();
	math->setParent(&project);

	NVI item(to_oak(math), oak::Node());
	olive::NodeViewItemConnector *in_conn = find_connector(&item, false);
	olive::NodeViewItemConnector *out_conn = find_connector(&item, true);
	ASSERT_NE(in_conn, nullptr);
	ASSERT_NE(out_conn, nullptr);

	const QRectF r = item.rect();

	// The connector offset is measured from the connector's CURRENT polygon,
	// which set_flow_direction replaces (horizontal and vertical triangles
	// have different extents), so re-read it after every direction change
	item.set_flow_direction(NVC::k_left_to_right);
	EXPECT_EQ(item.get_flow_direction(), NVC::k_left_to_right);
	EXPECT_EQ(out_conn->pos(), QPointF(r.right(), 0));
	EXPECT_EQ(in_conn->pos(),
			  QPointF(r.left() - in_conn->polygon().boundingRect().width(), 0));

	// The output point sits one triangle-width to the right of the connector
	const double tri_w = out_conn->polygon().boundingRect().width();
	const QPointF out_p = item.get_output_point();
	EXPECT_DOUBLE_EQ(out_p.x(), out_conn->scenePos().x() + tri_w);
	EXPECT_DOUBLE_EQ(out_p.y(), out_conn->scenePos().y());

	item.set_flow_direction(NVC::k_right_to_left);
	EXPECT_EQ(out_conn->pos(), QPointF(r.left(), 0));
	EXPECT_EQ(in_conn->pos(),
			  QPointF(r.right() + in_conn->polygon().boundingRect().width(), 0));

	item.set_flow_direction(NVC::k_top_to_bottom);
	EXPECT_EQ(out_conn->pos(), QPointF(r.center().x(), r.bottom()));
	EXPECT_EQ(in_conn->pos(), QPointF(r.center().x(),
									  r.top() - in_conn->polygon().boundingRect().height()));

	item.set_flow_direction(NVC::k_bottom_to_top);
	EXPECT_EQ(out_conn->pos(), QPointF(r.center().x(), r.top()));
	EXPECT_EQ(in_conn->pos(), QPointF(r.center().x(),
									  r.bottom() + in_conn->polygon().boundingRect().height()));
}

TEST(WidgetNodeViewItem, NodePositionRoundTripsThroughScreen)
{
	olive::Project project;
	project.initialize();

	auto *math = new olive::MathNode();
	math->setParent(&project);

	NVI item(to_oak(math), oak::Node());

	const NVC::FlowDirection dirs[] = { NVC::k_left_to_right, NVC::k_right_to_left,
										NVC::k_top_to_bottom, NVC::k_bottom_to_top };
	for (NVC::FlowDirection dir : dirs) {
		item.set_flow_direction(dir);
		item.set_node_position(QPointF(2, 3));

		// Logical position survives the trip through screen coordinates
		const QPointF logical = item.get_node_position();
		EXPECT_NEAR(logical.x(), 2.0, 1e-6) << int(dir);
		EXPECT_NEAR(logical.y(), 3.0, 1e-6) << int(dir);

		// And the scene position matches the static mapping
		const QPointF screen = NVI::node_to_screen_point(QPointF(2, 3), dir);
		EXPECT_NEAR(item.pos().x(), screen.x(), 1e-6) << int(dir);
		EXPECT_NEAR(item.pos().y(), screen.y(), 1e-6) << int(dir);
	}

	// The aggregate overload sets position and expanded state together
	olive::NodeViewItemPosition data;
	data.position = QPointF(4, 5);
	data.expanded = true;
	item.set_node_position(data);

	EXPECT_TRUE(item.is_expanded());
	const olive::NodeViewItemPosition out = item.get_node_position_data();
	EXPECT_TRUE(out.expanded);
	EXPECT_NEAR(out.position.x(), 4.0, 1e-6);
	EXPECT_NEAR(out.position.y(), 5.0, 1e-6);
}

TEST(WidgetNodeViewItem, ExpansionCreatesRowPerValidInput)
{
	olive::Project project;
	project.initialize();

	auto *math = new olive::MathNode();
	math->setParent(&project);
	// Measured via the same C ABI queries NodeViewItem::is_input_valid uses,
	// so the expected row count tracks whatever the engine reports
	const int valid_inputs = count_valid_inputs(math);
	ASSERT_GE(valid_inputs, 1);

	NVI item(to_oak(math), oak::Node());
	ASSERT_TRUE(item.can_be_expanded());

	olive::NodeViewItemConnector *in_conn = find_connector(&item, false);
	ASSERT_NE(in_conn, nullptr);
	EXPECT_TRUE(in_conn->isVisible());

	// Collapsed: every input resolves to the item itself
	const oak::Input input_a(to_oak(math).handle(), olive::MathNode::k_param_a_in);
	EXPECT_EQ(item.get_item_for_input(input_a), &item);

	item.toggle_expanded();
	ASSERT_TRUE(item.is_expanded());

	// Expanding swaps the single input connector for per-input rows
	EXPECT_FALSE(in_conn->isVisible());

	const QList<NVI *> rows = child_node_items(&item);
	ASSERT_EQ(rows.size(), valid_inputs);

	// Rows are stacked one item-height apart below the title row
	const double h = NVI::default_item_height();
	for (int i = 0; i < rows.size(); i++) {
		EXPECT_DOUBLE_EQ(rows.at(i)->pos().x(), 0.0);
		EXPECT_DOUBLE_EQ(rows.at(i)->pos().y(), (i + 1) * h);
		EXPECT_FALSE(rows.at(i)->is_output_item());
	}

	// The item grows to cover the title row plus all children
	EXPECT_DOUBLE_EQ(item.rect().height(), h * (1 + valid_inputs));

	// Expanded: inputs resolve to their row
	NVI *row = item.get_item_for_input(input_a);
	ASSERT_NE(row, nullptr);
	EXPECT_NE(row, &item);
	EXPECT_EQ(row->get_input().input_id(), olive::MathNode::k_param_a_in);

	// Collapsing destroys the rows and restores the single-unit rect
	item.toggle_expanded();
	EXPECT_FALSE(item.is_expanded());
	EXPECT_TRUE(child_node_items(&item).isEmpty());
	EXPECT_DOUBLE_EQ(item.rect().height(), h);
	EXPECT_TRUE(in_conn->isVisible());
}

TEST(WidgetNodeViewItem, LabelAsOutputHidesOutputConnector)
{
	olive::Project project;
	project.initialize();

	auto *math = new olive::MathNode();
	math->setParent(&project);

	NVI item(to_oak(math), oak::Node());
	olive::NodeViewItemConnector *out_conn = find_connector(&item, true);
	ASSERT_NE(out_conn, nullptr);
	EXPECT_TRUE(out_conn->isVisible());

	item.set_label_as_output(true);
	EXPECT_TRUE(item.is_labelled_as_output_of_context());
	EXPECT_FALSE(out_conn->isVisible());

	item.set_label_as_output(false);
	EXPECT_FALSE(item.is_labelled_as_output_of_context());
	EXPECT_TRUE(out_conn->isVisible());
}

TEST(WidgetNodeViewContext, AddChildCreatesRegisteredItem)
{
	olive::Project project;
	project.initialize();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	auto *math = new olive::MathNode();
	math->setParent(&project);

	QGraphicsScene scene;
	auto *ctx = new olive::NodeViewContext(to_oak(solid));
	// flow_dir_/curved_edges_ are only set via setters; NodeViewScene does
	// this in add_context, so do the same here
	ctx->set_flow_direction(NVC::k_left_to_right);
	ctx->set_curved_edges(true);
	scene.addItem(ctx);

	EXPECT_EQ(ctx->get_context(), to_oak(solid));
	EXPECT_EQ(ctx->get_item_from_map(to_oak(math).handle()), nullptr);

	ctx->add_child(to_oak(math).handle());

	NVI *item = ctx->get_item_from_map(to_oak(math).handle());
	ASSERT_NE(item, nullptr);
	EXPECT_EQ(item->get_node(), to_oak(math));
	EXPECT_EQ(item->get_context(), to_oak(solid));
	EXPECT_EQ(item->parentItem(), ctx);
	EXPECT_EQ(item->get_flow_direction(), NVC::k_left_to_right);

	// The context rect is padded around its children
	EXPECT_TRUE(ctx->rect().contains(ctx->childrenBoundingRect()));

	// Position changes route through to the item's logical position
	ctx->set_child_position(to_oak(math).handle(), QPointF(3, 4));
	EXPECT_NEAR(item->get_node_position().x(), 3.0, 1e-6);
	EXPECT_NEAR(item->get_node_position().y(), 4.0, 1e-6);

	delete ctx;
}

TEST(WidgetNodeViewContext, ConnectAndDisconnectManageEdges)
{
	olive::Project project;
	project.initialize();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	auto *math = new olive::MathNode();
	math->setParent(&project);

	auto *ctx = new olive::NodeViewContext(to_oak(solid));
	ctx->set_flow_direction(NVC::k_left_to_right);
	ctx->set_curved_edges(true);

	ctx->add_child(to_oak(solid).handle());
	ctx->add_child(to_oak(math).handle());
	NVI *from = ctx->get_item_from_map(to_oak(solid).handle());
	NVI *to = ctx->get_item_from_map(to_oak(math).handle());
	ASSERT_NE(from, nullptr);
	ASSERT_NE(to, nullptr);

	const oak::Input input(to_oak(math).handle(), olive::MathNode::k_param_a_in);

	// Disconnecting a nonexistent edge reports failure
	EXPECT_FALSE(ctx->child_input_disconnected(to_oak(solid).handle(), input));

	ctx->child_input_connected(to_oak(solid).handle(), input);

	ASSERT_EQ(from->edges().size(), 1);
	ASSERT_EQ(to->edges().size(), 1);
	olive::NodeViewEdge *edge = from->edges().first();
	EXPECT_EQ(edge->from_item(), from);
	EXPECT_EQ(edge->to_item(), to);
	EXPECT_EQ(edge->output(), to_oak(solid));
	EXPECT_EQ(edge->input().node_handle(), to_oak(math).handle());
	EXPECT_EQ(edge->input().input_id(), olive::MathNode::k_param_a_in);
	EXPECT_TRUE(edge->is_connected());

	// Disconnecting deletes the edge and unregisters it from the items
	EXPECT_TRUE(ctx->child_input_disconnected(to_oak(solid).handle(), input));
	EXPECT_TRUE(from->edges().isEmpty());
	EXPECT_TRUE(to->edges().isEmpty());
	EXPECT_FALSE(ctx->child_input_disconnected(to_oak(solid).handle(), input));

	delete ctx;
}

TEST(WidgetNodeViewContext, SelectionCollectsNodesAndEdges)
{
	olive::Project project;
	project.initialize();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	auto *math = new olive::MathNode();
	math->setParent(&project);

	QGraphicsScene scene;
	auto *ctx = new olive::NodeViewContext(to_oak(solid));
	ctx->set_flow_direction(NVC::k_left_to_right);
	ctx->set_curved_edges(true);
	scene.addItem(ctx);

	ctx->add_child(to_oak(solid).handle());
	ctx->add_child(to_oak(math).handle());
	NVI *solid_item = ctx->get_item_from_map(to_oak(solid).handle());
	ASSERT_NE(solid_item, nullptr);

	EXPECT_TRUE(ctx->get_selected_items().isEmpty());

	ctx->select({ to_oak(solid).handle() });
	EXPECT_TRUE(solid_item->isSelected());

	const QVector<NVI *> selected = ctx->get_selected_items();
	ASSERT_EQ(selected.size(), 1);
	EXPECT_EQ(selected.first(), solid_item);

	// Select the edge too so both selection kinds land in the deletion lists
	ctx->child_input_connected(
		to_oak(solid).handle(),
		oak::Input(to_oak(math).handle(), olive::MathNode::k_param_a_in));
	NVI *math_item = ctx->get_item_from_map(to_oak(math).handle());
	ASSERT_NE(math_item, nullptr);
	ASSERT_EQ(math_item->edges().size(), 1);
	olive::NodeViewEdge *edge = math_item->edges().first();
	edge->setSelected(true);

	QVector<OakEngineNode *> nodes, contexts;
	QVector<olive::NodeViewEdge *> edges;
	ctx->get_selected_for_deletion(nodes, contexts, edges);

	ASSERT_EQ(nodes.size(), 1);
	EXPECT_EQ(nodes.first(), to_oak(solid).handle());
	ASSERT_EQ(contexts.size(), 1);
	EXPECT_EQ(contexts.first(), to_oak(solid).handle());
	ASSERT_EQ(edges.size(), 1);
	EXPECT_EQ(edges.first(), edge);

	delete ctx;
}

TEST(WidgetNodeViewContext, RemoveChildNotifiesAndUnregisters)
{
	olive::Project project;
	project.initialize();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	auto *math = new olive::MathNode();
	math->setParent(&project);

	QGraphicsScene scene;
	auto *ctx = new olive::NodeViewContext(to_oak(solid));
	ctx->set_flow_direction(NVC::k_left_to_right);
	ctx->set_curved_edges(true);
	scene.addItem(ctx);

	ctx->add_child(to_oak(math).handle());
	NVI *item = ctx->get_item_from_map(to_oak(math).handle());
	ASSERT_NE(item, nullptr);

	// QSignalSpy cannot record the unregistered NodeViewItem* metatype, so
	// capture the emission with a lambda instead
	int notifications = 0;
	NVI *notified_item = nullptr;
	QObject::connect(ctx, &olive::NodeViewContext::item_about_to_be_deleted,
					 [&notifications, &notified_item](NVI *i) {
						 notifications++;
						 notified_item = i;
					 });

	ctx->remove_child(to_oak(math).handle());

	EXPECT_EQ(notifications, 1);
	EXPECT_EQ(notified_item, item);
	EXPECT_EQ(ctx->get_item_from_map(to_oak(math).handle()), nullptr);

	delete ctx;
}

TEST(WidgetNodeViewContext, MapScenePosToNodePos)
{
	olive::Project project;
	project.initialize();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	auto *math = new olive::MathNode();
	math->setParent(&project);

	QGraphicsScene scene;

	// With no items the mapping falls back to the origin
	auto *empty_ctx = new olive::NodeViewContext(to_oak(math));
	empty_ctx->set_flow_direction(NVC::k_left_to_right);
	EXPECT_EQ(empty_ctx->map_scene_pos_to_node_pos_in_context(QPointF(50, 50)),
			  QPointF(0, 0));
	delete empty_ctx;

	auto *ctx = new olive::NodeViewContext(to_oak(solid));
	ctx->set_flow_direction(NVC::k_left_to_right);
	ctx->set_curved_edges(true);
	scene.addItem(ctx);
	ctx->add_child(to_oak(math).handle());

	// The child sits at node position (0,0); one horizontal padding unit to
	// the right in scene coordinates is node position (1,0)
	const double hpad = NVI::default_item_horizontal_padding(NVC::k_left_to_right);
	const QPointF node_pos = ctx->map_scene_pos_to_node_pos_in_context(QPointF(hpad, 0));
	EXPECT_NEAR(node_pos.x(), 1.0, 1e-6);
	EXPECT_NEAR(node_pos.y(), 0.0, 1e-6);

	delete ctx;
}

TEST(WidgetNodeViewToolBar, ConstructionCreatesFiveButtons)
{
	olive::NodeViewToolBar bar;

	const auto buttons = bar.findChildren<QPushButton *>();
	ASSERT_EQ(buttons.size(), 5);

	// Only the mini-map toggle is checkable
	int checkable = 0;
	for (QPushButton *b : buttons) {
		if (b->isCheckable()) {
			checkable++;
		}
		EXPECT_FALSE(b->toolTip().isEmpty());
	}
	EXPECT_EQ(checkable, 1);

	// The fit button carries a text label, the rest are icon-only
	QPushButton *fit = find_button_by_tooltip(&bar, QStringLiteral("Fit to Content"));
	ASSERT_NE(fit, nullptr);
	EXPECT_EQ(fit->text(), QStringLiteral("Fit"));
}

TEST(WidgetNodeViewToolBar, ButtonsEmitTheirSignals)
{
	olive::NodeViewToolBar bar;

	QSignalSpy add_spy(&bar, &olive::NodeViewToolBar::add_node_clicked);
	QSignalSpy minimap_spy(&bar, &olive::NodeViewToolBar::mini_map_enabled_toggled);
	QSignalSpy zoom_in_spy(&bar, &olive::NodeViewToolBar::zoom_in_clicked);
	QSignalSpy zoom_out_spy(&bar, &olive::NodeViewToolBar::zoom_out_clicked);
	QSignalSpy fit_spy(&bar, &olive::NodeViewToolBar::fit_clicked);

	QPushButton *add = find_button_by_tooltip(&bar, QStringLiteral("Add Node"));
	QPushButton *minimap = find_button_by_tooltip(&bar, QStringLiteral("Toggle Mini-Map"));
	QPushButton *zoom_in = find_button_by_tooltip(&bar, QStringLiteral("Zoom In"));
	QPushButton *zoom_out = find_button_by_tooltip(&bar, QStringLiteral("Zoom Out"));
	QPushButton *fit = find_button_by_tooltip(&bar, QStringLiteral("Fit to Content"));
	ASSERT_NE(add, nullptr);
	ASSERT_NE(minimap, nullptr);
	ASSERT_NE(zoom_in, nullptr);
	ASSERT_NE(zoom_out, nullptr);
	ASSERT_NE(fit, nullptr);

	// set_mini_map_enabled drives the toggle's checked state
	ASSERT_TRUE(minimap->isCheckable());
	bar.set_mini_map_enabled(true);
	EXPECT_TRUE(minimap->isChecked());
	bar.set_mini_map_enabled(false);
	EXPECT_FALSE(minimap->isChecked());

	add->click();
	EXPECT_EQ(add_spy.count(), 1);

	zoom_in->click();
	EXPECT_EQ(zoom_in_spy.count(), 1);

	zoom_out->click();
	EXPECT_EQ(zoom_out_spy.count(), 1);

	fit->click();
	EXPECT_EQ(fit_spy.count(), 1);

	// The toggle signal carries the new checked state
	minimap->click();
	ASSERT_EQ(minimap_spy.count(), 1);
	EXPECT_TRUE(minimap_spy.first().first().toBool());

	minimap->click();
	ASSERT_EQ(minimap_spy.count(), 2);
	EXPECT_FALSE(minimap_spy.at(1).first().toBool());
}

TEST(WidgetNodeWidget, SetContextsTogglesToolbarAndView)
{
	ensure_core();

	olive::Project project;
	project.initialize();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);

	olive::NodeWidget widget;
	ASSERT_NE(widget.view(), nullptr);

	olive::NodeViewToolBar *bar = widget.findChild<olive::NodeViewToolBar *>();
	ASSERT_NE(bar, nullptr);
	EXPECT_TRUE(bar->isEnabled());

	// An empty context list disables the toolbar
	widget.set_contexts({});
	EXPECT_TRUE(widget.view()->get_contexts().isEmpty());
	EXPECT_FALSE(bar->isEnabled());

	// A real context re-enables it and lands in the view
	widget.set_contexts({ to_oak(solid) });
	EXPECT_TRUE(bar->isEnabled());
	ASSERT_EQ(widget.view()->get_contexts().size(), 1);
	EXPECT_EQ(widget.view()->get_contexts().first(), to_oak(solid));
}

TEST(WidgetNodeWidget, ToolbarMinimapToggleHidesMiniMap)
{
	ensure_core();

	olive::NodeWidget widget;

	olive::NodeViewMiniMap *minimap =
		widget.view()->findChild<olive::NodeViewMiniMap *>();
	ASSERT_NE(minimap, nullptr);

	// NodeWidget enables the mini-map by default
	EXPECT_FALSE(minimap->isHidden());

	olive::NodeViewToolBar *bar = widget.findChild<olive::NodeViewToolBar *>();
	ASSERT_NE(bar, nullptr);
	QPushButton *toggle = find_button_by_tooltip(bar, QStringLiteral("Toggle Mini-Map"));
	ASSERT_NE(toggle, nullptr);
	EXPECT_TRUE(toggle->isChecked());

	toggle->click();
	EXPECT_TRUE(minimap->isHidden());

	toggle->click();
	EXPECT_FALSE(minimap->isHidden());
}

TEST(WidgetNodeWidget, ToolbarZoomButtonsChangeViewScale)
{
	ensure_core();

	olive::NodeWidget widget;
	widget.resize(400, 300);

	olive::NodeViewToolBar *bar = widget.findChild<olive::NodeViewToolBar *>();
	ASSERT_NE(bar, nullptr);
	QPushButton *zoom_in = find_button_by_tooltip(bar, QStringLiteral("Zoom In"));
	QPushButton *zoom_out = find_button_by_tooltip(bar, QStringLiteral("Zoom Out"));
	ASSERT_NE(zoom_in, nullptr);
	ASSERT_NE(zoom_out, nullptr);

	const double initial = widget.view()->transform().m11();

	zoom_in->click();
	EXPECT_NEAR(widget.view()->transform().m11(), initial * 1.25, 1e-9);

	zoom_out->click();
	EXPECT_NEAR(widget.view()->transform().m11(), initial, 1e-9);
}

TEST(WidgetNodeViewMiniMap, ResizeEmitsSignalAndFitsSceneRect)
{
	olive::NodeViewScene scene;
	scene.setSceneRect(0, 0, 400, 300);

	olive::NodeViewMiniMap minimap(&scene);
	EXPECT_EQ(minimap.scene(), &scene);
	EXPECT_EQ(minimap.horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
	EXPECT_EQ(minimap.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
	EXPECT_TRUE(minimap.hasMouseTracking());

	// Hidden widgets do not receive QResizeEvent (Qt defers geometry events
	// until show), so the widget must be exposed before resize() has any
	// observable effect
	minimap.show();
	ASSERT_TRUE(QTest::qWaitForWindowExposed(&minimap));

	QSignalSpy spy(&minimap, &olive::NodeViewMiniMap::resized);

	minimap.resize(200, 150);
	ASSERT_GE(spy.count(), 1);

	// The scene rect is scaled to fit the smaller of the two axes
	EXPECT_NEAR(minimap.transform().m11(), 0.5, 1e-9);
	EXPECT_NEAR(minimap.transform().m22(), 0.5, 1e-9);
}

TEST(WidgetNodeViewMiniMap, ClickOutsideTriangleEmitsScenePoint)
{
	olive::NodeViewScene scene;
	scene.setSceneRect(0, 0, 400, 300);

	ProbeMiniMap minimap(&scene);

	// The fit transform is only computed once the widget is exposed and
	// receives its resize event
	minimap.show();
	ASSERT_TRUE(QTest::qWaitForWindowExposed(&minimap));

	minimap.resize(200, 150); // 0.5 scale in both axes

	QSignalSpy spy(&minimap, &olive::NodeViewMiniMap::move_to_scene_point);

	// Well outside the resize triangle (top-left corner)
	minimap.pub_press(QPoint(100, 75));

	ASSERT_EQ(spy.count(), 1);
	const QPointF scene_pos = spy.first().first().toPointF();
	EXPECT_NEAR(scene_pos.x(), 200.0, 10.0);
	EXPECT_NEAR(scene_pos.y(), 150.0, 10.0);

	minimap.pub_release(QPoint(100, 75));
	EXPECT_EQ(spy.count(), 1);
}

TEST(WidgetNodeViewMiniMap, ClickInsideTriangleStartsResizeNotMove)
{
	olive::NodeViewScene scene;
	scene.setSceneRect(0, 0, 400, 300);

	ProbeMiniMap minimap(&scene);
	minimap.show();
	ASSERT_TRUE(QTest::qWaitForWindowExposed(&minimap));
	minimap.resize(200, 150);

	// The resize triangle spans resize_triangle_sz_ = fontMetrics height / 2
	// in the top-left corner; (1,1) is always inside it
	const int triangle_sz = minimap.fontMetrics().height() / 2;
	ASSERT_GE(triangle_sz, 1);

	QSignalSpy spy(&minimap, &olive::NodeViewMiniMap::move_to_scene_point);
	minimap.pub_press(QPoint(1, 1));
	EXPECT_EQ(spy.count(), 0);
	minimap.pub_release(QPoint(1, 1));
	EXPECT_EQ(spy.count(), 0);
}
