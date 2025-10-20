#ifndef REGION_HPP
#define REGION_HPP

#include <vector>
#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <algorithm>
#include <chrono>
#include <utility>
#include <cmath>

#include "core.hpp"
#include <level.hpp>

namespace nbt {

namespace internal {

}

/// @brief Gets individual chunk NBT tags from the region file.
/// @param path The path of the region file to read.
/// @return All chunks stored in the region file, with no guarantee as to order. If a chunk is not present, its tag will be `NBTTag(TAG_BYTE, "EmptyChunk", 0)`.
std::array<NBTTag, 1024> read_region_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);

    std::array<uint_t, 1024> offsets, timestamps;
    std::array<byte_t, 1024> sizes;
    for (std::size_t i = 0; i < 1024; ++i) {
        uint_t location = internal::read(file, TAG_INT).i;
        offsets[i] = location >> 8;
        sizes[i] = (byte_t)(location & 0xFF);
    }
    for (std::size_t i = 0; i < 1024; ++i)
        // not a batch read for endianness reasons
        // (why do little-endian machines have to exist)
        timestamps[i] = internal::read(file, TAG_INT).i;
    
    std::array<NBTTag, 1024> ret;
    for (std::size_t i = 0; i < 1024; ++i) {
        if (offsets[i] == 0) {
            // Empty chunk marker
            ret[i] = NBTTag(TAG_BYTE, "EmptyChunk", 0);
            continue;
        }
        file.seekg(offsets[i] * 4096);
        ret[i] = readNbt(file);
    }

    return ret;
}

}

#endif