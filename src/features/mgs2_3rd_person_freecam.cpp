#include "stdafx.h"
#include "config_keys.hpp"

#include "mgs2_3rd_person_freecam.hpp"
#include "common.hpp"
#include "gamevars.hpp"
#include "input_handler.hpp"
#include "logging.hpp"
using namespace MGS2_StatusFlags;

namespace
{
    //TODO: 
    //      - disable camera angles when leaning against walls | partially done. see v3 known issues below
	// Make transition to static cam hold inverted input
	// Falling into ocean needs to force static

    float gCameraHSpeed = 0.0f;                      // what those settled on, to put back
    float gCameraVSpeed = 0.0f;

    bool bCameraForcedDisabled = false;
    bool bPreviousCameraState = false;

    // 3rd person camera overrides
    //FVECTOR gBP_3rdPersonCamera_Target         // Target position that camera looks at
    //FVECTOR gBP_3rdPersonCamera_Eye =      // Eye position where camera is placed
    //SVECTOR gBP_3rdPersonCamera_Rot =      // Rotation around player

    void ReleaseCameraState(bool enabled)
    {
        bCameraForcedDisabled = false;
        *g_GameVars.gBP_3rdPersonCamera_Override() = enabled;
    }

    void ForceCameraDisabled()
    {
        if (!bCameraForcedDisabled)
        {
            //spdlog::info("MGS2: Third Person Freecam: Forcing camera disabled. GM_Weapon value: {}, Get_GM_GameStatus value: {}", MGS2_LinkVarBuf::GM_Weapon.get(), g_GameVars.Get_GM_GameStatus());
            bCameraForcedDisabled = true;
            bPreviousCameraState = *g_GameVars.gBP_3rdPersonCamera_Override();
        }
        *g_GameVars.gBP_3rdPersonCamera_Override() = false;
    }

    void Toggle3rdPersonCamera()
    {
        if (bCameraForcedDisabled)
        {
            return;
        }
        *g_GameVars.gBP_3rdPersonCamera_Override() = !*g_GameVars.gBP_3rdPersonCamera_Override();
    }

    void IncreaseCameraDistance()
    {
        if (bCameraForcedDisabled) //don't increase while d-padding in menus and shit
        {
            return;
        }
        *g_GameVars.gBP_3rdPersonCamera_Dist() = std::min(*g_GameVars.gBP_3rdPersonCamera_Dist() + MGS2_ThirdPersonFreecam::iCameraDistanceStep, k3rdPersonMaxCameraDistance);
    }

    void DecreaseCameraDistance()
    {
        if (bCameraForcedDisabled) //don't decrease while d-padding in menus and shit
        {
            return;
        }
        *g_GameVars.gBP_3rdPersonCamera_Dist() = std::max(*g_GameVars.gBP_3rdPersonCamera_Dist() - MGS2_ThirdPersonFreecam::iCameraDistanceStep, k3rdPersonMinCameraDistance);
    }

    void ResetCameraDistance()
    {
        if (bCameraForcedDisabled)
        {
            return;
        }
        *g_GameVars.gBP_3rdPersonCamera_Dist() = MGS2_ThirdPersonFreecam::iMax_Camera_Distance;
        //spdlog::info("MGS2_LinkVarBuf::GM_PlayerPosX value: {}, MGS2_LinkVarBuf::GM_PlayerPosY value: {}, MGS2_LinkVarBuf::GM_PlayerPosZ value: {}", MGS2_LinkVarBuf::GM_PlayerPosX.get(), MGS2_LinkVarBuf::GM_PlayerPosY.get(), MGS2_LinkVarBuf::GM_PlayerPosZ.get());
    }

    /* v2
    safetyhook::InlineHook g_CheckBehindCamera_hook;
    safetyhook::InlineHook g_GM_ChangeCamera_hook;

    bool g_suppressBehindCameraChange = false;

    void __fastcall GM_ChangeCamera_hooked(int chanl) //straight returning CheckBehindCamera results in jumpout shots breaking
    {
        if (g_suppressBehindCameraChange)
        {
            return;
        }
        g_GM_ChangeCamera_hook.call<void>(chanl);
    }

    __int64 __fastcall CheckBehindCamera_hooked(__int64 a1) //fix wall hugging breaking third person cam
    {
        if (*g_GameVars.gBP_3rdPersonCamera_Override())
        {
            g_suppressBehindCameraChange = true;
        }
        __int64 result = g_CheckBehindCamera_hook.call<__int64>(a1);
        g_suppressBehindCameraChange = false;
        return result;
    }
    */

