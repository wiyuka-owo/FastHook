#pragma once

#ifndef USE_UNREAL
#define USE_UNREAL
#endif

#ifdef USE_UNREAL

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>

#include "HookManager.hpp"
#include "FastHook.hpp"

namespace FastHook {
namespace Unreal {

    struct UObject;
    struct UFunction;

    using ProcessEvent_t = void(__stdcall*)(UObject* Context, UFunction* Function, void* Parms);
    inline ProcessEvent_t OriginalProcessEvent = nullptr;

    using ProcessEventCallback_t = std::function<void(UObject* Context, UFunction* Function, void* Parms)>;

    struct ProcessEventHookEntry {
        std::string functionName;
        ProcessEventCallback_t preCallback;
        ProcessEventCallback_t postCallback;
    };

    inline std::vector<ProcessEventHookEntry>& GetPEHooks() {
        static std::vector<ProcessEventHookEntry> entries;
        return entries;
    }

    inline void RegisterProcessEventHook(const char* targetFunction, ProcessEventCallback_t preCb, ProcessEventCallback_t postCb) {
        GetPEHooks().push_back({targetFunction, preCb, postCb});
    }

    inline std::function<std::string(UFunction*)> GetFunctionNameImpl = nullptr;

    struct CachedCallbacks {
        std::vector<ProcessEventCallback_t> preCallbacks;
        std::vector<ProcessEventCallback_t> postCallbacks;
        bool hasCallbacks = false;
    };

    inline std::unordered_map<UFunction*, CachedCallbacks> g_FunctionCache;
    inline std::shared_mutex g_CacheMutex;

    inline void __stdcall HookedProcessEvent(UObject* Context, UFunction* Function, void* Parms) {
        if (!Function) {
            if (OriginalProcessEvent) OriginalProcessEvent(Context, Function, Parms);
            return;
        }

        CachedCallbacks* activeCallbacks = nullptr;

        {
            std::shared_lock<std::shared_mutex> readLock(g_CacheMutex);
            auto it = g_FunctionCache.find(Function);
            if (it != g_FunctionCache.end()) {
                activeCallbacks = &it->second;
            }
        }

        if (!activeCallbacks) {
            std::unique_lock<std::shared_mutex> writeLock(g_CacheMutex);
            auto it = g_FunctionCache.find(Function);
            if (it == g_FunctionCache.end()) {
                CachedCallbacks newCache;
                if (GetFunctionNameImpl) {
                    std::string funcName = GetFunctionNameImpl(Function);
                    for (const auto& entry : GetPEHooks()) {
                        if (funcName.find(entry.functionName) != std::string::npos) {
                            if (entry.preCallback) newCache.preCallbacks.push_back(entry.preCallback);
                            if (entry.postCallback) newCache.postCallbacks.push_back(entry.postCallback);
                            newCache.hasCallbacks = true;
                        }
                    }
                }
                auto inserted = g_FunctionCache.emplace(Function, std::move(newCache));
                activeCallbacks = &inserted.first->second;
            } else {
                activeCallbacks = &it->second;
            }
        }

        if (activeCallbacks && activeCallbacks->hasCallbacks) {
            for (const auto& cb : activeCallbacks->preCallbacks) {
                cb(Context, Function, Parms);
            }
        }

        if (OriginalProcessEvent) {
            OriginalProcessEvent(Context, Function, Parms);
        }

        if (activeCallbacks && activeCallbacks->hasCallbacks) {
            for (const auto& cb : activeCallbacks->postCallbacks) {
                cb(Context, Function, Parms);
            }
        }
    }

    inline bool InitializeByAddress(void* processEventAddress, std::function<std::string(UFunction*)> nameResolver) {
        if (!processEventAddress) return false;
        
        GetFunctionNameImpl = nameResolver;

        FastHook::HookManager::RegisterByAddress(
            "Unreal_ProcessEvent", 
            processEventAddress, 
            (void*)&HookedProcessEvent, 
            (void**)&OriginalProcessEvent
        );

        return true;
    }

    inline bool InitializeByVTable(void* anyUObjectInstance, int processEventIndex, std::function<std::string(UFunction*)> nameResolver) {
        if (!anyUObjectInstance) return false;
        void** vtable = *(void***)anyUObjectInstance;
        return InitializeByAddress(vtable[processEventIndex], nameResolver);
    }

    inline bool InitializeByPattern(const char* moduleName, const char* pattern, std::function<std::string(UFunction*)> nameResolver) {
        GetFunctionNameImpl = nameResolver;

        FastHook::HookManager::RegisterByPattern(
            "Unreal_ProcessEvent", 
            moduleName, 
            pattern, 
            (void*)&HookedProcessEvent, 
            (void**)&OriginalProcessEvent
        );

        return true;
    }

    inline void ClearCache() {
        std::unique_lock<std::shared_mutex> writeLock(g_CacheMutex);
        g_FunctionCache.clear();
    }

} // namespace Unreal
} // namespace FastHook

#define MIXIN_UNREAL_PE_PRE(Name, TargetFunction) \
    namespace UnrealHook_##Name { \
        void PreCallback(FastHook::Unreal::UObject* Context, FastHook::Unreal::UFunction* Function, void* Parms); \
        struct AutoReg { \
            AutoReg() { \
                FastHook::Unreal::RegisterProcessEventHook(TargetFunction, PreCallback, nullptr); \
            } \
        }; \
        static AutoReg g_AutoReg; \
    } \
    void UnrealHook_##Name::PreCallback(FastHook::Unreal::UObject* Context, FastHook::Unreal::UFunction* Function, void* Parms)

#define MIXIN_UNREAL_PE_POST(Name, TargetFunction) \
    namespace UnrealHook_##Name { \
        void PostCallback(FastHook::Unreal::UObject* Context, FastHook::Unreal::UFunction* Function, void* Parms); \
        struct AutoReg { \
            AutoReg() { \
                FastHook::Unreal::RegisterProcessEventHook(TargetFunction, nullptr, PostCallback); \
            } \
        }; \
        static AutoReg g_AutoReg; \
    } \
    void UnrealHook_##Name::PostCallback(FastHook::Unreal::UObject* Context, FastHook::Unreal::UFunction* Function, void* Parms)

#endif // USE_UNREAL
