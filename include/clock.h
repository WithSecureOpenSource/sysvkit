#pragma once

#include <stdint.h>

typedef unsigned long long usec_t;

static inline uintmax_t ul_div_up(uintmax_t numerator, uintmax_t denominator)
{
    return (numerator + denominator - 1) / denominator;
}

static inline usec_t s2us(uintmax_t s)
{
    return s * 1000 * 1000;
}

static inline usec_t ms2us(uintmax_t ms)
{
    return ms * 1000;
}

static inline usec_t ns2us(uintmax_t ns)
{
    return ul_div_up(ns, 1000);
}

static inline usec_t us2s(uintmax_t us)
{
    return ul_div_up(us, 1000 * 1000);
}

static inline usec_t us2ms(uintmax_t us)
{
    return ul_div_up(us, 1000);
}

static inline usec_t us2ns(uintmax_t us)
{
    return us * 1000;
}

usec_t clock_usec(void);
usec_t clock_realtime_usec(void);
