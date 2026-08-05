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

#ifndef OAK_NODEMATHTYPES_H
#define OAK_NODEMATHTYPES_H

#include <cmath>
#include <cstring>

namespace olive
{

/**
 * @brief De-Qt replacement for QPointF.
 *
 * Plain old data 2D point with the subset of the QPointF API used by the
 * node module.
 */
class PointF {
public:
	PointF()
		: x_(0.0)
		, y_(0.0)
	{
	}

	PointF(double x, double y)
		: x_(x)
		, y_(y)
	{
	}

	double x() const
	{
		return x_;
	}
	double y() const
	{
		return y_;
	}

	void set_x(double x)
	{
		x_ = x;
	}
	void set_y(double y)
	{
		y_ = y;
	}

	double &rx()
	{
		return x_;
	}
	double &ry()
	{
		return y_;
	}

	bool is_null() const
	{
		return x_ == 0.0 && y_ == 0.0;
	}

	double manhattan_length() const
	{
		return std::fabs(x_) + std::fabs(y_);
	}

	PointF &operator+=(const PointF &p)
	{
		x_ += p.x_;
		y_ += p.y_;
		return *this;
	}

	PointF &operator-=(const PointF &p)
	{
		x_ -= p.x_;
		y_ -= p.y_;
		return *this;
	}

	PointF &operator*=(double factor)
	{
		x_ *= factor;
		y_ *= factor;
		return *this;
	}

	PointF &operator/=(double divisor)
	{
		x_ /= divisor;
		y_ /= divisor;
		return *this;
	}

	bool operator==(const PointF &rhs) const
	{
		return x_ == rhs.x_ && y_ == rhs.y_;
	}

	bool operator!=(const PointF &rhs) const
	{
		return !(*this == rhs);
	}

	friend PointF operator+(PointF a, const PointF &b)
	{
		a += b;
		return a;
	}

	friend PointF operator-(PointF a, const PointF &b)
	{
		a -= b;
		return a;
	}

	friend PointF operator*(PointF p, double factor)
	{
		p *= factor;
		return p;
	}

	friend PointF operator*(double factor, PointF p)
	{
		p *= factor;
		return p;
	}

	friend PointF operator/(PointF p, double divisor)
	{
		p /= divisor;
		return p;
	}

	friend PointF operator-(const PointF &p)
	{
		return PointF(-p.x_, -p.y_);
	}

private:
	double x_;
	double y_;
};

/**
 * @brief De-Qt replacement for QVector2D.
 */
class Vector2D {
public:
	Vector2D()
		: x_(0.0f)
		, y_(0.0f)
	{
	}

	Vector2D(float x, float y)
		: x_(x)
		, y_(y)
	{
	}

	float x() const
	{
		return x_;
	}
	float y() const
	{
		return y_;
	}

	void set_x(float x)
	{
		x_ = x;
	}
	void set_y(float y)
	{
		y_ = y;
	}

	bool is_null() const
	{
		return x_ == 0.0f && y_ == 0.0f;
	}

	/**
	 * @brief QVector2D::toPointF() equivalent.
	 */
	PointF to_point_f() const
	{
		return PointF(x_, y_);
	}

	float length() const
	{
		return std::sqrt(x_ * x_ + y_ * y_);
	}

	Vector2D normalized() const
	{
		float len = length();
		if (len == 0.0f) {
			return Vector2D();
		}
		return Vector2D(x_ / len, y_ / len);
	}

	static float dot_product(const Vector2D &a, const Vector2D &b)
	{
		return a.x_ * b.x_ + a.y_ * b.y_;
	}

	Vector2D &operator+=(const Vector2D &v)
	{
		x_ += v.x_;
		y_ += v.y_;
		return *this;
	}

	Vector2D &operator-=(const Vector2D &v)
	{
		x_ -= v.x_;
		y_ -= v.y_;
		return *this;
	}

	Vector2D &operator*=(float factor)
	{
		x_ *= factor;
		y_ *= factor;
		return *this;
	}

	Vector2D &operator/=(float divisor)
	{
		x_ /= divisor;
		y_ /= divisor;
		return *this;
	}

	bool operator==(const Vector2D &rhs) const
	{
		return x_ == rhs.x_ && y_ == rhs.y_;
	}

	bool operator!=(const Vector2D &rhs) const
	{
		return !(*this == rhs);
	}

	friend Vector2D operator+(Vector2D a, const Vector2D &b)
	{
		a += b;
		return a;
	}

