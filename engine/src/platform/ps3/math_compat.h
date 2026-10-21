#pragma once

inline long double log2l(long double x) { return __builtin_log2((double)x); }
inline long double logbl(long double x) { return __builtin_logb((double)x); }
inline double nexttoward(double x, long double y) { return __builtin_nextafter(x, (double)y); }
inline float nexttowardf(float x, long double y) { return __builtin_nextafterf(x, (float)y); }
inline long double nexttowardl(long double x, long double y) { return __builtin_nextafter((double)x, (double)y); }
