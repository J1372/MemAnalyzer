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
#include <tlhelp32.h>

class Scanner
{
    std::string process_name;
    DWORD process_id;
    HANDLE process = nullptr;
    bool bit64;

    std::uintptr_t base_address;
    std::vector<AddressRange> ro_pages;
    std::vector<std::uintptr_t> cur_where_offsets; // offsets of the current where chain.
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
        std::vector<std::uintptr_t> offsets;

        for (const auto page : get_all_pages())
        {
            constexpr auto element_bytes = sizeof(T);
            auto num_elements = (page.size() / element_bytes);
            std::unique_ptr<T[]> buf = read_array<T>(page.start() - base_address, num_elements);

            if (!buf)
                continue;

            for (std::size_t i = 0; i < num_elements; ++i)
            {
                auto page_address = page.get_address_offset(i * element_bytes);
                T read_val = buf[i];

                if (eq_vals(read_val, val))
                {
                    offsets.push_back(page_address - base_address);
                }
            }
        }

        return offsets;
    }

    std::uintptr_t scan_base_address() const
    {
        const DWORD id = GetProcessId(process);
        MODULEENTRY32 me32;
        me32.dwSize = sizeof(MODULEENTRY32);
        HANDLE module_snap = INVALID_HANDLE_VALUE;
        module_snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, id);

        if(module_snap == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error("Could not get process module snapshot.");
        }

        if(!Module32First(module_snap, &me32))
        {
            CloseHandle(module_snap);
            throw std::runtime_error("Could not get first module.");
        }

        std::uintptr_t base_address = 0;
        do
        {
            if (strcmp(process_name.c_str(), me32.szModule) == 0)
            {
                base_address = reinterpret_cast<std::uintptr_t>(me32.modBaseAddr);
            }
        } while(Module32Next(module_snap, &me32) and base_address == 0);

        CloseHandle(module_snap);

        if (base_address == 0)
        {
            throw std::runtime_error("Could not find base address.");
        }

        return base_address;
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


            constexpr int name_max_size = 256;
            char process_name_buffer[name_max_size];
            auto name_str_size = GetModuleBaseName(process, NULL, process_name_buffer, name_max_size);

            if (name_str_size == 0)
                throw std::runtime_error("Could not read process name.");

            process_name = { process_name_buffer, name_str_size };

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
        base_address = scan_base_address();
    }

    Scanner(const Scanner& copy) = delete;
    Scanner& operator=(const Scanner& copy) = delete;

    Scanner(Scanner&& move)
    {
        process = move.process;
        move.process = nullptr;
        bit64 = move.bit64;
        ro_pages = std::move(move.ro_pages);
        cur_where_offsets = std::move(move.cur_where_offsets);
        cur_where_val = std::move(move.cur_where_val);
    }

    Scanner& operator=(Scanner&& move)
    {
        process = move.process;
        move.process = nullptr;
        bit64 = move.bit64;
        ro_pages = std::move(move.ro_pages);
        cur_where_offsets = std::move(move.cur_where_offsets);
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
        if (read_mem_safe(&val, reinterpret_cast<LPCVOID>(base_address + offset), sizeof val))
        {
            return val;
        };

        return {};
    }

    std::string read_string(std::uintptr_t offset, std::size_t max_size=256) const
    {
        std::array<char, 64> buf;
        std::string str;

        std::uintptr_t next_read = base_address + offset;
        std::size_t total_read = 0;
        while (total_read < max_size)
        {
            if (!read_mem_safe(buf.data(), reinterpret_cast<LPCVOID>(next_read), sizeof buf))
            {
                return str;
            }

            for (unsigned char byte : buf)
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
        if (read_mem_safe(val.get(), reinterpret_cast<LPCVOID>(base_address + offset), size * sizeof(T)))
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
        cur_where_offsets.clear();
        cur_where_val = val;

        cur_where_offsets = where_val_internal(val);

        return cur_where_offsets;
    }

    std::vector<std::uintptr_t> where_val(std::string_view str)
    {
        std::vector<std::uintptr_t> offsets;

        for (const auto page : get_all_pages())
        {
            // read entire range so that iterating over chars doesn't redundantly read.
            std::unique_ptr<char[]> buf = read_array<char>(page.start() - base_address, page.size());

            if (!buf)
                continue;

            for (std::size_t offset = 0; offset + str.length() <= page.size(); ++offset)
            {
                std::string_view view { buf.get() + offset, str.length() };
                if (view == str)
                {
                    offsets.push_back(page.get_address_offset(offset) - base_address);
                }
            }
        }

        return offsets;
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
        std::vector<std::uintptr_t> offsets;
        offsets.reserve(cur_where_offsets.size());

        for (std::uintptr_t offset : cur_where_offsets)
        {
            auto cur_val = read_mem<T>(offset);
            if (cur_val and eq_vals(*cur_val, val))
            {
                offsets.push_back(offset);
            }
        }

        cur_where_offsets = std::move(offsets);
        cur_where_val = val;
        return cur_where_offsets;
    }

    template<typename T>
    std::span<const std::uintptr_t> where_changed() // prev != cur
    {
        std::vector<std::uintptr_t> offsets;
        offsets.reserve(cur_where_offsets.size());

        auto prev_val = cur_where_val.get<T>();
        for (std::uintptr_t offset : cur_where_offsets)
        {
            auto cur_val = read_mem<T>(offset);
            if (cur_val and !eq_vals(prev_val, *cur_val))
            {
                offsets.push_back(offset);
            }
        }

        cur_where_offsets = std::move(offsets);
        return cur_where_offsets;
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

    void scan_pointers_to_internal(std::unordered_map<std::uintptr_t, std::vector<std::uintptr_t>>& pointed_to_map, std::uintptr_t offset) const
    {
        const auto& pointers = pointed_to_map[offset] = is_64_bit() ? where_val_internal(base_address + offset) : where_val_internal(static_cast<uint32_t>(base_address + offset));

        for (auto pointer : pointers)
        {
            scan_pointers_to_internal(pointed_to_map, pointer);
        }
    }

    std::unordered_map<std::uintptr_t, std::vector<std::uintptr_t>> scan_pointers_to(std::uintptr_t offset) const
    {
        std::unordered_map<std::uintptr_t, std::vector<std::uintptr_t>> pointed_to_map;

        scan_pointers_to_internal(pointed_to_map, offset);

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

    std::uintptr_t get_relative_address(std::uintptr_t address) const
    {
        return address - base_address;
    }

};

#endif //SCANNER_SCANNER_H
