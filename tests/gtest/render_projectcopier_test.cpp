#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QUuid>
#include <QVector>

#include "codec/proxymanager.h"
#include "core.h"
#include "node/color/colormanager/colormanager.h"
#include "node/generator/solid/solid.h"
#include "node/group/group.h"
#include "node/math/math/math.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "render/audioplaybackcache.h"
#include "render/diskmanager.h"
#include "render/playbackcache.h"
#include "render/projectcopier.h"

namespace
{

// Exposes the protected Validate() hook and records the virtual event
// callbacks so the invalidate/persist paths can be observed from the tests.
class TestPlaybackCache : public olive::PlaybackCache {
public:
	explicit TestPlaybackCache(QObject *parent = nullptr)
		: olive::PlaybackCache(parent)
	{
	}

	using olive::PlaybackCache::validate;

	int invalidate_event_count = 0;
	int load_state_event_count = 0;
	int save_state_event_count = 0;

protected:
	virtual void InvalidateEvent(const olive::TimeRange &) override
	{
		invalidate_event_count++;
	}

	virtual void LoadStateEvent(QDataStream &) override
	{
		load_state_event_count++;
	}

	virtual void SaveStateEvent(QDataStream &) override
	{
		save_state_event_count++;
	}
};

} // namespace

class RenderCopierTestBase : public ::testing::Test {
protected:
	void SetUp() override
	{
		if (!temp_dir_.isValid()) {
			GTEST_FAIL() << "Failed to create temporary directory";
		}

		olive::ColorManager::set_up_default_config();

		if (!olive::Core::instance()) {
			// Leaked intentionally: Core is process-wide (matches
			// render_diskcache_test)
			new olive::Core(olive::Core::CoreParams());
		}

		olive::DiskManager::create_instance();

		// Point the project cache at a folder alongside the (unsaved) project
		// file so every cache read/write stays inside the temporary directory.
		project_ = std::make_unique<olive::Project>();
		project_->initialize();
		project_->set_filename(
			QDir(temp_dir_.path()).filePath(QStringLiteral("test.ove")));
		project_->set_cache_location_setting(
			olive::Project::k_cache_store_alongside_project);
	}

	void TearDown() override
	{
		copier_.reset();
		project_.reset();
		olive::DiskManager::destroy_instance();
	}

	template <typename T> T *add_node()
	{
		T *node = new T();
		node->setParent(project_.get());
		return node;
	}

	QString cache_root() const
	{
		return QDir(temp_dir_.path()).filePath(QStringLiteral("cache"));
	}

	QTemporaryDir temp_dir_;
	std::unique_ptr<olive::Project> project_;
	std::unique_ptr<olive::ProjectCopier> copier_;
};

class RenderProjectCopierTest : public RenderCopierTestBase {
};

class RenderPlaybackCacheTest : public RenderCopierTestBase {
};

TEST_F(RenderProjectCopierTest, CopyIsSeparateProjectWithSameStructure)
{
	auto *math_a = add_node<olive::MathNode>();
	auto *math_b = add_node<olive::MathNode>();
	add_node<olive::SolidGenerator>();

	olive::Node::connect_edge(
		math_a, olive::NodeInput(math_b, olive::MathNode::k_param_a_in));

	project_->set_setting(QStringLiteral("copier_test_key"),
						 QStringLiteral("copier_test_value"));

	copier_ = std::make_unique<olive::ProjectCopier>();
	copier_->set_project(project_.get());

	olive::Project *copy = copier_->get_copied_project();
	ASSERT_NE(copy, nullptr);
	EXPECT_NE(copy, project_.get());

	// The copy is marked as an in-memory render proxy
	EXPECT_TRUE(copy->property("_oak_render_proxy").toBool());

	// Same number of nodes, same IDs, different objects
	ASSERT_EQ(copy->nodes().size(), project_->nodes().size());
	EXPECT_EQ(copier_->get_node_map().size(), project_->nodes().size());
	for (olive::Node *original : project_->nodes()) {
		olive::Node *cloned = copier_->get_copy(original);
		ASSERT_NE(cloned, nullptr);
		EXPECT_NE(cloned, original);
		EXPECT_EQ(cloned->id(), original->id());
		EXPECT_EQ(copier_->get_original(cloned), original);
		EXPECT_TRUE(copy->nodes().contains(cloned));
	}

	// Settings were copied
	EXPECT_EQ(copy->get_setting(QStringLiteral("copier_test_key")),
			  QStringLiteral("copier_test_value"));

	// The pre-existing edge was recreated between the copies
	olive::Node *copy_a = copier_->get_copy(math_a);
	olive::Node *copy_b = copier_->get_copy(math_b);
	ASSERT_EQ(copy_b->input_connections().size(), 1);
	EXPECT_EQ(copy_b->input_connections().at(
				  olive::NodeInput(copy_b, olive::MathNode::k_param_a_in)),
			  copy_a);

	// SetProject applies the initial sync synchronously
	EXPECT_FALSE(copier_->has_updates_in_queue());
}

