#pragma once

#define CONCAT_IMPL(a, b) a ## b
#define CONCAT(a, b) CONCAT_IMPL(a, b)

// __COUNTER__ is a unique value, that incremements each time it is encountered
#define DEFER(lambda) auto CONCAT(defer_name, __COUNTER__) = Defer(lambda)
