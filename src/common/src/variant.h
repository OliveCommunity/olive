/***

  Oak Video Editor - Non-Linear Video Editor
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

#ifndef OAK_NODEVARIANT_H
#define OAK_NODEVARIANT_H

#include <cstdint>
#include <memory>
#include <string>
#include <typeinfo>
#include <type_traits>
#include <utility>
#include <vector>

namespace olive
{

/**
 * @brief De-Qt replacement for QStringList.
 */
using StringList = std::vector<std::string>;

/**
 * @brief De-Qt replacement for QByteArray.
 */
using ByteArray = std::vector<char>;

/**
 * @brief De-Qt replacement for QVariant.
 *
 * A discriminated value container covering the data set actually carried
 * through the node module:
 *
 * - null (default-constructed)
 * - bool
 * - signed integer (all signed integer types are stored as int64_t)
 * - unsigned integer (stored as uint64_t)
 * - floating point (float and double are both stored as double)
 * - string (std::string, replaces QString)
 * - string list (StringList, replaces QStringList)
 * - byte array (ByteArray, replaces QByteArray)
 * - any other copy-constructible type, held type-erased (replaces
 *   Q_DECLARE_METATYPE custom types such as Color, Vector2D, TexturePtr,
 *   NodeValueArray, etc.)
 *
 * The API mirrors the QVariant subset used by the node module, renamed to
 * snake_case: value<T>(), from_value(), can_convert<T>(), is_null(),
 * to_double(), to_float(), to_int(), to_uint(), to_long_long(),
 * to_u_long_long(), to_bool(), to_string(), to_std_string(),
 * to_string_list(), to_byte_array().
 *
 * Conversion semantics follow QVariant for the stored kinds: numeric kinds
 * convert freely between each other, strings parse to numbers (failure
 * yields 0), and numbers format to strings (double uses QString::number's
 * default 'g' format with 6 significant digits). Custom (type-erased)
 * values only convert to their exact C++ type.
 *
 * Equality follows QVariant: two numeric values compare numerically across
 * kinds, strings/byte-arrays/lists compare by content, custom values
 * compare with the type's operator== (types without one compare unequal),
 * and two nulls compare equal.
 */
class Variant {
public:
	/**
	 * @brief Construct a null variant.
	 */
	Variant() = default;

	Variant(const Variant &other)
		: kind_(other.kind_)
		, int_(other.int_)
		, uint_(other.uint_)
		, double_(other.double_)
		, bool_(other.bool_)
		, string_(other.string_)
		, string_list_(other.string_list_)
		, byte_array_(other.byte_array_)
		, custom_(other.custom_ ? other.custom_->clone() : nullptr)
	{
	}

	Variant(Variant &&) = default;

	Variant &operator=(const Variant &other)
	{
		if (this != &other) {
			kind_ = other.kind_;
			int_ = other.int_;
			uint_ = other.uint_;
			double_ = other.double_;
			bool_ = other.bool_;
			string_ = other.string_;
			string_list_ = other.string_list_;
			byte_array_ = other.byte_array_;
			custom_.reset(other.custom_ ? other.custom_->clone() : nullptr);
		}
		return *this;
	}

	Variant &operator=(Variant &&) = default;

	Variant(const char *s)
		: kind_(k_string)
		, string_(s)
	{
	}

	/**
	 * @brief String literal overload (beats the generic template ctor).
	 */
	template <size_t N>
	Variant(const char (&s)[N])
		: kind_(k_string)
		, string_(s)
	{
	}

	Variant(const std::string &s)
		: kind_(k_string)
		, string_(s)
	{
	}

	Variant(const StringList &l)
		: kind_(k_string_list)
		, string_list_(l)
	{
	}

	Variant(const ByteArray &b)
		: kind_(k_byte_array)
		, byte_array_(b)
	{
	}

	Variant(bool b)
		: kind_(k_bool)
		, bool_(b)
	{
	}

	/**
	 * @brief Construct from any other type.
	 *
	 * Arithmetic types are stored in the corresponding numeric kind
	 * (mirroring QVariant's int/uint/double storage); everything else is
	 * stored type-erased, mirroring QVariant::fromValue() of a custom
	 * metatype.
	 */
	template <typename T,
			  typename std::enable_if<
				  !std::is_same<typename std::decay<T>::type, Variant>::value &&
					  !std::is_same<typename std::decay<T>::type, bool>::value &&
					  !std::is_same<typename std::decay<T>::type, std::string>::value &&
					  !std::is_same<typename std::decay<T>::type, StringList>::value &&
					  !std::is_same<typename std::decay<T>::type, ByteArray>::value,
				  int>::type = 0>
	Variant(const T &v)
	{
		set_value(v);
	}

