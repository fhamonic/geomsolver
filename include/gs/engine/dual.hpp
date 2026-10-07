#pragma once

// Floating-point contraction off for everything defined after this point in
// the including translation unit. With FMA available (-march=native, which
// OPTIMIZE_FOR_NATIVE adds) GCC defaults to -ffp-contract=fast and fuses
// a * b + c differently in the double and Dual instantiations of the same
// code, which breaks the bit identity documented below. Include this header
// before any code it must govern.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace gs {

// Forward-mode dual number with N derivative lanes. Each operation computes its
// value with exactly the floating-point operations of the plain double code,
// so a program evaluated with double and with Dual<N> yields bit-identical
// values. Keep it that way: rewriting a value as e.g. a * (1 / b) instead of
// a / b breaks the identity that tests/engine pins.
template <int N>
struct Dual {
    double v;
    double d[static_cast<std::size_t>(N)];

    constexpr Dual() : v(0.0), d{} {}
    constexpr Dual(double x) : v(x), d{} {}
};

template <class T>
inline constexpr bool is_dual_v = false;
template <int N>
inline constexpr bool is_dual_v<Dual<N>> = true;

template <int N>
inline Dual<N> operator+(const Dual<N> & a, const Dual<N> & b) {
    Dual<N> r;
    r.v = a.v + b.v;
    for(int i = 0; i < N; ++i) r.d[i] = a.d[i] + b.d[i];
    return r;
}
template <int N>
inline Dual<N> operator-(const Dual<N> & a, const Dual<N> & b) {
    Dual<N> r;
    r.v = a.v - b.v;
    for(int i = 0; i < N; ++i) r.d[i] = a.d[i] - b.d[i];
    return r;
}
template <int N>
inline Dual<N> operator*(const Dual<N> & a, const Dual<N> & b) {
    Dual<N> r;
    r.v = a.v * b.v;
    for(int i = 0; i < N; ++i) r.d[i] = a.d[i] * b.v + a.v * b.d[i];
    return r;
}
template <int N>
inline Dual<N> operator/(const Dual<N> & a, const Dual<N> & b) {
    Dual<N> r;
    r.v = a.v / b.v;
    const double inv = 1.0 / b.v;
    for(int i = 0; i < N; ++i) r.d[i] = (a.d[i] - r.v * b.d[i]) * inv;
    return r;
}
template <int N>
inline Dual<N> operator-(const Dual<N> & a) {
    Dual<N> r;
    r.v = -a.v;
    for(int i = 0; i < N; ++i) r.d[i] = -a.d[i];
    return r;
}
template <int N>
inline Dual<N> operator+(const Dual<N> & a, double s) {
    Dual<N> r = a;
    r.v = a.v + s;
    return r;
}
template <int N>
inline Dual<N> operator+(double s, const Dual<N> & a) {
    Dual<N> r = a;
    r.v = s + a.v;
    return r;
}
template <int N>
inline Dual<N> operator-(const Dual<N> & a, double s) {
    Dual<N> r = a;
    r.v = a.v - s;
    return r;
}
template <int N>
inline Dual<N> operator-(double s, const Dual<N> & a) {
    Dual<N> r;
    r.v = s - a.v;
    for(int i = 0; i < N; ++i) r.d[i] = -a.d[i];
    return r;
}
template <int N>
inline Dual<N> operator*(const Dual<N> & a, double s) {
    Dual<N> r;
    r.v = a.v * s;
    for(int i = 0; i < N; ++i) r.d[i] = a.d[i] * s;
    return r;
}
template <int N>
inline Dual<N> operator*(double s, const Dual<N> & a) {
    Dual<N> r;
    r.v = s * a.v;
    for(int i = 0; i < N; ++i) r.d[i] = s * a.d[i];
    return r;
}
template <int N>
inline Dual<N> operator/(const Dual<N> & a, double s) {
    Dual<N> r;
    r.v = a.v / s;
    for(int i = 0; i < N; ++i) r.d[i] = a.d[i] / s;
    return r;
}
template <int N>
inline Dual<N> operator/(double s, const Dual<N> & a) {
    Dual<N> r;
    r.v = s / a.v;
    const double k = -r.v / a.v;
    for(int i = 0; i < N; ++i) r.d[i] = k * a.d[i];
    return r;
}

