/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#include "renderticket.h"

#include <cstdio>

namespace olive
{

RenderTicket::RenderTicket()
	: is_running_(false)
	, has_result_(false)
	, finish_count_(0)
{
}

void RenderTicket::start()
{
	std::lock_guard<std::mutex> locker(lock_);

	is_running_ = true;
	has_result_ = false;
	result_ = Variant();
}

void RenderTicket::finish()
{
	finish_internal(false, Variant());
}

void RenderTicket::finish(Variant result)
{
	finish_internal(true, result);
}

Variant RenderTicket::get()
{
	wait_for_finished();

	// We don't have to mutex around this because there is no way to write to `result_` after
	// the ticket has finished and the above function blocks the calling thread until it is finished
	return result_;
}

void RenderTicket::wait_for_finished()
{
	std::unique_lock<std::mutex> locker(lock_);

	if (is_running_) {
		wait_.wait(locker);
	}
}

bool RenderTicket::is_running(bool lock)
{
	if (lock) {
		lock_.lock();
	}

	bool running = is_running_;

	if (lock) {
		lock_.unlock();
	}

	return running;
}

int RenderTicket::get_finish_count(bool lock)
{
	if (lock) {
		lock_.lock();
	}

	int count = finish_count_;

	if (lock) {
		lock_.unlock();
	}

	return count;
}

bool RenderTicket::has_result()
{
	std::lock_guard<std::mutex> locker(lock_);

	return has_result_;
}

void RenderTicket::finish_internal(bool has_result, Variant result)
{
	std::unique_lock<std::mutex> locker(lock_);

	if (!is_running_) {
		fprintf(stderr, "Tried to finish ticket that wasn't running\n");
	} else {
		is_running_ = false;
		has_result_ = has_result;
		result_ = result;
		finish_count_++;

		std::function<void()> callback = finished_callback_;

		wait_.notify_all();

		locker.unlock();

		if (callback) {
			callback();
		}
	}
}

RenderTicketWatcher::RenderTicketWatcher()
	: ticket_(nullptr)
{
}

void RenderTicketWatcher::set_ticket(RenderTicketPtr ticket)
{
	if (ticket_) {
		fprintf(stderr, "Tried to set a ticket on a RenderTicketWatcher twice\n");
		return;
	}

	if (!ticket) {
		fprintf(stderr, "Tried to set a null ticket on a RenderTicketWatcher\n");
		return;
	}

	ticket_ = ticket;

	ticket->set_finished_callback([this]() { ticket_finished(); });

	// Lock ticket so we can query if it's already finished by the time this code runs
	std::lock_guard<std::mutex> locker(*ticket->lock());

	if (!ticket_->is_running(false) && ticket_->get_finish_count(false) > 0) {
		// Ticket has already finished before. The Qt code re-notified
		// asynchronously through the event loop (Qt::QueuedConnection) so the
		// caller could receive the watcher pointer first; with no event loop
		// there is no deferred delivery, so the caller must observe the state
		// through is_running()/has_result()/get() instead. The facade/app
		// layer re-creates the deferred notification if it needs one.
	}
}

bool RenderTicketWatcher::is_running()
{
	if (ticket_) {
		return ticket_->is_running();
	} else {
		return false;
	}
}

void RenderTicketWatcher::wait_for_finished()
{
	if (ticket_) {
		ticket_->wait_for_finished();
	}
}

Variant RenderTicketWatcher::get()
{
	if (ticket_) {
		return ticket_->get();
	} else {
		return Variant();
	}
}

bool RenderTicketWatcher::has_result()
{
	if (ticket_) {
		return ticket_->has_result();
	} else {
		return false;
	}
}

void RenderTicketWatcher::cancel()
{
	if (ticket_) {
		ticket_->cancel();
	}
}

void RenderTicketWatcher::ticket_finished()
{
	if (finished_callback_) {
		finished_callback_(this);
	}
}

}
