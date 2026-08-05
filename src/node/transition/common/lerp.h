#pragma once
namespace olive {
template <typename T> T lerp(const T &a, const T &b, float t) { return a + (b - a) * t; }
}
