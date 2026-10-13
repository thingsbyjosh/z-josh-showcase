// dll/career/career_genome.h
// Deterministic driver genome. Identity (driver_id) is the only source: the same driver
// ALWAYS produces the same traits, on any machine, no matter how many times we regenerate the XML.
// Costs zero save bytes and is stable across the rewrite cycle the boot-time read imposes.
#pragma once
#include <cstdint>
#include <cmath>
#include <string>

namespace career {

// 64-bit FNV-1a: cheap, no dependencies, and good enough for seeding.
inline uint64_t GenomeSeed(const std::string& driverId) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : driverId) { h ^= (uint64_t)c; h *= 1099511628211ULL; }
    return h ? h : 1ULL;   // 0 would be a degenerate state for the mixer
}

// splitmix64: a one-step mixer, uniform and with no shared state -> lets the stream be indexed.
inline uint64_t GenomeMix(uint64_t seed, int idx) {
    uint64_t z = seed + 0x9E3779B97F4A7C15ULL * (uint64_t)(idx + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

inline double GenomeUniform(uint64_t seed, int idx) {
    // 53 mantissa bits -> (0,1). Exact 0 is avoided because Box-Muller takes a logarithm.
    uint64_t u = GenomeMix(seed, idx) >> 11;
    double d = (double)u / 9007199254740992.0;
    return d <= 0.0 ? 1e-12 : d;
}

// Standard normal via Box-Muller. Consumes TWO uniform draws per index, in disjoint
// ranges (idx*2, idx*2+1), so GenomeNormal(idx) is independent across indices.
inline double GenomeNormal(uint64_t seed, int idx) {
    double u1 = GenomeUniform(seed, idx * 2);
    double u2 = GenomeUniform(seed, idx * 2 + 1);
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
}

}   // namespace career
