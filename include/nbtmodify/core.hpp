#ifndef CORE_HPP
#define CORE_HPP

#include <cassert>
#include <string>
#include <vector>
#include <istream>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <variant>
#include <bitset>
#include <optional>

#include <zlib.h>
#include "gzip/decompress.hpp"

#define ZLIB_CHUNK 131072UL

namespace nbt {

typedef unsigned char byte_t;
typedef __int16_t short_t;
typedef __uint16_t ushort_t;
typedef __int32_t int_t;
typedef __uint32_t uint_t;
typedef __int64_t long_t;
typedef __uint64_t ulong_t;

namespace internal {

// See https://stackoverflow.com/questions/2100331/macro-definition-to-determine-big-endian-or-little-endian-machine.
#if CHAR_BIT != 8
#error "unsupported char size"
#endif

enum {
    O32_LITTLE_ENDIAN = 0x03020100ul,
    O32_BIG_ENDIAN = 0x00010203ul,
    O32_PDP_ENDIAN = 0x01000302ul,      /* DEC PDP-11 (aka ENDIAN_LITTLE_WORD) */
    O32_HONEYWELL_ENDIAN = 0x02030001ul /* Honeywell 316 (aka ENDIAN_BIG_WORD) */
};

static const union { unsigned char bytes[4]; uint32_t value; } o32_host_order =
    { { 0, 1, 2, 3 } };

#define O32_HOST_ORDER (o32_host_order.value)

static auto current_time_millis() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());}

template <typename T> static void print_bytes(std::vector<T> data) {
    std::cout << "[";
    for (int i = 0; i < data.size(); ++i) std::cout << data[i];
    std::cout << "]" << std::endl;
}

}

enum Tag {
    TAG_END = '\x00',
    TAG_BYTE = '\x01',
    TAG_SHORT = '\x02',
    TAG_INT = '\x03',
    TAG_LONG = '\x04',
    TAG_FLOAT = '\x05',
    TAG_DOUBLE = '\x06',
    TAG_BYTEARRAY = '\x07',
    TAG_STRING = '\x08',
    TAG_ARRAY = '\t',
    TAG_COMPOUND = '\n',
    TAG_INTARRAY = '\v',
    TAG_LONGARRAY = '\f',
};

static std::string get_tag_type(Tag type) {
    switch (type) {
        case TAG_BYTE: return "Byte";
        case TAG_SHORT: return "Short";
        case TAG_INT: return "Int";
        case TAG_LONG: return "Long";
        case TAG_FLOAT: return "Float";
        case TAG_DOUBLE: return "Double";
        case TAG_STRING: return "String";
        case TAG_BYTEARRAY: return "ByteArray";
        case TAG_INTARRAY: return "IntArray";
        case TAG_LONGARRAY: return "LongArray";
        case TAG_ARRAY: return "Array";
        case TAG_COMPOUND: return "Compound";
        default: return "N/A (" + std::to_string(type) + ")";
    }
}

namespace internal {

union Number {
    char b;
    short_t s;
    int_t i;
    long_t l;
    float f;
    double d;
};

//janky but functional
static Number unpack(const byte_t bytes[], Tag code) {
    if (code == TAG_BYTE) {
        return {.b=(char)bytes[0]};
    } else if (code == TAG_SHORT) {
        if (O32_HOST_ORDER == O32_BIG_ENDIAN)
            return {.s=*((short_t*)bytes)};
        return {.s=(short_t)((short_t)bytes[0] << 8 | bytes[1])};
    } else if (code == TAG_INT) {
        if (O32_HOST_ORDER == O32_BIG_ENDIAN)
            return {.i=*((int_t*)bytes)};
        return {.i=(int_t)((int_t)bytes[0] << 24 | (int_t)bytes[1] << 16 | (int_t)bytes[2] << 8 | bytes[3])};
    } else if (code == TAG_LONG) {
        if (O32_HOST_ORDER == O32_BIG_ENDIAN)
            return {.l=*((long_t*)bytes)};
        return {.l=(long_t)((long_t)bytes[0] << 56 | (long_t)bytes[1] << 48 | (long_t)bytes[2] << 40 | (long_t)bytes[3] << 32 | (long_t)bytes[4] << 24 | (long_t)bytes[5] << 16 | (long_t)bytes[6] << 8 | (long_t)bytes[7])};
    } else if (code == TAG_FLOAT) {
        float out;
        int_t in = bytes[0] << 24 | bytes[1] << 16 | bytes[2] << 8 | bytes[3];
        std::memcpy(&out, &in, sizeof(in));
        return {.f=out};
    } else if (code == TAG_DOUBLE) {
        double out;
        long_t in = (long_t)(bytes[0] << 24 | bytes[1] << 16 | bytes[2] << 8 | bytes[3]) * 4294967296LL | bytes[4] << 24 | bytes[5] << 16 | bytes[6] << 8 | bytes[7];
        std::memcpy(&out, &in, sizeof(in));
        return {.d=out};
    }
    throw;
}

static constexpr ulong_t get_mask(int byte_level) {
    return 255UL * (ulong_t)(pow(2, byte_level * 8));
}

static byte_t get_highest_byte(ulong_t byte_data) {
    for (int i = 7; i >= 0; --i) if ((byte_data >> (i * 8)) != 0) return byte_data >> (i * 8);
    return 0;
}

}