	friend Vector2D operator-(Vector2D a, const Vector2D &b)
	{
		a -= b;
		return a;
	}

	friend Vector2D operator*(Vector2D v, float factor)
	{
		v *= factor;
		return v;
	}

	friend Vector2D operator*(float factor, Vector2D v)
	{
		v *= factor;
		return v;
	}

	friend Vector2D operator/(Vector2D v, float divisor)
	{
		v /= divisor;
		return v;
	}

	friend Vector2D operator-(const Vector2D &v)
	{
		return Vector2D(-v.x_, -v.y_);
	}

	/**
	 * @brief Component-wise multiplication (QVector2D * QVector2D).
	 */
	friend Vector2D operator*(const Vector2D &a, const Vector2D &b)
	{
		return Vector2D(a.x_ * b.x_, a.y_ * b.y_);
	}

	/**
	 * @brief Component-wise division (QVector2D / QVector2D).
	 */
	friend Vector2D operator/(const Vector2D &a, const Vector2D &b)
	{
		return Vector2D(a.x_ / b.x_, a.y_ / b.y_);
	}

private:
	float x_;
	float y_;
};

/**
 * @brief De-Qt replacement for QVector3D.
 */
class Vector3D {
public:
	Vector3D()
		: x_(0.0f)
		, y_(0.0f)
		, z_(0.0f)
	{
	}

	Vector3D(float x, float y, float z)
		: x_(x)
		, y_(y)
		, z_(z)
	{
	}

	float x() const
	{
		return x_;
	}
	float y() const
	{
		return y_;
	}
	float z() const
	{
		return z_;
	}

	void set_x(float x)
	{
		x_ = x;
	}
	void set_y(float y)
	{
		y_ = y;
	}
	void set_z(float z)
	{
		z_ = z;
	}

	bool is_null() const
	{
		return x_ == 0.0f && y_ == 0.0f && z_ == 0.0f;
	}

	float length() const
	{
		return std::sqrt(x_ * x_ + y_ * y_ + z_ * z_);
	}

	Vector3D normalized() const
	{
		float len = length();
		if (len == 0.0f) {
			return Vector3D();
		}
		return Vector3D(x_ / len, y_ / len, z_ / len);
	}

	static float dot_product(const Vector3D &a, const Vector3D &b)
	{
		return a.x_ * b.x_ + a.y_ * b.y_ + a.z_ * b.z_;
	}

	static Vector3D cross_product(const Vector3D &a, const Vector3D &b)
	{
		return Vector3D(a.y_ * b.z_ - a.z_ * b.y_, a.z_ * b.x_ - a.x_ * b.z_,
						a.x_ * b.y_ - a.y_ * b.x_);
	}

	Vector3D &operator+=(const Vector3D &v)
	{
		x_ += v.x_;
		y_ += v.y_;
		z_ += v.z_;
		return *this;
	}

	Vector3D &operator-=(const Vector3D &v)
	{
		x_ -= v.x_;
		y_ -= v.y_;
		z_ -= v.z_;
		return *this;
	}

	Vector3D &operator*=(float factor)
	{
		x_ *= factor;
		y_ *= factor;
		z_ *= factor;
		return *this;
	}

	Vector3D &operator/=(float divisor)
	{
		x_ /= divisor;
		y_ /= divisor;
		z_ /= divisor;
		return *this;
	}

	bool operator==(const Vector3D &rhs) const
	{
		return x_ == rhs.x_ && y_ == rhs.y_ && z_ == rhs.z_;
	}

	bool operator!=(const Vector3D &rhs) const
	{
		return !(*this == rhs);
	}

	friend Vector3D operator+(Vector3D a, const Vector3D &b)
	{
		a += b;
		return a;
	}

	friend Vector3D operator-(Vector3D a, const Vector3D &b)
	{
		a -= b;
		return a;
	}

	friend Vector3D operator*(Vector3D v, float factor)
	{
		v *= factor;
		return v;
	}

	friend Vector3D operator*(float factor, Vector3D v)
	{
		v *= factor;
		return v;
	}

	friend Vector3D operator/(Vector3D v, float divisor)
	{
		v /= divisor;
		return v;
	}

	friend Vector3D operator-(const Vector3D &v)
	{
		return Vector3D(-v.x_, -v.y_, -v.z_);
	}

