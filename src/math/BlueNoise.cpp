// BlueNoise.cpp
#include "BlueNoise.h"
#include "utils/HashFunctions.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

// Gaussian falloff radius (in texels) of the void-and-cluster energy field.
static constexpr double s_sigma{ 1.9 };

// Adds (sign +1) or removes (sign -1) one point's Gaussian energy
// contribution. The kernel is indexed by toroidal offset, so contributions
// wrap around the edges and the finished map tiles seamlessly.
static void applyPoint(
    std::vector<double>& energy,
    const std::vector<double>& kernel,
    int size,
    int pos,
    double sign)
{
    int py{ pos / size };
    int px{ pos % size };
    for (int ky{ 0 }; ky < size; ++ky) {
        int rowOut{ ((py + ky) % size) * size };
        int rowKernel{ ky * size };
        for (int kx{ 0 }; kx < size; ++kx) {
            energy[rowOut + (px + kx) % size] += sign * kernel[rowKernel + kx];
        }
    }
}

// Index of the 1 with the highest energy (the tightest cluster). Ties break
// on the lowest index so results are reproducible.
static int tightestCluster(
    const std::vector<uint8_t>& pattern, const std::vector<double>& energy)
{
    int best{ -1 };
    double bestEnergy{ -1.0 };
    for (int i{ 0 }; i < static_cast<int>(pattern.size()); ++i) {
        if (pattern[i] == 1 && energy[i] > bestEnergy) {
            bestEnergy = energy[i];
            best = i;
        }
    }
    return best;
}

// Index of the 0 with the lowest energy (the largest void). Ties break on
// the lowest index so results are reproducible.
static int largestVoid(
    const std::vector<uint8_t>& pattern, const std::vector<double>& energy)
{
    int best{ -1 };
    double bestEnergy{ std::numeric_limits<double>::max() };
    for (int i{ 0 }; i < static_cast<int>(pattern.size()); ++i) {
        if (pattern[i] == 0 && energy[i] < bestEnergy) {
            bestEnergy = energy[i];
            best = i;
        }
    }
    return best;
}

std::vector<uint8_t> BlueNoise::generate(int size, uint64_t seed) {
    if (size <= 0) {
        throw std::invalid_argument("BlueNoise::generate: size must be positive");
    }
    int count{ size * size };

    // Wrapped Gaussian kernel, indexed by toroidal offset.
    std::vector<double> kernel(count);
    for (int dy{ 0 }; dy < size; ++dy) {
        for (int dx{ 0 }; dx < size; ++dx) {
            double wx{ static_cast<double>(std::min(dx, size - dx)) };
            double wy{ static_cast<double>(std::min(dy, size - dy)) };
            kernel[dy * size + dx] =
                std::exp(-(wx * wx + wy * wy) / (2.0 * s_sigma * s_sigma));
        }
    }

    // Initial binary pattern: ~10% minority points placed by a seeded
    // Fisher-Yates shuffle (Hash::pcg keeps this reproducible).
    int initialOnes{ std::max(1, count / 10) };
    std::vector<int> order(count);
    for (int i{ 0 }; i < count; ++i) {
        order[i] = i;
    }
    for (int i{ count - 1 }; i > 0; --i) {
        uint64_t j{ Hash::pcg(seed, static_cast<uint64_t>(i)) %
                    static_cast<uint64_t>(i + 1) };
        std::swap(order[i], order[static_cast<int>(j)]);
    }
    std::vector<uint8_t> pattern(count, 0);
    std::vector<double> energy(count, 0.0);
    for (int i{ 0 }; i < initialOnes; ++i) {
        pattern[order[i]] = 1;
        applyPoint(energy, kernel, size, order[i], 1.0);
    }

    // Relaxation: move the point in the tightest cluster into the largest
    // void until the pattern is stable (the removed point lands back where it
    // was). The iteration cap only guards against cycling.
    for (int iteration{ 0 }; iteration < count * 10; ++iteration) {
        int cluster{ tightestCluster(pattern, energy) };
        pattern[cluster] = 0;
        applyPoint(energy, kernel, size, cluster, -1.0);
        int voidPos{ largestVoid(pattern, energy) };
        pattern[voidPos] = 1;
        applyPoint(energy, kernel, size, voidPos, 1.0);
        if (voidPos == cluster) {
            break;
        }
    }

    std::vector<int> rank(count, 0);

    // Phase 1: peel the initial points off tightest-cluster-first, assigning
    // ranks initialOnes-1 down to 0. Works on copies; the fill phase below
    // needs the relaxed prototype intact.
    {
        std::vector<uint8_t> phasePattern{ pattern };
        std::vector<double> phaseEnergy{ energy };
        for (int r{ initialOnes - 1 }; r >= 0; --r) {
            int cluster{ tightestCluster(phasePattern, phaseEnergy) };
            phasePattern[cluster] = 0;
            applyPoint(phaseEnergy, kernel, size, cluster, -1.0);
            rank[cluster] = r;
        }
    }

    // Phases 2 and 3: fill the remaining ranks largest-void-first. On a torus
    // the "largest void among the 0s" criterion equals "lowest 1-energy", so
    // one loop covers both of Ulichney's phases.
    for (int r{ initialOnes }; r < count; ++r) {
        int voidPos{ largestVoid(pattern, energy) };
        pattern[voidPos] = 1;
        applyPoint(energy, kernel, size, voidPos, 1.0);
        rank[voidPos] = r;
    }

    // Ranks to bytes; every level gets an equal share of texels whenever
    // count is a multiple of 256.
    std::vector<uint8_t> result(count);
    for (int i{ 0 }; i < count; ++i) {
        double scaled{ (static_cast<double>(rank[i]) + 0.5) * 256.0 /
                       static_cast<double>(count) };
        result[i] = static_cast<uint8_t>(std::min(static_cast<int>(scaled), 255));
    }
    return result;
}