struct NBTTag {
    Tag type;
    std::string name;
    std::variant<std::vector<NBTTag>, char, short_t, int_t, long_t, float, double, std::string> value;

    NBTTag() :  name(), value() {}
    NBTTag(Tag type, std::string name, 
             std::variant<std::vector<NBTTag>, char,
                          short_t, int_t, long_t, float, double, std::string> value);
    ~NBTTag() {}
    static NBTTag from_nbt(std::vector<byte_t>::iterator& bytes, bool suppress_name = false, std::optional<byte_t> type_override = {});
    std::vector<byte_t> to_nbt();
    std::string to_string(int tab_level = 0) const;
    template <size_t Size> std::optional<NBTTag> operator[](const char (&name)[Size]) const;
    std::optional<NBTTag> operator[](std::size_t index) const;
    operator char() {if (this->type == TAG_BYTE) return std::get<char>(this->value); std::cerr << "Tried to extract byte from non-byte tag " << this->name << std::endl; throw;}
    operator short_t() {if (this->type == TAG_SHORT) return std::get<short_t>(this->value); std::cerr << "Tried to extract short from non-short tag " << this->name << std::endl; throw;}
    operator int_t() {if (this->type == TAG_INT) return std::get<int_t>(this->value); std::cerr << "Tried to extract int from non-int tag" << this->name << std::endl; throw;}
    operator long_t() {if (this->type == TAG_LONG) return std::get<long_t>(this->value); std::cerr << "Tried to extract long from non-long tag " << this->name << std::endl; throw;}
    operator float() {if (this->type == TAG_FLOAT) return std::get<float>(this->value); std::cerr << "Tried to extract float from non-float tag " << this->name << std::endl; throw;}
    operator double() {if (this->type == TAG_DOUBLE) return std::get<double>(this->value); std::cerr << "Tried to extract double from non-double tag " << this->name << std::endl; throw;}
    operator std::string() {if (this->type == TAG_STRING) return std::get<std::string>(this->value); std::cerr << "Tried to extract string from non-string tag " << this->name << std::endl; throw;}
    operator std::vector<NBTTag>() {if (this->type == TAG_BYTEARRAY or this->type == TAG_INTARRAY or this->type == TAG_LONGARRAY or this->type == TAG_ARRAY or this->type == TAG_COMPOUND) return std::get<std::vector<NBTTag>>(this->value); std::cerr << "Tried to extract container from non-container tag " << this->name << std::endl; throw;}
};

NBTTag::NBTTag(Tag type, std::string name, std::variant<std::vector<NBTTag>, char, short_t, int_t, long_t, float, double, std::string> value) : name(name), value(value), type(type) {}

