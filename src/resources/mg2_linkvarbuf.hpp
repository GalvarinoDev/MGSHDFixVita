// ReSharper disable CppClangTidyClangDiagnosticUniqueObjectDuplication
#pragma once

namespace MG2_LinkVarBuf
{
    struct PlayStats
    {
        int GM_PlayTime;
        int GM_RationUseCount;
        int GM_KillCount;
        int GM_AlertCount;
        int GM_SpecialItemUsed;
        int GM_SaveCount;
        int GM_ContinueCount;
    };

    static_assert(sizeof(PlayStats) == 0x1C, "PlayStats size");

    inline uintptr_t* stateSlot = nullptr;
    inline PlayStats* playStats = nullptr;

    template <typename T, uintptr_t Offset>
    struct StateVarValue
    {
        [[nodiscard]] static T* resolve()
        {
            if (!stateSlot || !*stateSlot) return nullptr;
            return reinterpret_cast<T*>(*stateSlot + Offset);
        }

        operator T () const
        {
            T* p = resolve();
            return p ? *p : T{};
        }

        StateVarValue& operator=(const T value)
        {
            if (T* p = resolve()) *p = value;
            return *this;
        }
    };

    inline StateVarValue<uint32_t, 0x88> GM_Difficulty;

    inline void Initialize()
    {
        HMODULE mg2Module = GetModuleHandleW(L"mg2.dll");
        if (!mg2Module)
        {
            spdlog::error("MG2_LinkVarBuf::Initialize: mg2.dll module handle not found - called too early?");
            return;
        }

        stateSlot = reinterpret_cast<uintptr_t*>(Memory::GetRelativeOffset(Memory::PatternScan(mg2Module, "48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 8B F8 E8 ?? ?? ?? ?? 48 8B C8", "MG2: stateSlot") + 3));

        playStats = reinterpret_cast<PlayStats*>(Memory::GetRelativeOffset(Memory::PatternScan(mg2Module, "F2 0F 10 0D", "MG2: playStats (GM_PlayTime)") + 11));

        spdlog::info("GameVars: MG2 stateSlot address is mg2.dll+{:X}", (uintptr_t)stateSlot - (uintptr_t)mg2Module);
        spdlog::info("GameVars: MG2 playStats address is mg2.dll+{:X}", (uintptr_t)playStats - (uintptr_t)mg2Module);
    }
}
