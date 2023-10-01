#include <iostream>
#include <functional>
#include <span>
#include "Scanner/Scanner.h"
#include "CommandLineUtility.h"

using namespace CommandLineUtility;

using ArgList = std::span<std::string_view>;
using Command = std::function<void(Scanner&, ArgList)>;
bool running = true;

std::string cur_where_type = "i";

using ValueType = std::variant<int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t, float, double>;

ValueType convert_value(std::string_view val, std::string_view type)
{
    static const std::unordered_map<std::string_view, std::function<ValueType(std::string_view)>> conv_map
    {
        {"c", lexical_cast<int8_t>},
        {"s", lexical_cast<int16_t>},
        {"l", lexical_cast<int64_t>},
        {"f", lexical_cast<float>},
        {"d", lexical_cast<double>},
        {"uc", lexical_cast<uint8_t>},
        {"us", lexical_cast<uint16_t>},
        {"u", lexical_cast<uint32_t>},
        {"ui", lexical_cast<uint32_t>},
        {"ul", lexical_cast<uint64_t>},
    };

    auto conversion_function_it = conv_map.find(type);

    if (conversion_function_it != conv_map.end())
    {
        auto& conversion_function = conversion_function_it->second;
        return conversion_function(val);
    }
    else
    {
        return lexical_cast<int32_t>(val); // default to int if type not in map.
    }
}

ValueType convert_type(std::string_view type)
{
    // Since we only care about type, return a convert_value call on a dummy value.
    return convert_value("0", type);
}

template <typename T>
void print_val(T val)
{
    std::cout << val;
    // If the value is an int, additionally print out a hex representation.
    if constexpr(std::is_integral_v<T>)
    {
        std::cout << "\t( ";
        print_hex(val);
        std::cout << " )";
    }
}

void print_addresses(std::span<const std::uintptr_t> addresses)
{
    for (auto address : addresses)
    {
        print_hex(address);
        std::cout << '\n';
    }
    std::cout << "Addresses: " << addresses.size() << '\n';
}

void handle_where_became(Scanner& scanner, ArgList args)
{
    if (args.empty())
    {
        return;
    }

    auto val_str = args.front();
    ValueType val = convert_value(val_str, cur_where_type);

    std::visit([&scanner](auto&& val)
    {
       using T = std::decay_t<decltype(val)>;
       std::span<const std::uintptr_t> addresses = scanner.where_became(val);

       for (std::uintptr_t address : addresses)
       {
           print_hex(address);
           std::cout << " => ";
           print_val(*scanner.read_mem<T>(address));
           std::cout << '\n';
       }

       std::cout << "Addresses: " << addresses.size() << '\n';
    }, val);
}

void handle_where(Scanner& scanner, ArgList args)
{
    if (args.empty())
    {
        return;
    }

    // Starting 'where' command chain does a full scan, so print something out to acknowledge command before doing so.
    std::cout << "Scanning...\n";

    // Handle where string search
    if (args[0][0] == '\'')
    {
        const char* end = args.back().end();
        std::string_view whole_str { args[0].data() + 1, end };

        auto addresses = scanner.where_val(whole_str);
        print_addresses(addresses);
    }
    else
    {
        // Use type if provided, default to int (4 byte int).
        cur_where_type = args.size() > 1 ? args[1] : "i";
        std::string_view val_str = args.front();

        ValueType val = convert_value(val_str, cur_where_type);

        std::visit([&scanner](auto&& val)
        {
            std::span<const std::uintptr_t> addresses = scanner.where_val(val);
            print_addresses(addresses);
        }, val);
    }

    std::cout << "Finished.\n";
}

void handle_possible_pointer(Scanner& scanner, std::uintptr_t possible_pointer)
{
    constexpr int num_elements = 8;
    auto try_to_read = scanner.read_array<char>(possible_pointer, num_elements);

    if (try_to_read)
    {
        // We were able to deref possible_pointer, print the read memory.
        std::cout << " -> *(";
        // find first null char. it -> 1st null char or end of array;
        auto it = std::find(try_to_read.get(), try_to_read.get() + num_elements, '\0');

        auto is_printable = [](char byte){ return std::isprint(byte); };
        if (std::all_of(try_to_read.get(), it, is_printable))
        {
            // interpret as string, print first few chars of that string.
            std::string_view str { try_to_read.get(), static_cast<std::size_t>(std::distance(try_to_read.get(), it)) };
            std::cout << str;
        }
        std::cout << ")";
    }
    else
    {
        // do nothing, unreadable memory, cannot possibly be a pointer.
    }
}