NBTTag NBTTag::from_nbt(std::vector<byte_t>::iterator& bytes, bool suppress_name, std::optional<byte_t> type_override) {
    std::string name;
    short_t namelen;
    std::variant<std::vector<NBTTag>, char, short_t, int_t, long_t, float, double, std::string> value;
    byte_t type;
    if (type_override) {
        type = type_override.value();
    } else {
        type = *bytes++;
    }
    if (!suppress_name) {
        namelen = *bytes++ * 256 + *bytes++;
        auto name_begin = bytes;
        auto name_end = name_begin + namelen;
        name = std::string(name_begin, name_end);
        bytes += namelen;
    } else {
        name = "";
        namelen = -2;
    }
    switch (type) {
        case TAG_BYTE: {
            value = (char)*bytes++;
            break;
        }
        case TAG_SHORT: {
            value = internal::unpack(&(*bytes), TAG_SHORT).s;
            bytes += 2;
            break;
        }
        case TAG_INT: {
            value = internal::unpack(&(*bytes), TAG_INT).i;
            bytes += 4;
            break;
        }
        case TAG_LONG: {
            value = internal::unpack(&(*bytes), TAG_LONG).l;
            bytes += 8;
            break;
        }
        case TAG_FLOAT: {
            value = internal::unpack(&(*bytes), TAG_FLOAT).f;
            bytes += 4;
            break;
        }
        case TAG_DOUBLE: {
            value = internal::unpack(&(*bytes), TAG_DOUBLE).d;
            bytes += 8;
            break;
        }
        case TAG_STRING: {
            short_t valuelen = (*bytes++ << 8) + *bytes++;
            auto value_begin = bytes;
            auto value_end = value_begin + valuelen;
            value = std::string(value_begin, value_end);
            bytes += valuelen;
            break;
        }
        case TAG_BYTEARRAY: {
            int length = (*bytes++ << 24) + (*bytes++ << 16) + (*bytes++ << 8) + (*bytes++);
            //std::cout << "namelen: " << namelen << " name: " << name << std::endl;
            std::vector<NBTTag> out_values(length);
            for (int i = 0; i < length; ++i) out_values[i] = NBTTag(TAG_BYTE, "", (char)(*bytes++)); //explicit constructor is quicker
            value = out_values;
            break;
        }
        case TAG_INTARRAY: {
            int length = (*bytes++ << 24) + (*bytes++ << 16) + (*bytes++ << 8) + (*bytes++);
            std::vector<NBTTag> out_values(length);
            for (int i = 0; i < length; ++i) out_values[i] = NBTTag::from_nbt(bytes, true, {TAG_INT});
            value = out_values;
            break;
        }
        case TAG_LONGARRAY: {
            int length = (*bytes++ << 24) + (*bytes++ << 16) + (*bytes++ << 8) + (*bytes++);
            std::vector<NBTTag> out_values(length);
            for (int i = 0; i < length; ++i) out_values[i] = NBTTag::from_nbt(bytes, true, {TAG_LONG});;
            value = out_values;
            break;
        }
        case TAG_ARRAY: {
            byte_t type = *bytes++;
            int length = (*bytes++ << 24) + (*bytes++ << 16) + (*bytes++ << 8) + (*bytes++);
            std::vector<NBTTag> out_values(length);
            if (length < 0) throw;
            if (length == 0) out_values = {};
            if (length > 0) for (int i = 0; i < length; ++i) out_values[i] = NBTTag::from_nbt(bytes, true, {type});
            value = out_values;
            break;
        }
        case TAG_COMPOUND: {
            std::vector<NBTTag> out_values;
            byte_t nextType = *bytes++;
            while (nextType != TAG_END) {
                out_values.push_back(NBTTag::from_nbt(bytes, false, {nextType}));
                nextType = *bytes++;
            }
            value = out_values;
            break;
        }
        default: {
            std::cerr << "found illegal type " << type << std::endl;
            throw;
        }
    }
    return NBTTag((Tag)type, name, value);
}

