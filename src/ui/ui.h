#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

namespace jl {
namespace ui {

int run(HINSTANCE instance, int showCommand, bool everywhere);

}
}