	/**
	 * @brief Component-wise multiplication (QVector3D * QVector3D).
	 */
	friend Vector3D operator*(const Vector3D &a, const Vector3D &b)
	{
		return Vector3D(a.x_ * b.x_, a.y_ * b.y_, a.z_ * b.z_);
	}

	/**
	 * @brief Component-wise division (QVector3D / QVector3D).
	 */
	friend Vector3D operator/(const Vector3D &a, const Vector3D &b)
	{
		return Vector3D(a.x_ / b.x_, a.y_ / b.y_, a.z_ / b.z_);
	}

private:
	float x_;
	float y_;
	float z_;
};

/**
 * @brief De-Qt replacement for QVector4D.
 */
class Vector4D {
public:
	Vector4D()
		: x_(0.0f)
		, y_(0.0f)
		, z_(0.0f)
		, w_(0.0f)
	{
	}

	Vector4D(float x, float y, float z, float w)
		: x_(x)
		, y_(y)
		, z_(z)
		, w_(w)
	{
	}

	float x() const
	{
		return x_;
	}
	float y() const
	{
		return y_;
	}
	float z() const
	{
		return z_;
	}
	float w() const
	{
		return w_;
	}

	void set_x(float x)
	{
		x_ = x;
	}
	void set_y(float y)
	{
		y_ = y;
	}
	void set_z(float z)
	{
		z_ = z;
	}
	void set_w(float w)
	{
		w_ = w;
	}

	bool is_null() const
	{
		return x_ == 0.0f && y_ == 0.0f && z_ == 0.0f && w_ == 0.0f;
	}

	float length() const
	{
		return std::sqrt(x_ * x_ + y_ * y_ + z_ * z_ + w_ * w_);
	}

	static float dot_product(const Vector4D &a, const Vector4D &b)
	{
		return a.x_ * b.x_ + a.y_ * b.y_ + a.z_ * b.z_ + a.w_ * b.w_;
	}

	Vector4D &operator+=(const Vector4D &v)
	{
		x_ += v.x_;
		y_ += v.y_;
		z_ += v.z_;
		w_ += v.w_;
		return *this;
	}

	Vector4D &operator-=(const Vector4D &v)
	{
		x_ -= v.x_;
		y_ -= v.y_;
		z_ -= v.z_;
		w_ -= v.w_;
		return *this;
	}

	Vector4D &operator*=(float factor)
	{
		x_ *= factor;
		y_ *= factor;
		z_ *= factor;
		w_ *= factor;
		return *this;
	}

	Vector4D &operator/=(float divisor)
	{
		x_ /= divisor;
		y_ /= divisor;
		z_ /= divisor;
		w_ /= divisor;
		return *this;
	}

	bool operator==(const Vector4D &rhs) const
	{
		return x_ == rhs.x_ && y_ == rhs.y_ && z_ == rhs.z_ && w_ == rhs.w_;
	}

	bool operator!=(const Vector4D &rhs) const
	{
		return !(*this == rhs);
	}

	friend Vector4D operator+(Vector4D a, const Vector4D &b)
	{
		a += b;
		return a;
	}

	friend Vector4D operator-(Vector4D a, const Vector4D &b)
	{
		a -= b;
		return a;
	}

	friend Vector4D operator*(Vector4D v, float factor)
	{
		v *= factor;
		return v;
	}

	friend Vector4D operator*(float factor, Vector4D v)
	{
		v *= factor;
		return v;
	}

	friend Vector4D operator/(Vector4D v, float divisor)
	{
		v /= divisor;
		return v;
	}

	friend Vector4D operator-(const Vector4D &v)
	{
		return Vector4D(-v.x_, -v.y_, -v.z_, -v.w_);
	}

	/**
	 * @brief Component-wise multiplication (QVector4D * QVector4D).
	 */
	friend Vector4D operator*(const Vector4D &a, const Vector4D &b)
	{
		return Vector4D(a.x_ * b.x_, a.y_ * b.y_, a.z_ * b.z_, a.w_ * b.w_);
	}

	/**
	 * @brief Component-wise division (QVector4D / QVector4D).
	 */
	friend Vector4D operator/(const Vector4D &a, const Vector4D &b)
	{
		return Vector4D(a.x_ / b.x_, a.y_ / b.y_, a.z_ / b.z_, a.w_ / b.w_);
	}

private:
	float x_;
	float y_;
	float z_;
	float w_;
};

/**
 * @brief De-Qt replacement for QMatrix4x4.
 *
 * Row-major 4x4 float matrix, matching QMatrix4x4's m[row][column] layout.
 * Also covers the subset of QTransform used by the node module (2D affine
 * operations are expressed on the Z plane, as QTransform does internally).
 */
class Matrix4x4 {
public:
	/**
	 * @brief Construct an identity matrix (same as QMatrix4x4's default).
	 */
	Matrix4x4()
	{
		set_to_identity();
	}