TEST_F(RenderProjectCopierTest, CopiedNodesHaveDisabledCachesAndSharedUuids)
{
	auto *math = add_node<olive::MathNode>();

	copier_ = std::make_unique<olive::ProjectCopier>();
	copier_->set_project(project_.get());

	olive::Node *copy = copier_->get_copy(math);
	ASSERT_NE(copy, nullptr);

	// Caches are disabled on the render-proxy copy but keep the same UUIDs so
	// the copy can read frames written by the original
	EXPECT_TRUE(math->are_caches_enabled());
	EXPECT_FALSE(copy->are_caches_enabled());
	EXPECT_EQ(copy->video_frame_cache()->get_uuid(),
			  math->video_frame_cache()->get_uuid());
	EXPECT_EQ(copy->audio_playback_cache()->get_uuid(),
			  math->audio_playback_cache()->get_uuid());
}

TEST_F(RenderProjectCopierTest, AddedNodeSignalFiresForEachCopiedNode)
{
	add_node<olive::MathNode>();
	add_node<olive::SolidGenerator>();

	copier_ = std::make_unique<olive::ProjectCopier>();

	QVector<olive::Node *> added;
	QObject::connect(copier_.get(), &olive::ProjectCopier::added_node,
					 [&added](olive::Node *n) { added.append(n); });

	copier_->set_project(project_.get());

	// One signal per node in the original project (color manager, root folder,
	// and the two nodes added above), carrying the *original* pointers
	EXPECT_EQ(added.size(), project_->nodes().size());
	for (olive::Node *n : project_->nodes()) {
		EXPECT_TRUE(added.contains(n));
	}
}

TEST_F(RenderProjectCopierTest, QueuedNodeAddIsAppliedByProcessUpdateQueue)
{
	copier_ = std::make_unique<olive::ProjectCopier>();
	copier_->set_project(project_.get());

	QVector<olive::Node *> added;
	QObject::connect(copier_.get(), &olive::ProjectCopier::added_node,
					 [&added](olive::Node *n) { added.append(n); });

	auto *math = add_node<olive::MathNode>();

	// The change is queued, not applied immediately
	EXPECT_TRUE(copier_->has_updates_in_queue());
	EXPECT_EQ(copier_->get_copy(math), nullptr);
	EXPECT_TRUE(added.isEmpty());

	copier_->process_update_queue();

	EXPECT_FALSE(copier_->has_updates_in_queue());
	olive::Node *copy = copier_->get_copy(math);
	ASSERT_NE(copy, nullptr);
	EXPECT_EQ(copy->id(), math->id());
	EXPECT_TRUE(copier_->get_copied_project()->nodes().contains(copy));
	ASSERT_EQ(added.size(), 1);
	EXPECT_EQ(added.first(), math);

	// Processing the queue marked the copy as modified
	EXPECT_TRUE(copier_->get_copied_project()->is_modified());
}

TEST_F(RenderProjectCopierTest, QueuedNodeRemoveDeletesCopy)
{
	auto *math = add_node<olive::MathNode>();

	copier_ = std::make_unique<olive::ProjectCopier>();
	copier_->set_project(project_.get());

	olive::Node *copy = copier_->get_copy(math);
	ASSERT_NE(copy, nullptr);

	QVector<olive::Node *> removed;
	QObject::connect(copier_.get(), &olive::ProjectCopier::removed_node,
					 [&removed](olive::Node *n) { removed.append(n); });

	delete math;
	EXPECT_TRUE(copier_->has_updates_in_queue());

	copier_->process_update_queue();

	EXPECT_FALSE(copier_->has_updates_in_queue());
	ASSERT_EQ(removed.size(), 1);
	EXPECT_EQ(removed.first(), math);
	EXPECT_EQ(copier_->get_copy(math), nullptr);
	EXPECT_FALSE(copier_->get_copied_project()->nodes().contains(copy));
}

