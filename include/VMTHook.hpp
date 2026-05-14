#pragma once
#include <windows.h>
#include <memory>
#include <cstring>

namespace FastHook {

    class VMTHook {
    private:
        void*** m_Instance = nullptr;
        void**  m_OriginalVTable = nullptr;
        void**  m_NewVTable = nullptr;
        std::unique_ptr<void*[]> m_AllocatedMemory;
        int     m_VTableLength = 0;

        static int CountVirtualFunctions(void** vtable) {
            int count = 0;
            MEMORY_BASIC_INFORMATION mbi = { 0 };
            while (true) {
                if (!vtable[count]) break;
                if (!VirtualQuery(vtable[count], &mbi, sizeof(mbi))) break;
                if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) break;
                if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) break;
                count++;
            }
            return count;
        }

    public:
        VMTHook() = default;
        ~VMTHook() { Unhook(); }

        bool Init(void* instance) {
            if (!instance) return false;

            m_Instance = reinterpret_cast<void***>(instance);
            m_OriginalVTable = *m_Instance;

            if (!m_OriginalVTable) return false;

            m_VTableLength = CountVirtualFunctions(m_OriginalVTable);
            if (m_VTableLength == 0) return false;

            m_AllocatedMemory = std::make_unique<void*[]>(m_VTableLength + 1);
            
            std::memcpy(m_AllocatedMemory.get(), m_OriginalVTable - 1, (m_VTableLength + 1) * sizeof(void*));
            
            m_NewVTable = m_AllocatedMemory.get() + 1;

            return true;
        }

        void HookFunction(int index, void* hookedFunc, void** originalFuncOut) {
            if (index < 0 || index >= m_VTableLength) return;
            
            if (originalFuncOut) {
                *originalFuncOut = m_OriginalVTable[index];
            }
            m_NewVTable[index] = hookedFunc;
        }

        void Apply() {
            if (!m_Instance || !m_NewVTable) return;
            DWORD oldProtect;
            VirtualProtect(m_Instance, sizeof(void*), PAGE_READWRITE, &oldProtect);
            *m_Instance = m_NewVTable;
            VirtualProtect(m_Instance, sizeof(void*), oldProtect, &oldProtect);
        }

        void Unhook() {
            if (!m_Instance || !m_OriginalVTable) return;
            DWORD oldProtect;
            VirtualProtect(m_Instance, sizeof(void*), PAGE_READWRITE, &oldProtect);
            *m_Instance = m_OriginalVTable;
            VirtualProtect(m_Instance, sizeof(void*), oldProtect, &oldProtect);
        }

        void** GetOriginalVTable() const {
            return m_OriginalVTable;
        }
    };

} // namespace FastHook
