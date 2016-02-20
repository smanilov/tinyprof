#ifndef ICSA_DSWP_TINYPROF_H
#define ICSA_DSWP_TINYPROF_H

#include <iostream>
using std::cerr;

#include <cstdint>

inline uint64_t rdtsc(){
    unsigned int lo,hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

#endif
