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

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "codec/frame.h"
#include "rendermanager.h"
#include "workerprocess.h"
#include "project/serializer/serializer.h"

namespace olive
{

// The worker IPC shared-memory/frame-slot wrappers live in ipc/ (namespace
// olive::ipc). Only forward-declared here; PooledWorker (which holds IPC
// objects by value) is defined in the .cpp for the same reason.
namespace ipc
{
class FrameSlotPool;
}

class Project;

class RenderWorkerPool {
public:
	explicit RenderWorkerPool(DecoderCache *decoder_cache,
							  const std::string &gpu_backend);
	~RenderWorkerPool();

	void start();

	bool submit_frame(RenderTicketPtr ticket,
					 const RenderManager::RenderVideoParams &params);

	bool remove_ticket(RenderTicketPtr ticket);

	void shutdown();

private:
	struct Job {
		Job(RenderTicketPtr t, const RenderManager::RenderVideoParams &p)
			: ticket(t)
			, params(p)
		{
		}

		RenderTicketPtr ticket;
		RenderManager::RenderVideoParams params;
		std::string graph_path;
		std::string node_token;
		std::vector<FramePtr> input_frames;
	};

	enum class JobResult {
		k_finished,
		k_retryable_failure,
		k_fatal_failure,
		k_cancelled
	};

	struct ActiveJob {
		RenderTicketPtr ticket;
		int64_t process_id = 0;
		int64_t ticket_id = 0;
	};

	// Defined in the .cpp: holds the per-worker IPC shared-memory regions and
	// frame slot pools by value.
	struct PooledWorker;

	struct CachedGraph {
		std::string path;
	};

	void run();

	bool prepare_job(RenderTicketPtr ticket,
					const RenderManager::RenderVideoParams &params, Job *job);
	bool write_graph_snapshot(Project *project, std::string *path);
	bool is_supported(const RenderManager::RenderVideoParams &params) const;

	void worker_loop(int worker_index,
					std::vector<std::unique_ptr<PooledWorker>> *local_pool);
	void process_job(const Job &job, int worker_index,
					std::vector<std::unique_ptr<PooledWorker>> *local_pool);
	JobResult process_job_attempt(const Job &job, int worker_index,
								int attempt_index, PooledWorker *worker);
	void finish_with_frame(RenderTicketPtr ticket,
						 const ipc::FrameSlotPool &pool, uint32_t slot);
	void cleanup_graph_file(const std::string &path);
	void add_graph_path_ref(const std::string &path);
	void add_graph_path_ref_locked(const std::string &path);
	void release_graph_path_ref(const std::string &path);
	void release_graph_path_ref_locked(const std::string &path);
	void set_graph_path_cached(const std::string &path, bool cached);
	void set_graph_path_cached_locked(const std::string &path, bool cached);
	void cancel_active_process(int64_t process_id);
	void set_active_worker(int worker_index, RenderTicketPtr ticket,
						 WorkerProcess *worker, int64_t ticket_id);
	void clear_active_worker(int worker_index, int64_t process_id);
	int worker_count() const;

	std::unique_ptr<PooledWorker>
	acquire_worker(std::vector<std::unique_ptr<PooledWorker>> *local_pool,
				  const std::string &graph_path);
	void return_worker(std::vector<std::unique_ptr<PooledWorker>> *local_pool,
					  std::unique_ptr<PooledWorker> worker, bool keep_alive);
	void shutdown_worker(PooledWorker *worker);
	void
	shutdown_local_pool(std::vector<std::unique_ptr<PooledWorker>> *local_pool);
	void clear_graph_cache();

	DecoderCache *decoder_cache_;
	std::string gpu_backend_;
	std::mutex mutex_;
	std::condition_variable wait_;
	std::deque<Job> queue_;
	bool stopping_ = false;
	std::vector<ActiveJob> active_jobs_;
	std::map<std::string, CachedGraph> graph_cache_;
	std::map<std::string, int> graph_path_ref_count_;
	std::set<std::string> cached_graph_paths_;

	std::thread thread_;

	static constexpr uint32_t k_output_slots = 2;
	static constexpr int k_max_attempts = 2;
	static constexpr int k_max_width = 4096;
	static constexpr int k_max_height = 2160;
	static constexpr int k_worker_idle_timeout_ms = 30000;
	static constexpr int k_worker_max_uses = 100;
};

}

#endif // OAK_RENDERWORKERPOOL_H
