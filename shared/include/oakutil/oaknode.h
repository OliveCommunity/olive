/***

  Oak - Non-Linear Video Editor
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

#ifndef OAKUTIL_OAKNODE_H
#define OAKUTIL_OAKNODE_H

#include <QBrush>
#include <QPointF>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QHash>

#include "oakengine/node.h"
#include "oakengine/project.h"
#include "oakengine/viewer.h"
#include "oakengine/footage.h"
#include "oakengine/undo.h"

namespace oak
{

/**
 * @file oaknode.h
 * @brief C++ wrapper layer over the engine's pure C ABI (oakengine_*).
 *
 * The engine boundary is a flat C API so that it can be reimplemented in Rust
 * and consumed through FFI. Calling that flat API directly from the UI layer
 * is verbose, error-prone and destroys RAII / object semantics. This header
 * re-wraps the C ABI into lightweight C++ value types so that application code
 * can keep a natural, object-oriented shape:
 *
 * @code
 *   oak::Node item = ...;
 *   QString label = item.get_label();
 *   oak::Node parent = item.folder();
 * @endcode
 *
 * Semantics:
 *   - A wrapper is a *borrowed, non-owning* view over an opaque engine handle
 *     (OakEngineNode* / OakEngineProject*). Copying a wrapper copies the
 *     handle, not the underlying object. Lifetime follows the engine object.
 *   - A default-constructed wrapper holds nullptr and is "null"; it is safe to
 *     call any method on a null wrapper (it yields a no-op / empty result,
 *     exactly like the underlying C ABI which accepts NULL handles).
 *   - Identity is the handle address, so wrappers work as QHash keys and in
 *     pointer-style comparisons (a == b, a != nullptr).
 *
 * The wrapper lives in namespace `oak` (deliberately distinct from the
 * engine's internal `olive` namespace, which is still visible in parts of the
 * application through coreengine.h; keeping the names separate avoids clashes).
 *
 * Application code should reach the engine through these wrappers rather than
 * calling oakengine_*() directly; direct C ABI calls in app/ are exceptions
 * that must be justified and registered (see docs/zh/plans/r8-p3-node-param-abi.md).
 */

class Node;
class Project;
class Footage;
class Input;
class Keyframe;

/**
 * @brief Mirror of engine's olive::Node::CategoryID ordinals
 * (engine/node/node.h). Values must stay in sync with the engine enum;
 * oakengine_node_category_at() returns these ordinals directly.
 */
enum NodeCategory {
	k_category_unknown = -1,
	k_category_output,
	k_category_generator,
	k_category_math,
	k_category_keying,
	k_category_filter,
	k_category_color,
	k_category_time,
	k_category_timeline,
	k_category_transition,
	k_category_distort,
	k_category_project,
	k_category_open_fx,
	k_category_count
};

struct NodeConnection;
struct ContextNodeItem;

/**
 * @brief Wrapper around an opaque OakEngineNode handle.
 */
class Node
{
public:
	/// Construct a null node.
	Node() = default;

	/// Wrap a borrowed engine handle (no ownership transfer).
	Node(OakEngineNode *h) : h_(h) {}
	Node(OakEngineProject *) = delete; // avoid accidental wrong-handle wrapping

	/// The underlying borrowed handle (for C ABI interop / Qt internalPointer).
	OakEngineNode *handle() const { return h_; }

	/// Implicit conversion to the raw handle for boundary interop.
	operator OakEngineNode *() const { return h_; }

	/// True when the wrapper holds no handle.
	bool is_null() const { return h_ == nullptr; }

	/* ---- Identity -------------------------------------------------------- */

	bool operator==(const Node &o) const { return h_ == o.h_; }
	bool operator!=(const Node &o) const { return h_ != o.h_; }
	bool operator==(std::nullptr_t) const { return h_ == nullptr; }
	bool operator!=(std::nullptr_t) const { return h_ != nullptr; }

	/* ---- Type queries ---------------------------------------------------- */

	QString id() const { return str([this](char *b, int n) { return oakengine_node_get_type_id(h_, b, n); }); }
	QString name() const { return str([this](char *b, int n) { return oakengine_node_get_name(h_, b, n); }); }
	QString get_label() const { return str([this](char *b, int n) { return oakengine_node_get_label(h_, b, n); }); }
	QString description() const { return str([this](char *b, int n) { return oakengine_node_get_description(h_, b, n); }); }
	QString short_name() const { return str([this](char *b, int n) { return oakengine_node_get_short_name(h_, b, n); }); }
	QString label_and_name() const { return str([this](char *b, int n) { return oakengine_node_get_label_and_name(h_, b, n); }); }