void print_pointer_map(const std::unordered_map<std::uintptr_t, std::vector<std::uintptr_t>>& pointer_map, std::uintptr_t address, int level)
{
    auto align_level = [](int level)
    {
        for (int i = 0; i < level; ++i)
        {
            std::cout << "\t";
        }
    };

    const auto& pointers = pointer_map.at(address);

    for (auto pointer : pointers)
    {
        align_level(level);
        std::cout << "<- ";
        print_hex(pointer);
        std::cout << '\n';
        print_pointer_map(pointer_map, pointer, level + 1);
    }
}

void handle_pointer_scan(Scanner& scanner, ArgList args)
{
    if (args.empty())
    {
        return;
    }

    std::string_view str_address = args[0];
    std::string_view opt_type = args.size() == 1 ? "i" : args[1];
    int range = args.size() == 3 ? std::max(lexical_cast<int>(args[2]), 1) : 1;

    auto offset = lexical_cast<std::uintptr_t>(str_address);

    std::cout << "Scanning...\n";

    ValueType type = convert_type(opt_type);

    std::visit([&scanner, offset, range](auto&& type)
    {
        using T = std::decay_t<decltype(type)>;

        std::uintptr_t start = offset - (range - 1) * sizeof(T);
        std::uintptr_t end = offset + sizeof(T);

        for (std::uintptr_t i = start; i < end; i += sizeof(T))
        {
            print_hex(i);
            std::cout << "\n";
            auto pointer_map = scanner.scan_pointers_to(i);
            print_pointer_map(pointer_map, i, 1);
        }
    }, type);

    std::cout << "Finished.\n";
}

void handle_scan(Scanner& scanner, ArgList args)
{
    if (args.empty())
    {
        return;
    }

    std::string_view str_address = args[0];
    std::string_view opt_type = args.size() == 1 ? "i" : args[1];
    int num_elements = args.size() == 3 ? lexical_cast<int>(args[2]) : 1;
    auto offset = lexical_cast<std::uintptr_t>(str_address);

    if (opt_type == "t")
    {
        std::cout << scanner.read_string(offset) << '\n';
        return;
    }

    if (num_elements == 0)
    {
        return;
    }

    ValueType type = convert_type(opt_type);

    std::visit([&scanner, offset, num_elements](auto&& type) mutable
    {
        using T = std::decay_t<decltype(type)>;

        if (num_elements < 0)
        {
            num_elements = -num_elements;
            offset -= (num_elements - 1) * sizeof(T);
        }

        auto vals = scanner.read_array<T>(offset, num_elements);
        if (!vals)
        {
            std::cout << "Read unsuccessful.\n";
            return;
        }

        for (int i = 0; i < num_elements; ++i)
        {
            auto val = vals[i];
            auto address = offset + i * sizeof val;
            print_hex(address);
            std::cout << " - ";
            print_val(val);

            if (scanner.is_possible_pointer(val))
            {
                handle_possible_pointer(scanner, val);
            }

            std::cout << "\n";
        }
    }, type);
}


void handle_where_changed(Scanner& scanner, ArgList args)
{
    ValueType type = convert_type(cur_where_type);
    std::visit([&scanner](auto&& type)
    {
        using T = std::decay_t<decltype(type)>;

        auto prev_val = scanner.get_where_chain_val<T>();
        std::span<const std::uintptr_t> addresses = scanner.where_changed<T>();
        for (const auto change : addresses)
        {
            print_hex(change);
            std::cout << " : " << prev_val << "\t->\t" << *scanner.read_mem<T>(change) << '\n';
        }
        std::cout << "Addresses changed: " << addresses.size() << '\n';
    }, type);
}

