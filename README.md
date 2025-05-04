# NBTModify

NBTModify is an old program of mine to read and write files in Minecraft's NBT (Named Binary Tag) format.

## Functionality

Currently, only reading from raw NBT files (compressed with GZip or Zlib, or uncompressed) and Anvil (region) files is supported. Writing is not supported, but may be implemented in the near future. (After all, I can't call it "NBT*Modify*" if the NBT can't actually be modified...)

## Installation

NBTModify is header-only. Just make sure that you have a working Zlib installation somewhere that NBTModify can find. GZip is included in the `include` directory.

## Usage

```C++
#include <nbtmodify/core.hpp>

#include <iostream>

int main() {
    // Read NBT data
    nbt::NBTTag root = nbt::readNbtGzip("path/to/NBT/file");
    // Pretty-print to cout
    std::cout << root.to_string() << std::endl;
    // Access member of compound tag
    std::cout << (nbt::int_t)(root["foo"].get()) << std::endl;

    // To be continued...

    return 0;
}
```

## Credits

GZip in the headers is from <https://github.com/mapbox/gzip-hpp> (if I remember correctly; it's been a while).
