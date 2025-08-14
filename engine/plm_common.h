#pragma once

#include "iostream"

#ifndef NDEBUG
#define ASSERT(condition, message)                                             \
    do                                                                         \
    {                                                                          \
        auto bCondition = static_cast<bool>(condition);                        \
        if (!bCondition)                                                       \
        {                                                                      \
            std::cerr << "Assertion `" #condition "` failed in " << __FILE__   \
                      << " line " << __LINE__ << ": " << message << std::endl; \
            std::terminate();                                                  \
        }                                                                      \
    }                                                                          \
    while (false)
#else
#define ASSERT(condition, message) do { } while (false)
#endif


inline size_t AlignUp(size_t originalSize, size_t alignment)
{
    size_t alignedSize = originalSize;
	if (alignment > 0)
	{
		alignedSize = (alignedSize + alignment - 1) & ~(alignment - 1);
	}
	return alignedSize;
}
