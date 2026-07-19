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

namespace olive
{

RenderTicket::RenderTicket()
	: is_running_(false)
	, has_result_(false)
	, finish_count_(0)
{
}

void RenderTicket::wait_for_finished(QMutex *mutex)
{
	if (is_running_) {
		wait_.wait(mutex);
	}
}

void RenderTicket::start()
{
	QMutexLocker locker(&lock_);

	is_running_ = true;
	has_result_ = false;
	result_.clear();
}

void RenderTicket::finish()
{
	finish_internal(false, QVariant());
}

void RenderTicket::finish(QVariant result)
{
	finish_internal(true, result);
}

QVariant RenderTicket::get()
{
	wait_for_finished();

	// We don't have to mutex around this because there is no way to write to `result_` after
	// the ticket has finished and the above function blocks the calling thread until it is finished
	return result_;
}

void RenderTicket::wait_for_finished()
{
	QMutexLocker locker(&lock_);

	wait_for_finished(&lock_);
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
	QMutexLocker locker(&lock_);

	return has_result_;
}

void RenderTicket::finish_internal(bool has_result, QVariant result)
{
	QMutexLocker locker(&lock_);

	if (!is_running_) {
		qWarning() << "Tried to finish ticket that wasn't running";
	} else {
		is_running_ = false;
		has_result_ = has_result;
		result_ = result;
		finish_count_++;

		wait_.wakeAll();

		locker.unlock();

		emit finished();
	}
}

RenderTicketWatcher::RenderTicketWatcher(QObject *parent)
	: QObject(parent)
	, ticket_(nullptr)
{
}

void RenderTicketWatcher::set_ticket(RenderTicketPtr ticket)
{
	if (ticket_) {
		qCritical() << "Tried to set a ticket on a RenderTicketWatcher twice";
		return;
	}

	if (!ticket) {
		qCritical() << "Tried to set a null ticket on a RenderTicketWatcher";
		return;
	}

	ticket_ = ticket;

	// Lock ticket so we can query if it's already finished by the time this code runs
	QMutexLocker locker(ticket->lock());

	connect(ticket_.get(), &RenderTicket::finished, this,
			&RenderTicketWatcher::ticket_finished);

	if (!ticket_->is_running(false) && ticket_->get_finish_count(false) > 0) {
		// Ticket has already finished before, so we emit a signal asynchronously
		// to avoid deleting this watcher before the caller has a chance to use
		// the returned pointer.
		QMetaObject::invokeMethod(this, &RenderTicketWatcher::ticket_finished,
								  Qt::QueuedConnection);
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

QVariant RenderTicketWatcher::get()
{
	if (ticket_) {
		return ticket_->get();
	} else {
		return QVariant();
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
	emit finished(this);
}

}