// Elementary functions for double and Dual<N>. Generic code must call these
// qualified (ad::sqrt): an unqualified call with a double argument finds
// ::sqrt instead, which lacks the derivative conventions documented here.
namespace ad {

inline double val(double x) { return x; }
template <int N>
inline double val(const Dual<N> & x) {
    return x.v;
}

template <int N>
inline Dual<N> chain(const Dual<N> & a, double f, double df) {
    Dual<N> r;
    r.v = f;
    for(int i = 0; i < N; ++i) r.d[i] = df * a.d[i];
    return r;
}

// sqrt: NaN for negative arguments (both paths); derivative 0 at 0.
inline double sqrt(double x) { return std::sqrt(x); }
template <int N>
inline Dual<N> sqrt(const Dual<N> & a) {
    const double s = std::sqrt(a.v);
    return chain(a, s, a.v > 0.0 ? 0.5 / s : 0.0);
}
// Every sine and cosine goes through one sincos() call. GCC fuses separate
// sin(x) and cos(x) calls into sincos() in some inlining contexts and not in
// others, and glibc's sincos differs from sin/cos by one ulp on about 0.1% of
// inputs: mixing the two breaks the double/Dual bit identity.
inline void sin_cos(double x, double & s, double & c) {
#if defined(__GLIBC__)
    ::sincos(x, &s, &c);
#else
    s = std::sin(x);
    c = std::cos(x);
#endif
}
inline double sin(double x) {
    double s, c;
    sin_cos(x, s, c);
    return s;
}
template <int N>
inline Dual<N> sin(const Dual<N> & a) {
    double s, c;
    sin_cos(a.v, s, c);
    return chain(a, s, c);
}
inline double cos(double x) {
    double s, c;
    sin_cos(x, s, c);
    return c;
}
template <int N>
inline Dual<N> cos(const Dual<N> & a) {
    double s, c;
    sin_cos(a.v, s, c);
    return chain(a, c, -s);
}
// (cos x, sin x) from a single sincos().
inline void cis(double x, double & c, double & s) { sin_cos(x, s, c); }
template <int N>
inline void cis(const Dual<N> & a, Dual<N> & c, Dual<N> & s) {
    double sv, cv;
    sin_cos(a.v, sv, cv);
    c = chain(a, cv, -sv);
    s = chain(a, sv, cv);
}
inline double tan(double x) { return std::tan(x); }
template <int N>
inline Dual<N> tan(const Dual<N> & a) {
    const double t = std::tan(a.v);
    return chain(a, t, 1.0 + t * t);
}
// asin/acos clamp the argument to [-1, 1]; the derivative is 0 where |x| >= 1.
inline double asin(double x) { return std::asin(std::clamp(x, -1.0, 1.0)); }
template <int N>
inline Dual<N> asin(const Dual<N> & a) {
    const double f = std::asin(std::clamp(a.v, -1.0, 1.0));
    return chain(a, f,
                 std::fabs(a.v) < 1.0 ? 1.0 / std::sqrt(1.0 - a.v * a.v) : 0.0);
}
inline double acos(double x) { return std::acos(std::clamp(x, -1.0, 1.0)); }
template <int N>
inline Dual<N> acos(const Dual<N> & a) {
    const double f = std::acos(std::clamp(a.v, -1.0, 1.0));
    return chain(
        a, f, std::fabs(a.v) < 1.0 ? -1.0 / std::sqrt(1.0 - a.v * a.v) : 0.0);
}
inline double atan(double x) { return std::atan(x); }
template <int N>
inline Dual<N> atan(const Dual<N> & a) {
    return chain(a, std::atan(a.v), 1.0 / (1.0 + a.v * a.v));
}
inline double exp(double x) { return std::exp(x); }
template <int N>
inline Dual<N> exp(const Dual<N> & a) {
    const double e = std::exp(a.v);
    return chain(a, e, e);
}
inline double log(double x) { return std::log(x); }
template <int N>
inline Dual<N> log(const Dual<N> & a) {
    return chain(a, std::log(a.v), 1.0 / a.v);
}
// abs: derivative 0 at 0.
inline double abs(double x) { return std::fabs(x); }
template <int N>
inline Dual<N> abs(const Dual<N> & a) {
    return chain(a, std::fabs(a.v), a.v > 0.0 ? 1.0 : (a.v < 0.0 ? -1.0 : 0.0));
}
// atan2: derivative 0 at the origin.
inline double atan2(double y, double x) { return std::atan2(y, x); }
template <int N>
inline Dual<N> atan2(const Dual<N> & y, const Dual<N> & x) {
    Dual<N> r;
    r.v = std::atan2(y.v, x.v);
    const double r2 = x.v * x.v + y.v * y.v;
    if(r2 > 0.0) {
        const double ix = x.v / r2, iy = y.v / r2;
        for(int i = 0; i < N; ++i) r.d[i] = ix * y.d[i] - iy * x.d[i];
    }
    return r;
}
// pow: the exponent derivative is taken only for a positive base.
inline double pow(double a, double b) { return std::pow(a, b); }
template <int N>
inline Dual<N> pow(const Dual<N> & a, const Dual<N> & b) {
    Dual<N> r;
    r.v = std::pow(a.v, b.v);
    const double da = b.v == 0.0 ? 0.0 : b.v * std::pow(a.v, b.v - 1.0);
    const double db = a.v > 0.0 ? r.v * std::log(a.v) : 0.0;
    for(int i = 0; i < N; ++i) r.d[i] = da * a.d[i] + db * b.d[i];
    return r;
}

template <class T>
inline T constant(double v) {
    return T(v);
}

}  // namespace ad
}  // namespace gs
