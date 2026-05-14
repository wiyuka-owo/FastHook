#pragma once
#include <vector>
#include <string>

#include "MinHook.h" 

namespace FastHook {

    class HookManager {
    public:
        struct HookEntry {
            std::string name;
            std::string moduleName;
            std::string pattern;
            uintptr_t offset;
            void* targetAddress;
            void* hookFunction;
            void** originalFunction;
        };

        static std::vector<HookEntry>& GetEntries() {
            static std::vector<HookEntry> entries;
            return entries;
        }

        static void RegisterByAddress(const char* name, void* targetAddress, void* hookFunction, void** originalFunction);
        static void RegisterByPattern(const char* name, const char* moduleName, const char* pattern, void* hookFunction, void** originalFunction);
        static void RegisterByOffset(const char* name, const char* moduleName, uintptr_t offset, void* hookFunction, void** originalFunction); 
        
        static bool EnableAll();
        static void DisableAll();
    };

} // namespace FastHook

#define MIXIN_PATTERN(Name, Module, Pattern, RetType, CallConv, Args) \
    typedef RetType(CallConv *TargetFn_##Name) Args; \
    inline TargetFn_##Name Original_##Name = nullptr; \
    RetType CallConv HookFn_##Name Args; \
    namespace { \
        struct AutoReg_##Name { \
            AutoReg_##Name() { \
                FastHook::HookManager::RegisterByPattern( \
                    #Name, Module, Pattern, \
                    (void*)&HookFn_##Name, \
                    (void**)&Original_##Name \
                ); \
            } \
        }; \
        static AutoReg_##Name g_AutoReg_##Name; \
    } \
    RetType CallConv HookFn_##Name Args

#define MIXIN_OFFSET(Name, Module, Offset, RetType, CallConv, Args) \
    typedef RetType(CallConv *TargetFn_##Name) Args; \
    inline TargetFn_##Name Original_##Name = nullptr; \
    RetType CallConv HookFn_##Name Args; \
    namespace { \
        struct AutoReg_##Name { \
            AutoReg_##Name() { \
                FastHook::HookManager::RegisterByOffset(#Name, Module, Offset, (void*)&HookFn_##Name, (void**)&Original_##Name); \
            } \
        }; \
        static AutoReg_##Name g_AutoReg_##Name; \
    } \
    RetType CallConv HookFn_##Name Args

#define MIXIN_VTABLE(Name, RetType, CallConv, Args) \
    typedef RetType(CallConv *TargetFn_##Name) Args; \
    inline TargetFn_##Name Original_##Name = nullptr; \
    RetType CallConv HookFn_##Name Args; \
    struct AutoBinder_##Name { \
        static void Bind(FastHook::VMTHook& vmtHooker, int vtableIndex) { \
            vmtHooker.HookFunction(vtableIndex, (void*)&HookFn_##Name, (void**)&Original_##Name); \
        } \
    }; \
    RetType CallConv HookFn_##Name Args