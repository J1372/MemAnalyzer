#ifndef SCANNER_VALUE_H
#define SCANNER_VALUE_H

#include <variant>
#include <cstdint>

class Value
{
    // can use just a union for release builds to bypass std::get check.
    using ValueType = std::variant<int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t, float, double>;
    ValueType value;

public:

    template<typename T>
    Value& operator=(T&& other)
    {
        value = std::forward<T>(other); // forward unnecessary but good practice.
        return *this;
    };

    template<typename T>
    T get() const
    {
        return std::get<T>(value);
    };
};

#endif //SCANNER_VALUE_H