	bool is_folder() const { return oakengine_node_is_folder(h_) != 0; }
	bool is_item() const { return oakengine_node_is_item(h_) != 0; }
	bool is_footage() const { return oakengine_node_is_footage(h_) != 0; }
	bool is_sequence() const { return oakengine_node_is_sequence(h_) != 0; }
	bool is_track() const { return oakengine_node_is_track(h_) != 0; }
	bool is_clip() const { return oakengine_node_is_clip(h_) != 0; }
	bool is_group() const { return oakengine_node_is_group(h_) != 0; }
	bool is_multicam() const { return oakengine_node_is_multicam(h_) != 0; }
	bool is_viewer_output() const { return oakengine_node_is_viewer_output(h_) != 0; }

	int color_label() const { return oakengine_node_get_color_label(h_); }
	int effective_color_label() const { return oakengine_node_get_effective_color_label(h_); }

	/// The node's flags (OR-combination of engine Node::Flag values).
	uint64_t flags() const { return oakengine_node_get_flags(h_); }

	/// Re-run the node's translation pass (Node::retranslate()).
	void retranslate() const { oakengine_node_retranslate(h_); }

	/**
	 * @brief Standalone copy of this node (Node::copy()).
	 *
	 * NOTE: unlike everything else on this wrapper, the returned handle is
	 * OWNED by the caller (not added to any project). Hand it on to a
	 * project/undo command or release it through the C ABI.
	 */
	Node create_copy() const { return Node(oakengine_node_create_copy(h_)); }

	/// The node's title-bar brush (Node::brush()); Qt type crosses the ABI
	/// as an opaque out-pointer (same precedent as the C ABI it wraps).
	QBrush brush(double top, double bottom) const
	{
		QBrush b;
		oakengine_node_get_brush(h_, top, bottom, &b);
		return b;
	}

	/* ---- Factory library (static) ----------------------------------------- */

	/// Number of registered node prototypes in the engine's NodeFactory.
	static int factory_count() { return oakengine_node_factory_id_count(); }

	/// Borrowed prototype node at `index` (read-only metadata queries only).
	static Node factory_node_at(int index) { return Node(oakengine_node_factory_node_at(index)); }

	/// Value of the engine's Node::k_dont_show_in_create_menu flag.
	static uint64_t flag_dont_show_in_create_menu() { return oakengine_node_flag_dont_show_in_create_menu(); }

	/// Translated display name of a NodeCategory ordinal (Node::get_category_name()).
	static QString category_name(int category_id)
	{
		char buf[256];
		buf[0] = '\0';
		oakengine_node_category_name(category_id, buf, sizeof(buf));
		return QString::fromUtf8(buf);
	}

	/* ---- Categories -------------------------------------------------------- */

	int category_count() const { return oakengine_node_category_count(h_); }

	/// NodeCategory ordinal at `index` (-1 when out of range).
	int category_at(int index) const { return oakengine_node_category_at(h_, index); }

	/// True when `category` is in this node's category list.
	bool in_category(NodeCategory category) const
	{
		const int n = category_count();
		for (int i = 0; i < n; i++) {
			if (category_at(i) == category) {
				return true;
			}
		}
		return false;
	}

	QString sub_category() const { return str([this](char *b, int n) { return oakengine_node_get_sub_category(h_, b, n); }); }

	/* ---- Graph navigation -------------------------------------------------- */

	Node folder() const { return Node(oakengine_node_folder(h_)); }
	Project project() const; // defined after Project

	/**
	 * @brief Node::data() equivalent. Returns a QVariant mirroring the engine
	 * type: QString for icon/tooltip, qint64 for created/modified time, and an
	 * invalid QVariant when the engine returns no value.
	 *
	 * Role ordinals match olive::Node::DataType: 0=icon, 1=duration,
	 * 2=created_time, 3=modified_time, 4=frequency_rate, 5=tooltip.
	 */
	QVariant data(int role) const
	{
		int type = 0;
		int64_t iv = 0;
		char buf[4096];
		buf[0] = '\0';
		oakengine_node_get_data(h_, role, &type, &iv, buf, sizeof(buf));
		switch (type) {
		case 1:
			return QString::fromUtf8(buf);
		case 2:
			return QVariant(qint64(iv));
		default:
			return QVariant();
		}
	}

