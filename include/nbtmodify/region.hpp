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
#include <nbtmodify/core.hpp>

namespace nbt {

namespace internal {

struct pos {
    long_t x;
    long_t y;
    long_t z;

    bool operator ==(const pos& other) const {
        return (this->x == other.x and this->y == other.y and this->z == other.z);
    }
};

struct hash_pos_region {
    std::size_t operator()(pos& arg) const {
        return arg.y * 512 * 512 + arg.z * 512 + arg.x;
    }
};

struct hash_pos_section {
    std::size_t operator()(pos arg) const {
        return arg.y * 16 * 16 + arg.z * 16 + arg.x;
    }
};

//This is a performance-critical function.
template <typename T> int getVectorObjectIndex(T object, std::vector<T> vector) {
    for (int i = 0; i < vector.size(); ++i) if (vector[i] == object) return i;
    return -1;
}

struct BlockProperties {
    std::string name;
    std::vector<std::pair<std::string, std::string>> properties;

    bool operator==(const BlockProperties& other) {
        return (this->name == other.name && this->properties == other.properties);
    }
};

BlockProperties tagToProperties(NBTTag tag) {
    BlockProperties ret{};
    ret.name = (std::string)tag["Name"].value();
    std::optional<NBTTag> propertiesTag = tag["Properties"];
    std::vector<NBTTag> properties;
    if (propertiesTag) properties = (std::vector<NBTTag>)propertiesTag.value();
    ret.properties.resize(properties.size());
    for (int i = 0; i < properties.size(); ++i)
        ret.properties[i] = {properties[i].name, (std::string)properties[i]};
    return ret;
}

struct RegionBlockCache {
    int getOrAddProperties(BlockProperties properties) {
        int ret = getVectorObjectIndex(properties, this->cache);
        if (ret != -1) return ret;
        this->cache.push_back(properties);
        return this->cache.size();
    }

    BlockProperties getFromIndex(int index) {
        return this->cache[index];
    }

private:
    std::vector<BlockProperties> cache;
};

struct RegionBiomeCache {
    int getOrAddBiome(std::string biome) {
        int ret = getVectorObjectIndex(biome, this->cache);
        if (ret != -1) return ret;
        this->cache.push_back(biome);
        return this->cache.size();
    }

