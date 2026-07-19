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

#ifndef OAK_IPC_SHAREDMEMORYREGION_H
#define OAK_IPC_SHAREDMEMORYREGION_H

#include <cstddef>
#include <QString>

#include "oakengine/ipc.h"

namespace olive
{
namespace ipc
{

/**
 * @brief A named, fixed-size shared memory segment mapped into the process address space.
 *
 * Consumer-side wrapper over the liboakengine C ABI: the object only holds an opaque
 * OakSharedMemoryRegion handle and forwards every call across the C boundary. The public API is
 * unchanged from the original implementation.
 *
 * One process open()s the segment with k_create (owner); the peer process open()s it by the same
 * key with k_attach. The mapping is a raw contiguous byte range accessible via data() — the IPC
 * ring buffers and frame slot pools are laid out inside it. Nothing here is locked;
 * synchronization is entirely the caller's responsibility via the lock-free structures placed in
 * the mapping.
 */
class SharedMemoryRegion {
public:
	enum Mode {
		/// Create (and own) the segment. Fails if it already exists; unlinks on destruction.
		k_create = OAK_IPC_SHM_MODE_CREATE,
		/// Attach to a segment created by the peer. Does not unlink on destruction.
		k_attach = OAK_IPC_SHM_MODE_ATTACH
	};

	SharedMemoryRegion()
		: handle_(oakengine_ipc_shm_create())
	{
	}

	~SharedMemoryRegion()
	{
		oakengine_ipc_shm_free(handle_);
	}

	SharedMemoryRegion(const SharedMemoryRegion &) = delete;
	SharedMemoryRegion &operator=(const SharedMemoryRegion &) = delete;

	/**
   * @brief Open the segment identified by `key` with the given `size` in bytes.
   *
   * `key` is a short identifier (no leading slash needed; the platform prefix is added internally).
   * Returns true on success. On failure, error() carries a human-readable reason.
   */
	bool open(const QString &key, size_t size, Mode mode)
	{
		const bool ok = oakengine_ipc_shm_open(
							handle_, key.toUtf8().constData(), size,
							static_cast<oak_ipc_shm_mode>(mode)) != 0;
		refresh_caches();
		return ok;
	}

	/**
   * @brief Unmap and (if owner) unlink the segment. Called automatically by the destructor.
   */
	void close()
	{
		oakengine_ipc_shm_close(handle_);
	}

	bool is_valid() const
	{
		return oakengine_ipc_shm_is_valid(handle_) != 0;
	}

	void *data() const
	{
		return oakengine_ipc_shm_data(handle_);
	}

	size_t size() const
	{
		return oakengine_ipc_shm_size(handle_);
	}

	const QString &key() const
	{
		return key_;
	}

	const QString &error() const
	{
		return error_;
	}

	/**
   * @brief Build a unique segment key for a worker, e.g. "olive-rw-<pid>-<index>".
   *
   * Centralized so the owner and the spawned worker agree on the same name.
   */
	static QString make_key(qint64 owner_pid, int worker_index)
	{
		const int size = oakengine_ipc_shm_make_key(owner_pid, worker_index,
													nullptr, 0);
		QByteArray buf(size + 1, '\0');
		oakengine_ipc_shm_make_key(owner_pid, worker_index, buf.data(),
								   size + 1);
		return QString::fromUtf8(buf.constData());
	}

	/**
   * @brief The wrapped C handle, for cross-type wrappers and direct C API use
   */
	OakSharedMemoryRegion *handle() const
	{
		return handle_;
	}

private:
	static QString query_string(int (*query)(const OakSharedMemoryRegion *,
											 char *, int),
								const OakSharedMemoryRegion *handle)
	{
		const int size = query(handle, nullptr, 0);
		if (size <= 0) {
			return QString();
		}
		QByteArray buf(size + 1, '\0');
		query(handle, buf.data(), size + 1);
		return QString::fromUtf8(buf.constData());
	}

	void refresh_caches()
	{
		key_ = query_string(oakengine_ipc_shm_key, handle_);
		error_ = query_string(oakengine_ipc_shm_error, handle_);
	}

	OakSharedMemoryRegion *handle_;
	QString key_;
	QString error_;
};

} // namespace ipc
} // namespace olive

#endif // OAK_IPC_SHAREDMEMORYREGION_H