	/* ---- Folder operations (valid when is_folder()) ------------------------ */

	int item_child_count() const { return oakengine_folder_item_child_count(h_); }
	Node item_child(int index) const { return Node(oakengine_folder_item_child(h_, index)); }
	int index_of_child(const Node &child) const { return oakengine_folder_index_of_child(h_, child.h_); }
	bool has_child_recursive(const Node &child) const { return oakengine_folder_has_child_recursive(h_, child.h_) != 0; }

	/* ---- ViewerOutput operations (valid when is_viewer_output()) ----------- */

	int enabled_stream_count() const { return oakengine_viewer_get_enabled_stream_count(h_); }

	/* ---- Label mutation ---------------------------------------------------- */

	void set_label(const QString &label) const { oakengine_node_set_label(h_, label.toUtf8().constData()); }

	/* ---- Inputs ------------------------------------------------------------ */

	/// Number of declared inputs (array elements not counted separately).
	int input_count() const { return oakengine_node_input_count(h_); }

	/// The input id at `index` (empty when out of range).
	QString input_id(int index) const
	{
		char buf[256];
		buf[0] = '\0';
		oakengine_node_input_id(h_, index, buf, sizeof(buf));
		return QString::fromUtf8(buf);
	}

	/// All declared inputs of this node.
	QVector<Input> inputs() const; // defined after Input

	/// This node's effect input (typically the texture input); null when none.
	Input effect_input() const; // defined after Input

	/* ---- Topology ----------------------------------------------------------- */

	/// True when this node (recursively, when requested) receives input from `other`.
	bool inputs_from(const Node &other, bool recursive) const
	{
		return oakengine_node_inputs_from(h_, other.h_, recursive ? 1 : 0) != 0;
	}

	int output_connection_count() const { return oakengine_node_output_connection_count(h_); }

	/// The receiving node of the output connection at `index` (null if out of range).
	Node output_connection_node(int index) const
	{
		OakEngineNode *n = nullptr;
		char buf[256];
		int element = 0;
		oakengine_node_output_connection_at(h_, index, &n, buf, sizeof(buf), &element);
		return Node(n);
	}

	/// Full output connection info at `index` (including hidden flag).
	NodeConnection output_connection_at_ex(int index) const; // defined after NodeConnection is complete

	/// Total number of input connections on this node (flat enumeration).
	int input_connection_count_all() const { return oakengine_node_input_connection_count_all(h_); }

	/// Full input connection info at flat `index` (including source node).
	NodeConnection input_connection_at_all(int index) const; // defined after NodeConnection is complete

	/* ---- Exclusive dependencies (Node::get_exclusive_dependencies()) -------- */

	int exclusive_dependency_count() const { return oakengine_node_get_exclusive_dependency_count(h_); }
	Node exclusive_dependency_at(int index) const { return Node(oakengine_node_get_exclusive_dependency_at(h_, index)); }

	/* ---- Context operations (valid when this node is a context, e.g. group) -- */

	int context_node_count() const { return oakengine_node_context_node_count(h_); }

	/// The child at `index` with its graph position and expanded flag.
	ContextNodeItem context_node_at(int index) const; // defined after ContextNodeItem is complete

	/// Graph position of `node` inside this context; false when not found.
	bool context_position_of(const Node &node, QPointF *pos, bool *expanded = nullptr) const
	{
		double x = 0, y = 0;
		int exp = 0;
		if (oakengine_node_get_context_position(h_, node.h_, &x, &y, &exp) != 0) {
			return false;
		}
		if (pos) {
			*pos = QPointF(x, y);
		}
		if (expanded) {
			*expanded = exp != 0;
		}
		return true;
	}

	/// Set the graph position of `node` inside this context (undoable).
	void set_context_position_of(const Node &node, const QPointF &pos) const
	{
		oakengine_node_set_context_position(h_, node.h_, pos.x(), pos.y());
	}

	/// Set the expanded flag of `node` inside this context (undoable).
	void set_context_expanded_of(const Node &node, bool expanded) const
	{
		oakengine_node_set_context_expanded(h_, node.h_, expanded ? 1 : 0);
	}

