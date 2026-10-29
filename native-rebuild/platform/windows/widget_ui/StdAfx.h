#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cmath>
#include <cwchar>
#include <string>
#include <type_traits>
#include <vector>
template<class A,class B> inline std::common_type_t<A,B> min(A a,B b) { using T=std::common_type_t<A,B>; return static_cast<T>(a)<static_cast<T>(b) ? static_cast<T>(a) : static_cast<T>(b); }
template<class A,class B> inline std::common_type_t<A,B> max(A a,B b) { using T=std::common_type_t<A,B>; return static_cast<T>(a)>static_cast<T>(b) ? static_cast<T>(a) : static_cast<T>(b); }