	float operator()(int row, int column) const
	{
		return m_[row][column];
	}

	float &operator()(int row, int column)
	{
		return m_[row][column];
	}

	const float *const_data() const
	{
		return &m_[0][0];
	}

	bool is_identity() const
	{
		Matrix4x4 identity;
		return *this == identity;
	}

	void set_to_identity()
	{
		std::memset(m_, 0, sizeof(m_));
		for (int i = 0; i < 4; i++) {
			m_[i][i] = 1.0f;
		}
	}

	Matrix4x4 transposed() const
	{
		Matrix4x4 r;
		for (int row = 0; row < 4; row++) {
			for (int col = 0; col < 4; col++) {
				r.m_[row][col] = m_[col][row];
			}
		}
		return r;
	}

	/**
	 * @brief Return the inverse of this matrix.
	 *
	 * @param invertible If not null, set to whether the matrix is invertible.
	 *        If not, an identity matrix is returned (QMatrix4x4 behavior).
	 */
	Matrix4x4 inverted(bool *invertible = nullptr) const;

	/**
	 * @brief QMatrix4x4::map(QPointF) equivalent.
	 *
	 * Maps p through the matrix treating it as (x, y, 0, 1) and dividing by
	 * the resulting w whenever w is not exactly 1 (Qt divides
	 * unconditionally in that case, including w == 0).
	 */
	PointF map(const PointF &p) const
	{
		double x = p.x() * m_[0][0] + p.y() * m_[0][1] + m_[0][3];
		double y = p.x() * m_[1][0] + p.y() * m_[1][1] + m_[1][3];
		double w = p.x() * m_[3][0] + p.y() * m_[3][1] + m_[3][3];
		if (w == 1.0) {
			return PointF(x, y);
		}
		return PointF(x / w, y / w);
	}

	/**
	 * @brief QTransform-style 2D translate (post-multiply on the Z plane).
	 */
	Matrix4x4 &translate(double x, double y)
	{
		return translate(x, y, 0.0);
	}

	/**
	 * @brief QMatrix4x4-style translate.
	 */
	Matrix4x4 &translate(double x, double y, double z)
	{
		Matrix4x4 t;
		t.m_[0][3] = float(x);
		t.m_[1][3] = float(y);
		t.m_[2][3] = float(z);
		*this = *this * t;
		return *this;
	}

	/**
	 * @brief QTransform-style 2D scale.
	 */
	Matrix4x4 &scale(double x, double y)
	{
		return scale(x, y, 1.0);
	}

	/**
	 * @brief QMatrix4x4-style scale.
	 */
	Matrix4x4 &scale(double x, double y, double z)
	{
		Matrix4x4 s;
		s.m_[0][0] = float(x);
		s.m_[1][1] = float(y);
		s.m_[2][2] = float(z);
		*this = *this * s;
		return *this;
	}

	/**
	 * @brief QTransform-style 2D rotation (degrees, around the Z axis).
	 */
	Matrix4x4 &rotate(double degrees)
	{
		double radians = degrees * M_PI / 180.0;
		float c = float(std::cos(radians));
		float s = float(std::sin(radians));
		Matrix4x4 r;
		r.m_[0][0] = c;
		r.m_[0][1] = -s;
		r.m_[1][0] = s;
		r.m_[1][1] = c;
		*this = *this * r;
		return *this;
	}

	Matrix4x4 &operator*=(const Matrix4x4 &rhs)
	{
		*this = *this * rhs;
		return *this;
	}

	Matrix4x4 &operator+=(const Matrix4x4 &rhs)
	{
		for (int row = 0; row < 4; row++) {
			for (int col = 0; col < 4; col++) {
				m_[row][col] += rhs.m_[row][col];
			}
		}
		return *this;
	}

	Matrix4x4 &operator-=(const Matrix4x4 &rhs)
	{
		for (int row = 0; row < 4; row++) {
			for (int col = 0; col < 4; col++) {
				m_[row][col] -= rhs.m_[row][col];
			}
		}
		return *this;
	}

