/* TrackerXXL: compatibility shims so the Polyend SDK headers (written for arm-none-eabi libstdc++) build with the
 * Zig/libc++ toolchain. Force-included (-include) in every translation unit that sees FEATURE_SYNTH. */
#pragma once
#ifndef _GLIBCXX_THROW_OR_ABORT
#define _GLIBCXX_THROW_OR_ABORT(x) __builtin_abort()
#endif
