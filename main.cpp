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

template <typename T>
void print_val(T val)
{
    std::cout << val;
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
    auto print_addresses_and_new_vals = []<typename T>(Scanner& scanner, T val)
    {
        std::span<const std::uintptr_t> addresses = scanner.where_became(val);

        for (std::uintptr_t address : addresses)
        {
            print_hex(address);
            std::cout << " => ";
            print_val(*scanner.read_mem<T>(address));
            std::cout << '\n';
        }

        std::cout << "Addresses: " << addresses.size() << '\n';
    };

    if (args.empty())
    {
        return;
    }

    auto val = args.front();

    if (cur_where_type == "s")
    {
        print_addresses_and_new_vals(scanner, lexical_cast<int16_t>(val));
    }
    else if (cur_where_type == "l")
    {
        print_addresses_and_new_vals(scanner, lexical_cast<int64_t>(val));
    }
    else if (cur_where_type == "f")
    {
        print_addresses_and_new_vals(scanner, string_view_convert<std::strtof>(val));
    }
    else if (cur_where_type == "d")
    {
        print_addresses_and_new_vals(scanner, string_view_convert<std::strtod>(val));
    }
    else if (cur_where_type == "c")
    {
        print_addresses_and_new_vals(scanner, lexical_cast<int8_t>(val));
    }
    else if (cur_where_type == "u" or cur_where_type == "ui")
    {
        print_addresses_and_new_vals(scanner, lexical_cast<uint32_t>(val));
    }
    else if (cur_where_type == "us")
    {
        print_addresses_and_new_vals(scanner, lexical_cast<uint16_t>(val));
    }
    else if (cur_where_type == "ul")
    {
        print_addresses_and_new_vals(scanner, lexical_cast<uint64_t>(val));
    }
    else if (cur_where_type == "uc")
    {
        print_addresses_and_new_vals(scanner, lexical_cast<uint8_t>(val));
    }
    else
    {
        // default to int 4 bytes
        print_addresses_and_new_vals(scanner, lexical_cast<int32_t>(val));
    }
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
        std::string_view val = args.front();

        std::span<const std::uintptr_t> addresses;
        if (cur_where_type == "s")
        {
            addresses = scanner.where_val(lexical_cast<int16_t>(val));
        }
        else if (cur_where_type == "l")
        {
            addresses = scanner.where_val(lexical_cast<int64_t>(val));
        }
        else if (cur_where_type == "f")
        {
            addresses = scanner.where_val(string_view_convert<std::strtof>(val));
        }
        else if (cur_where_type == "d")
        {
            addresses = scanner.where_val(string_view_convert<std::strtod>(val));
        }
        else if (cur_where_type == "c")
        {
            addresses = scanner.where_val(lexical_cast<int8_t>(val));
        }
        else if (cur_where_type == "u" or cur_where_type == "ui")
        {
            addresses = scanner.where_val(lexical_cast<uint32_t>(val));
        }
        else if (cur_where_type == "us")
        {
            addresses = scanner.where_val(lexical_cast<uint16_t>(val));
        }
        else if (cur_where_type == "ul")
        {
            addresses = scanner.where_val(lexical_cast<uint64_t>(val));
        }
        else if (cur_where_type == "uc")
        {
            addresses = scanner.where_val(lexical_cast<uint8_t>(val));
        }
        else
        {
            addresses = scanner.where_val(lexical_cast<int32_t>(val));
        }

        print_addresses(addresses);
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

    auto print_pointers = []<typename T>(Scanner& scanner, std::uintptr_t offset, int range)
    {
        std::uintptr_t start = offset - (range - 1) * sizeof(T);
        std::uintptr_t end = offset + sizeof(T);

        for (std::uintptr_t i = start; i < end; i += sizeof(T))
        {
            print_hex(i);
            std::cout << "\n";
            auto pointer_map = scanner.scan_pointers_to(i);
            print_pointer_map(pointer_map, i, 1);
        }
    };

    std::cout << "Scanning...\n";

    if (opt_type == "s")
    {
        print_pointers.operator()<int16_t>(scanner, offset, range);
    }
    else if (opt_type == "l")
    {
        print_pointers.operator()<int64_t>(scanner, offset, range);
    }
    else if (opt_type == "f")
    {
        print_pointers.operator()<float>(scanner, offset, range);
    }
    else if (opt_type == "d")
    {
        print_pointers.operator()<double>(scanner, offset, range);
    }
    else if (opt_type == "c")
    {
        print_pointers.operator()<int8_t>(scanner, offset, range);
    }
    else if (opt_type == "u" or opt_type == "ui")
    {
        print_pointers.operator()<uint32_t>(scanner, offset, range);
    }
    else if (opt_type == "us")
    {
        print_pointers.operator()<uint16_t>(scanner, offset, range);
    }
    else if (opt_type == "ul")
    {
        print_pointers.operator()<uint64_t>(scanner, offset, range);
    }
    else if (opt_type == "uc")
    {
        print_pointers.operator()<uint8_t>(scanner, offset, range);
    }
    else
    {
        // default to int 4 bytes
        print_pointers.operator()<int32_t>(scanner, offset, range);
    }

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

    if (num_elements == 0)
    {
        return;
    }

    auto offset = lexical_cast<std::uintptr_t>(str_address);

    auto print_val_address = []<typename T>(Scanner& scanner, std::uintptr_t offset, int num_elements)
    {
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
    };

    if (opt_type == "s")
    {
        print_val_address.operator()<int16_t>(scanner, offset, num_elements);
    }
    else if (opt_type == "l")
    {
        print_val_address.operator()<int64_t>(scanner, offset, num_elements);
    }
    else if (opt_type == "f")
    {
        print_val_address.operator()<float>(scanner, offset, num_elements);
    }
    else if (opt_type == "d")
    {
        print_val_address.operator()<double>(scanner, offset, num_elements);
    }
    else if (opt_type == "c")
    {
        print_val_address.operator()<int8_t>(scanner, offset, num_elements);
    }
    else if (opt_type == "u" or opt_type == "ui")
    {
        print_val_address.operator()<uint32_t>(scanner, offset, num_elements);
    }
    else if (opt_type == "us")
    {
        print_val_address.operator()<uint16_t>(scanner, offset, num_elements);
    }
    else if (opt_type == "ul")
    {
        print_val_address.operator()<uint64_t>(scanner, offset, num_elements);
    }
    else if (opt_type == "uc")
    {
        print_val_address.operator()<uint8_t>(scanner, offset, num_elements);
    }
    else if (opt_type == "t")
    {
        std::cout << scanner.read_string(offset) << '\n';
    }
    else
    {
        // default to int 4 bytes
        print_val_address.operator()<int32_t>(scanner, offset, num_elements);
    }

}