std::vector<byte_t> NBTTag::to_nbt() {
    std::vector<byte_t> nbt;
    nbt.push_back(type);
    short_t name_length = (short_t)(name.length());
    nbt.insert(nbt.end(), {(byte_t)((name_length & internal::get_mask(1)) >> 8), (byte_t)(name_length & internal::get_mask(0))});
    nbt.insert(nbt.end(), name.begin(), name.end());
    switch (type) {
        case TAG_BYTE: {
            nbt.push_back((byte_t)(std::get<char>(value)));
            break;
        }
        case TAG_SHORT: {
            nbt.insert(nbt.end(), {internal::get_highest_byte(std::get<short_t>(value) & internal::get_mask(1)), internal::get_highest_byte(std::get<short_t>(value) & internal::get_mask(0))});
            break;
        }
        case TAG_INT: {
            nbt.insert(nbt.end(), {internal::get_highest_byte(std::get<int_t>(value) & internal::get_mask(3)), internal::get_highest_byte(std::get<int_t>(value) & internal::get_mask(2)), internal::get_highest_byte(std::get<int_t>(value) & internal::get_mask(1)), internal::get_highest_byte(std::get<int_t>(value) & internal::get_mask(0))});
            break;
        }
        case TAG_LONG: {
            nbt.insert(nbt.end(), {internal::get_highest_byte(std::get<long_t>(value) & internal::get_mask(7)), internal::get_highest_byte(std::get<long_t>(value) & internal::get_mask(6)), internal::get_highest_byte(std::get<long_t>(value) & internal::get_mask(5)), internal::get_highest_byte(std::get<long_t>(value) & internal::get_mask(4)), internal::get_highest_byte(std::get<long_t>(value) & internal::get_mask(3)), internal::get_highest_byte(std::get<long_t>(value) & internal::get_mask(2)), internal::get_highest_byte(std::get<long_t>(value) & internal::get_mask(1)), internal::get_highest_byte(std::get<long_t>(value) & internal::get_mask(0))});
            break;
        }
        case TAG_FLOAT: {
            int_t val2;
            float real_value = std::get<float>(value);
            std::memcpy(&val2, &real_value, sizeof(real_value));
            nbt.insert(nbt.end(), {internal::get_highest_byte(val2 & internal::get_mask(3)), internal::get_highest_byte(val2 & internal::get_mask(2)), internal::get_highest_byte(val2 & internal::get_mask(1)), internal::get_highest_byte(val2 & internal::get_mask(0))});
            break;
        }
        case TAG_DOUBLE: {
            long_t val2;
            double real_value = std::get<double>(value);
            std::memcpy(&val2, &real_value, sizeof(real_value));
            nbt.insert(nbt.end(), {internal::get_highest_byte(val2 & internal::get_mask(7)), internal::get_highest_byte(val2 & internal::get_mask(6)), internal::get_highest_byte(val2 & internal::get_mask(5)), internal::get_highest_byte(val2 & internal::get_mask(4)), internal::get_highest_byte(val2 & internal::get_mask(3)), internal::get_highest_byte(val2 & internal::get_mask(2)), internal::get_highest_byte(val2 & internal::get_mask(1)), internal::get_highest_byte(val2 & internal::get_mask(0))});
            break;
        }
        case TAG_STRING: {
            short_t value_length = (short_t)(std::get<std::string>(value).length());
            std::string real_value = std::get<std::string>(value);
            nbt.insert(nbt.end(), {(byte_t)((value_length & 0xff00) >> 8), (byte_t)(value_length & 0xff)});
            nbt.insert(nbt.end(), real_value.begin(), real_value.end());
            break;
        }
        case TAG_BYTEARRAY: {
            std::vector<NBTTag> real_value = std::get<std::vector<NBTTag>>(value);
            int size = real_value.size();
            nbt.insert(nbt.end(), {internal::get_highest_byte(size & internal::get_mask(3)), internal::get_highest_byte(size & internal::get_mask(2)), internal::get_highest_byte(size & internal::get_mask(1)), internal::get_highest_byte(size & internal::get_mask(0))});
            for (int i = 0; i < real_value.size(); ++i) {
                if (real_value[i].type != TAG_BYTE) throw;
                std::vector<byte_t> inner_nbt = real_value[i].to_nbt();
                nbt.insert(nbt.end(), inner_nbt.begin() + 3 + real_value[i].name.size(), inner_nbt.end());
            }
            break;
        }
        case TAG_INTARRAY: {
            std::vector<NBTTag> real_value = std::get<std::vector<NBTTag>>(value);
            int size = real_value.size();
            nbt.insert(nbt.end(), {internal::get_highest_byte(size & internal::get_mask(3)), internal::get_highest_byte(size & internal::get_mask(2)), internal::get_highest_byte(size & internal::get_mask(1)), internal::get_highest_byte(size & internal::get_mask(0))});
            for (int i = 0; i < real_value.size(); ++i) {
                if (real_value[i].type != TAG_INT) throw;
                std::vector<byte_t> inner_nbt = real_value[i].to_nbt();
                nbt.insert(nbt.end(), inner_nbt.begin() + 3 + real_value[i].name.size(), inner_nbt.end());
            }
            break;
        }
        case TAG_LONGARRAY: {
            std::vector<NBTTag> real_value = std::get<std::vector<NBTTag>>(value);
            int size = real_value.size();
            nbt.insert(nbt.end(), {internal::get_highest_byte(size & internal::get_mask(3)), internal::get_highest_byte(size & internal::get_mask(2)), internal::get_highest_byte(size & internal::get_mask(1)), internal::get_highest_byte(size & internal::get_mask(0))});
            for (int i = 0; i < real_value.size(); ++i) {
                if (real_value[i].type != TAG_LONG) throw;
                std::vector<byte_t> inner_nbt = real_value[i].to_nbt();
                nbt.insert(nbt.end(), inner_nbt.begin() + 3 + real_value[i].name.size(), inner_nbt.end());
            }
            break;
        }
        case TAG_ARRAY: {
            std::vector<NBTTag> real_value = std::get<std::vector<NBTTag>>(value);
            byte_t tag_byte;
            if (real_value.size() > 0) {
                tag_byte = real_value[0].type;
            } else {
                tag_byte = TAG_END;
            }
            nbt.push_back(tag_byte);
            int size = real_value.size();
            nbt.insert(nbt.end(), {internal::get_highest_byte(size & internal::get_mask(3)), internal::get_highest_byte(size & internal::get_mask(2)), internal::get_highest_byte(size & internal::get_mask(1)), internal::get_highest_byte(size & internal::get_mask(0))});
            for (int i = 0; i < real_value.size(); ++i) {
                if (real_value[i].type != tag_byte) throw;
                std::vector<byte_t> inner_nbt = real_value[i].to_nbt();
                nbt.insert(nbt.end(), inner_nbt.begin() + 3 + real_value[i].name.size(), inner_nbt.end());
            }
            break;
        }
        case TAG_COMPOUND: {
            std::vector<NBTTag> real_value = std::get<std::vector<NBTTag>>(value);
            for (int i = 0; i < real_value.size(); ++i) {
                std::vector<byte_t> inner_nbt = real_value[i].to_nbt();
                nbt.insert(nbt.end(), inner_nbt.begin(), inner_nbt.end());
            }
            nbt.push_back(0);
            break;
        }
        default: throw;
    }
    return nbt;
}

