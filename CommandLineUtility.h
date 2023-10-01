#ifndef SCANNER_UTILITY_H
#define SCANNER_UTILITY_H

#include <iostream>
#include <iomanip>
#include <functional>
#include <string_view>

namespace CommandLineUtility
{

    template <typename T>
    T lexical_cast(std::string_view str)
    {
        T store;
        std::stringstream ss;

        bool is_hex = str.length() > 2 and str[0] == '0' and std::tolower(str[1]) == 'x';
        if (is_hex)
        {
            ss << std::hex << str.substr(2);
        }
        else
        {
            ss << str;
        }

        ss >> store;
        return store;
    }

    template <>
    inline float lexical_cast<float>(std::string_view str)
    {
        char* end;
        auto val = std::strtof(str.data(), &end);

        if (end != str.end())
        {
            throw std::runtime_error("Could not convert string view.");
        }
        else
        {
            return val;
        }
    }

    template <>
    inline double lexical_cast<double>(std::string_view str)
    {
        char* end;
        auto val = std::strtod(str.data(), &end);

        if (end != str.end())
        {
            throw std::runtime_error("Could not convert string view.");
        }
        else
        {
            return val;
        }
    }

    template <typename T>
    void print_hex(T num) // print in hex if possible.
    {
        static_assert(std::is_integral_v<T>, "Num must be an integer.");

        std::cout << "0x" << std::hex; // manual 0x printing because showbase does not do so when num = 0.
        auto cast_to_unsigned = static_cast<std::make_unsigned_t<decltype(num)>>(num); // cast num to unsigned equivalent  (necessary to prevent sign propagation).
        auto cast_to_size_t = static_cast<std::size_t>(cast_to_unsigned); // cast to size_t (necessary for printing 'char' and 'unsigned char' in hex).
        std::cout << cast_to_size_t;
        std::cout << std::dec;
    }

    inline std::vector<std::string_view> tokenize_string(std::string_view string, char delimiter)
    {
        std::vector<std::string_view> tokens;

        const char* start = &string[0];
        const char* cur = &string[0];
        int length = 0;
        while (cur < &string[0] + string.length())
        {
            if (*cur == delimiter)
            {
                if (length != 0)
                {
                    tokens.emplace_back(std::string_view{ start, static_cast<std::size_t>(length) });
                }
                length = 0;
            }
            else if (length == 0)
            {
                start = cur;
                length = 1;
            }
            else
            {
                ++length;
            }

            ++cur;
        }

        if (length != 0)
        {
            tokens.emplace_back(std::string_view{ start, static_cast<std::size_t>(length) });
        }

        return tokens;
    }

}

#endif //SCANNER_UTILITY_H