    std::string getFromIndex(int index) {
        return this->cache[index];
    }

private:
    std::vector<std::string> cache;
};

//Requires x, y, and z to be in the range [0, 15]
static int to_idx(int x, int y, int z) {
    if (x >= 16 || y >= 16 || z >= 16) throw;
    return x * 256 + y * 16 + z;
}

//Requires idx to be in the range [0, 4095]
static internal::pos to_pos(int idx) {
    if (idx >= 4096) throw;
    return {.x = (nbt::long_t)std::floor(idx / 256), .y = (nbt::long_t)std::floor(idx / 16) % 16, .z = idx % 16};
}

std::vector<int> parse_paletted_container_block(NBTTag container, RegionBlockCache * cache) {
    int num_outputs = 4096;
    std::vector<int> out(num_outputs);
    std::vector<NBTTag> palette = (std::vector<NBTTag>)container["palette"].value();
    if (palette.size() == 1) {
        int entry_index = cache->getOrAddProperties(internal::tagToProperties(palette[0]));
        for (int idx = 0; idx < num_outputs; ++idx)
            out[idx] = entry_index;
        return out;
    }
    std::vector<BlockProperties> normalisedPalette;
    normalisedPalette.resize(palette.size());
    for (int i = 0; i < palette.size(); ++i) normalisedPalette[i] = internal::tagToProperties(palette[i]);
    std::vector<NBTTag> data = container["data"].value();
    nbt::byte_t bit_size = std::max((int)std::ceil(std::log2(palette.size())), 4);
    nbt::ulong_t mask = std::pow(2, bit_size) - 1;
    nbt::ulong_t index = 0;
    nbt::byte_t len_val = 64;
    nbt::ulong_t val = (nbt::ulong_t)(nbt::long_t)data[index];
    nbt::ushort_t i = 0;
    //if using biome parsing and the size of the palette is 1 or 2, entries will fit cleanly into longs,
    //and the other jiggery-pokery is not required
    while (i < num_outputs) {
        if (len_val < bit_size) {
            ++index;
            len_val = 64;
            val = (nbt::ulong_t)(nbt::long_t)data[index];
        }
        auto palette_index = val & mask;
        if (palette_index >= palette.size()) {
            std::cerr << "Index to be searched for cannot be found within palette!!" /*" val = " << val << ", mask = " << mask << ", val & mask = " << palette_index << ", palette.size() = " << normalisedPalette.size() << ", i = " << i */<< std::endl;
            throw;
        }
        out[i] = cache->getOrAddProperties(normalisedPalette[palette_index]);
        len_val -= bit_size;
        val >>= bit_size;
        ++i;
    }
    
    return out;
}

std::vector<int> parse_paletted_container_biome(NBTTag container, RegionBiomeCache * cache) {
    int num_outputs = 64;
    std::vector<int> out(num_outputs);
    std::vector<NBTTag> palette = (std::vector<NBTTag>)container["palette"].value();
    if (palette.size() == 1) {
        int entry_index = cache->getOrAddBiome(palette[0]);
        for (int idx = 0; idx < num_outputs; ++idx)
            out[idx] = entry_index;
        return out;
    }
    std::vector<std::string> normalisedPalette;
    normalisedPalette.resize(palette.size());
    for (std::size_t i = 0; i < palette.size(); ++i) normalisedPalette[i] = (std::string)palette[i];
    std::vector<NBTTag> data = container["data"].value();
    nbt::byte_t bit_size = (nbt::byte_t)std::ceil(std::log2(palette.size()));
    nbt::ulong_t mask = std::pow(2, bit_size) - 1;
    nbt::ulong_t index = 0;
    nbt::byte_t len_val = 64;
    nbt::ulong_t val = (nbt::ulong_t)(nbt::long_t)data[index];
    nbt::ushort_t i = 0;
    //if using biome parsing and the size of the palette is 1 or 2, entries will fit cleanly into longs,
    //and the other jiggery-pokery is not required
    while (i < num_outputs) {
        if (len_val < bit_size) {
            ++index;
            if (palette.size() < 3) {
                len_val = 64;
                val = (nbt::ulong_t)(nbt::long_t)data[index];
            } else {
                nbt::byte_t len_last_val = len_val;
                len_val = 64;
                nbt::ulong_t last_data = val;
                val = (nbt::ulong_t)(nbt::long_t)data[index];
                nbt::byte_t bit_size_to_extract = bit_size - len_last_val;
                nbt::ulong_t temp_mask = std::pow(2, bit_size_to_extract) - 1;
                out[i] = cache->getOrAddBiome(normalisedPalette[((val & temp_mask) << len_last_val) | last_data]);
                val >>= bit_size_to_extract;
                ++i;
            }
        }
        auto palette_index = val & mask;
        if (palette_index >= palette.size()) {
            std::cerr << "Index to be searched for cannot be found within palette!!" /*" val = " << val << ", mask = " << mask << ", val & mask = " << palette_index << ", palette.size() = " << normalisedPalette.size() << ", i = " << i */<< std::endl;
            throw;
        }
        out[i] = cache->getOrAddBiome(normalisedPalette[palette_index]);
        len_val -= bit_size;
        val >>= bit_size;
        ++i;
    }
    
    return out;
}

}

struct Chunk {
    std::optional<NBTTag> data;
    byte_t x, z;
    std::string status;
    std::optional<std::vector<std::vector<int>>> blocks;
    std::optional<std::vector<std::vector<int>>> biomes;

    Chunk(std::optional<NBTTag> data, byte_t x, byte_t z, internal::RegionBlockCache * blockCache, internal::RegionBiomeCache * biomeCache) : data(data) {
        if (data) {
            NBTTag rdata = data.value();
            this->status = (std::string)rdata["Status"].value();
            if (this->status != "minecraft:full" and this->status != "full") {
                this->blocks = {};
                this->biomes = {};
            } else {
                std::vector<std::vector<int>> block_sections(24);
                std::vector<std::vector<int>> biome_sections(24);
                for (std::size_t i = 0; i < ((std::vector<NBTTag>)rdata["sections"].value()).size(); ++i) {
                    /*if (x == 0 and z == 0 and i == 5) {
                        //maybe I could have done better with this notation...
                        std::cout << (rdata["sections"].value()[5].value()).to_string() << std::endl;
                    }*/
                    NBTTag section = ((std::vector<NBTTag>)rdata["sections"].value())[i];
                    char y = section["Y"].value();
                    //std::vector<NBTTag> blocklight = section["BlockLight"];
                    //std::vector<NBTTag> skylight = section["SkyLight"];
                    std::vector<int> sorted_blocks = internal::parse_paletted_container_block(section["block_states"].value(), blockCache);
                    std::vector<int> sorted_biomes = internal::parse_paletted_container_biome(section["biomes"].value(), biomeCache);
                    block_sections[i] = sorted_blocks;
                    biome_sections[i] = sorted_biomes;
                }
                this->blocks = block_sections;
                this->biomes = biome_sections;
            }
        } else {
            this->blocks = {};
            this->biomes = {};
        }
        this->x = x;
        this->z = z;
    }
};

struct Region {
    std::vector<Chunk> chunks;
    internal::RegionBlockCache blocks{};
    internal::RegionBiomeCache biomes{};

