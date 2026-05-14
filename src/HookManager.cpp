#include "HookManager.hpp"
#include <windows.h>
#include <iostream>

namespace FastHook {
    inline void* ResolveRelativeAddress(void* Address, int instructionOffset, int instructionSize) {
        if (!Address) return nullptr;
        uintptr_t instruction = reinterpret_cast<uintptr_t>(Address);
        int32_t relativeOffset = *reinterpret_cast<int32_t*>(instruction + instructionOffset);
        return reinterpret_cast<void*>(instruction + instructionSize + relativeOffset);
    }
    static std::vector<int> PatternToBytes(const char* pattern) {
        std::vector<int> bytes;
        const char* current = pattern;
        auto hex2int = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        while (*current != '\0') {
            if (*current == ' ') {
                ++current;
                continue;
            }
            if (*current == '?') {
                bytes.push_back(-1);
                ++current;
                if (*current == '?') {
                    ++current;
                }
                continue;
            }
            int high = hex2int(current[0]);
            int low  = hex2int(current[1]);
            if (high != -1 && low != -1) {
                bytes.push_back((high << 4) | low);
                current += 2;
            } else {
                ++current;
            }
        }
        return bytes;
    }

    static void* ScanPattern(const char* moduleName, const char* pattern) {
        HMODULE hModule = GetModuleHandleA(moduleName);
        if (!hModule) return nullptr;

        PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hModule;
        PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((std::uint8_t*)hModule + dosHeader->e_lfanew);

        std::uint8_t* base = (std::uint8_t*)hModule;
        DWORD sizeOfImage = ntHeaders->OptionalHeader.SizeOfImage;
        std::vector<int> patternBytes = PatternToBytes(pattern);
        size_t s = patternBytes.size();
        int* d = patternBytes.data();

        for (DWORD i = 0; i < sizeOfImage - s; ++i) {
            bool found = true;
            for (size_t j = 0; j < s; ++j) {
                if (d[j] != -1 && base[i + j] != d[j]) {
                    found = false;
                    break;
                }
            }
            if (found) return &base[i];
        }
        return nullptr;
    }

    void HookManager::RegisterByAddress(const char* name, void* targetAddress, void* hookFunction, void** originalFunction) {
        HookEntry entry = { name, "", "", 0, targetAddress, hookFunction, originalFunction };
        GetEntries().push_back(entry);
    }
    void HookManager::RegisterByPattern(const char* name, const char* moduleName, const char* pattern, void* hookFunction, void** originalFunction) {
        HookEntry entry = { name, moduleName, pattern, 0, nullptr, hookFunction, originalFunction };
        GetEntries().push_back(entry);
    }
    void HookManager::RegisterByOffset(const char* name, const char* moduleName, uintptr_t offset, void* hookFunction, void** originalFunction) {
        HookEntry entry = { name, moduleName, "", offset, nullptr, hookFunction, originalFunction };
        GetEntries().push_back(entry);
    }

    bool HookManager::EnableAll() {
        MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            return false;
        }
        for (auto& entry : GetEntries()) {
            if (entry.targetAddress == nullptr) {
                if (!entry.pattern.empty()) {
                    entry.targetAddress = ScanPattern(entry.moduleName.empty() ? NULL : entry.moduleName.c_str(), entry.pattern.c_str());
                } 
                else if (entry.offset != 0) {
                    HMODULE hMod = GetModuleHandleA(entry.moduleName.empty() ? NULL : entry.moduleName.c_str());
                    if (hMod) {
                        entry.targetAddress = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(hMod) + entry.offset);
                    }
                }
            }
            if (entry.targetAddress) {
                if (MH_CreateHook(entry.targetAddress, entry.hookFunction, entry.originalFunction) == MH_OK) {
                    MH_EnableHook(entry.targetAddress);
                }
            } else {
                std::cout << "Failed to resolve address for hook: " << entry.name << std::endl;
            }
        }
        return true;
    }

    void HookManager::DisableAll() {
        MH_DisableHook(MH_ALL_HOOKS);
    }
}
