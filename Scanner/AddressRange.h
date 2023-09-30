#ifndef SCANNER_ADDRESSRANGE_H
#define SCANNER_ADDRESSRANGE_H

#include <windows.h>
#include <vector>
#include <cstdint>

class AddressRange
{

    std::uintptr_t base;
    std::size_t length;

public:

    AddressRange() = default;
    AddressRange(std::uintptr_t start, std::size_t size)
            :   base(start), length(size)
    {}

    bool contains(AddressRange other) const
    {
        bool starts_in_range = other.base >= base and other.base < end();
        bool ends_in_range = other.end() <= end(); // assumes that start is in range.
        return starts_in_range and ends_in_range;
    }

    bool contains(std::uintptr_t address) const
    {
        return address >= start() and address < end();
    }

    std::uintptr_t start() const { return base; };
    std::uintptr_t end() const { return base + length; };

    std::size_t size() const { return length;}

    std::uintptr_t get_address_offset(std::size_t offset) const { return base + offset; }
};


#endif //SCANNER_ADDRESSRANGE_H