    Region(std::vector<std::optional<NBTTag>>& chunk_tags, std::vector<nbt::uint_t> timestamps) {
        for (int z = 0; z < 32; ++z) {
            for (int x = 0; x < 32; ++x) {
                auto start_time = internal::current_time_millis();
                this->chunks.push_back(Chunk(chunk_tags[z * 32 + x], x, z, &this->blocks, &this->biomes));
                auto end_time = internal::current_time_millis();
                std::cout << "Inserted chunk at (" << x << ", " << z << ") (took " << (end_time - start_time).count() << "ms)" << std::endl;
            }
        }
    }
};

enum SchemeType {
    GZIP = 1,
    ZLIB = 2,
    NOTHING = 3
};

static std::string get_scheme(SchemeType scheme) {
    switch (scheme) {
        case GZIP: {
            return "gzip";
        }
        case ZLIB: {
            return "zlib";
        }
        case NOTHING: {
            return "no compression";
        }
        default: {
            return "unknown (" + std::to_string(scheme) + ")";
        }
    }
}

Region readRegion(const std::string& path) {
    std::cout << "init" << std::endl;
    std::ifstream stream;
    stream.open(path, std::ios_base::in | std::ios_base::binary);
    std::vector<nbt::uint_t> locations(1024), lengths(1024), timestamps(1024);
    std::vector<std::optional<NBTTag>> out(1024);
    byte_t locations_raw[4096], timestamps_raw[4096];
    stream.read((char*)locations_raw, 4096);
    stream.read((char*)timestamps_raw, 4096);
    for (int i = 0; i < 1024; ++i) {
        locations[i] = ((nbt::uint_t)(locations_raw[i*4+0]) << 16) + ((nbt::uint_t)(locations_raw[i*4+1]) << 8) + (nbt::uint_t)(locations_raw[i*4+2]);
        lengths[i] = (nbt::uint_t)(locations_raw[i*4+3]);
        timestamps[i] = ((nbt::uint_t)(timestamps_raw[i*4+0]) << 24) + ((nbt::uint_t)(timestamps_raw[i*4+1]) << 16) + ((nbt::uint_t)(timestamps_raw[i*4+2]) << 8) + (nbt::uint_t)(timestamps_raw[i*4+3]);
    }
    std::vector<std::array<byte_t, 4096>> sectors;
    std::array<byte_t, 4096> data;
    while (!stream.eof()) {
        stream.read((char*)(data.begin()), 4096);
        sectors.push_back(data);
    }
    for (int i = 0; i < 1024; ++i) {
        auto start = internal::current_time_millis();
        auto offset = (nbt::ulong_t)locations[i];
        auto ilength = (nbt::ulong_t)lengths[i];
        if (!offset or !ilength) {
            out.push_back({});
            auto end = internal::current_time_millis();
            std::cout << "Read chunk " << i << " (took " << (end - start).count() << "ms)..." << std::endl;
            continue;
        }
        std::vector<byte_t> chunk_bytes(sectors[offset-2].begin(), sectors[offset-2+ilength].end());
        nbt::uint_t length = (chunk_bytes[0] << 24) + (chunk_bytes[1] << 16) + (chunk_bytes[2] << 8) + chunk_bytes[3];
        chunk_bytes = std::vector<byte_t>(chunk_bytes.begin() + 4, chunk_bytes.begin() + 4 + length);
        byte_t scheme = chunk_bytes[0];
        chunk_bytes = std::vector<byte_t>(chunk_bytes.begin() + 1, chunk_bytes.end());
        NBTTag tag;
        switch (scheme) {
            case GZIP: {
                std::string sdata = gzip::decompress((const char*)&chunk_bytes[0], chunk_bytes.size());
                std::vector<byte_t> decompressed_bytes = std::vector<byte_t>(sdata.begin(), sdata.end());
                auto iter = decompressed_bytes.begin();
                tag = NBTTag::from_nbt(iter);
                break;
            }
            case ZLIB: {
                tag = readNbtBytesZlib(chunk_bytes);
                break;
            }
            case NOTHING: {
                auto iter = chunk_bytes.begin();
                tag = NBTTag::from_nbt(iter);
                break;
            }
            default: throw;
        }
        out[i] = tag;
        auto end = internal::current_time_millis();
        std::cout << "Read chunk " << i << " (took " << (end - start).count() << "ms)..." << std::endl;
    }
    std::cout << "Making region and returning..." << std::endl;
    return Region(out, timestamps);
}

void writeRegion(const std::string& path, const Region& region) {
    throw std::runtime_error("Unimplemented function writeRegion()!");
}

}

#endif