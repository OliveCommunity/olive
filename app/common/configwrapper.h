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

#ifndef OAK_CONFIGWRAPPER_H
#define OAK_CONFIGWRAPPER_H

#include <QVariant>

#include "olive/core/util/rational.h"
#include "oakengine/config.h"

// Facade migration B9b: replace the engine's OAK_CONFIG macro (which
// references olive::Config::current()/operator[] and brings C++ symbols into
// the editor binary) with a thin header-only wrapper around the C ABI.
//
// Include this header instead of "config/config.h" in app code. It undefines
// the engine macros and redefines them to return an inline OakConfigValue that
// forwards reads/writes to oakengine_config_*().

namespace olive
{

class OakConfigValue {
public:
	explicit OakConfigValue(const QString &key) : key_(key) {}

	operator bool() const
	{
		return oakengine_config_get_int(key_utf8(), 0) != 0;
	}
	operator int() const
	{
		return static_cast<int>(oakengine_config_get_int(key_utf8(), 0));
	}
	operator qint64() const
	{
		return static_cast<qint64>(oakengine_config_get_int(key_utf8(), 0));
	}
	operator quint64() const
	{
		return static_cast<quint64>(oakengine_config_get_int(key_utf8(), 0));
	}
	// int64_t/uint64_t overloads only exist where they differ from
	// qint64/quint64 (Linux LP64: int64_t is long; on macOS/Windows both are
	// long long, where declaring them would be a redeclaration).
#if defined(__linux__)
	operator int64_t() const
	{
		return oakengine_config_get_int(key_utf8(), 0);
	}
	operator uint64_t() const
	{
		return static_cast<uint64_t>(oakengine_config_get_int(key_utf8(), 0));
	}
#endif
	operator QString() const
	{
		char buf[1024];
		const int len = oakengine_config_get_string(key_utf8(), buf,
												sizeof(buf));
		return QString::fromUtf8(buf, len);
	}
	operator QVariant() const
	{
		return QVariant(static_cast<QString>(*this));
	}

	OakConfigValue &operator=(bool v)
	{
		oakengine_config_set_int(key_utf8(), v ? 1 : 0);
		return *this;
	}
	OakConfigValue &operator=(int v)
	{
		oakengine_config_set_int(key_utf8(), static_cast<int64_t>(v));
		return *this;
	}
	OakConfigValue &operator=(uint v)
	{
		oakengine_config_set_int(key_utf8(), static_cast<int64_t>(v));
		return *this;
	}
	OakConfigValue &operator=(qint64 v)
	{
		oakengine_config_set_int(key_utf8(), static_cast<int64_t>(v));
		return *this;
	}
	OakConfigValue &operator=(quint64 v)
	{
		oakengine_config_set_int(key_utf8(), static_cast<int64_t>(v));
		return *this;
	}
#if defined(__linux__)
	OakConfigValue &operator=(int64_t v)
	{
		oakengine_config_set_int(key_utf8(), v);
		return *this;
	}
	OakConfigValue &operator=(uint64_t v)
	{
		oakengine_config_set_int(key_utf8(), static_cast<int64_t>(v));
		return *this;
	}
#endif
	OakConfigValue &operator=(const QString &v)
	{
		const QByteArray utf8 = v.toUtf8();
		oakengine_config_set_string(key_utf8(), utf8.constData());
		return *this;
	}
	OakConfigValue &operator=(const char *v)
	{
		oakengine_config_set_string(key_utf8(), v ? v : "");
		return *this;
	}
	OakConfigValue &operator=(const QVariant &v)
	{
		switch (v.typeId()) {
		case QMetaType::Bool:
			*this = v.toBool();
			break;
		case QMetaType::Int:
		case QMetaType::UInt:
		case QMetaType::LongLong:
		case QMetaType::ULongLong:
		case QMetaType::Long:
		case QMetaType::Short:
		case QMetaType::Char:
		case QMetaType::ULong:
		case QMetaType::UShort:
		case QMetaType::UChar:
			*this = v.toLongLong();
			break;
		case QMetaType::Double:
		case QMetaType::Float:
			*this = static_cast<int64_t>(v.toDouble());
			break;
		default:
			*this = v.toString();
			break;
		}
		return *this;
	}

	bool toBool() const { return static_cast<bool>(*this); }
	int toInt() const { return static_cast<int>(*this); }
	qint64 toLongLong() const { return static_cast<qint64>(*this); }
	quint64 toULongLong() const { return static_cast<quint64>(*this); }
	QString toString() const { return static_cast<QString>(*this); }

	bool operator==(int rhs) const { return toInt() == rhs; }
	bool operator!=(int rhs) const { return toInt() != rhs; }
	bool operator==(qint64 rhs) const { return toLongLong() == rhs; }
	bool operator!=(qint64 rhs) const { return toLongLong() != rhs; }
	bool operator==(const QString &rhs) const { return toString() == rhs; }
	bool operator!=(const QString &rhs) const { return toString() != rhs; }
	bool operator==(const char *rhs) const { return toString() == QString::fromUtf8(rhs); }
	bool operator!=(const char *rhs) const { return toString() != QString::fromUtf8(rhs); }

	template <typename T> T value() const
	{
		if constexpr (std::is_same_v<T, olive::core::Rational>) {
			const QString s = static_cast<QString>(*this);
			const QByteArray utf8 = s.toUtf8();
			return olive::core::Rational::from_string(
					std::string(utf8.constData(), size_t(utf8.size())));
		} else {
			return static_cast<T>(*this);
		}
	}

private:
	const char *key_utf8() const
	{
		key_utf8_ = key_.toUtf8();
		return key_utf8_.constData();
	}

	QString key_;
	mutable QByteArray key_utf8_;
};

} // namespace olive

#ifdef OAK_CONFIG
#undef OAK_CONFIG
#endif
#ifdef OAK_CONFIG_STR
#undef OAK_CONFIG_STR
#endif

#define OAK_CONFIG(x) olive::OakConfigValue(QStringLiteral(x))
#define OAK_CONFIG_STR(x) olive::OakConfigValue(x)

#endif // OAK_CONFIGWRAPPER_H