	/* ---- Plugin ------------------------------------------------------------- */

	bool has_plugin() const { return oakengine_node_has_plugin(h_) != 0; }
	int plugin_message_count() const { return oakengine_node_plugin_message_count(h_); }

	/// Plugin persistent message text at `index`; *type receives
	/// 0=error, 1=warning, 2=message. Empty string when out of range.
	QString plugin_message_at(int index, int *type = nullptr) const
	{
		char buf[2048];
		buf[0] = '\0';
		int t = 0;
		if (oakengine_node_plugin_message_at(h_, index, &t, buf, sizeof(buf)) != 0) {
			return QString();
		}
		if (type) {
			*type = t;
		}
		return QString::fromUtf8(buf);
	}

	/// Clear all persistent messages on the node's plugin instance.
	void clear_plugin_messages() const { oakengine_node_plugin_clear_messages(h_); }

	/* ---- Footage view (valid when is_footage()) ----------------------------- */

	/// Borrow this node as a Footage; returns a null Footage when not footage.
	Footage as_footage() const; // defined after Footage

	/// Enabled stream references as (track_type, index) pairs.
	QVector<QPair<int, int>> enabled_streams() const
	{
		QVector<QPair<int, int>> out;
		const int n = oakengine_viewer_get_enabled_stream_count(h_);
		if (n <= 0) {
			return out;
		}
		QVector<int> types(n), indices(n);
		const int got = oakengine_viewer_get_enabled_streams(h_, types.data(), indices.data(), n);
		out.reserve(got);
		for (int i = 0; i < got; i++) {
			out.append({ types[i], indices[i] });
		}
		return out;
	}

private:
	/// Helper: run a buf/size C ABI getter and return the result as QString.
	template <typename Fn>
	QString str(Fn fn) const
	{
		char buf[1024];
		buf[0] = '\0';
		fn(buf, sizeof(buf));
		return QString::fromUtf8(buf);
	}

	OakEngineNode *h_ = nullptr;
};

/**
 * @brief One edge of the node graph: a receiving (input) side plus,
 * for input-side enumeration, the feeding (output) source node.
 *
 * Returned by value from Node::output_connection_at_ex() and
 * Node::input_connection_at_all().
 */
struct NodeConnection {
	Node node; ///< The node owning the receiving input.
	QString input_id; ///< Id of the receiving input on `node`.
	int element = -1; ///< Array element of the receiving input (-1 = whole input).
	bool hidden = false; ///< Whether the receiving input is hidden.
	Node source_node; ///< Input-side enumeration only: the node feeding the input.
};

/**
 * @brief A node's placement inside a context (graph position + expanded flag).
 */
struct ContextNodeItem {
	Node node;
	QPointF position;
	bool expanded = false;
};

/**
 * @brief Wrapper around an opaque OakEngineProject handle.
 *
 * Like Node this is a borrowed view; project *ownership* (create/free) is
 * managed elsewhere (the application core). This wrapper only provides
 * query/navigation access.
 */
class Project
{
public:
	Project() = default;
	Project(OakEngineProject *h) : h_(h) {}

	OakEngineProject *handle() const { return h_; }
	operator OakEngineProject *() const { return h_; }

	bool is_null() const { return h_ == nullptr; }

	bool operator==(const Project &o) const { return h_ == o.h_; }
	bool operator!=(const Project &o) const { return h_ != o.h_; }
	bool operator==(std::nullptr_t) const { return h_ == nullptr; }
	bool operator!=(std::nullptr_t) const { return h_ != nullptr; }

	/// Root folder node of the project (Project::root()).
	Node root() const { return Node(oakengine_project_root(h_)); }

	QString name() const
	{
		char buf[1024];
		buf[0] = '\0';
		oakengine_project_name(h_, buf, sizeof(buf));
		return QString::fromUtf8(buf);
	}

private:
	OakEngineProject *h_ = nullptr;
};

inline Project Node::project() const
{
	return Project(oakengine_node_get_project(h_));
}

/**
 * @brief RAII wrapper around a borrowed OakEngineFootage handle.
 *
 * Unlike Node/Project (non-owning views), a Footage wrapper *owns* the
 * borrowed handle and releases it with oakengine_footage_free() on
 * destruction, mirroring the engine's borrow/free contract. Copying is
 * disabled; move transfers ownership.
 */
