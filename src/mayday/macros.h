#pragma once

#define CONCAT_IMPL(a, b) a ## b
#define CONCAT(a, b) CONCAT_IMPL(a, b)

// __COUNTER__ is a unique value, that incremements each time it is encountered
// Defer needs to take in an unknown number of args, because the pre-processor splits arbitrarily with commas
// so in the capture list [capture_1, capture_2] of the lambda it will think the , there is a new arg
#define DEFER(...) auto CONCAT(defer_name, __COUNTER__) = Defer(__VA_ARGS__)
