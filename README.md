# FastHook
一个基于 C++ 的逆向开发框架，支持 Unity 和 Unreal 引擎。

## 依赖
- MinHook
- ImGui
- Kiero
- UnityResolve

## 使用示例

```cpp
#include "FastHook.hpp"
#include "HookManager.hpp"
#define USE_UNREAL
#include "UnrealAdapter.hpp" 
#include "imgui.h"

MIXIN_OFFSET(Weapon_ConsumeAmmo, "", 0x1873A20, bool, __fastcall, (void* _this, uint8_t isSomething)) {
    return Original_Weapon_ConsumeAmmo(_this, isSomething);
}

MIXIN_UNREAL_PE_POST(CameraTick, "Function BP_FSDCameraManager.BP_FSDCameraManager_C.ReceiveTick") {
    if (!Context) return;
}

void OnRender() {
    ImGui::Begin("FastHook Menu");
    ImGui::Text("Hello World!");
    ImGui::End();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        
        FastHook::Init(FastHook::RenderAPI::D3D12, OnRender);
        FastHook::HookManager::EnableAll();
    }
    return TRUE;
}
```

##开源协议
MIT