	/**
	 * @brief QVariant::fromValue() equivalent.
	 */
	template <typename T> static Variant from_value(const T &v)
	{
		return Variant(v);
	}

	bool is_null() const
	{
		return kind_ == k_null;
	}

	/**
	 * @brief Extract the value as type T.
	 *
	 * For the built-in kinds this performs QVariant-style conversion; for
	 * custom types it returns the stored value if the type matches exactly,
	 * or a default-constructed T otherwise (QVariant behavior).
	 */
	template <typename T> T value() const
	{
		return value_impl(static_cast<T *>(nullptr));
	}

	/**
	 * @brief QVariant::canConvert<T>() equivalent.
	 */
	template <typename T> bool can_convert() const
	{
		return can_convert_impl(static_cast<T *>(nullptr));
	}

	double to_double(bool *ok = nullptr) const;
	float to_float(bool *ok = nullptr) const;
	int to_int(bool *ok = nullptr) const;
	unsigned int to_uint(bool *ok = nullptr) const;
	int64_t to_long_long(bool *ok = nullptr) const;
	uint64_t to_u_long_long(bool *ok = nullptr) const;
	bool to_bool() const;

	/**
	 * @brief QVariant::toString() equivalent (QString::number formatting for
	 * doubles: 'g' with 6 significant digits).
	 */
	std::string to_string() const;

	/**
	 * @brief QByteArray-like conversion of string data.
	 */
	std::string to_std_string() const
	{
		return to_string();
	}

	StringList to_string_list() const;
	ByteArray to_byte_array() const;

	bool operator==(const Variant &rhs) const;
	bool operator!=(const Variant &rhs) const
	{
		return !(*this == rhs);
	}

private:
	enum Kind {
		k_null,
		k_bool,
		k_int,
		k_uint,
		k_double,
		k_string,
		k_string_list,
		k_byte_array,
		k_custom
	};

	struct CustomHolderBase {
		virtual ~CustomHolderBase() = default;
		virtual CustomHolderBase *clone() const = 0;
		virtual const std::type_info &type() const = 0;
		virtual const void *data() const = 0;
		virtual bool equals(const CustomHolderBase *rhs) const = 0;
	};

	template <typename T, typename = void> struct HasEquality : std::false_type {
	};

	template <typename T>
	struct HasEquality<T,
					   typename std::enable_if< std::is_convertible<
						   decltype(std::declval<const T &>() == std::declval<const T &>()),
						   bool>::value>::type> : std::true_type {
	};

	template <typename T> bool custom_equals(const T &a, const T &b, std::true_type) const
	{
		return a == b;
	}

	template <typename T> bool custom_equals(const T &, const T &, std::false_type) const
	{
		return false;
	}

	template <typename T> struct CustomHolder : CustomHolderBase {
		explicit CustomHolder(const T &v)
			: value_(v)
		{
		}

		CustomHolderBase *clone() const override
		{
			return new CustomHolder<T>(value_);
		}

		const std::type_info &type() const override
		{
			return typeid(T);
		}

		const void *data() const override
		{
			return &value_;
		}

		bool equals(const CustomHolderBase *rhs) const override
		{
			if (rhs->type() != typeid(T)) {
				return false;
			}
			const T &other = *static_cast<const T *>(rhs->data());
			Variant dummy;
			return dummy.custom_equals(value_, other, HasEquality<T>());
		}

		T value_;
	};

	template <typename T>
	typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value &&
								!std::is_same<T, bool>::value,
							void>::type
	set_value(const T &v)
	{
		kind_ = k_int;
		int_ = int64_t(v);
	}

	template <typename T>
	typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value &&
								!std::is_same<T, bool>::value,
							void>::type
	set_value(const T &v)
	{
		kind_ = k_uint;
		uint_ = uint64_t(v);
	}

	template <typename T>
	typename std::enable_if<std::is_floating_point<T>::value, void>::type
	set_value(const T &v)
	{
		kind_ = k_double;
		double_ = double(v);
	}

	template <typename T>
	typename std::enable_if<
		!std::is_arithmetic<T>::value && !std::is_same<T, bool>::value &&
			!std::is_same<T, std::string>::value && !std::is_same<T, StringList>::value &&
			!std::is_same<T, ByteArray>::value,
		void>::type
	set_value(const T &v)
	{
		kind_ = k_custom;
		custom_.reset(new CustomHolder<T>(v));
	}