TEST_F(RenderProjectCopierTest, QueuedEdgeAddAndRemoveAreMirrored)
{
	auto *src = add_node<olive::MathNode>();
	auto *dst = add_node<olive::MathNode>();

	copier_ = std::make_unique<olive::ProjectCopier>();
	copier_->set_project(project_.get());

	olive::Node *copy_src = copier_->get_copy(src);
	olive::Node *copy_dst = copier_->get_copy(dst);
	ASSERT_NE(copy_src, nullptr);
	ASSERT_NE(copy_dst, nullptr);

	olive::Node::connect_edge(
		src, olive::NodeInput(dst, olive::MathNode::k_param_a_in));
	EXPECT_TRUE(copier_->has_updates_in_queue());
	EXPECT_TRUE(copy_dst->input_connections().empty());

	copier_->process_update_queue();

	ASSERT_EQ(copy_dst->input_connections().size(), 1);
	EXPECT_EQ(copy_dst->input_connections().at(
				  olive::NodeInput(copy_dst, olive::MathNode::k_param_a_in)),
			  copy_src);

	olive::Node::disconnect_edge(
		src, olive::NodeInput(dst, olive::MathNode::k_param_a_in));
	copier_->process_update_queue();

	EXPECT_TRUE(copy_dst->input_connections().empty());
	EXPECT_TRUE(copy_src->output_connections().empty());
}

TEST_F(RenderProjectCopierTest, QueuedValueChangeKeepsCopyIndependentUntilProcessed)
{
	auto *math = add_node<olive::MathNode>();

	copier_ = std::make_unique<olive::ProjectCopier>();
	copier_->set_project(project_.get());

	olive::Node *copy = copier_->get_copy(math);
	ASSERT_NE(copy, nullptr);

	const double before =
		math->get_standard_value(olive::MathNode::k_param_a_in).toDouble();
	const double changed = before + 2.5;
	math->set_standard_value(olive::MathNode::k_param_a_in, changed);

	EXPECT_TRUE(copier_->has_updates_in_queue());
	// The copy must not change until the queue is processed
	EXPECT_DOUBLE_EQ(
		copy->get_standard_value(olive::MathNode::k_param_a_in).toDouble(), before);

	copier_->process_update_queue();

	EXPECT_DOUBLE_EQ(
		copy->get_standard_value(olive::MathNode::k_param_a_in).toDouble(), changed);
	EXPECT_DOUBLE_EQ(
		math->get_standard_value(olive::MathNode::k_param_a_in).toDouble(), changed);
}

TEST_F(RenderProjectCopierTest, QueuedValueHintChangeIsMirrored)
{
	auto *math = add_node<olive::MathNode>();

	copier_ = std::make_unique<olive::ProjectCopier>();
	copier_->set_project(project_.get());

	olive::Node *copy = copier_->get_copy(math);
	ASSERT_NE(copy, nullptr);

	const olive::Node::ValueHint hint({ olive::NodeValue::k_float }, 7,
									  QStringLiteral("copier_hint"));
	math->set_value_hint_for_input(olive::MathNode::k_param_a_in, hint);
	EXPECT_TRUE(copier_->has_updates_in_queue());

	copier_->process_update_queue();

	const olive::Node::ValueHint copied_hint =
		copy->get_value_hint_for_input(olive::MathNode::k_param_a_in);
	EXPECT_EQ(copied_hint.tag(), QStringLiteral("copier_hint"));
	EXPECT_EQ(copied_hint.index(), 7);
	ASSERT_EQ(copied_hint.types().size(), 1);
	EXPECT_EQ(copied_hint.types().first(), olive::NodeValue::k_float);
}