class Footage
{
public:
	Footage() = default;

	/// Take ownership of a borrowed handle (from oakengine_footage_borrow()).
	explicit Footage(OakEngineFootage *h) : h_(h) {}

	/// Borrow the footage view of a node (null when the node is not footage).
	static Footage borrow(OakEngineNode *node) { return Footage(oakengine_footage_borrow(node)); }

	~Footage() { reset(); }

	Footage(const Footage &) = delete;
	Footage &operator=(const Footage &) = delete;

	Footage(Footage &&o) noexcept : h_(o.h_) { o.h_ = nullptr; }
	Footage &operator=(Footage &&o) noexcept
	{
		if (this != &o) {
			reset();
			h_ = o.h_;
			o.h_ = nullptr;
		}
		return *this;
	}

	OakEngineFootage *handle() const { return h_; }
	bool is_null() const { return h_ == nullptr; }
	explicit operator bool() const { return h_ != nullptr; }

	QString filename() const { return str([this](char *b, int n) { return oakengine_footage_get_filename(h_, b, n); }); }
	QString proxy_path() const { return str([this](char *b, int n) { return oakengine_footage_proxy_get_path(h_, b, n); }); }
	bool proxy_enabled() const { return oakengine_footage_proxy_is_enabled(h_) != 0; }
	void set_proxy_enabled(bool e) const { oakengine_footage_proxy_set_enabled(h_, e ? 1 : 0); }
	void invalidate() const { oakengine_footage_invalidate(h_); }
	int relink(const QString &path) const { return oakengine_footage_relink(h_, path.toUtf8().constData()); }
	int proxy_delete() const { return oakengine_footage_proxy_delete(h_); }

	static QString last_error()
	{
		char buf[512];
		buf[0] = '\0';
		oakengine_footage_last_error(buf, sizeof(buf));
		return QString::fromUtf8(buf);
	}

private:
	void reset()
	{
		if (h_) {
			oakengine_footage_free(h_);
			h_ = nullptr;
		}
	}

	template <typename Fn>
	QString str(Fn fn) const
	{
		char buf[4096];
		buf[0] = '\0';
		fn(buf, sizeof(buf));
		return QString::fromUtf8(buf);
	}

	OakEngineFootage *h_ = nullptr;
};

inline Footage Node::as_footage() const
{
	return Footage::borrow(h_);
}

/**
 * @brief Value type identifying a specific input (parameter) on a node.
 *
 * Mirrors the engine's olive::NodeInput (engine/node/param.h) but stores an
 * opaque OakEngineNode* handle instead of the internal Node* pointer.
 * Lightweight and copyable; suitable for QVariant storage and QHash keys.
 */
class Input
{
public:
	Input() : node_(nullptr), element_(-1) {}

	Input(OakEngineNode *node, const QString &input_id, int element = -1)
		: node_(node), input_(input_id), element_(element) {}

	/* ---- Accessors --------------------------------------------------------- */

	OakEngineNode *node_handle() const { return node_; }
	Node node() const { return Node(node_); }
	const QString &input_id() const { return input_; }
	int element() const { return element_; }

	void set_node_handle(OakEngineNode *n) { node_ = n; }
	void set_input_id(const QString &id) { input_ = id; }
	void set_element(int e) { element_ = e; }

	/* ---- Validity ----------------------------------------------------------- */

	bool is_valid() const
	{
		return node_ && !input_.isEmpty() && element_ >= -1;
	}

	/* ---- Comparison (for QHash / containers) -------------------------------- */

	bool operator==(const Input &o) const
	{
		return node_ == o.node_ && input_ == o.input_ && element_ == o.element_;
	}
	bool operator!=(const Input &o) const { return !(*this == o); }
	bool operator<(const Input &o) const
	{
		if (node_ != o.node_) return node_ < o.node_;
		if (input_ != o.input_) return input_ < o.input_;
		return element_ < o.element_;
	}

	/* ---- C ABI delegated queries -------------------------------------------- */

	QString name() const
	{
		char buf[1024];
		buf[0] = '\0';
		oakengine_node_get_input_name(node_, input_.toUtf8().constData(), buf, sizeof(buf));
		return QString::fromUtf8(buf);
	}

	bool is_hidden() const
	{
		return oakengine_node_input_is_hidden(node_, input_.toUtf8().constData()) != 0;
	}