	bool is_numeric_kind() const
	{
		return kind_ == k_bool || kind_ == k_int || kind_ == k_uint ||
			   kind_ == k_double;
	}

	double numeric_as_double() const
	{
		switch (kind_) {
		case k_bool:
			return bool_ ? 1.0 : 0.0;
		case k_int:
			return double(int_);
		case k_uint:
			return double(uint_);
		case k_double:
			return double_;
		default:
			return 0.0;
		}
	}

	int64_t numeric_as_int64() const
	{
		switch (kind_) {
		case k_bool:
			return bool_ ? 1 : 0;
		case k_int:
			return int_;
		case k_uint:
			return int64_t(uint_);
		case k_double:
			return int64_t(double_);
		default:
			return 0;
		}
	}

	uint64_t numeric_as_uint64() const
	{
		switch (kind_) {
		case k_bool:
			return bool_ ? 1 : 0;
		case k_int:
			return uint64_t(int_);
		case k_uint:
			return uint_;
		case k_double:
			return uint64_t(double_);
		default:
			return 0;
		}
	}

	// value<T>() dispatchers

	template <typename T>
	typename std::enable_if<std::is_same<T, std::string>::value, T>::type
	value_impl(T *) const
	{
		return to_string();
	}

	template <typename T>
	typename std::enable_if<std::is_same<T, StringList>::value, T>::type
	value_impl(T *) const
	{
		return to_string_list();
	}

	template <typename T>
	typename std::enable_if<std::is_same<T, ByteArray>::value, T>::type
	value_impl(T *) const
	{
		return to_byte_array();
	}

	template <typename T>
	typename std::enable_if<std::is_same<T, bool>::value, T>::type
	value_impl(T *) const
	{
		return to_bool();
	}

	template <typename T>
	typename std::enable_if<std::is_floating_point<T>::value, T>::type
	value_impl(T *) const
	{
		return T(to_double());
	}

	template <typename T>
	typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value &&
								!std::is_same<T, bool>::value,
							T>::type
	value_impl(T *) const
	{
		return T(to_long_long());
	}

	template <typename T>
	typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value &&
								!std::is_same<T, bool>::value,
							T>::type
	value_impl(T *) const
	{
		return T(to_u_long_long());
	}

	template <typename T>
	typename std::enable_if<
		!std::is_arithmetic<T>::value && !std::is_same<T, std::string>::value &&
			!std::is_same<T, StringList>::value && !std::is_same<T, ByteArray>::value,
		T>::type
	value_impl(T *) const
	{
		if (kind_ == k_custom && custom_->type() == typeid(T)) {
			return *static_cast<const T *>(custom_->data());
		}
		return T();
	}

	// can_convert<T>() dispatchers

	template <typename T>
	typename std::enable_if<std::is_same<T, std::string>::value, bool>::type
	can_convert_impl(T *) const
	{
		return is_numeric_kind() || kind_ == k_string || kind_ == k_byte_array;
	}

	template <typename T>
	typename std::enable_if<std::is_same<T, StringList>::value, bool>::type
	can_convert_impl(T *) const
	{
		return kind_ == k_string_list || kind_ == k_string;
	}

	template <typename T>
	typename std::enable_if<std::is_same<T, ByteArray>::value, bool>::type
	can_convert_impl(T *) const
	{
		return kind_ == k_byte_array || kind_ == k_string;
	}

	template <typename T>
	typename std::enable_if<std::is_arithmetic<T>::value, bool>::type
	can_convert_impl(T *) const
	{
		return is_numeric_kind() || kind_ == k_string;
	}

	template <typename T>
	typename std::enable_if<
		!std::is_arithmetic<T>::value && !std::is_same<T, std::string>::value &&
			!std::is_same<T, StringList>::value && !std::is_same<T, ByteArray>::value,
		bool>::type
	can_convert_impl(T *) const
	{
		return kind_ == k_custom && custom_->type() == typeid(T);
	}

	Kind kind_ = k_null;

	int64_t int_ = 0;
	uint64_t uint_ = 0;
	double double_ = 0.0;
	bool bool_ = false;
	std::string string_;
	StringList string_list_;
	ByteArray byte_array_;
	std::unique_ptr<CustomHolderBase> custom_;
};

/**
 * @brief QByteArray::toBase64() equivalent.
 */
std::string byte_array_to_base64(const ByteArray &data);

/**
 * @brief QByteArray::fromBase64() equivalent.
 */
ByteArray byte_array_from_base64(const std::string &text);

}

#endif // OAK_NODEVARIANT_H