TEST_F(RenderProjectCopierTest, QueuedProjectSettingChangeIsMirrored)
{
	copier_ = std::make_unique<olive::ProjectCopier>();
	copier_->set_project(project_.get());

	project_->set_setting(QStringLiteral("copier_late_key"),
						 QStringLiteral("copier_late_value"));
	EXPECT_TRUE(copier_->has_updates_in_queue());
	EXPECT_TRUE(copier_->get_copied_project()
					->get_setting(QStringLiteral("copier_late_key"))
					.isEmpty());

	copier_->process_update_queue();

	EXPECT_EQ(copier_->get_copied_project()->get_setting(
				  QStringLiteral("copier_late_key")),
			  QStringLiteral("copier_late_value"));
}

TEST_F(RenderProjectCopierTest, GroupNodesAreNotCopied)
{
	copier_ = std::make_unique<olive::ProjectCopier>();
	copier_->set_project(project_.get());

	const int copy_count_before =
		copier_->get_copied_project()->nodes().size();

	auto *group = add_node<olive::NodeGroup>();
	copier_->process_update_queue();

	// Group nodes are dummies for rendering and must not appear in the copy
	EXPECT_EQ(copier_->get_copy(group), nullptr);
	EXPECT_EQ(copier_->get_copied_project()->nodes().size(), copy_count_before);
	for (olive::Node *n : copier_->get_copied_project()->nodes()) {
		EXPECT_NE(n->id(), group->id());
	}
}

TEST_F(RenderProjectCopierTest, GraphChangeTimeAdvancesAndSyncCatchesUp)
{
	copier_ = std::make_unique<olive::ProjectCopier>();
	copier_->set_project(project_.get());

	// SetProject leaves the sync point at or after the graph change point
	EXPECT_GE(copier_->get_last_update_time().value(),
			  copier_->get_graph_change_time().value());

	add_node<olive::MathNode>();

	// A queued change moves the graph change point ahead of the sync point
	EXPECT_GT(copier_->get_graph_change_time().value(),
			  copier_->get_last_update_time().value());

	copier_->process_update_queue();

	EXPECT_GE(copier_->get_last_update_time().value(),
			  copier_->get_graph_change_time().value());
}

TEST_F(RenderProjectCopierTest, FootageProxySettingsSyncToCopyImmediately)
{
	auto *footage = add_node<olive::Footage>();

	copier_ = std::make_unique<olive::ProjectCopier>();
	copier_->set_project(project_.get());

	olive::Footage *copy = copier_->get_copy(footage);
	ASSERT_NE(copy, nullptr);
	EXPECT_FALSE(copy->proxy_enabled());

	// Proxy settings are not Node inputs, so the copier mirrors them through a
	// direct connection without involving the update queue
	footage->set_proxy(QStringLiteral("/cache/proxy/example.mp4"),
					  olive::ProxyManager::k_proxy_ready, 2, 3, true);

	EXPECT_FALSE(copier_->has_updates_in_queue());
	EXPECT_TRUE(copy->proxy_enabled());
	EXPECT_EQ(copy->proxy_path(), QStringLiteral("/cache/proxy/example.mp4"));
	EXPECT_EQ(copy->proxy_state(), olive::ProxyManager::k_proxy_ready);
	EXPECT_EQ(copy->proxy_video_stream_index(), 2);
	EXPECT_EQ(copy->proxy_preset_version(), 3);

	footage->set_proxy(QString(), olive::ProxyManager::k_proxy_missing, -1, 0,
					  false);
	EXPECT_FALSE(copy->proxy_enabled());
	EXPECT_EQ(copy->proxy_state(), olive::ProxyManager::k_proxy_missing);
}

TEST_F(RenderProjectCopierTest, SetProjectTwiceReplacesCopyContents)
{
	auto *first = add_node<olive::MathNode>();

	copier_ = std::make_unique<olive::ProjectCopier>();
	copier_->set_project(project_.get());
	ASSERT_NE(copier_->get_copy(first), nullptr);

	olive::Project second_project;
	second_project.initialize();
	auto *second = new olive::SolidGenerator();
	second->setParent(&second_project);

	copier_->set_project(&second_project);

	// Nodes from the first project are gone, nodes from the second are present
	EXPECT_EQ(copier_->get_copy(first), nullptr);
	EXPECT_NE(copier_->get_copy(second), nullptr);
	EXPECT_EQ(copier_->get_copied_project()->nodes().size(),
			  second_project.nodes().size());
	EXPECT_FALSE(copier_->has_updates_in_queue());
}

