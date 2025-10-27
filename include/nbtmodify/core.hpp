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

#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filter/zlib.hpp>

/// TODO: convert all `std::runtime_error`s into a custom argument type (maybe `nbt::runtime_error`?)

namespace nbt {

struct NBTTag;

// If these types won't compile on your machine (for some reason),
// replace intX_t with int_fastX_t.
typedef unsigned char byte_t;
typedef int8_t short_t;
typedef uint16_t ushort_t;
typedef int32_t int_t;
typedef uint32_t uint_t;
typedef int64_t long_t;
typedef uint64_t ulong_t;

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

static constexpr union { unsigned char bytes[4]; uint32_t value; } o32_host_order =
    { { 0, 1, 2, 3 } };

/// TODO: make an `is_little_endian` function
#define O32_HOST_ORDER o32_host_order.value

bool is_little_endian() {
    return is_little_endian();
}

auto current_time_millis() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());}

template <typename T> void print_bytes(std::vector<T> data) {
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
Number unpack(const byte_t bytes[], Tag code) {
    if (code == TAG_BYTE) {
        return {.b=(char)bytes[0]};
    } else if (code == TAG_SHORT) {
        if (!is_little_endian())
            return {.s=*((short_t*)bytes)};
        return {.s=(short_t)((short_t)bytes[0] << 8 | bytes[1])};
    } else if (code == TAG_INT) {
        if (!is_little_endian())
            return {.i=*((int_t*)bytes)};
        return {.i=(int_t)((int_t)bytes[0] << 24 | (int_t)bytes[1] << 16 | (int_t)bytes[2] << 8 | bytes[3])};
    } else if (code == TAG_LONG) {
        if (!is_little_endian())
            return {.l=*((long_t*)bytes)};
        return {.l=(long_t)((long_t)bytes[0] << 56 | (long_t)bytes[1] << 48 | (long_t)bytes[2] << 40 | (long_t)bytes[3] << 32 | (long_t)bytes[4] << 24 | (long_t)bytes[5] << 16 | (long_t)bytes[6] << 8 | (long_t)bytes[7])};
    } else if (code == TAG_FLOAT) {
        float out;
        int_t in = (int_t)(bytes[0]) << 24 | (int_t)(bytes[1]) << 16 | (int_t)(bytes[2]) << 8 | bytes[3];
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

Number read(std::istream& bytes, Tag code) {
    Number ret;
    if (code == TAG_BYTE) {
        bytes.read(&ret.b, 1);
    } else if (code == TAG_SHORT) {
        bytes.read((char*)&ret.s, 2);
        if (is_little_endian())
            ret.s = __builtin_bswap16(ret.s);
    } else if (code == TAG_INT) {
        bytes.read((char*)&ret.i, 4);
        if (is_little_endian())
            ret.i = __builtin_bswap32(ret.i);
    } else if (code == TAG_LONG) {
        bytes.read((char*)&ret.l, 8);
        if (is_little_endian())
            ret.l = __builtin_bswap64(ret.l);
    } else if (code == TAG_FLOAT) {
        bytes.read((char*)&ret.f, 4);
        if (is_little_endian())
            // operate on ret.i instead of ret.f because both have the same number of bits
            // and __builtin_bswap doesn't work on floats
            ret.i = __builtin_bswap32(ret.i);
    } else if (code == TAG_DOUBLE) {
        bytes.read((char*)&ret.d, 8);
        if (is_little_endian())
            ret.l == __builtin_bswap64(ret.l);
    } else throw;
    return ret;
}
// write byte
void writeb(std::ostream& bytes, char num) {
    bytes.write(&num, 1);
}
// write short
void writes(std::ostream& bytes, short_t num) {
    if (is_little_endian())
        num = __builtin_bswap16(num);
    bytes.write((char*)&num, 2);
}
// write int
void writei(std::ostream& bytes, int_t num) {
    if (is_little_endian())
        num = __builtin_bswap32(num);
    bytes.write((char*)&num, 4);
}
// write long
void writel(std::ostream& bytes, long_t num) {
    if (is_little_endian())
        num = __builtin_bswap64(num);
    bytes.write((char*)&num, 8);
}
// write float
void writef(std::ostream& bytes, float num) {
    uint_t num_bytes;
    if (is_little_endian())
        num_bytes = __builtin_bswap32(std::bit_cast<uint_t>(num));
    else
        num_bytes = std::bit_cast<uint_t>(num);
    bytes.write((char*)&num_bytes, 4);
}
// write double
void writed(std::ostream& bytes, double num) {
    ulong_t num_bytes;
    if (is_little_endian())
        num_bytes = __builtin_bswap64(std::bit_cast<ulong_t>(num));
    else
        num_bytes = std::bit_cast<ulong_t>(num);
    bytes.write((char*)&num_bytes, 8);
}
// write string
void writestr(std::ostream& bytes, const std::string& string) {
    short_t name_length = string.size();
    writes(bytes, name_length);
    /// TODO: does this output any potential null termination characters (\0)?
    /// I don't think so but might be worthwhile to figure it out
    bytes << string;
}

std::string read_string(std::istream& bytes) {
    std::string ret;
    short_t len = internal::read(bytes, TAG_SHORT).s;
    for (short_t i = 0; i < len; ++i)
        ret.append("\0");
    bytes.read(ret.data(), len);
    return ret;
}

constexpr ulong_t get_mask(int byte_level) {
    return 255UL * (ulong_t)(pow(2, byte_level * 8));
}

byte_t get_highest_byte(ulong_t byte_data) {
    for (int i = 7; i >= 0; --i) if ((byte_data >> (i * 8)) != 0) return byte_data >> (i * 8);
    return 0;
}

template <typename T> constexpr bool is_nbt_type = false;
template <> constexpr bool is_nbt_type<byte_t> = true;
template <> constexpr bool is_nbt_type<short_t> = true;
template <> constexpr bool is_nbt_type<int_t> = true;
template <> constexpr bool is_nbt_type<long_t> = true;
template <> constexpr bool is_nbt_type<float> = true;
template <> constexpr bool is_nbt_type<double> = true;
template <> constexpr bool is_nbt_type<std::string> = true;
template <> constexpr bool is_nbt_type<std::vector<NBTTag>> = true;
template <> constexpr bool is_nbt_type<std::vector<byte_t>> = true;
template <> constexpr bool is_nbt_type<std::vector<int_t>> = true;
template <> constexpr bool is_nbt_type<std::vector<long_t>> = true;
template <> constexpr bool is_nbt_type<NBTTag> = true;

}

template <typename T> concept NbtType = internal::is_nbt_type<T>;

struct NBTTag {
    using DataType = std::variant<std::vector<NBTTag>,
        std::vector<byte_t>, std::vector<int_t>, std::vector<long_t>,
        char, short_t, int_t, long_t, float, double, std::string>;

    Tag type;
    /// @todo Switch this for a map in the DataType definition
    /// (that would speed up access dramatically and make some other QOL changes possible)
    std::string name;
    DataType value;

    NBTTag() :  name(), value() {}
    NBTTag(Tag type, std::string name, DataType value);
    ~NBTTag() {}
    static NBTTag from_nbt(std::vector<byte_t>::iterator& bytes, bool suppress_name = false, std::optional<byte_t> type_override = {});
    static NBTTag from_nbt(std::istream& bytes, bool suppress_name = false, std::optional<byte_t> type_override = {});
    std::vector<byte_t> to_nbt() const;
    void to_nbt(std::ostream& stream) const;
    /// @brief Pretty-print this tag.
    /// @param tab_level The number of `\t` characters to insert before every line. Used internally to tabulate lines. When a value is supplied externally, every line will be indented this many times on top of normal tabulation.
    /// @return A string representing this tag.
    std::string to_string(int tab_level = 0) const;
    /// @brief Compound tag element access. Will throw if `this` is not a compound tag.
    /// @param name The name of the element to access.
    /// @return An element with name `name`. Will create it (with value 0b) if no such element exists.
    /// @sa NBTTag::at(const std::string&)
    NBTTag& operator[](const std::string& name);
    /// @brief Compound tag element access. Will throw if `this` is not a compound tag.
    /// @param name The name of the element to access.
    /// @return An element with name `name`. Will throw if no such element exists.
    NBTTag& at(const std::string& name) const;
    /// @brief Array tag element access. Will throw if `this` is not an array tag.
    /// @param index The index of the element to access.
    /// @return An element with index `index`. Will throw if no such element exists.
    NBTTag& operator[](std::size_t index) const;
    /// @brief Gets the value of this tag.
    /// @tparam T The type to get the value of this tag as.
    /// @return The value of this tag.
    /// @note Implemented using template specialisations that will throw if the tag is not of the correct type.
    /// This means that, for example, `get<int_t>` should NOT be called if `this.type` is `TAG_BYTE`.
    /// In that case, call `get<byte_t>` and use a cast instead.
    /// This also means that `get<builtin_int_type>` should not be called. Only use NBT types.
    /// (This will cause a compilation error if violated.)
    template <NbtType T> T get() const;
    /// @brief Gets the size of this array tag.
    /// @return The size of this array tag.
    /// @exception Throws if `this.type` is not one of `TAG_BYTEARRAY`, `TAG_INTARRAY`, `TAG_LONGARRAY`, or `TAG_ARRAY`.
    std::size_t size() const;
    /// @brief Returns whether this compound tag contains the given key.
    /// @param key The key.
    /// @return Whether this tag contains the key.
    /// @exception Throws if `this.type` is not `TAG_COMPOUND`.
    bool contains(const std::string& key) const;
};

NBTTag::NBTTag(Tag type, std::string name, DataType value) : name(name), value(value), type(type) {}

NBTTag NBTTag::from_nbt(std::vector<byte_t>::iterator& bytes, bool suppress_name, std::optional<byte_t> type_override) {
    /// TODO: update this function (see other overload)
    std::string name;
    short_t namelen;
    DataType value;
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
            uint_t length = (*bytes++ << 24) + (*bytes++ << 16) + (*bytes++ << 8) + (*bytes++);
            value = std::vector<byte_t>(bytes, (bytes += length));
            break;
        }
        case TAG_INTARRAY: {
            uint_t length = (*bytes++ << 24) + (*bytes++ << 16) + (*bytes++ << 8) + (*bytes++);
            value = std::vector<int_t>(bytes, (bytes += length));
            break;
        }
        case TAG_LONGARRAY: {
            uint_t length = (*bytes++ << 24) + (*bytes++ << 16) + (*bytes++ << 8) + (*bytes++);
            value = std::vector<long_t>(bytes, (bytes += length));
            break;
        }
        case TAG_ARRAY: {
            byte_t type = *bytes++;
            uint_t length = (*bytes++ << 24) + (*bytes++ << 16) + (*bytes++ << 8) + (*bytes++);
            std::vector<NBTTag> out_values(length);
            if (length == 0) out_values = {};
            else for (int i = 0; i < length; ++i) out_values[i] = NBTTag::from_nbt(bytes, true, {type});
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
NBTTag NBTTag::from_nbt(std::istream& bytes, bool suppress_name, std::optional<byte_t> type_override) {
    NBTTag ret;

    if (type_override)
        ret.type = static_cast<Tag>(type_override.value());
    else
        ret.type = static_cast<Tag>(internal::read(bytes, TAG_BYTE).b);

    if (!suppress_name)
        ret.name = internal::read_string(bytes);

    switch (ret.type) {
        case TAG_BYTE: {
            ret.value = internal::read(bytes, TAG_BYTE).b;
            break;
        }
        case TAG_SHORT: {
            ret.value = internal::read(bytes, TAG_SHORT).s;
            break;
        }
        case TAG_INT: {
            ret.value = internal::read(bytes, TAG_INT).i;
            break;
        }
        case TAG_LONG: {
            ret.value = internal::read(bytes, TAG_LONG).l;
            break;
        }
        case TAG_FLOAT: {
            ret.value = internal::read(bytes, TAG_FLOAT).f;
            break;
        }
        case TAG_DOUBLE: {
            ret.value = internal::read(bytes, TAG_DOUBLE).d;
            break;
        }
        case TAG_STRING: {
            ret.value = internal::read_string(bytes);
            break;
        }
        case TAG_BYTEARRAY: {
            int_t length = internal::read(bytes, TAG_INT).i;
            std::vector<byte_t> out_values(length);
            bytes.read((char*)out_values.data(), length);
            break;
        }
        case TAG_INTARRAY: {
            int_t length = internal::read(bytes, TAG_INT).i;
            std::vector<int_t> out_values(length);
            bytes.read((char*)out_values.data(), length * sizeof(int_t));
            if (internal::is_little_endian())
                for (int_t& val : out_values)
                    val = __builtin_bswap32(val);
            break;
        }
        case TAG_LONGARRAY: {
            int_t length = internal::read(bytes, TAG_INT).i;
            std::vector<long_t> out_values(length);
            bytes.read((char*)out_values.data(), length * sizeof(long_t));
            if (internal::is_little_endian())
                for (long_t& val : out_values)
                    val = __builtin_bswap64(val);
            break;
        }
        case TAG_ARRAY: {
            byte_t type = internal::read(bytes, TAG_BYTE).b;
            uint_t length = internal::read(bytes, TAG_INT).i;
            std::vector<NBTTag> out_values(length);
            if (length == 0) out_values = {};
            if (length > 0) for (int i = 0; i < length; ++i) out_values[i] = NBTTag::from_nbt(bytes, true, {type});
            ret.value = out_values;
            break;
        }
        case TAG_COMPOUND: {
            std::vector<NBTTag> out_values;
            byte_t nextType = internal::read(bytes, TAG_BYTE).b;
            while (nextType != TAG_END) {
                out_values.push_back(NBTTag::from_nbt(bytes, false, {nextType}));
                nextType = internal::read(bytes, TAG_BYTE).b;
            }
            ret.value = out_values;
            break;
        }
        default: {
            throw std::runtime_error("Found illegal type " + std::to_string(ret.type));
        }
    }
    return ret;
}

std::vector<byte_t> NBTTag::to_nbt() const {
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
void NBTTag::to_nbt(std::ostream& stream) const {
    internal::writeb(stream, this->type);
    internal::writestr(stream, this->name);
    switch (this->type) {
        case TAG_BYTE: {
            internal::writeb(stream, std::get<char>(this->value));
            break;
        }
        case TAG_SHORT: {
            internal::writes(stream, std::get<short_t>(this->value));
            break;
        }
        case TAG_INT: {
            internal::writei(stream, std::get<int_t>(this->value));
            break;
        }
        case TAG_LONG: {
            internal::writel(stream, std::get<long_t>(this->value));
            break;
        }
        case TAG_FLOAT: {
            internal::writef(stream, std::get<float>(this->value));
            break;
        }
        case TAG_DOUBLE: {
            internal::writed(stream, std::get<double>(this->value));
            break;
        }
        case TAG_STRING: {
            internal::writestr(stream, std::get<std::string>(this->value));
            break;
        }
        case TAG_BYTEARRAY: {
            std::vector<byte_t> real_value = std::get<std::vector<byte_t>>(this->value);
            uint_t size = real_value.size();
            internal::writei(stream, size);
            for (byte_t byte : real_value)
                internal::writeb(stream, byte);
            break;
        }
        case TAG_INTARRAY: {
            std::vector<int_t> real_value = std::get<std::vector<int_t>>(this->value);
            uint_t size = real_value.size();
            internal::writei(stream, size);
            for (int_t byte : real_value)
                internal::writei(stream, byte);
            break;
        }
        case TAG_LONGARRAY: {
            std::vector<long_t> real_value = std::get<std::vector<long_t>>(this->value);
            uint_t size = real_value.size();
            internal::writei(stream, size);
            for (long_t byte : real_value)
                internal::writel(stream, byte);
            break;
        }
        case TAG_ARRAY: {
            std::vector<NBTTag> real_value = std::get<std::vector<NBTTag>>(value);
            byte_t tag_byte;
            uint_t size = real_value.size();
            if (size > 0)
                tag_byte = real_value[0].type;
            else
                tag_byte = TAG_END;
            internal::writeb(stream, tag_byte);
            internal::writei(stream, size);
            for (const NBTTag& tag : real_value) {
                if (tag.type != tag_byte) throw;
                tag.to_nbt(stream);
            }
            break;
        }
        case TAG_COMPOUND: {
            std::vector<NBTTag> real_value = std::get<std::vector<NBTTag>>(value);
            for (const NBTTag& tag : real_value)
                tag.to_nbt(stream);
            internal::writeb(stream, TAG_END);
            break;
        }
        default: throw;
    }
}

std::string NBTTag::to_string(int tab_level) const {
    std::string out;
    for (int i = 0; i < tab_level; ++i) out += "\t";
    switch (this->type) {
        case TAG_BYTE: out += std::to_string(std::get<char>(this->value)) + "b"; break;
        case TAG_SHORT: out += std::to_string(std::get<short>(this->value)) + "s"; break;
        case TAG_INT: out += std::to_string(std::get<int_t>(this->value)) + "i"; break;
        case TAG_LONG: out += std::to_string(std::get<long_t>(this->value)) + "l"; break;
        case TAG_FLOAT: out += std::to_string(std::get<float>(this->value)) + "f"; break;
        case TAG_DOUBLE: out += std::to_string(std::get<double>(this->value)) + "d"; break;
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

NBTTag& NBTTag::operator[](const std::string& name) {
    switch (this->type) {
    case TAG_COMPOUND: {
        std::vector<NBTTag> val = std::get<std::vector<NBTTag>>(this->value);
        for (int i = 0; i < val.size(); ++i) if (val[i].name == name) return val[i];
        std::get<std::vector<NBTTag>>(this->value).emplace_back(TAG_BYTE, name, 0);
    }
    default:
        throw std::runtime_error("Tried to get value by name " + name + " from tag " + this->name + ", but that tag is not a compound");
    }
}
NBTTag& NBTTag::at(const std::string& name) const {
    switch (this->type) {
    case TAG_COMPOUND: {
        std::vector<NBTTag> val = std::get<std::vector<NBTTag>>(this->value);
        for (int i = 0; i < val.size(); ++i) if (val[i].name == name) return val[i];
        throw std::runtime_error("Tried to get value by name" + name + " from tag " + this->name + ", but that value does not exist in the compound");
    }
    default:
        throw std::runtime_error("Tried to get value by name " + name + " from tag " + this->name + ", but that tag is not a compound");
    }
}
NBTTag& NBTTag::operator[](std::size_t index) const {
    switch (this->type) {
        case TAG_BYTEARRAY:
        case TAG_INTARRAY:
        case TAG_LONGARRAY:
        case TAG_ARRAY: {
            std::vector<NBTTag> val = std::get<std::vector<NBTTag>>(this->value);
            if (val.size() <= index) {
                throw std::runtime_error("Tried to get value by index " + std::to_string(index) + " from tag " + this->name + " which does not exist");
            }
            return val[index];
        }
        default: {
            throw std::runtime_error("Tried to get value by index " + std::to_string(index) + " from tag " + this->name + ", but that tag is not an array");
        }
    }
}

template <> byte_t NBTTag::get<byte_t>() const {
    if (this->type == TAG_BYTE) return std::get<byte_t>(this->value);
    throw std::runtime_error("Tried to extract byte from non-byte tag " + this->name);
}
template <> short_t NBTTag::get<short_t>() const {
    if (this->type == TAG_SHORT) return std::get<short_t>(this->value);
    throw std::runtime_error("Tried to extract short from non-short tag " + this->name);
}
template <> int_t NBTTag::get<int_t>() const {
    if (this->type == TAG_INT) return std::get<int_t>(this->value);
    throw std::runtime_error("Tried to extract int from non-int tag " + this->name);
}
template <> long_t NBTTag::get<long_t>() const {
    if (this->type == TAG_LONG) return std::get<long_t>(this->value);
    throw std::runtime_error("Tried to extract long from non-long tag " + this->name);
}
template <> float NBTTag::get<float>() const {
    if (this->type == TAG_FLOAT) return std::get<float>(this->value);
    throw std::runtime_error("Tried to extract float from non-float tag " + this->name);
}
template <> double NBTTag::get<double>() const {
    if (this->type == TAG_DOUBLE) return std::get<double>(this->value);
    throw std::runtime_error("Tried to extract double from non-double tag " + this->name);
}
template <> std::string NBTTag::get<std::string>() const {
    if (this->type == TAG_STRING) return std::get<std::string>(this->value);
    throw std::runtime_error("Tried to extract string from non-string tag " + this->name);
}
template <> std::vector<byte_t> NBTTag::get<std::vector<byte_t>>() const {
    if (this->type == TAG_BYTEARRAY) return std::get<std::vector<byte_t>>(this->value);
    throw std::runtime_error("Tried to extract byte array from non-byte-array tag " + this->name);
}
template <> std::vector<int_t> NBTTag::get<std::vector<int_t>>() const {
    if (this->type == TAG_INTARRAY) return std::get<std::vector<int_t>>(this->value);
    throw std::runtime_error("Tried to extract int array from non-int-array tag " + this->name);
}
template <> std::vector<long_t> NBTTag::get<std::vector<long_t>>() const {
    if (this->type == TAG_LONGARRAY) return std::get<std::vector<long_t>>(this->value);
    throw std::runtime_error("Tried to extract long array from non-long-array tag " + this->name);
}
template <> std::vector<NBTTag> NBTTag::get<std::vector<NBTTag>>() const {
    if (this->type == TAG_ARRAY) return std::get<std::vector<NBTTag>>(this->value);
    throw std::runtime_error("Tried to extract array from non-array tag " + this->name);
}

std::size_t NBTTag::size() const {}

bool NBTTag::contains(const std::string& key) const {
    if (this->type != TAG_COMPOUND)
        throw std::runtime_error("Tried to use contains() on non-compound tag " + this->name);
    return std::any_of(std::get<std::vector<NBTTag>>(this->value).begin(), std::get<std::vector<NBTTag>>(this->value).end(),
        [key](const NBTTag& tag) { return tag.name == key; });
}

NBTTag read_nbt_gzip(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    boost::iostreams::filtering_istreambuf buf;
    buf.push(boost::iostreams::gzip_decompressor());
    buf.push(input);
    std::istream gzstream(&buf);
    return NBTTag::from_nbt(gzstream);
}
NBTTag read_nbt_gzip(std::istream& stream) {
    boost::iostreams::filtering_istreambuf buf;
    buf.push(boost::iostreams::gzip_decompressor());
    buf.push(stream);
    std::istream gzstream(&buf);
    return NBTTag::from_nbt(gzstream);
}

NBTTag read_nbt_zlib(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    boost::iostreams::filtering_istreambuf buf;
    buf.push(boost::iostreams::zlib_decompressor());
    buf.push(input);
    std::istream gzstream(&buf);
    return NBTTag::from_nbt(gzstream);
}
NBTTag read_nbt_zlib(std::istream& stream) {
    boost::iostreams::filtering_istreambuf buf;
    buf.push(boost::iostreams::zlib_decompressor());
    buf.push(stream);
    std::istream gzstream(&buf);
    return NBTTag::from_nbt(gzstream);
}

void write_nbt_gzip(const std::string& path, const NBTTag& tag) {
    std::ofstream input(path, std::ios::binary);
    boost::iostreams::filtering_ostreambuf buf;
    buf.push(boost::iostreams::gzip_compressor());
    buf.push(input);
    std::ostream gzstream(&buf);
    tag.to_nbt(gzstream);
}
void write_nbt_gzip(std::ostream& stream, const NBTTag& tag) {
    boost::iostreams::filtering_ostreambuf buf;
    buf.push(boost::iostreams::gzip_compressor());
    buf.push(stream);
    std::ostream gzstream(&buf);
    tag.to_nbt(gzstream);
}

void write_nbt_zlib(const std::string& path, const NBTTag& tag) {
    std::ofstream input(path, std::ios::binary);
    boost::iostreams::filtering_ostreambuf buf;
    buf.push(boost::iostreams::zlib_compressor());
    buf.push(input);
    std::ostream zstream(&buf);
    tag.to_nbt(zstream);
}
void write_nbt_zlib(std::ostream& stream, const NBTTag& tag) {
    boost::iostreams::filtering_ostreambuf buf;
    buf.push(boost::iostreams::zlib_compressor());
    buf.push(stream);
    std::ostream zstream(&buf);
    tag.to_nbt(zstream);
}


}

#endif