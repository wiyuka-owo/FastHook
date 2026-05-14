#pragma once
#include <functional>

namespace FastHook {
    enum class RenderAPI {
        D3D9,
        D3D11,
        D3D12
    };

    void Init(RenderAPI api, std::function<void()> renderCallback);
    void Shutdown();
}
