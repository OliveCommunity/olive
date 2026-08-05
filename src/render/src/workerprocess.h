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

#ifndef OAK_WORKERPROCESS_H
#define OAK_WORKERPROCESS_H

// Qt-free QProcess replacement for the render-worker control channel. The
// transport is deliberately tiny: stdin/stdout pipes carrying NDJSON lines,
// stderr redirected to a file, plus kill/wait/liveness. The wire protocol
// itself is untouched (see renderworkerpool.cpp).
//
// POSIX is fully implemented (fork/exec/poll). Windows builds keep the
// process-management helpers in renderworkerpool.cpp; the WorkerProcess class
// itself is POSIX-only for now (see M7 notes).

#include <cstdint>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace olive
{

class WorkerProcess {
public:
	WorkerProcess() = default;

	~WorkerProcess()
	{
#if !defined(_WIN32)
		if (pid_ > 0) {
			kill();
			wait_finished(-1);
		}
		close_fd(&stdin_fd_);
		close_fd(&stdout_fd_);
#endif
	}

	WorkerProcess(const WorkerProcess &) = delete;
	WorkerProcess &operator=(const WorkerProcess &) = delete;

#if !defined(_WIN32)

	/**
	 * @brief Spawn the worker with arguments, an environment-variable denylist
	 *        applied in the child, and stderr appended to stderr_file.
	 *
	 * Returns false (with error_string() set) if the fork or exec failed.
	 */
	bool start(const std::string &program,
			   const std::vector<std::string> &args,
			   const std::vector<std::string> &remove_env,
			   const std::string &stderr_file)
	{
		int stdin_pipe[2] = { -1, -1 };
		int stdout_pipe[2] = { -1, -1 };
		int err_pipe[2] = { -1, -1 };
		if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 ||
			pipe(err_pipe) != 0) {
			error_string_ = std::string("pipe: ") + strerror(errno);
			return false;
		}
		// CLOEXEC on the error pipe write end: a successful exec closes it,
		// which is how the parent distinguishes exec success from failure.
		fcntl(err_pipe[1], F_SETFD, FD_CLOEXEC);

		pid_t pid = fork();
		if (pid < 0) {
			error_string_ = std::string("fork: ") + strerror(errno);
			return false;
		}
		if (pid == 0) {
			// Child
			dup2(stdin_pipe[0], STDIN_FILENO);
			dup2(stdout_pipe[1], STDOUT_FILENO);
			close(stdin_pipe[1]);
			close(stdout_pipe[0]);
			close(err_pipe[0]);

			if (!stderr_file.empty()) {
				int fd = open(stderr_file.c_str(),
							  O_WRONLY | O_CREAT | O_APPEND, 0644);
				if (fd >= 0) {
					dup2(fd, STDERR_FILENO);
					close(fd);
				}
			}

			for (const std::string &name : remove_env) {
				unsetenv(name.c_str());
			}

			std::vector<char *> argv;
			argv.push_back(const_cast<char *>(program.c_str()));
			for (const std::string &a : args) {
				argv.push_back(const_cast<char *>(a.c_str()));
			}
			argv.push_back(nullptr);

			execvp(program.c_str(), argv.data());
			const int e = errno;
			(void)!write(err_pipe[1], &e, sizeof(e));
			_exit(127);
		}

		// Parent
		close(stdin_pipe[0]);
		close(stdout_pipe[1]);
		close(err_pipe[1]);
		stdin_fd_ = stdin_pipe[1];
		stdout_fd_ = stdout_pipe[0];

		int child_errno = 0;
		const ssize_t n = read(err_pipe[0], &child_errno, sizeof(child_errno));
		close(err_pipe[0]);
		if (n > 0) {
			error_string_ = std::string("exec: ") + strerror(child_errno);
			close_fd(&stdin_fd_);
			close_fd(&stdout_fd_);
			int status;
			waitpid(pid, &status, 0);
			return false;
		}

		pid_ = pid;
		return true;
	}

	bool is_running()
	{
		if (pid_ <= 0) {
			return false;
		}
		int status;
		const pid_t r = waitpid(pid_, &status, WNOHANG);
		if (r == pid_) {
			reap(status);
			return false;
		}
		return r == 0;
	}

	int64_t process_id() const
	{
		return pid_;
	}

	/**
	 * @brief Write the whole buffer, waiting up to timeout_ms for the pipe to
	 *        accept it (waitForBytesWritten equivalent).
	 */
	bool write_all(const char *data, size_t size, int timeout_ms)
	{
		size_t done = 0;
		while (done < size) {
			if (!poll_fd(stdin_fd_, POLLOUT, timeout_ms)) {
				return false;
			}
			const ssize_t n = write(stdin_fd_, data + done, size - done);
			if (n <= 0) {
				error_string_ = std::string("write: ") + strerror(errno);
				return false;
			}
			done += size_t(n);
		}
		return true;
	}

	bool write_all(const std::string &s, int timeout_ms)
	{
		return write_all(s.data(), s.size(), timeout_ms);
	}

	/**
	 * @brief Read one '\n'-terminated line (without the terminator).
	 *
	 * Bytes are buffered across calls. Returns false on timeout or EOF; a
	 * timeout leaves partial bytes buffered for the next call.
	 */
	bool read_line(std::string *out, int timeout_ms)
	{
		while (true) {
			const size_t nl = read_buffer_.find('\n');
			if (nl != std::string::npos) {
				*out = read_buffer_.substr(0, nl);
				read_buffer_.erase(0, nl + 1);
				return true;
			}
			if (!poll_fd(stdout_fd_, POLLIN, timeout_ms)) {
				return false;
			}
			char buf[4096];
			const ssize_t n = read(stdout_fd_, buf, sizeof(buf));
			if (n <= 0) {
				// EOF or error: peer is gone
				reap_if_done();
				return false;
			}
			read_buffer_.append(buf, size_t(n));
		}
	}

	void close_write_channel()
	{
		close_fd(&stdin_fd_);
	}

	void kill()
	{
		if (pid_ > 0) {
			::kill(pid_, SIGKILL);
		}
	}

	/**
	 * @brief Wait for process exit, timeout_ms < 0 waits forever.
	 */
	bool wait_finished(int timeout_ms)
	{
		if (pid_ <= 0) {
			return true;
		}
		int status;
		if (timeout_ms < 0) {
			if (waitpid(pid_, &status, 0) == pid_) {
				reap(status);
			}
			return true;
		}
		const int step_ms = 10;
		for (int waited = 0; waited < timeout_ms; waited += step_ms) {
			const pid_t r = waitpid(pid_, &status, WNOHANG);
			if (r == pid_) {
				reap(status);
				return true;
			}
			if (r < 0) {
				pid_ = -1;
				return true;
			}
			usleep(useconds_t(step_ms) * 1000);
		}
		return false;
	}

	bool crashed() const
	{
		return crashed_;
	}

	int exit_code() const
	{
		return exit_code_;
	}

	std::string error_string() const
	{
		return error_string_;
	}

#else // _WIN32

	bool start(const std::string &, const std::vector<std::string> &,
			   const std::vector<std::string> &, const std::string &)
	{
		error_string_ = "WorkerProcess is not implemented on Windows yet";
		return false;
	}
	bool is_running() { return false; }
	int64_t process_id() const { return 0; }
	bool write_all(const char *, size_t, int) { return false; }
	bool write_all(const std::string &, int) { return false; }
	bool read_line(std::string *, int) { return false; }
	void close_write_channel() {}
	void kill() {}
	bool wait_finished(int) { return true; }
	bool crashed() const { return false; }
	int exit_code() const { return 0; }
	std::string error_string() const { return error_string_; }

#endif

private:
#if !defined(_WIN32)
	static void close_fd(int *fd)
	{
		if (*fd >= 0) {
			close(*fd);
			*fd = -1;
		}
	}

	static bool poll_fd(int fd, short events, int timeout_ms)
	{
		if (fd < 0) {
			return false;
		}
		struct pollfd pfd;
		pfd.fd = fd;
		pfd.events = events;
		pfd.revents = 0;
		while (true) {
			const int r = poll(&pfd, 1, timeout_ms);
			if (r > 0) {
				return (pfd.revents & (events | POLLERR | POLLHUP)) != 0 ||
					   (pfd.revents & POLLIN) != 0;
			}
			if (r == 0) {
				return false; // timeout
			}
			if (errno != EINTR) {
				return false;
			}
		}
	}

	void reap(int status)
	{
		pid_ = -1;
		if (WIFEXITED(status)) {
			exit_code_ = WEXITSTATUS(status);
			crashed_ = false;
		} else {
			exit_code_ = 0;
			crashed_ = true;
		}
	}

	void reap_if_done()
	{
		if (pid_ > 0) {
			int status;
			if (waitpid(pid_, &status, WNOHANG) == pid_) {
				reap(status);
			}
		}
	}

	int pid_ = -1;
	int stdin_fd_ = -1;
	int stdout_fd_ = -1;
	std::string read_buffer_;
	bool crashed_ = false;
	int exit_code_ = 0;
#endif
	std::string error_string_;
};

}

#endif // OAK_WORKERPROCESS_H