	bool operator==(const Matrix4x4 &rhs) const
	{
		return std::memcmp(m_, rhs.m_, sizeof(m_)) == 0;
	}

	bool operator!=(const Matrix4x4 &rhs) const
	{
		return !(*this == rhs);
	}

	friend Matrix4x4 operator*(const Matrix4x4 &a, const Matrix4x4 &b)
	{
		Matrix4x4 r;
		for (int row = 0; row < 4; row++) {
			for (int col = 0; col < 4; col++) {
				float sum = 0.0f;
				for (int k = 0; k < 4; k++) {
					sum += a.m_[row][k] * b.m_[k][col];
				}
				r.m_[row][col] = sum;
			}
		}
		return r;
	}

	friend Matrix4x4 operator+(Matrix4x4 a, const Matrix4x4 &b)
	{
		a += b;
		return a;
	}

	friend Matrix4x4 operator-(Matrix4x4 a, const Matrix4x4 &b)
	{
		a -= b;
		return a;
	}

	/**
	 * @brief Row-vector times matrix, mirroring QVector4D * QMatrix4x4.
	 */
	friend Vector4D operator*(const Vector4D &v, const Matrix4x4 &m)
	{
		return Vector4D(v.x() * m.m_[0][0] + v.y() * m.m_[1][0] +
							v.z() * m.m_[2][0] + v.w() * m.m_[3][0],
						v.x() * m.m_[0][1] + v.y() * m.m_[1][1] +
							v.z() * m.m_[2][1] + v.w() * m.m_[3][1],
						v.x() * m.m_[0][2] + v.y() * m.m_[1][2] +
							v.z() * m.m_[2][2] + v.w() * m.m_[3][2],
						v.x() * m.m_[0][3] + v.y() * m.m_[1][3] +
							v.z() * m.m_[2][3] + v.w() * m.m_[3][3]);
	}

	/**
	 * @brief Matrix times column-vector, mirroring QMatrix4x4 * QVector4D.
	 */
	friend Vector4D operator*(const Matrix4x4 &m, const Vector4D &v)
	{
		return Vector4D(m.m_[0][0] * v.x() + m.m_[0][1] * v.y() +
							m.m_[0][2] * v.z() + m.m_[0][3] * v.w(),
						m.m_[1][0] * v.x() + m.m_[1][1] * v.y() +
							m.m_[1][2] * v.z() + m.m_[1][3] * v.w(),
						m.m_[2][0] * v.x() + m.m_[2][1] * v.y() +
							m.m_[2][2] * v.z() + m.m_[2][3] * v.w(),
						m.m_[3][0] * v.x() + m.m_[3][1] * v.y() +
							m.m_[3][2] * v.z() + m.m_[3][3] * v.w());
	}

private:
	float m_[4][4];
};

inline Matrix4x4 Matrix4x4::inverted(bool *invertible) const
{
	// Gaussian elimination with partial pivoting over an augmented matrix
	Matrix4x4 inv;
	float aug[4][8];
	for (int row = 0; row < 4; row++) {
		for (int col = 0; col < 4; col++) {
			aug[row][col] = m_[row][col];
			aug[row][col + 4] = inv.m_[row][col];
		}
	}

	for (int col = 0; col < 4; col++) {
		int pivot = col;
		for (int row = col + 1; row < 4; row++) {
			if (std::fabs(aug[row][col]) > std::fabs(aug[pivot][col])) {
				pivot = row;
			}
		}

		if (aug[pivot][col] == 0.0f) {
			if (invertible) {
				*invertible = false;
			}
			return Matrix4x4();
		}

		if (pivot != col) {
			for (int k = 0; k < 8; k++) {
				std::swap(aug[col][k], aug[pivot][k]);
			}
		}

		float d = aug[col][col];
		for (int k = 0; k < 8; k++) {
			aug[col][k] /= d;
		}

		for (int row = 0; row < 4; row++) {
			if (row != col) {
				float f = aug[row][col];
				for (int k = 0; k < 8; k++) {
					aug[row][k] -= f * aug[col][k];
				}
			}
		}
	}

	for (int row = 0; row < 4; row++) {
		for (int col = 0; col < 4; col++) {
			inv.m_[row][col] = aug[row][col + 4];
		}
	}

	if (invertible) {
		*invertible = true;
	}
	return inv;
}

}

#endif // OAK_NODEMATHTYPES_H