TEST_F(RenderProjectCopierTest, SetProjectNullStopsTracking)
{
	copier_ = std::make_unique<olive::ProjectCopier>();
	copier_->set_project(project_.get());

	add_node<olive::MathNode>();
	EXPECT_TRUE(copier_->has_updates_in_queue());

	copier_->set_project(nullptr);

	// Pending changes are discarded and the original is no longer tracked
	EXPECT_FALSE(copier_->has_updates_in_queue());
	EXPECT_NE(copier_->get_copied_project(), nullptr);

	add_node<olive::SolidGenerator>();
	EXPECT_FALSE(copier_->has_updates_in_queue());
}

TEST_F(RenderPlaybackCacheTest, UuidAndSavingFlagAccessors)
{
	auto *node = add_node<olive::MathNode>();
	TestPlaybackCache cache(node);

	EXPECT_FALSE(cache.get_uuid().isNull());
	EXPECT_EQ(cache.parent(), node);
	EXPECT_TRUE(cache.is_saving_enabled());
	EXPECT_NE(cache.mutex(), nullptr);

	TestPlaybackCache other(node);
	EXPECT_NE(other.get_uuid(), cache.get_uuid());

	const QUuid uuid = QUuid::createUuid();
	cache.set_uuid(uuid);
	EXPECT_EQ(cache.get_uuid(), uuid);

	cache.set_saving_enabled(false);
	EXPECT_FALSE(cache.is_saving_enabled());
}

TEST_F(RenderPlaybackCacheTest, ValidateAndInvalidateBookkeeping)
{
	auto *node = add_node<olive::MathNode>();
	TestPlaybackCache cache(node);

	QVector<olive::TimeRange> validated_signals;
	QVector<olive::TimeRange> invalidated_signals;
	QObject::connect(&cache, &olive::PlaybackCache::validated,
					 [&validated_signals](const olive::TimeRange &r) {
						 validated_signals.append(r);
					 });
	QObject::connect(&cache, &olive::PlaybackCache::invalidated,
					 [&invalidated_signals](const olive::TimeRange &r) {
						 invalidated_signals.append(r);
					 });

	const olive::TimeRange whole(olive::Rational(0), olive::Rational(10));
	EXPECT_TRUE(cache.has_invalidated_ranges(whole));
	EXPECT_FALSE(cache.has_validated_ranges());

	cache.validate(whole);

	EXPECT_TRUE(cache.has_validated_ranges());
	EXPECT_TRUE(cache.get_validated_ranges().contains(whole));
	EXPECT_FALSE(cache.has_invalidated_ranges(whole));
	EXPECT_TRUE(cache.get_invalidated_ranges(olive::Rational(10)).isEmpty());
	EXPECT_EQ(cache.invalidate_event_count, 0);
	ASSERT_EQ(validated_signals.size(), 1);
	EXPECT_EQ(validated_signals.first(), whole);

	// A larger query range still reports the remainder as invalidated
	EXPECT_TRUE(cache.has_invalidated_ranges(
		olive::TimeRange(olive::Rational(0), olive::Rational(11))));

	const olive::TimeRange hole(olive::Rational(2), olive::Rational(5));
	cache.invalidate(hole);

	EXPECT_EQ(cache.invalidate_event_count, 1);
	ASSERT_EQ(invalidated_signals.size(), 1);
	EXPECT_EQ(invalidated_signals.first(), hole);
	EXPECT_TRUE(cache.has_invalidated_ranges(whole));

	const olive::TimeRangeList invalidated =
		cache.get_invalidated_ranges(olive::Rational(10));
	ASSERT_EQ(invalidated.size(), 1);
	EXPECT_EQ(invalidated.first(), hole);
}

TEST_F(RenderPlaybackCacheTest, InvalidateZeroLengthRangeIsIgnored)
{
	auto *node = add_node<olive::MathNode>();
	TestPlaybackCache cache(node);

	const olive::TimeRange whole(olive::Rational(0), olive::Rational(10));
	cache.validate(whole);

	int invalidated_count = 0;
	QObject::connect(&cache, &olive::PlaybackCache::invalidated,
					 [&invalidated_count](const olive::TimeRange &) {
						 invalidated_count++;
					 });

	// Zero-length invalidations are rejected with a warning
	cache.invalidate(olive::TimeRange(olive::Rational(5), olive::Rational(5)));

	EXPECT_EQ(invalidated_count, 0);
	EXPECT_EQ(cache.invalidate_event_count, 0);
	EXPECT_TRUE(cache.get_validated_ranges().contains(whole));
}