void handle_where_changed(Scanner& scanner, ArgList args)
{
    //handle all change
    auto print_addresses_with_changes = []<typename T>(Scanner& scanner)
    {
        auto prev_val = scanner.get_where_chain_val<T>();
        std::span<const std::uintptr_t> addresses = scanner.where_changed<T>();
        for (const auto change : addresses)
        {
            print_hex(change);
            std::cout << " : " << prev_val << "\t->\t" << *scanner.read_mem<T>(change) << '\n';
        }
        std::cout << "Addresses changed: " << addresses.size() << '\n';
    };

    if (cur_where_type == "s")
    {
        print_addresses_with_changes.operator()<int16_t>(scanner);
    }
    else if (cur_where_type == "l")
    {
        print_addresses_with_changes.operator()<int64_t>(scanner);
    }
    else if (cur_where_type == "f")
    {
        print_addresses_with_changes.operator()<float>(scanner);
    }
    else if (cur_where_type == "d")
    {
        print_addresses_with_changes.operator()<double>(scanner);
    }
    else if (cur_where_type == "c")
    {
        print_addresses_with_changes.operator()<int8_t>(scanner);
    }
    else if (cur_where_type == "u" or cur_where_type == "ui")
    {
        print_addresses_with_changes.operator()<uint32_t>(scanner);
    }
    else if (cur_where_type == "us")
    {
        print_addresses_with_changes.operator()<uint16_t>(scanner);
    }
    else if (cur_where_type == "ul")
    {
        print_addresses_with_changes.operator()<uint64_t>(scanner);
    }
    else if (cur_where_type == "uc")
    {
        print_addresses_with_changes.operator()<uint8_t>(scanner);
    }
    else
    {
        // default to int 4 bytes
        print_addresses_with_changes.operator()<int32_t>(scanner);
    }
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
