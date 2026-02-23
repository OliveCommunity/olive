/***
  This file is part of Oak Video Editor - A fork of original project Olive 

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team

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

#ifndef RENDERTICKET_H
#define RENDERTICKET_H

#include <QDateTime>
#include <QMutex>
#include <QWaitCondition>
#include <QVariant>
#include <QVariantMap>

#include "codec/frame.h"
#include "common/cancelableobject.h"
#include "node/output/viewer/viewer.h"

namespace olive
{

// 前向声明
class RenderTicketWatcher;

// RenderTicket 不继承 QObject，避免线程亲和性问题
class RenderTicket : public CancelableObject {
public:
	RenderTicket();
	~RenderTicket() = default;

	/**
   * @brief Get the ticket's current state
   */
	bool IsRunning(bool lock = true);

	/**
   * @brief Determine how many times ticket has been finished
   */
	int GetFinishCount(bool lock = true);

	/**
   * @brief Check if this ticket has a result
   */
	bool HasResult();

	/**
   * @brief Get value, if any
   */
	QVariant Get();

	/**
   * @brief Wait for ticket to be finished
   */
	void WaitForFinished();
	void WaitForFinished(QMutex *mutex);

	/**
   * @brief Access this ticket's mutex
   */
	QMutex *lock()
	{
		return &lock_;
	}

	/**
   * @brief Signal to the ticket that it is running
   */
	void Start();

	/**
   * @brief Finish ticket with no value
   */
	void Finish();

	/**
   * @brief Finish ticket with value
   */
	void Finish(QVariant result);

	/**
   * @brief 设置属性（线程安全）
   */
	void setProperty(const QString &name, const QVariant &value);

	/**
   * @brief 获取属性（线程安全）
   */
	QVariant property(const QString &name) const;
	/**
   * @brief 注册观察者（线程安全）
   * 当任务完成时，会通知所有观察者
   */
	void AddWatcher(RenderTicketWatcher *watcher, bool lock = true);

	/**
   * @brief 移除观察者（线程安全）
   * 当 watcher 被销毁时调用
   */
	void RemoveWatcher(RenderTicketWatcher *watcher);

private:
	void FinishInternal(bool has_result, QVariant result);
	void NotifyWatchers();

	bool is_running_;

	QVariant result_;

	bool has_result_;

	int finish_count_;

	QMutex lock_;

	QWaitCondition wait_;

	QVariantMap properties_;

	// 观察者列表（不需要互斥锁保护，因为只在 Start/Finish 时访问，且此时已持有 lock_）
	QVector<RenderTicketWatcher *> watchers_;
};

using RenderTicketPtr = std::shared_ptr<RenderTicket>;

class RenderTicketWatcher : public QObject {
	Q_OBJECT
public:
	explicit RenderTicketWatcher(QObject *parent = nullptr);
	~RenderTicketWatcher();

	RenderTicketPtr GetTicket() const
	{
		return ticket_;
	}

	void SetTicket(RenderTicketPtr ticket);

	bool IsRunning();

	void WaitForFinished();

	QVariant Get();

	bool HasResult();

	void Cancel();

	// 供 RenderTicket 调用，通知任务完成
	void NotifyFinished();
signals:
	void Finished(RenderTicketWatcher *watcher);

private slots:
	void EmitFinished();

private:
	RenderTicketPtr ticket_;
};

}

Q_DECLARE_METATYPE(olive::RenderTicketPtr)

#endif // RENDERTICKET_H