TEST_F(RenderPlaybackCacheTest, GetInvalidatedRangesClampsNegativeTimes)
{
	auto *node = add_node<olive::MathNode>();
	TestPlaybackCache cache(node);

	// Nothing is validated, so the whole (clamped) range is invalidated
	const olive::TimeRangeList invalidated = cache.get_invalidated_ranges(
		olive::TimeRange(olive::Rational(-5), olive::Rational(10)));

	ASSERT_EQ(invalidated.size(), 1);
	EXPECT_EQ(invalidated.first().in(), olive::Rational(0));
	EXPECT_EQ(invalidated.first().out(), olive::Rational(10));
}

TEST_F(RenderPlaybackCacheTest, PassthroughCoversInvalidatedRanges)
{
	auto *node = add_node<olive::MathNode>();
	TestPlaybackCache source(node);
	TestPlaybackCache dest(node);

	const olive::TimeRange whole(olive::Rational(0), olive::Rational(10));
	source.validate(whole);

	dest.set_passthrough(&source);

	ASSERT_EQ(dest.get_passthroughs().size(), 1);
	EXPECT_EQ(dest.get_passthroughs().front().cache, source.get_uuid());
	EXPECT_EQ(dest.get_passthroughs().front().in(), whole.in());
	EXPECT_EQ(dest.get_passthroughs().front().out(), whole.out());

	// GetInvalidatedRanges honors passthroughs ...
	EXPECT_TRUE(dest.get_invalidated_ranges(olive::Rational(10)).isEmpty());
	// ... but HasInvalidatedRanges only looks at locally validated ranges
	EXPECT_TRUE(dest.has_invalidated_ranges(whole));

	// Invalidating trims the passthrough too
	const olive::TimeRange hole(olive::Rational(2), olive::Rational(3));
	dest.invalidate(hole);
	const olive::TimeRangeList invalidated =
		dest.get_invalidated_ranges(olive::Rational(10));
	ASSERT_EQ(invalidated.size(), 1);
	EXPECT_EQ(invalidated.first(), hole);
}

TEST_F(RenderPlaybackCacheTest, PassthroughChainsAcrossCaches)
{
	auto *node = add_node<olive::MathNode>();
	TestPlaybackCache a(node);
	TestPlaybackCache b(node);
	TestPlaybackCache c(node);

	a.validate(olive::TimeRange(olive::Rational(0), olive::Rational(10)));
	b.set_passthrough(&a);
	c.set_passthrough(&b);

	// c inherits b's passthrough of a
	ASSERT_EQ(c.get_passthroughs().size(), 1);
	EXPECT_EQ(c.get_passthroughs().front().cache, a.get_uuid());
	EXPECT_TRUE(c.get_invalidated_ranges(olive::Rational(10)).isEmpty());
}

TEST_F(RenderPlaybackCacheTest, RequestResignalAndClear)
{
	auto *node = add_node<olive::MathNode>();
	TestPlaybackCache cache(node);

	int requested_count = 0;
	olive::TimeRange last_requested;
	QObject::connect(&cache, &olive::PlaybackCache::requested,
					 [&requested_count, &last_requested](
						 olive::ViewerOutput *context,
						 const olive::TimeRange &r) {
						 EXPECT_EQ(context, nullptr);
						 requested_count++;
						 last_requested = r;
					 });

	const olive::TimeRange first(olive::Rational(0), olive::Rational(5));
	const olive::TimeRange second(olive::Rational(10), olive::Rational(20));

	cache.request(nullptr, first);
	EXPECT_EQ(requested_count, 1);
	EXPECT_EQ(last_requested, first);

	cache.request(nullptr, second);
	EXPECT_EQ(requested_count, 2);

	// Both pending ranges are re-signaled
	cache.resignal_requests();
	EXPECT_EQ(requested_count, 4);

	// Clearing one range leaves the other pending
	cache.clear_request_range(first);
	cache.resignal_requests();
	EXPECT_EQ(requested_count, 5);
	EXPECT_EQ(last_requested, second);
}

