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

/// @brief A compression scheme used for individual chunks.
enum CompressionScheme {
    GZIP = 1,
    ZLIB = 2,
    NOTHING = 3,
    LZ4 = 4,
    CUSTOM = 127,
};

namespace internal {

// pad the stream to the nearest 4096-byte sector end
void pad(std::ostream& stream) {
    std::size_t pos = stream.tellp();
    // offset from beginning of sector
    std::size_t offset = pos & 0xfff;
    std::size_t bytes_left = 0x1000 - offset;
    if (bytes_left == 0) return;
    for (; bytes_left > 0; --bytes_left)
        stream.put(0);
}

}

/// @brief Gets individual chunk NBT tags from the region file.
/// @param path The path of the region file to read.
/// @return All chunks stored in the region file, with no guarantee as to order. If a chunk is not present, its tag will be `{"EmptyChunk":0b}` (`NBTTag(TAG_BYTE, "EmptyChunk", 0)`).
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
        uint_t byte_length = internal::read(file, TAG_INT).i;
        CompressionScheme scheme = (CompressionScheme)internal::read(file, TAG_BYTE).b;
        switch (scheme) {
        case NOTHING:
            ret[i] = NBTTag::from_nbt(file);
            break;
        case GZIP:
            ret[i] = read_nbt_gzip(file);
            break;
        case ZLIB:
            ret[i] = read_nbt_zlib(file);
            break;
        case LZ4:
        case CUSTOM:
        default:
            throw std::runtime_error("Unsupported compression type with ordinal " + std::to_string(scheme));
        }
    }

    return ret;
}

/// @brief Writes individual chunk NBT tags to the region file.
/// @param path The path to the region file. Overwrites it if it exists, and creates it if it does not.
/// @param chunk_tags The chunk NBT tags to write to the file (see https://minecraft.wiki/w/Chunk_format). Must be in an order such that the formula `x + z * 32`, where `x` and `z` are the chunk coordinates of the chunk relative to the region, indexes the chunk at `(x, z)`. If a chunk does not exist, its corresponding tag must not be of type `TAG_COMPOUND`.
/// @param chunk_compression The compression format to use for chunks. Defaults to `ZLIB` (which is also Minecraft's default). `LZ4` and `CUSTOM` are not currently supported.
/// @note All chunks will have their timestamps set, even if they don't exist or weren't modified. This behaviour is subject to change in a later version of the library.
void write_region_file(const std::string& path, const std::array<NBTTag, 1024>& chunk_tags, CompressionScheme chunk_compression = ZLIB) {
    std::ofstream file(path, std::ios::binary);

    // pre-write locations to reserve space
    std::array<uint_t, 1024> locations;
    file.write((char*)locations.data(), 4096);

    std::array<uint_t, 1024> timestamps;
    for (std::size_t i = 0; i < 1024; ++i) {
        /// TODO: this is suboptimal behaviour and should be modified to at least allow the user to set the timestamps
        // I could just use std::time(nullptr), but that's less fun
        timestamps[i] = std::chrono::sys_seconds().time_since_epoch().count();
        // bswap if on little-endian
        if (internal::is_little_endian())
            timestamps[i] = __builtin_bswap32(timestamps[i]);
    }

    for (std::size_t i = 0; i < 1024; ++i) {
        if (chunk_tags[i].type != TAG_COMPOUND) {
            locations[i] = 0;
            continue;
        }

        // write offset
        std::size_t pos = file.tellp();
        locations[i] = ((pos >> 12) << 8) & 0xffffff00;

        // init space for tag size
        uint_t bytes = 0;
        internal::writei(file, bytes);
        file.put(chunk_compression);

        switch (chunk_compression) {
        case GZIP:
            write_nbt_gzip(file, chunk_tags[i]);
            break;
        case ZLIB:
            write_nbt_zlib(file, chunk_tags[i]);
            break;
        case NOTHING:
            chunk_tags[i].to_nbt(file);
            break;
        case LZ4:
        case CUSTOM:
        default:
            throw std::runtime_error("Unsupported compression type with ordinal " + std::to_string(chunk_compression));
        }

        // write tag size
        std::size_t end_pos = file.tellp();
        bytes = end_pos - pos;
        file.seekp(pos);
        internal::writei(file, bytes);
        file.seekp(end_pos);

        internal::pad(file);
        // write sector size to header
        std::size_t new_pos = file.tellp();
        locations[i] |= ((new_pos - pos) >> 12) & 0xff;

        // use bswap if on little-endian
        if (internal::is_little_endian())
            locations[i] = __builtin_bswap32(locations[i]);
    }

    file.seekp(0);
    file.write((char*)locations.data(), 4096);
}

}

#endif