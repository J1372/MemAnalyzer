#ifndef SCANNER_SCANNER_H
#define SCANNER_SCANNER_H

#include <windows.h>
#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <span>
#include <unordered_map>
#include <psapi.h>
#include "AddressRange.h"
#include "Value.h"
#include <limits>

class Scanner
{
    std::string process_name;
    DWORD process_id;
    HANDLE process = nullptr;
    bool bit64;

    std::vector<AddressRange> ro_pages;
    std::vector<std::uintptr_t> cur_where_addresses; // addresses of the current where chain.
    Value cur_where_val;

    [[nodiscard]]
    bool read_mem_safe(LPVOID buf, LPCVOID from, std::size_t to_read) const
    {
        SIZE_T bytes_read;

        if (ReadProcessMemory(process, from, buf, to_read, &bytes_read) == 0)
        {
            // func failed.
            return false;
        }

        return to_read == bytes_read;
    }

    template <typename T>
    std::vector<std::uintptr_t> where_val_internal(T val) const
    {
        std::vector<std::uintptr_t> addresses;

        for (const auto page : get_all_pages())
        {
            constexpr auto element_bytes = sizeof(T);
            auto num_elements = (page.size() / element_bytes);
            std::unique_ptr<T[]> buf = read_array<T>(page.start(), num_elements);

            if (!buf)
                continue;

            for (std::size_t i = 0; i < num_elements; ++i)
            {
                auto base_address = page.get_address_offset(i * element_bytes);
                T read_val = buf[i];

                if (eq_vals(read_val, val))
                {
                    addresses.push_back(base_address);
                }
            }
        }

        return addresses;
    }

public:

    Scanner(const std::string& window_name)
    {
        HWND window = FindWindow(NULL, window_name.c_str());

        if (window)
        {
            DWORD process_id;
            GetWindowThreadProcessId(window, &process_id);
            this->process_id = process_id;

            process = OpenProcess( PROCESS_VM_READ | PROCESS_QUERY_INFORMATION , FALSE, process_id );
            if (!process)
                throw std::runtime_error("Could not open process.");


            constexpr int name_max_size = 128;
            char process_name_buffer[name_max_size];
            auto name_str_size = GetModuleBaseNameA(process, NULL, process_name_buffer, name_max_size);

            if (name_str_size == 0)
                throw std::runtime_error("Could not read process name.");

            process_name = std::string_view{ process_name_buffer, name_str_size };

            BOOL wow64Ret;
            IsWow64Process(process, &wow64Ret);
            if (wow64Ret)
            {
                bit64 = false; // returns true if process is 32 bit on a 64bit system.
            }
            else
            {
                // process is same bit as os.
                SYSTEM_INFO sys_info;
                GetNativeSystemInfo(&sys_info);
                bit64 = sys_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 or
                        sys_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 or
                        sys_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64;
            }

        }
        else
        {
            throw std::runtime_error("Could not find process (Is it running?).");
        }

        ro_pages = scan_pages(PAGE_READONLY);
    }

    Scanner(const Scanner& copy) = delete;
    Scanner& operator=(const Scanner& copy) = delete;

    Scanner(Scanner&& move)
    {
        process = move.process;
        move.process = nullptr;
        bit64 = move.bit64;
        ro_pages = std::move(move.ro_pages);
        cur_where_addresses = std::move(move.cur_where_addresses);
        cur_where_val = std::move(move.cur_where_val);
    }

    Scanner& operator=(Scanner&& move)
    {
        process = move.process;
        move.process = nullptr;
        bit64 = move.bit64;
        ro_pages = std::move(move.ro_pages);
        cur_where_addresses = std::move(move.cur_where_addresses);
        cur_where_val = std::move(move.cur_where_val);
        return *this;
    }

    ~Scanner()
    {
        if (process)
        {
            CloseHandle(process);
        }
    }

    std::string_view get_process_name() const
    {
        return process_name;
    }

    unsigned long get_process_id() const
    {
        return process_id;
    }

    template <typename T>
    std::optional<T> read_mem(std::uintptr_t offset) const
    {
        T val;
        if (read_mem_safe(&val, reinterpret_cast<LPCVOID>(offset), sizeof val))
        {
            return val;
        };

        return {};
    }

    std::string read_string(std::uintptr_t offset, std::size_t max_size=256) const
    {
        std::array<char, 64> buf;
        std::string str;

        std::uintptr_t next_read = offset;
        std::size_t total_read = 0;
        while (total_read < max_size)
        {
            if (!read_mem_safe(buf.data(), reinterpret_cast<LPCVOID>(next_read), sizeof buf))
            {
                return str;
            }

            for (char byte : buf)
            {
                if (byte == '\0' or !std::isprint(byte))
                {
                    return str;
                }
                else
                {
                    str += byte;
                }
            }

            total_read += sizeof buf;
            next_read = offset + total_read;
        }

        return str;
    }