	bool is_connected() const
	{
		return oakengine_node_input_is_connected(node_, input_.toUtf8().constData()) != 0;
	}

	bool is_connectable() const
	{
		return oakengine_node_input_is_connectable(node_, input_.toUtf8().constData()) != 0;
	}

	bool is_keyframing() const
	{
		return oakengine_node_input_is_keyframed_ex(node_, input_.toUtf8().constData(), element_) != 0;
	}

	bool is_array() const
	{
		return oakengine_node_input_is_array(node_, input_.toUtf8().constData()) != 0;
	}

	int array_size() const
	{
		return oakengine_node_input_array_size(node_, input_.toUtf8().constData());
	}

	int flags() const
	{
		return oakengine_node_input_get_flags(node_, input_.toUtf8().constData());
	}

	/// NodeValue::Type enum ordinal (engine-internal numbering).
	int data_type() const
	{
		return oakengine_node_input_get_data_type(node_, input_.toUtf8().constData());
	}

	/// oak_node_value_type (C ABI type enum) for this input.
	int c_type() const
	{
		return oakengine_node_input_get_type(node_, input_.toUtf8().constData());
	}

	/// Number of keyframe tracks for this input's data type.
	int keyframe_track_count() const
	{
		return oakengine_node_value_keyframe_track_count(c_type());
	}

	bool is_keyframable() const
	{
		return oakengine_node_input_is_keyframable(node_, input_.toUtf8().constData());
	}

	Node connected_node() const
	{
		return Node(oakengine_node_input_get_connected_node(node_, input_.toUtf8().constData(), element_));
	}

	/// Remove the edge feeding this input (undoable).
	bool disconnect() const
	{
		return oakengine_node_disconnect_ex(node_, input_.toUtf8().constData(), element_) == 0;
	}

	/// Read an integer input property; true on success.
	bool property_int(const char *key, int64_t *out) const
	{
		return oakengine_node_input_get_property_int(node_, input_.toUtf8().constData(), key, out) == 0;
	}

	/// True if the input has a property with the given key.
	bool has_property(const char *key) const
	{
		return oakengine_node_input_has_property(node_, input_.toUtf8().constData(), key) != 0;
	}

	/// Read a numeric input property as a double (-1 track = whole value); true on success.
	bool property_number(const char *key, int track, double *out) const
	{
		return oakengine_node_input_get_property_number(node_, input_.toUtf8().constData(), key, track, out) == 0;
	}

	/// Keyframes at a rational time across all tracks of this input.
	QVector<Keyframe> keyframes_at_time(int64_t time_num, int64_t time_den) const; // defined after Keyframe

	/// Earliest keyframe time on this input; false when no keyframes.
	bool earliest_keyframe_time(int64_t *num, int64_t *den) const
	{
		return oakengine_node_keyframe_earliest_time(node_, input_.toUtf8().constData(), element_, num, den) != 0;
	}

	/// Latest keyframe time on this input; false when no keyframes.
	bool latest_keyframe_time(int64_t *num, int64_t *den) const
	{
		return oakengine_node_keyframe_latest_time(node_, input_.toUtf8().constData(), element_, num, den) != 0;
	}

	void reset() { *this = Input(); }

private:
	OakEngineNode *node_;
	QString input_;
	int element_;
};

inline QVector<Input> Node::inputs() const
{
	QVector<Input> out;
	const int n = input_count();
	out.reserve(n);
	for (int i = 0; i < n; i++) {
		out.append(Input(h_, input_id(i)));
	}
	return out;
}

inline Input Node::effect_input() const
{
	char buf[256];
	buf[0] = '\0';
	int element = -1;
	if (oakengine_node_get_effect_input(h_, buf, sizeof(buf), &element) != 0) {
		return Input();
	}
	return Input(h_, QString::fromUtf8(buf), element);
}

/**
 * @brief Wrapper around an opaque OakEngineKeyframe handle (borrowed view).
 */
class Keyframe
{
public:
	Keyframe() = default;

	/// Wrap a borrowed engine handle (no ownership transfer).
	Keyframe(OakEngineKeyframe *h) : h_(h) {}

	OakEngineKeyframe *handle() const { return h_; }
	operator OakEngineKeyframe *() const { return h_; }

	bool is_null() const { return h_ == nullptr; }

