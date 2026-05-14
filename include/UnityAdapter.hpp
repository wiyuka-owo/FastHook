#pragma once

#ifdef USE_UNITY

#include <windows.h>
#include <string>
#include <vector>
#include <functional>

#include "HookManager.hpp"
#include "FastHook.hpp"
#include "../UnityResolve.hpp"
#include "../imgui.h"

namespace FastHook {
namespace Unity {

    inline bool Initialize() {
        HMODULE hIl2cpp = GetModuleHandleA("GameAssembly.dll");
        if (hIl2cpp) {
            UnityResolve::Init(hIl2cpp, UnityResolve::Mode::Il2Cpp);
            return true;
        }

        HMODULE hMono = GetModuleHandleA("mono-2.0-bdwgc.dll");
        if (!hMono) hMono = GetModuleHandleA("mono.dll");
        if (hMono) {
            UnityResolve::Init(hMono, UnityResolve::Mode::Mono);
            return true;
        }
        
        return false;
    }

    class ScopedThreadAttach {
    public:
        ScopedThreadAttach() {
            UnityResolve::ThreadAttach();
        }
        ~ScopedThreadAttach() {
            UnityResolve::ThreadDetach();
        }
    };

    struct UnityHookEntry {
        std::string assemblyName;
        std::string className;
        std::string methodName;
        void* hookFunction;
        void** originalFunction;
        std::string hookName; 
    };

    inline std::vector<UnityHookEntry>& GetUnityHooks() {
        static std::vector<UnityHookEntry> entries;
        return entries;
    }

    inline void RegisterUnityHook(const char* name, const char* assembly, const char* cls, const char* method, void* hookFn, void** origFn) {
        GetUnityHooks().push_back({assembly, cls, method, hookFn, origFn, name});
    }

    inline bool EnableAllHooks() {
        ScopedThreadAttach attach;

        for (const auto& entry : GetUnityHooks()) {
            auto assembly = UnityResolve::Get(entry.assemblyName);
            if (!assembly) continue;

            auto cls = assembly->Get(entry.className);
            if (!cls) continue;

            auto method = cls->Get<UnityResolve::Method>(entry.methodName);
            if (!method) continue;

            method->Compile();
            void* targetAddress = method->function;

            if (targetAddress) {
                FastHook::HookManager::RegisterByAddress(
                    entry.hookName.c_str(), 
                    targetAddress, 
                    entry.hookFunction, 
                    entry.originalFunction
                );
            }
        }
        
        return FastHook::HookManager::EnableAll();
    }

    namespace Helpers {

        inline bool WorldToScreen(const UnityResolve::UnityType::Vector3& worldPos, ImVec2& outScreenPos) {
            auto* pCamera = UnityResolve::UnityType::Camera::GetMain();
            if (!pCamera) return false;

            UnityResolve::UnityType::Vector3 screenPos = pCamera->WorldToScreenPoint(
                worldPos,
                UnityResolve::UnityType::Camera::Eye::Mono
            );

            if (screenPos.z > 0.01f) {
                outScreenPos.x = screenPos.x;
                outScreenPos.y = ImGui::GetIO().DisplaySize.y - screenPos.y;
                return true;
            }
            return false;
        }

        inline std::vector<void*> FindObjectsOfType(const std::string& assemblyName, const std::string& className) {
            auto assembly = UnityResolve::Get(assemblyName);
            if (!assembly) return {};
            auto pClass = assembly->Get(className);
            if (!pClass) return {};
            
            return pClass->FindObjectsByType<void*>();
        }
    }

} // namespace Unity
} // namespace FastHook

#define MIXIN_UNITY(Name, Assembly, Class, Method, RetType, CallConv, Args) \
    namespace UnityHook_##Name { \
        typedef RetType(CallConv *TargetFn) Args; \
        inline TargetFn Original = nullptr; \
        RetType CallConv HookFn Args; \
        struct AutoReg { \
            AutoReg() { \
                FastHook::Unity::RegisterUnityHook( \
                    #Name, Assembly, Class, Method, \
                    (void*)&HookFn, \
                    (void**)&Original \
                ); \
            } \
        }; \
        static AutoReg g_AutoReg; \
    } \
    RetType CallConv UnityHook_##Name::HookFn Args

#endif