    // v3 - known issue: hug wall -> enter first person -> exit first person -> wall camera angle activates -> exit wall hug -> need to tap first person to reset camera angle.
    safetyhook::InlineHook g_GM_ChangeCamera_hook;
    safetyhook::InlineHook g_GM_SetCameraInterpMode_hook;
    safetyhook::InlineHook g_PL_LeaveSubject_hook;
    int g_leavingSubjectFrames = 0;

    bool ShouldSuppressCameraChange()
    {
        if (g_GameVars.InCutscene() || g_GameVars.InScriptedSequence())
        {
            return false;
        }
        return *g_GameVars.gBP_3rdPersonCamera_Override() && (g_leavingSubjectFrames <= 0) &&
            (!(g_GameVars.Get_PL_Status() & PLAYER_WATCH) &&
             (g_GameVars.Get_PL_Status() & (PLAYER_CAUTION | PLAYER_BEHIND)));
    }

    void __fastcall GM_ChangeCamera_hooked(int chanl)
    {
        if (ShouldSuppressCameraChange()) return;
        g_GM_ChangeCamera_hook.call<void>(chanl);
    }

    void __fastcall GM_SetCameraInterpMode_hooked(void* cam, int in, int out, int a, int b)
    {
        if (ShouldSuppressCameraChange()) return;
        g_GM_SetCameraInterpMode_hook.call<void>(cam, in, out, a, b);
    }

    void __fastcall PL_LeaveSubject_hooked(__int64 a1)
    {
        g_leavingSubjectFrames = 30;
        g_PL_LeaveSubject_hook.call<void>(a1);
    }

    bool isW45a = false;
    bool isMainGameOrAlternate = false;

    // bladeply.c slashes on any right stick deflection, so Triangle decides who the stick belongs to.
    constexpr ptrdiff_t kPlayerWorkPad = 0xD00;     // raiden\pl_inline.c -> PL_UseStickR()
    constexpr ptrdiff_t kPadPressure = 0x18;        // libgv.h GV_PAD.pressure[12]
    constexpr size_t kPadPressTriangle = 4;         // libgv.h PAD_PRESS_X

    safetyhook::InlineHook g_PL_UseStickR_hook;
    bool gBladeHasStick = false;

    bool BladeShouldShareStick()
    {
        return g_GameVars.gBP_3rdPersonCamera_Override() != nullptr && *g_GameVars.gBP_3rdPersonCamera_Override()
            && MGS2_LinkVarBuf::GM_Weapon == MGS2_WEAPON_INDEX_HIGH_FREQUENCY_BLADE;
    }

    int __fastcall PL_UseStickR_hooked(void* work)
    {
        const int used = g_PL_UseStickR_hook.fastcall<int>(work);
        if (work == nullptr || !BladeShouldShareStick())
        {
            gBladeHasStick = false;
            return used;
        }
        const uint8_t* pad = *reinterpret_cast<uint8_t* const*>(static_cast<uint8_t*>(work) + kPlayerWorkPad);
        gBladeHasStick = pad != nullptr && pad[kPadPressure + kPadPressTriangle] != 0;
        return gBladeHasStick ? used : 0;
    }

    // Freeze the camera's own read rather than the pad, so the blade still sees a live stick.
    void ApplyStickOwner()
    {
        static bool frozen = false;
        const bool freeze = gBladeHasStick;
        if (freeze == frozen || g_GameVars.gBP_3rdPersonCamera_HSpeed() == nullptr || g_GameVars.gBP_3rdPersonCamera_VSpeed() == nullptr)
        {
            return;
        }
        frozen = freeze;
        *g_GameVars.gBP_3rdPersonCamera_HSpeed() = freeze ? 0.0f : gCameraHSpeed;
        *g_GameVars.gBP_3rdPersonCamera_VSpeed() = freeze ? 0.0f : gCameraVSpeed;
    }
}