    template <typename T>
    std::unique_ptr<T[]> read_array(std::uintptr_t offset, std::size_t size) const
    {
        std::unique_ptr<T[]> val = std::make_unique_for_overwrite<T[]>(size);
        if (read_mem_safe(val.get(), reinterpret_cast<LPCVOID>(offset), size * sizeof(T)))
        {
            return val;
        }
        else
        {
            return nullptr;
        }
    }

    std::vector<AddressRange> scan_pages(DWORD protect) const
    {
        std::vector<AddressRange> pages;

        MEMORY_BASIC_INFORMATION mbi;
        LPVOID address = nullptr;

        while (VirtualQueryEx(process, address, &mbi, sizeof mbi) == sizeof mbi)
        {
            AddressRange page_range { reinterpret_cast<std::uintptr_t>(mbi.BaseAddress), mbi.RegionSize };

            if (mbi.State == MEM_COMMIT and mbi.Protect == protect)
            {
                pages.emplace_back(page_range);
            }

            auto next_addr = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            address = (LPVOID) next_addr;
        }

        return pages;
    }

    bool is_64_bit() const
    {
        return bit64;
    }

    int bytes_in_pointer() const
    {
        return is_64_bit() ? 8 : 4;
    }

    template <typename T>
    std::span<const std::uintptr_t> where_val(T val)
    {
        cur_where_addresses.clear();
        cur_where_val = val;

        cur_where_addresses = where_val_internal(val);

        return cur_where_addresses;
    }

    std::vector<std::uintptr_t> where_val(std::string_view str)
    {
        std::vector<std::uintptr_t> addresses;

        for (const auto page : get_all_pages())
        {
            // read entire page so that iterating over chars doesn't redundantly read.
            // could optimize, move unique_ptr out of loop, only call read_array if page size increases
            // otherwise, call read_to_buf.
            std::unique_ptr<char[]> buf = read_array<char>(page.start(), page.size());

            if (!buf)
                continue;

            for (std::size_t offset = 0; offset + str.length() <= page.size(); ++offset)
            {
                std::string_view view { buf.get() + offset, str.length() };
                if (view == str)
                {
                    addresses.push_back(page.get_address_offset(offset));
                }
            }
        }

        return addresses;
    }

    template <typename T>
    bool eq_vals(T val1, T val2) const
    {
        if constexpr(std::is_floating_point_v<T>)
        {
            constexpr T precision = 0.001;
            auto dif = std::abs(val1 - val2);
            return dif <= precision;
        }
        else
        {
            return val1 == val2;
        }
    }

    template <typename T>
    std::span<const std::uintptr_t> where_became(T val) // prev == cur_where_val and cur == val
    {
        std::vector<std::uintptr_t> addresses;
        addresses.reserve(cur_where_addresses.size());

        for (std::uintptr_t address : cur_where_addresses)
        {
            auto cur_val = read_mem<T>(address);
            if (cur_val and eq_vals(*cur_val, val))
            {
                addresses.push_back(address);
            }
        }

        cur_where_addresses = std::move(addresses);
        cur_where_val = val;
        return cur_where_addresses;
    }

    template<typename T>
    std::span<const std::uintptr_t> where_changed() // prev != cur
    {
        std::vector<std::uintptr_t> addresses;
        addresses.reserve(cur_where_addresses.size());

        auto prev_val = cur_where_val.get<T>();
        for (std::uintptr_t address : cur_where_addresses)
        {
            auto cur_val = read_mem<T>(address);
            if (cur_val and !eq_vals(prev_val, *cur_val))
            {
                addresses.push_back(address);
            }
        }

        cur_where_addresses = std::move(addresses);
        return cur_where_addresses;
    }

    bool is_sizeof_pointer(auto val) const
    {
        return bytes_in_pointer() == sizeof val;
    }

    bool is_possible_pointer(auto val) const
    {
        return std::is_integral_v<decltype(val)> and is_sizeof_pointer(val);
    }

    template <typename T>
    T get_where_chain_val() const
    {
        return cur_where_val.get<T>();
    }

    void scan_pointers_to_internal(std::unordered_map<std::uintptr_t, std::vector<std::uintptr_t>>& pointed_to_map, std::uintptr_t address) const
    {
        const auto& pointers = pointed_to_map[address] = is_64_bit() ? where_val_internal(address) : where_val_internal(static_cast<uint32_t>(address));

        for (auto pointer : pointers)
        {
            scan_pointers_to_internal(pointed_to_map, pointer);
        }
    }

    std::unordered_map<std::uintptr_t, std::vector<std::uintptr_t>> scan_pointers_to(std::uintptr_t address) const
    {
        std::unordered_map<std::uintptr_t, std::vector<std::uintptr_t>> pointed_to_map;

        scan_pointers_to_internal(pointed_to_map, address);

        return pointed_to_map;
    }

    std::vector<AddressRange> get_all_pages() const
    {
        std::vector<AddressRange> all = ro_pages;
        std::vector<AddressRange> rw = get_rw_pages();
        all.insert(all.end(), rw.begin(), rw.end());
        return all;
    }

    std::vector<AddressRange> get_rw_pages() const
    {
        return scan_pages(PAGE_READWRITE);
    }

};

#endif //SCANNER_SCANNER_H