void print_help_message(Scanner& scanner, ArgList args)
{
    std::cout << "Types:\n";
    std::cout << "Integer types can be combined with a leading 'u' to find and print unsigned values.\n";
    std::cout << "c: 8 bit int\n";
    std::cout << "s: 16 bit int\n";
    std::cout << "i: 32 bit int (default)\n";
    std::cout << "l: 64 bit int\n";
    std::cout << "f: float\n";
    std::cout << "d: double\n";
    std::cout << "t: string (used only by the scan command)\n\n";

    std::cout << "Commands:\n";
    std::cout << "where [value] (type)\n";
    std::cout << "\tAlias: w\n";
    std::cout << "\tPrints a list of addresses where the value is located.\n";
    std::cout << "\tIf the value begins with an apostrophe ('), the value and all subsequent characters will be interpreted as a string.\n";
    std::cout << "\tIf the value is not a string, this command starts a chain and can be used with multiple 'became' commands or one 'changed' command.\n\n";

    std::cout << "became [value]\n";
    std::cout << "\tAlias: b\n";
    std::cout << "\tFilters the current addresses located by where, prints addresses where the value is now [value].\n\n";

    std::cout << "changed\n";
    std::cout << "\tAlias: c\n";
    std::cout << "\tFilters the current addresses located by where, prints addresses where the value is different from the initial value.\n";
    std::cout << "\tThis command is particularly useful for finding floating point numbers.\n";
    std::cout << "\tFinishes the 'where' chain.\n\n";

    std::cout << "scan [address] (type) (range = 1) \n";
    std::cout << "\tAlias: s\n";
    std::cout << "\tScans at the given address for value(s) of a given type.\n";
    std::cout << "\tRange can be a negative number to instead scan upwards from the given address.\n";
    std::cout << "\tIf scanning for an integer the size of a pointer,\n";
    std::cout << "\t\twill additionally indicate whether the value is potentially a pointer.\n";
    std::cout << "\t\tIf the pointer points to a printable string, will additionally print the first few characters of that string.\n\n";

    std::cout << "pointers [address] (type) (range = 0) \n";
    std::cout << "\tAlias: p\n";
    std::cout << "\tSearches for possible pointers to the given address, then recursively searches for pointers to those pointers.\n";
    std::cout << "\tA range can be given to additionally scan for pointers to addresses at offsets equal to the given type's byte size above the given address.\n";

    std::cout << "quit\n";
    std::cout << "\tAlias: q\n";
    std::cout << "\tExits the program.\n\n";

    std::cout << "help\n";
    std::cout << "\tAlias: h\n";
    std::cout << "\tDisplays this help message.\n\n";
}

void set_quit_program(Scanner& a, ArgList args)
{
    running = false;
}

void print_intro(Scanner& scanner)
{
    std::cout << "Found:\n";
    std::cout << scanner.get_process_name() << '\n';
    std::cout << "ID: " << scanner.get_process_id() << '\n';
    std::string bit_rep = scanner.is_64_bit() ? "64 bit" : "32 bit";
    std::cout << bit_rep << "\n\n";

    print_help_message(scanner, {});
}

std::unordered_map<std::string_view, Command> construct_command_map()
{
    return
            {
                    {"quit", set_quit_program},
                    {"q", set_quit_program},

                    {"where", handle_where },
                    {"w", handle_where },
                    {"became", handle_where_became },
                    {"b", handle_where_became },
                    {"changed", handle_where_changed },
                    {"c", handle_where_changed },

                    {"scan", handle_scan},
                    {"s", handle_scan},

                    {"pointers", handle_pointer_scan},
                    {"p", handle_pointer_scan},


                    {"help", print_help_message},
                    {"h", print_help_message},
            };
}

std::string prompt_window_name()
{
    std::cout << "Enter window name:\n";
    std::string response;
    std::getline(std::cin, response);
    return response;
}

int main()
{
    const std::string window_name = prompt_window_name();
    Scanner scanner { window_name };
    print_intro(scanner);

    const auto commands = construct_command_map();
    while (running)
    {
        std::string response;
        std::getline(std::cin, response);

        if (response.empty())
        {
            continue;
        }

        std::vector<std::string_view> args = tokenize_string(response, ' ');
        std::string_view command = args[0];

        auto command_it = commands.find(command);
        if (command_it != commands.end())
        {
            const Command& to_run = command_it->second;
            std::invoke(to_run, scanner, std::span(args).subspan(1));
        }
        else
        {
            std::cout << "Invalid command\n\n";
        }
    }

    return 0;
}