TEST_F(RenderPlaybackCacheTest, InvalidateAllClearsEverything)
{
	auto *node = add_node<olive::MathNode>();
	TestPlaybackCache cache(node);
	TestPlaybackCache source(node);

	source.validate(olive::TimeRange(olive::Rational(0), olive::Rational(10)));
	cache.validate(olive::TimeRange(olive::Rational(0), olive::Rational(10)));
	cache.set_passthrough(&source);

	QVector<olive::TimeRange> invalidated_signals;
	QObject::connect(&cache, &olive::PlaybackCache::invalidated,
					 [&invalidated_signals](const olive::TimeRange &r) {
						 invalidated_signals.append(r);
					 });

	cache.invalidate_all();

	EXPECT_FALSE(cache.has_validated_ranges());
	EXPECT_TRUE(cache.get_passthroughs().empty());
	ASSERT_EQ(invalidated_signals.size(), 1);
	EXPECT_EQ(invalidated_signals.first(),
			  olive::TimeRange(olive::Rational(0), RATIONAL_MAX));
}

TEST_F(RenderPlaybackCacheTest, StatePersistsAcrossCaches)
{
	auto *node = add_node<olive::MathNode>();
	const QUuid uuid = QUuid::createUuid();
	const QUuid source_uuid = QUuid::createUuid();

	const olive::TimeRange valid(olive::Rational(5), olive::Rational(15));
	const olive::TimeRange pass(olive::Rational(20), olive::Rational(30));

	{
		TestPlaybackCache cache(node);
		cache.set_uuid(uuid);
		cache.validate(valid);

		TestPlaybackCache source(node);
		source.set_uuid(source_uuid);
		source.validate(pass);
		cache.set_passthrough(&source);

		EXPECT_GE(cache.save_state_event_count, 1);
	}

	const QString state_file =
		QDir(QDir(cache_root()).filePath(uuid.toString()))
			.filePath(QStringLiteral("state"));
	ASSERT_TRUE(QFileInfo::exists(state_file));

	TestPlaybackCache restored(node);
	restored.set_uuid(uuid);

	EXPECT_EQ(restored.load_state_event_count, 1);
	EXPECT_TRUE(restored.get_validated_ranges().contains(valid));
	ASSERT_EQ(restored.get_passthroughs().size(), 1);
	EXPECT_EQ(restored.get_passthroughs().front().cache, source_uuid);
	EXPECT_EQ(restored.get_passthroughs().front().in(), pass.in());
	EXPECT_EQ(restored.get_passthroughs().front().out(), pass.out());
}

TEST_F(RenderPlaybackCacheTest, CacheDirectoryHelpers)
{
	auto *node = add_node<olive::MathNode>();
	TestPlaybackCache cache(node);

	const QUuid uuid = QUuid::createUuid();
	cache.set_uuid(uuid);

	EXPECT_EQ(olive::PlaybackCache::get_this_cache_directory(
				  QStringLiteral("/base/path"), uuid),
			  QDir(QDir(QStringLiteral("/base/path")).filePath(
				  uuid.toString())));

	EXPECT_EQ(cache.get_this_cache_directory(),
			  QDir(QDir(cache_root()).filePath(uuid.toString())));

	EXPECT_GT(olive::PlaybackCache::get_cache_indicator_height(), 0);
}

TEST_F(RenderPlaybackCacheTest, DrawPaintsValidatedRangesGreen)
{
	auto *node = add_node<olive::MathNode>();
	TestPlaybackCache cache(node);
	cache.validate(olive::TimeRange(olive::Rational(2), olive::Rational(5)));

	QImage image(100, 10, QImage::Format_RGB32);
	image.fill(Qt::black);
	{
		QPainter painter(&image);
		cache.draw(&painter, olive::Rational(0), 10.0, QRect(0, 0, 100, 10));
	}

	// 10 px/s: validated seconds [2,5) cover pixels [20,50)
	EXPECT_EQ(image.pixelColor(30, 5), QColor(Qt::green));
	EXPECT_EQ(image.pixelColor(10, 5), QColor(Qt::red));
	EXPECT_EQ(image.pixelColor(60, 5), QColor(Qt::red));
}