	bool operator==(const Keyframe &o) const { return h_ == o.h_; }
	bool operator!=(const Keyframe &o) const { return h_ != o.h_; }
	bool operator==(std::nullptr_t) const { return h_ == nullptr; }
	bool operator!=(std::nullptr_t) const { return h_ != nullptr; }

	/// Node that owns this keyframe.
	Node node() const { return Node(oakengine_keyframe_get_node(h_)); }

	/// Id of the input that owns this keyframe.
	QString input_id() const
	{
		char buf[256];
		buf[0] = '\0';
		oakengine_keyframe_get_input_id(h_, buf, sizeof(buf));
		return QString::fromUtf8(buf);
	}

	int element() const { return oakengine_keyframe_get_element(h_); }
	int track() const { return oakengine_keyframe_get_track(h_); }

	/// The keyframe's time as a rational (num/den).
	void time(int64_t *num, int64_t *den) const { oakengine_keyframe_get_time(h_, num, den); }

	/// Easing type (0=linear, 1=bezier, 2=hold; -1 on null).
	int type() const { return oakengine_keyframe_get_type(h_); }

	/// The keyframe's value on its track (POD; convert via app/common/oakvaluehelper.h).
	oak_node_value value() const
	{
		oak_node_value v{};
		oakengine_keyframe_get_value(h_, &v);
		return v;
	}

	/// Bezier control point (0 = in-handle, 1 = out-handle).
	QPointF bezier_point(int point_index) const
	{
		double x = 0, y = 0;
		oakengine_keyframe_get_bezier_point(h_, point_index, &x, &y);
		return QPointF(x, y);
	}

	/// Bezier control point, or the identity point for non-bezier keyframes.
	QPointF valid_bezier_point(int point_index) const
	{
		double x = 0, y = 0;
		oakengine_keyframe_get_valid_bezier_point(h_, point_index, &x, &y);
		return QPointF(x, y);
	}

	/// True if a sibling keyframe exists at `time_ts` on a different track of the same input.
	bool has_sibling_at_time(int64_t time_ts, int track) const
	{
		return oakengine_keyframe_has_sibling_at_time(h_, time_ts, track) != 0;
	}

private:
	OakEngineKeyframe *h_ = nullptr;
};

/**
 * @brief Value type identifying a specific keyframe track on a node input.
 *
 * Mirrors the engine's olive::NodeKeyframeTrackReference. A track index of
 * -1 means "the whole input" (no specific track selected).
 */
class KeyframeTrackRef
{
public:
	KeyframeTrackRef() : track_(-1) {}

	KeyframeTrackRef(const Input &input, int track = 0)
		: input_(input), track_(track) {}

	const Input &input() const { return input_; }
	int track() const { return track_; }

	bool is_valid() const { return input_.is_valid() && track_ >= 0; }

	bool operator==(const KeyframeTrackRef &o) const
	{
		return input_ == o.input_ && track_ == o.track_;
	}
	bool operator!=(const KeyframeTrackRef &o) const { return !(*this == o); }

	/// Number of keyframes on this track.
	int keyframe_count() const
	{
		if (!is_valid()) {
			return 0;
		}
		return oakengine_node_keyframe_count_on_track(
			input_.node_handle(), input_.input_id().toUtf8().constData(),
			input_.element(), track_);
	}

	/// Borrowed keyframe at on-track `index` (null when out of range).
	Keyframe keyframe_at(int index) const
	{
		if (!is_valid()) {
			return Keyframe();
		}
		return Keyframe(oakengine_node_keyframe_handle_on_track(
			input_.node_handle(), input_.input_id().toUtf8().constData(),
			input_.element(), track_, index));
	}

	/// All keyframes on this track.
	QVector<Keyframe> keyframes() const
	{
		QVector<Keyframe> out;
		const int n = keyframe_count();
		out.reserve(n);
		for (int i = 0; i < n; i++) {
			out.append(keyframe_at(i));
		}
		return out;
	}

	void reset() { *this = KeyframeTrackRef(); }

private:
	Input input_;
	int track_;
};

/**
 * @brief Value type identifying an input on a node without an array element.
 *
 * Mirrors the engine's olive::NodeInputPair ({node, input} — NO element);
 * used as the array-widget key.
 */
class InputPair
{
public:
	InputPair() = default;