std::string NBTTag::to_string(int tab_level) const {
    std::string out;
    for (int i = 0; i < tab_level; ++i) out += "\t";
    switch (this->type) {
        case TAG_BYTE: out += std::to_string(std::get<char>(this->value)); break;
        case TAG_SHORT: out += std::to_string(std::get<short>(this->value)); break;
        case TAG_INT: out += std::to_string(std::get<int_t>(this->value)); break;
        case TAG_LONG: out += std::to_string(std::get<long_t>(this->value)); break;
        case TAG_FLOAT: out += std::to_string(std::get<float>(this->value)); break;
        case TAG_DOUBLE: out += std::to_string(std::get<double>(this->value)); break;
        case TAG_STRING: out += "\"" + std::get<std::string>(this->value) + "\""; break;
        case TAG_BYTEARRAY:
        case TAG_INTARRAY:
        case TAG_LONGARRAY:
        case TAG_ARRAY: {
            out += "[\n";
            for (int i = 0; i < std::get<std::vector<NBTTag>>(this->value).size(); ++i) {
                out += std::get<std::vector<NBTTag>>(this->value)[i].to_string(tab_level + 1) + ",\n";
            }
            out += "]";
            break;
        }
        case TAG_COMPOUND: {
            out += "{\n";
            for (int i = 0; i < std::get<std::vector<NBTTag>>(this->value).size(); ++i) {
                out += std::get<std::vector<NBTTag>>(this->value)[i].name + ": " +  std::get<std::vector<NBTTag>>(this->value)[i].to_string(tab_level + 1) + ",\n";
            }
            out += "}";
            break;
        }
        default: throw;
    }
    return out;
}

