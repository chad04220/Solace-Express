// Air Xpress - shared math and utilities
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <thread>

static const float PI = 3.14159265358979f;
static const float DEG = PI / 180.0f;
static const float G0 = 9.81f;
static const float MS_TO_KT = 1.943844f;
static const float M_TO_FT = 3.28084f;

inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
inline float wrapAngle(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
inline float wrapDeg360(float a) { a = fmodf(a, 360.0f); return a < 0 ? a + 360.0f : a; }
// Exponential approach, frame-rate independent
inline float approach(float cur, float target, float rate, float dt) { return target + (cur - target) * expf(-rate * dt); }

struct vec2 { float x = 0, y = 0; vec2() {} vec2(float a, float b) : x(a), y(b) {}
  vec2 operator+(vec2 o) const { return {x + o.x, y + o.y}; } vec2 operator-(vec2 o) const { return {x - o.x, y - o.y}; }
  vec2 operator*(float s) const { return {x * s, y * s}; } };
inline float length(vec2 v) { return sqrtf(v.x * v.x + v.y * v.y); }

struct vec3 {
  float x = 0, y = 0, z = 0;
  vec3() {}
  vec3(float a, float b, float c) : x(a), y(b), z(c) {}
  explicit vec3(float s) : x(s), y(s), z(s) {}
  vec3 operator+(const vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  vec3 operator-(const vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
  vec3 operator-() const { return {-x, -y, -z}; }
  vec3& operator+=(const vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  vec3& operator-=(const vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
  float operator[](int i) const { return (&x)[i]; }
};
inline vec3 operator*(float s, const vec3& v) { return v * s; }
inline float dot(const vec3& a, const vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(const vec3& a, const vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(const vec3& v) { return sqrtf(dot(v, v)); }
inline vec3 normalize(const vec3& v) { float l = length(v); return l > 1e-9f ? v / l : vec3(0, 0, 0); }
inline vec3 mul(const vec3& a, const vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline vec3 lerp(const vec3& a, const vec3& b, float t) { return a + (b - a) * t; }

struct quat {
  float w = 1, x = 0, y = 0, z = 0;
  quat() {}
  quat(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}
  static quat axisAngle(const vec3& ax, float a) { vec3 n = ::normalize(ax); float s = sinf(a * 0.5f); return {cosf(a * 0.5f), n.x * s, n.y * s, n.z * s}; }
  quat operator*(const quat& q) const {
    return {w * q.w - x * q.x - y * q.y - z * q.z, w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x, w * q.z + x * q.y - y * q.x + z * q.w};
  }
  quat conj() const { return {w, -x, -y, -z}; }
  vec3 rotate(const vec3& v) const { vec3 u(x, y, z); vec3 t = cross(u, v) * 2.0f; return v + t * w + cross(u, t); }
  void normalize() { float l = sqrtf(w * w + x * x + y * y + z * z); w /= l; x /= l; y /= l; z /= l; }
};
inline quat slerp(quat a, quat b, float t) {
  float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
  if (d < 0) { b = {-b.w, -b.x, -b.y, -b.z}; d = -d; }
  quat r(a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
  r.normalize(); return r;
}

// Column-major 4x4 matrix (OpenGL convention)
struct mat4 {
  float m[16];
  mat4() { memset(m, 0, sizeof(m)); m[0] = m[5] = m[10] = m[15] = 1; }
  float& operator()(int r, int c) { return m[c * 4 + r]; }
  float operator()(int r, int c) const { return m[c * 4 + r]; }
  mat4 operator*(const mat4& b) const {
    mat4 r; for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { float s = 0; for (int k = 0; k < 4; k++) s += (*this)(i, k) * b(k, j); r(i, j) = s; } return r;
  }
};
inline mat4 perspective(float fovy, float aspect, float n, float f) {
  mat4 r; float t = 1.0f / tanf(fovy * 0.5f);
  r(0, 0) = t / aspect; r(1, 1) = t; r(2, 2) = (f + n) / (n - f); r(2, 3) = 2 * f * n / (n - f); r(3, 2) = -1; r(3, 3) = 0; return r;
}
inline mat4 lookAt(vec3 eye, vec3 at, vec3 up) {
  vec3 f = normalize(at - eye), s = normalize(cross(f, up)), u = cross(s, f);
  mat4 r; r(0, 0) = s.x; r(0, 1) = s.y; r(0, 2) = s.z; r(1, 0) = u.x; r(1, 1) = u.y; r(1, 2) = u.z;
  r(2, 0) = -f.x; r(2, 1) = -f.y; r(2, 2) = -f.z; r(0, 3) = -dot(s, eye); r(1, 3) = -dot(u, eye); r(2, 3) = dot(f, eye); return r;
}

// Deterministic RNG
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed = 1234567) : s(seed ? seed : 1) {}
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float uni() { return (next() & 0xFFFFFF) / 16777216.0f; }
  float range(float a, float b) { return a + (b - a) * uni(); }
};

inline std::string fmt(const char* f, ...) {
  char buf[1024]; va_list ap; va_start(ap, f); vsnprintf(buf, sizeof(buf), f, ap); va_end(ap); return buf;
}

// Runs f(i) for i in [0, n) across the CPU's cores (each index exactly once, in any order); returns when all are done
template <class F> inline void parallelFor(int n, F&& f) {
  int nt = std::max(1, std::min((int)std::thread::hardware_concurrency(), n));
  if (nt <= 1) { for (int i = 0; i < n; i++) f(i); return; }
  std::atomic<int> next{0};
  auto run = [&]() { for (int i; (i = next.fetch_add(1)) < n;) f(i); };
  std::vector<std::thread> ts;
  for (int t = 1; t < nt; t++) ts.emplace_back(run);
  run();
  for (auto& t : ts) t.join();
}