	InputPair(OakEngineNode *node, const QString &input_id)
		: node_(node), input_(input_id) {}

	OakEngineNode *node_handle() const { return node_; }
	Node node() const { return Node(node_); }
	const QString &input_id() const { return input_; }

	bool operator==(const InputPair &o) const
	{
		return node_ == o.node_ && input_ == o.input_;
	}
	bool operator!=(const InputPair &o) const { return !(*this == o); }

private:
	OakEngineNode *node_ = nullptr;
	QString input_;
};

/* ---- Out-of-line Node methods needing complete helper types ----------------- */

inline NodeConnection Node::output_connection_at_ex(int index) const
{
	NodeConnection c;
	char buf[256];
	buf[0] = '\0';
	OakEngineNode *n = nullptr;
	int element = -1;
	int hidden = 0;
	if (oakengine_node_output_connection_at_ex(h_, index, &n, buf, sizeof(buf), &element, &hidden) != 0) {
		return c;
	}
	c.node = Node(n);
	c.input_id = QString::fromUtf8(buf);
	c.element = element;
	c.hidden = hidden != 0;
	return c;
}

inline NodeConnection Node::input_connection_at_all(int index) const
{
	NodeConnection c;
	char buf[256];
	buf[0] = '\0';
	OakEngineNode *n = nullptr;
	OakEngineNode *src = nullptr;
	int element = -1;
	int hidden = 0;
	if (oakengine_node_input_connection_at_all(h_, index, &n, buf, sizeof(buf), &element, &src, &hidden) != 0) {
		return c;
	}
	c.node = Node(n);
	c.input_id = QString::fromUtf8(buf);
	c.element = element;
	c.hidden = hidden != 0;
	c.source_node = Node(src);
	return c;
}

inline ContextNodeItem Node::context_node_at(int index) const
{
	ContextNodeItem item;
	double x = 0, y = 0;
	int exp = 0;
	item.node = Node(oakengine_node_context_node_at(h_, index, &x, &y, &exp));
	item.position = QPointF(x, y);
	item.expanded = exp != 0;
	return item;
}

inline QVector<Keyframe> Input::keyframes_at_time(int64_t time_num, int64_t time_den) const
{
	QVector<Keyframe> out;
	if (!is_valid()) {
		return out;
	}
	QVector<OakEngineKeyframe *> buf(128);
	int n = oakengine_node_keyframes_at_time(
		node_, input_.toUtf8().constData(), element_, time_num, time_den,
		buf.data(), buf.size());
	if (n == buf.size()) {
		// Rare: more keyframes at one instant than the first buffer held
		buf.resize(4096);
		n = oakengine_node_keyframes_at_time(
			node_, input_.toUtf8().constData(), element_, time_num, time_den,
			buf.data(), buf.size());
	}
	out.reserve(n);
	for (int i = 0; i < n; i++) {
		out.append(Keyframe(buf.at(i)));
	}
	return out;
}

/// qHash support so wrappers can be used as QHash keys.
inline uint qHash(const Node &n, uint seed = 0)
{
	return ::qHash(reinterpret_cast<quintptr>(n.handle()), seed);
}
inline uint qHash(const Project &p, uint seed = 0)
{
	return ::qHash(reinterpret_cast<quintptr>(p.handle()), seed);
}
inline uint qHash(const Input &i, uint seed = 0)
{
	uint h = ::qHash(reinterpret_cast<quintptr>(i.node_handle()), seed);
	h ^= ::qHash(i.input_id(), seed);
	h ^= ::qHash(i.element(), seed);
	return h;
}
inline uint qHash(const Keyframe &k, uint seed = 0)
{
	return ::qHash(reinterpret_cast<quintptr>(k.handle()), seed);
}
inline uint qHash(const KeyframeTrackRef &r, uint seed = 0)
{
	return qHash(r.input(), seed) ^ ::qHash(r.track(), seed);
}
inline uint qHash(const InputPair &p, uint seed = 0)
{
	return ::qHash(reinterpret_cast<quintptr>(p.node_handle()), seed) ^
		   ::qHash(p.input_id(), seed);
}

} // namespace oak

Q_DECLARE_METATYPE(oak::Input)
Q_DECLARE_METATYPE(oak::KeyframeTrackRef)
Q_DECLARE_METATYPE(oak::InputPair)

#endif // OAKUTIL_OAKNODE_H
