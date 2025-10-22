# NBTModify

NBTModify is an old program of mine to read and write files in Minecraft's NBT (Named Binary Tag) format.

## Functionality

Currently, reading and writing from raw NBT files (compressed with GZip or Zlib, or uncompressed) and Anvil (region) files is supported.

Uploading of regional NBT tags to MCLevel `Level`s is to be implemented.

## Installation

NBTModify is header-only. Just make sure that you have a working Zlib and Boost installation somewhere that NBTModify can find.

## Usage

```C++
#include <nbtmodify/core.hpp>

#include <iostream>

int main() {
    // Read NBT data
    nbt::NBTTag root = nbt::read_nbt_gzip("path/to/NBT/file");
    // Pretty-print to cout
    std::cout << root.to_string() << std::endl;
    // Access member of compound tag
    std::cout << root["foo"].get<nbt::int_t>() << std::endl;

    // To be continued...

    return 0;
}
```

## Credits

Boost: <https://boost.org> (also download link if you don't already have it)