void MGS2_ThirdPersonFreecam::Tick()
{
    if (!bEnabled)
    {
        return;
    }
    if (g_GameVars.gBP_3rdPersonCamera_Override() == nullptr)
    {
        return;
    }

    if (g_leavingSubjectFrames > 0)
        g_leavingSubjectFrames--;

    if (g_GameVars.InCutscene() || g_GameVars.InScriptedSequence())
    {
        ForceCameraDisabled();
        return;
    }

    if (!isMainGameOrAlternate)
    {
        ForceCameraDisabled();
        return;
    }

    //if (Get_PL_Status() & (PLAYER_CAUTION|STATE_CUT_IN))

    // These drive a camera of their own, but only take the channel while the player is subjective.
    if ((MGS2_LinkVarBuf::GM_Weapon == MGS2_WEAPON_INDEX_HIGH_FREQUENCY_BLADE || MGS2_LinkVarBuf::GM_Weapon == MGS2_WEAPON_INDEX_COOLANT)
        && (g_GameVars.Get_PL_Status() & (PLAYER_WATCH | PLAYER_INTRUDE)))
    {
        ForceCameraDisabled();
        return;
    }

    if (!BladeShouldShareStick())
    {
        gBladeHasStick = false;      // the blade is away, so nothing polls it any more
    }
    ApplyStickOwner();


    if (isW45a)
    {
        const int playerPosX = MGS2_LinkVarBuf::GM_PlayerPosX;
        const int playerPosZ = MGS2_LinkVarBuf::GM_PlayerPosZ;
        if (playerPosX >= 1358 && playerPosX <= 3000 && playerPosZ >= -137100) //doorway to the room. freecam clips through the geometry pretty heavy when you enter.
        {
            ForceCameraDisabled();
            return;
        }
    }
    if (bCameraForcedDisabled)
    {
        ReleaseCameraState(bPreviousCameraState);
        return;
    }


}


void MGS2_ThirdPersonFreecam::HandleLevelTransition()
{
    if (!bEnabled)
    {
        return;
    }
    if (g_GameVars.gBP_3rdPersonCamera_Override() == nullptr)
    {
        return;
    }

    isMainGameOrAlternate = ((g_GameVars.MGS2_GetGameMode() == MGS2GameMode::Plant) || (g_GameVars.MGS2_GetGameMode() == MGS2GameMode::Tanker) || (g_GameVars.MGS2_GetGameMode() == MGS2GameMode::Alternate) || (g_GameVars.MGS2_GetGameMode() == MGS2GameMode::SnakeTales));

    isW45a = (g_GameVars.IsStage(MGS2Stages::W45A) || g_GameVars.IsStage(MGS2Stages::A45A));

}

