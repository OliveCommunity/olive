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

#ifndef OAK_RENDERWORKERPOOL_H
#define OAK_RENDERWORKERPOOL_H

#include <QHash>
#include <QMutex>
#include <QSet>
#include <QThread>
#include <QVector>
#include <QWaitCondition>
#include <deque>
#include <memory>

#include "codec/frame.h"
#include "node/project/serializer/serializer.h"
#include "render/rendermanager.h"

class QProcess;

namespace olive
{

// The worker IPC implementation lives behind the liboakengine C ABI facade
// (src/oliveimpl/render/ipc). This header is consumed outside the engine
// library, so it can only forward-declare the internal types used by private
// method signatures; PooledWorker (which holds IPC objects by value) is
// defined in the .cpp for the same reason.
namespace engine::internal::ipc
{
class FrameSlotPool;
}

class Project;

class RenderWorkerPool : public QThread {
	Q_OBJECT
public:
	explicit RenderWorkerPool(DecoderCache *decoder_cache,
							  const QString &gpu_backend,
							  QObject *parent = nullptr);
	~RenderWorkerPool() override;

	bool submit_frame(RenderTicketPtr ticket,
					 const RenderManager::RenderVideoParams &params);

	bool remove_ticket(RenderTicketPtr ticket);

	void shutdown();

protected:
	void run() override;

private:
	struct Job {
		Job(RenderTicketPtr t, const RenderManager::RenderVideoParams &p)
			: ticket(t)
			, params(p)
		{
		}

		RenderTicketPtr ticket;
		RenderManager::RenderVideoParams params;
		QString graph_path;
		QString node_token;
		QVector<FramePtr> input_frames;
	};

	enum class JobResult {
		k_finished,
		k_retryable_failure,
		k_fatal_failure,
		k_cancelled
	};

	struct ActiveJob {
		RenderTicketPtr ticket;
		qint64 process_id = 0;
		qint64 ticket_id = 0;
	};

	// Defined in the .cpp: holds the per-worker IPC shared-memory regions and
	// frame slot pools (internal oliveimpl types) by value.
	struct PooledWorker;

	struct CachedGraph {
		QString path;
	};

	bool prepare_job(RenderTicketPtr ticket,
					const RenderManager::RenderVideoParams &params, Job *job);
	bool write_graph_snapshot(Project *project, QString *path);
	bool is_supported(const RenderManager::RenderVideoParams &params) const;

	void worker_loop(int worker_index,
					std::vector<std::unique_ptr<PooledWorker>> *local_pool);
	void process_job(const Job &job, int worker_index,
					std::vector<std::unique_ptr<PooledWorker>> *local_pool);
	JobResult process_job_attempt(const Job &job, int worker_index,
								int attempt_index, PooledWorker *worker);
	void finish_with_frame(RenderTicketPtr ticket,
						 const engine::internal::ipc::FrameSlotPool &pool,
						 uint32_t slot);
	void cleanup_graph_file(const QString &path);
	void add_graph_path_ref(const QString &path);
	void add_graph_path_ref_locked(const QString &path);
	void release_graph_path_ref(const QString &path);
	void release_graph_path_ref_locked(const QString &path);
	void set_graph_path_cached(const QString &path, bool cached);
	void set_graph_path_cached_locked(const QString &path, bool cached);
	void cancel_active_process(qint64 process_id);
	void set_active_worker(int worker_index, RenderTicketPtr ticket,
						 QProcess *worker, qint64 ticket_id);
	void clear_active_worker(int worker_index, qint64 process_id);
	int worker_count() const;

	std::unique_ptr<PooledWorker>
	acquire_worker(std::vector<std::unique_ptr<PooledWorker>> *local_pool,
				  const QString &graph_path);
	void return_worker(std::vector<std::unique_ptr<PooledWorker>> *local_pool,
					  std::unique_ptr<PooledWorker> worker, bool keep_alive);
	void shutdown_worker(PooledWorker *worker);
	void
	shutdown_local_pool(std::vector<std::unique_ptr<PooledWorker>> *local_pool);
	void clear_graph_cache();

	DecoderCache *decoder_cache_;
	QString gpu_backend_;
	QMutex mutex_;
	QWaitCondition wait_;
	std::deque<Job> queue_;
	bool stopping_ = false;
	QVector<ActiveJob> active_jobs_;
	QHash<QUuid, CachedGraph> graph_cache_;
	QHash<QString, int> graph_path_ref_count_;
	QSet<QString> cached_graph_paths_;

	static constexpr uint32_t k_output_slots = 2;
	static constexpr int k_max_attempts = 2;
	static constexpr int k_max_width = 4096;
	static constexpr int k_max_height = 2160;
	static constexpr int k_worker_idle_timeout_ms = 30000;
	static constexpr int k_worker_max_uses = 100;
};

}

#endif // OAK_RENDERWORKERPOOL_H