/// TODO: Why can I not just use a string?
template <size_t Size> std::optional<NBTTag> NBTTag::operator[](const char (&name)[Size]) const {
    switch (this->type) {
        case TAG_COMPOUND: {
            std::vector<NBTTag> val = std::get<std::vector<NBTTag>>(this->value);
            for (int i = 0; i < val.size(); ++i) if (val[i].name == name) return std::optional<NBTTag>(val[i]);
            return std::optional<NBTTag>();
        }
        default: {
            std::cerr << "Tried to get value by name " << name << " from tag " << this->name << ", but that tag is not a compound" << std::endl;
            throw;
        }
    }
}

std::optional<NBTTag> NBTTag::operator[](std::size_t index) const {
    switch (this->type) {
        case TAG_BYTEARRAY:
        case TAG_INTARRAY:
        case TAG_LONGARRAY:
        case TAG_ARRAY: {
            std::vector<NBTTag> val = std::get<std::vector<NBTTag>>(this->value);
            if (val.size() <= index) {
                std::cerr << "Tried to get value by index " << std::to_string(index) << " from tag " << this->name << " which does not exist";
                return std::optional<NBTTag>();
            }
            return std::optional<NBTTag>(val[index]);
        }
        default: {
            std::cerr << "Tried to get value by index " << std::to_string(index) << " from tag " << this->name << ", but that tag is not an array";
            throw;
        }
    }
}

NBTTag readNbt(std::string path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<byte_t> data(std::istreambuf_iterator<char>(input), {});
    auto iter = data.begin();
    return NBTTag::from_nbt(iter);
}

NBTTag readNbtGzip(std::string path, bool print = false) {
    std::ifstream input(path, std::ios::binary);
    std::vector<char> data(std::istreambuf_iterator<char>(input), {});
    std::string textdata = gzip::decompress(&data[0], data.size());
    std::vector<byte_t> realdata(textdata.begin(), textdata.end());
    if (print) internal::print_bytes(realdata);
    auto iter = realdata.begin();
    return NBTTag::from_nbt(iter);
}

NBTTag readNbtBytesZlib(std::vector<byte_t> bytes, bool print = false) {
    std::vector<byte_t> decompressed_bytes;
    decompressed_bytes.reserve(16384);
    z_stream zlibstream{.opaque=Z_NULL, .zalloc=Z_NULL, .zfree=Z_NULL, .avail_in=0, .next_in=Z_NULL};
    std::size_t idx = 0;
    int ret = inflateInit(&zlibstream);
    if (ret != Z_OK) {
        std::cerr << "Zlib decompression failed to init with code: " << ret << std::endl;
        throw;
    }
    do {
        if (bytes.size() <= idx) break;
        zlibstream.avail_in = std::min(ZLIB_CHUNK, bytes.size() - idx);
        if (zlibstream.avail_in == 0) break;
        zlibstream.next_in = (byte_t*)&bytes[idx];
        do {
            std::array<byte_t, ZLIB_CHUNK> zlib_next_chunk;
            zlibstream.avail_out = ZLIB_CHUNK;
            zlibstream.next_out = zlib_next_chunk.begin();
            ret = inflate(&zlibstream, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);
            switch (ret) {
                case Z_NEED_DICT: {ret = Z_DATA_ERROR;}
                case Z_DATA_ERROR:
                case Z_MEM_ERROR: {
                    (void)inflateEnd(&zlibstream);
                    std::cerr << "Zlib decompression failed with code: " << ret << std::endl;
                    throw;
                }
            }
            decompressed_bytes.insert(decompressed_bytes.end(), zlib_next_chunk.begin(), zlib_next_chunk.end()); //ungodly slow sometimes
        } while (zlibstream.avail_out == 0);
        idx += ZLIB_CHUNK;
    } while (ret != Z_STREAM_END);
    inflateEnd(&zlibstream);
    if (print) internal::print_bytes(decompressed_bytes);
    auto iter = decompressed_bytes.begin();
    return NBTTag::from_nbt(iter);
}

NBTTag readNbtZlib(std::string path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<byte_t> data(std::istreambuf_iterator<char>(input), {});
    return readNbtBytesZlib(data);
}

}

#endif