void MGS2_ThirdPersonFreecam::Activate()
{
    if (!(eGameType & MGS2))
    {
        return;
    }

    if (!bEnabled)
    {
        spdlog::info("MGS2: Third Person Freecam: Config disabled, skipping.");
        return;
    }

    if (g_GameVars.gBP_3rdPersonCamera_Override() == nullptr)
    {
        spdlog::error("MGS2: Third Person Freecam: Failed to find g_BP3rdPersonCameraOverride.");
        return;
    }

    Toggle3rdPersonCamera();
    if (!*g_GameVars.gBP_3rdPersonCamera_Override())
    {
        spdlog::error("MGS2: Third Person Freecam: Failed to enable third person camera.");
        return;
    }
    g_InputHandler.RegisterHotkey(vkToggle_Camera, "Third Person Camera Toggle", []()
                                  {
                                      Toggle3rdPersonCamera();
                                  });

    if (g_GameVars.gBP_3rdPersonCamera_HSpeed() != nullptr)
    {
        const float fHorizontalSpeed = bInvertJoystickX ? -fHorizontal_Sensitivity : fHorizontal_Sensitivity;
        if (fHorizontal_Sensitivity != k3rdPersonFreecamDefaultHorizontalSensitivity || bInvertJoystickX)
        {
            *g_GameVars.gBP_3rdPersonCamera_HSpeed() = fHorizontalSpeed;
            spdlog::info("MGS2: Third Person Freecam: Set horizontal sensitivity to {}", fHorizontalSpeed);
        }
        gCameraHSpeed = *g_GameVars.gBP_3rdPersonCamera_HSpeed();
    }
    if (g_GameVars.gBP_3rdPersonCamera_VSpeed() != nullptr)
    {
        const float fVerticalSpeed = bInvertJoystickY ? -fVertical_Sensitivity : fVertical_Sensitivity;
        if (fVertical_Sensitivity != k3rdPersonFreecamDefaultVerticalSensitivity || bInvertJoystickY)
        {
            *g_GameVars.gBP_3rdPersonCamera_VSpeed() = fVerticalSpeed;
            spdlog::info("MGS2: Third Person Freecam: Set vertical sensitivity to {}", fVerticalSpeed);
        }
        gCameraVSpeed = *g_GameVars.gBP_3rdPersonCamera_VSpeed();
    }

    if (g_GameVars.gBP_3rdPersonCamera_Dist() != nullptr)
    {
        if (iMax_Camera_Distance != k3rdPersonFreecamDefaultMaxCameraDistance)
        {
            ResetCameraDistance();
        }
        
        g_InputHandler.RegisterHeldHotkey(vkToggle_Increase_Camera_Distance, "Third Person Camera - Increase Distance", []()
                                      {
                                          IncreaseCameraDistance();
                                      }, iCameraDistanceChangeSpeed);
        g_InputHandler.RegisterHeldHotkey(vkToggle_Decrease_Camera_Distance, "Third Person Camera - Decrease Distance", []()
                                      {
                                          DecreaseCameraDistance();
                                      }, iCameraDistanceChangeSpeed);
                                      
        g_InputHandler.RegisterHotkey(vkToggle_Reset_Camera_Distance, "Third Person Camera - Reset Distance", []()
                                      {
                                          ResetCameraDistance();
                                      });


    }


    
    if (g_GameVars.gBP_Camera_InheritRot() != nullptr)
    {
        *g_GameVars.gBP_Camera_InheritRot() = bInherit_Camera_Rotation;
        spdlog::info("MGS2: Third Person Freecam: Set inherit camera rotation to {}", *g_GameVars.gBP_Camera_InheritRot() ? "true" : "false");
        g_InputHandler.RegisterHotkey(vkToggle_Inherit_Camera_Rotation, "Third Person Camera - Inherit Rotation Toggle", []()
                                      {
                                          if (g_GameVars.gBP_Camera_InheritRot() != nullptr)
                                          {
                                              *g_GameVars.gBP_Camera_InheritRot() = !*g_GameVars.gBP_Camera_InheritRot();
                                          }
                                      });
    }

    //g_CheckBehindCamera_hook = safetyhook::create_inline( reinterpret_cast<void*>(Memory::PatternScan(baseModule, "40 55 53 57 41 56 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 ?? 4C 8B F1", "MGS 2: Third Person Freecam: CheckBehindCamera")),reinterpret_cast<void*>(CheckBehindCamera_hooked));
    g_GM_ChangeCamera_hook = safetyhook::create_inline(reinterpret_cast<void*>(Memory::GetRelativeOffset(Memory::PatternScan(baseModule, "E8 ?? ?? ?? ?? B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 B8", "MGS 2: Third Person Freecam: GM_ChangeCamera")+1)), reinterpret_cast<void*>(GM_ChangeCamera_hooked));
    g_GM_SetCameraInterpMode_hook = safetyhook::create_inline(reinterpret_cast<void*>(Memory::PatternScan(baseModule, "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 41 56 48 83 EC ?? 33 FF 4C 8D 35", "MGS 2: Third Person Freecam: GM_SetCameraInterpMode")), reinterpret_cast<void*>(GM_SetCameraInterpMode_hooked));
    g_PL_LeaveSubject_hook = safetyhook::create_inline(reinterpret_cast<void*>(Memory::PatternScan(baseModule, "40 53 48 83 EC ?? 83 3D ?? ?? ?? ?? 00 48 8B D9 74 ?? 0F BF 89", "MGS 2: Third Person Freecam: PL_LeaveSubject")), reinterpret_cast<void*>(PL_LeaveSubject_hooked));

    // Every blade poll of the right stick goes through here, so it is the one place to deny it.
    if (uint8_t* useStickR = Memory::PatternScan(baseModule, "48 8B 81 ?? ?? ?? ?? 0F BF 40", "MGS 2: Third Person Freecam: sonoyama\\raiden\\pl_inline.c -> PL_UseStickR()"))
    {
        g_PL_UseStickR_hook = safetyhook::create_inline(reinterpret_cast<void*>(useStickR),
            reinterpret_cast<void*>(PL_UseStickR_hooked));
        LOG_HOOK(g_PL_UseStickR_hook, "MGS 2: Third Person Freecam: PL_UseStickR")
    }
}