TEST_F(RenderPlaybackCacheTest, AudioParametersRoundTrip)
{
	auto *node = add_node<olive::MathNode>();
	olive::AudioPlaybackCache cache(node);

	const olive::AudioParams params(48000, olive::k_channel_layout_stereo,
									olive::SampleFormat::f32_p);
	cache.set_parameters(params);
	EXPECT_TRUE(cache.get_parameters() == params);
	EXPECT_EQ(cache.get_parameters().sample_rate(), 48000);
	EXPECT_EQ(cache.get_parameters().channel_count(), 2);

	// Setting identical parameters again takes the no-op path
	cache.set_parameters(params);
	EXPECT_TRUE(cache.get_parameters() == params);

	const olive::AudioParams other(44100, olive::k_channel_layout_mono,
								   olive::SampleFormat::f32_p);
	cache.set_parameters(other);
	EXPECT_EQ(cache.get_parameters().sample_rate(), 44100);
	EXPECT_EQ(cache.get_parameters().channel_count(), 1);
}

// NOTE: AudioPlaybackCache::WriteSilence() is intentionally not exercised: it
// calls WritePCM() with an empty SampleBuffer, which makes
// WritePartOfSampleBuffer() loop forever (the write cursor never advances when
// the buffer provides no bytes). See the bug notes in the test report.
TEST_F(RenderPlaybackCacheTest, WritePcmValidatesRangesAndWritesSegments)
{
	auto *node = add_node<olive::MathNode>();
	olive::AudioPlaybackCache cache(node);

	const olive::AudioParams params(48000, olive::k_channel_layout_stereo,
									olive::SampleFormat::f32_p);
	cache.set_parameters(params);

	// Two adjacent 0.1s ranges at 48kHz stereo float
	const olive::TimeRange r1(olive::Rational(0), olive::Rational(1, 10));
	const olive::TimeRange r2(olive::Rational(1, 10), olive::Rational(1, 5));
	const olive::TimeRange whole(olive::Rational(0), olive::Rational(1, 5));

	const qint64 total_bytes = params.time_to_bytes_per_channel(whole.length());
	const qint64 range_bytes = params.time_to_bytes_per_channel(r1.length());
	ASSERT_GT(total_bytes, 0);
	ASSERT_GT(range_bytes, 0);

	olive::SampleBuffer samples(params,
								size_t(params.time_to_samples(whole.length())));
	ASSERT_TRUE(samples.is_allocated());
	for (int ch = 0; ch < params.channel_count(); ch++) {
		for (int64_t i = 0; i < params.time_to_samples(whole.length()); i++) {
			samples.data(ch)[i] = 0.0001f * float(i) + float(ch);
		}
	}

	int validated_count = 0;
	QObject::connect(&cache, &olive::PlaybackCache::validated,
					 [&validated_count](const olive::TimeRange &) {
						 validated_count++;
					 });

	cache.write_pcm(whole, { r1, r2 }, samples);

	// Adjacent ranges merge into one validated block
	EXPECT_EQ(validated_count, 2);
	EXPECT_TRUE(cache.get_validated_ranges().contains(r1));
	EXPECT_TRUE(cache.get_validated_ranges().contains(r2));
	EXPECT_FALSE(cache.has_invalidated_ranges(whole));

	// One segment file per channel in this cache's directory
	const QDir cache_dir = cache.get_this_cache_directory();
	const QString seg_ch0 = cache_dir.filePath(QStringLiteral("0.0"));
	const QString seg_ch1 = cache_dir.filePath(QStringLiteral("0.1"));
	ASSERT_TRUE(QFileInfo::exists(seg_ch0));
	ASSERT_TRUE(QFileInfo::exists(seg_ch1));

	// The segment starts with exactly the PCM data that was written
	QFile f0(seg_ch0);
	ASSERT_TRUE(f0.open(QFile::ReadOnly));
	const QByteArray raw0 = f0.read(total_bytes);
	ASSERT_EQ(raw0.size(), total_bytes);
	EXPECT_EQ(std::memcmp(raw0.constData(), samples.data(0), size_t(total_bytes)),
			  0);

	QFile f1(seg_ch1);
	ASSERT_TRUE(f1.open(QFile::ReadOnly));
	const QByteArray raw1 = f1.read(total_bytes);
	ASSERT_EQ(raw1.size(), total_bytes);
	EXPECT_EQ(std::memcmp(raw1.constData(), samples.data(1), size_t(total_bytes)),
			  0);
}
