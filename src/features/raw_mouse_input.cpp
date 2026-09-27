#include "stdafx.h"

#include "raw_mouse_input.hpp"

#include "common.hpp"
#include "config.hpp"
#include "gamevars.hpp"
#include "helper.hpp"
#include "input_handler.hpp"
#include "logging.hpp"
#include "mgs2_3rd_person_freecam.hpp"

namespace
{
    struct MouseStateLayout
    {
        ptrdiff_t iHalfWidth;
        ptrdiff_t iHalfHeight;
        ptrdiff_t iSensitivity;
        ptrdiff_t iMovementX;
        ptrdiff_t iMovementY;
        ptrdiff_t iMouseEnabled;
    };

    constexpr ptrdiff_t kAxisRange = 0x00;
    constexpr ptrdiff_t kAxisDeadzone = 0x08;
    constexpr ptrdiff_t kWindowHandle = 0x10;
    constexpr float kDirectCameraScale = 2.0f / 3.0f;
    constexpr MouseStateLayout kMGS2MouseStateLayout { 0x30, 0x34, 0x38, 0x60, 0x64, 0x68 };
    constexpr MouseStateLayout kMGS3MouseStateLayout { 0x20, 0x24, 0x28, 0x50, 0x54, 0x58 };

    const MouseStateLayout* pMouseStateLayout = nullptr;
    SafetyHookInline pad_read_act_mouse_hook {};
    SafetyHookInline PutCamera_hook {};
    SafetyHookInline SphericalCameraYaw_hook {};
    SafetyHookInline SphericalCameraHeight_hook {};
    bool bMGS3RawInputReady = false;
    constexpr float kMGS3AngleScale = 16.0f * kDirectCameraScale;
    constexpr float kMGS3HeightScale = 1.0f / 1200.0f;
    constexpr ptrdiff_t kSphericalYaw = 0x338;
    constexpr ptrdiff_t kSphericalYawVelocity = 0x33C;
    constexpr ptrdiff_t kSphericalHeight = 0x340;
    constexpr ptrdiff_t kSphericalHeightIndex = 0x344;
    constexpr ptrdiff_t kSphericalMinHeight = 0x98;
    constexpr ptrdiff_t kSphericalMaxHeight = 0x9C;
    struct RawCameraAxis
    {
        uint64_t iGeneration = 0;
        float fResidual = 0.0f;
        const void* pOwner = nullptr;
    };
    RawCameraAxis sMGS3YawAxis;
    RawCameraAxis sMGS3HeightAxis;
    POINT ptLatestRawDelta {};
    uint64_t iRawDeltaGeneration = 0;
    uint64_t iPutCameraGeneration = 0;
    float fCameraResidualX = 0.0f;
    float fCameraResidualY = 0.0f;
    const void* pResidualOwner = nullptr;
    float fResidualX = 0.0f;
    float fResidualY = 0.0f;

    template <typename T>
    T& Field(uint8_t* pBase, ptrdiff_t iOffset)
    {
        return *reinterpret_cast<T*>(pBase + iOffset);
    }

    uint32_t AbsAxis(int32_t value)
    {
        return value < 0
            ? static_cast<uint32_t>(-static_cast<int64_t>(value))
            : static_cast<uint32_t>(value);
    }

    int32_t ConvertRawAxis(LONG rawDelta, float sensitivityMultiplier, int32_t halfExtent, int32_t axisRange, float gameSensitivity, float& residual)
    {
        if (halfExtent == 0)
        {
            return 0;
        }

        const float scaledDeltaWithResidual = static_cast<float>(rawDelta) * gameSensitivity * sensitivityMultiplier + residual;
        const int32_t scaledDelta = static_cast<int32_t>(scaledDeltaWithResidual);
        residual = scaledDeltaWithResidual - static_cast<float>(scaledDelta);
        return static_cast<int32_t>((static_cast<float>(scaledDelta) / static_cast<float>(halfExtent)) * static_cast<float>(axisRange));
    }

    void ApplyAxisDeadzone(int32_t& movementX, int32_t& movementY, uint32_t deadzone)
    {
        const uint32_t absoluteX = AbsAxis(movementX);
        const uint32_t absoluteY = AbsAxis(movementY);

        if (absoluteX > deadzone)
        {
            if (absoluteY < deadzone)
            {
                movementY = 0;
            }
        }
        else if (absoluteX < deadzone && absoluteY > deadzone)
        {
            movementX = 0;
        }
    }

    int16_t NormalizeRotation(int32_t rotation)
    {
        rotation %= 4096;
        if (rotation > 2047)
        {
            rotation -= 4096;
        }
        else if (rotation < -2048)
        {
            rotation += 4096;
        }
        return static_cast<int16_t>(rotation);
    }

    int32_t ScaleCameraDelta(LONG rawDelta, float sensitivityMultiplier, float& residual)
    {
        const float scaledDelta = static_cast<float>(rawDelta) * kDirectCameraScale * sensitivityMultiplier + residual;
        const float integralDelta = std::trunc(scaledDelta);
        residual = scaledDelta - integralDelta;
        return static_cast<int32_t>(integralDelta);
    }

    void __fastcall PutCamera_hook_dest(uint8_t* pWork)
    {
        SVECTOR* cameraRotation = g_GameVars.gBP_3rdPersonCamera_Rot();
        const int16_t originalPitch = cameraRotation->x;
        const int16_t originalYaw = cameraRotation->y;

        PutCamera_hook.call<void>(pWork);

        if (*g_GameVars.gBP_3rdPersonCamera_Override() == 0 || iPutCameraGeneration == iRawDeltaGeneration || (ptLatestRawDelta.x == 0 && ptLatestRawDelta.y == 0))
        {
            return;
        }

        iPutCameraGeneration = iRawDeltaGeneration;
        const float fHorizontalDirection = RawMouseInput::bInvertThirdPersonX ? -1.0f : 1.0f;
        const float fVerticalDirection = RawMouseInput::bInvertThirdPersonY ? -1.0f : 1.0f;
        const int32_t yawDelta = ScaleCameraDelta(ptLatestRawDelta.x, RawMouseInput::fThirdPersonSensitivityX * fHorizontalDirection, fCameraResidualX);
        const int32_t pitchDelta = ScaleCameraDelta(ptLatestRawDelta.y, RawMouseInput::fThirdPersonSensitivityY * fVerticalDirection, fCameraResidualY);
        cameraRotation->x = std::clamp<int16_t>(NormalizeRotation(static_cast<int32_t>(originalPitch) + pitchDelta), -800, 800);
        cameraRotation->y = NormalizeRotation(static_cast<int32_t>(originalYaw) - yawDelta);
    }

    void __fastcall pad_read_act_mouse_hook_dest(uint8_t* pMouseState)
    {
        bMGS3RawInputReady = false;
        const HWND hWindow = Field<HWND>(pMouseState, kWindowHandle);
        const bool bRawInputReady = g_InputHandler.RegisterRawMouseInput(hWindow);
        pad_read_act_mouse_hook.call<void>(pMouseState);
        if (!bRawInputReady || pMouseStateLayout == nullptr)
        {
            return;
        }

        const POINT ptRawDelta = g_InputHandler.ConsumeRawMouseDelta();
        ptLatestRawDelta = ptRawDelta;
        ++iRawDeltaGeneration;
        if (!RawMouseInput::bEnabled)
        {
            return;
        }

        if (pResidualOwner != pMouseState)
        {
            pResidualOwner = pMouseState;
            fResidualX = 0.0f;
            fResidualY = 0.0f;
        }

        if (!Field<bool>(pMouseState, pMouseStateLayout->iMouseEnabled) || GetForegroundWindow() != hWindow)
        {
            fResidualX = 0.0f;
            fResidualY = 0.0f;
            return;
        }

        const int32_t iHalfWidth = Field<int32_t>(pMouseState, pMouseStateLayout->iHalfWidth);
        const int32_t iHalfHeight = Field<int32_t>(pMouseState, pMouseStateLayout->iHalfHeight);
        if (iHalfWidth == 0 || iHalfHeight == 0)
        {
            fResidualX = 0.0f;
            fResidualY = 0.0f;
            return;
        }

        const float fSensitivityX = bMouseSensitivity ? fMouseSensitivityXMulti : 1.0f;
        const float fSensitivityY = bMouseSensitivity ? fMouseSensitivityYMulti : 1.0f;
        const int32_t iAxisRange = Field<int32_t>(pMouseState, kAxisRange);
        bMGS3RawInputReady = (eGameType & MGS3) != 0;
        const float fGameSensitivity = Field<float>(pMouseState, pMouseStateLayout->iSensitivity);
        int32_t iMovementX = ConvertRawAxis(ptRawDelta.x, fSensitivityX, iHalfWidth, iAxisRange, fGameSensitivity, fResidualX);
        int32_t iMovementY = ConvertRawAxis(-ptRawDelta.y, fSensitivityY, iHalfHeight, iAxisRange, fGameSensitivity, fResidualY);
        ApplyAxisDeadzone(iMovementX, iMovementY, Field<uint32_t>(pMouseState, kAxisDeadzone));
        Field<int32_t>(pMouseState, pMouseStateLayout->iMovementX) = iMovementX;
        Field<int32_t>(pMouseState, pMouseStateLayout->iMovementY) = iMovementY;
    }

    bool UseMGS3RawCamera()
    {
        return bMGS3RawInputReady && RawMouseInput::bEnabled && (g_GameVars.IsMouseInputActive() || ptLatestRawDelta.x != 0 || ptLatestRawDelta.y != 0);
    }

    float ConsumeMGS3CameraAxis(RawCameraAxis& sAxis, const void* pOwner, LONG iDelta, float fScale)
    {
        if (sAxis.pOwner != pOwner || sAxis.iGeneration + 1 < iRawDeltaGeneration)
        {
            sAxis.fResidual = 0.0f;
            sAxis.pOwner = pOwner;
        }
        if (sAxis.iGeneration == iRawDeltaGeneration)
        {
            return 0.0f;
        }
        sAxis.iGeneration = iRawDeltaGeneration;
        return static_cast<float>(iDelta) * fScale;
    }

    int32_t ConsumeMGS3Angle(RawCameraAxis& sAxis, const void* pOwner, LONG iDelta, float fScale)
    {
        const float fValue = ConsumeMGS3CameraAxis(sAxis, pOwner, iDelta, fScale) + sAxis.fResidual;
        const float fIntegral = std::trunc(fValue);
        sAxis.fResidual = fValue - fIntegral;
        return static_cast<int32_t>(std::clamp(fIntegral, -32767.0f, 32767.0f));
    }

    int __fastcall SphericalCameraYaw_hook_dest(uint8_t* pWork)
    {
        if (!UseMGS3RawCamera())
        {
            sMGS3YawAxis.fResidual = 0.0f;
            return SphericalCameraYaw_hook.call<int>(pWork);
        }
        const float fHorizontalDirection = RawMouseInput::bInvertThirdPersonX ? -1.0f : 1.0f;
        const int iDelta = ConsumeMGS3Angle(sMGS3YawAxis, pWork, ptLatestRawDelta.x, -kMGS3AngleScale * RawMouseInput::fThirdPersonSensitivityX * fHorizontalDirection);
        Field<float>(pWork, kSphericalYawVelocity) = 0.0f;
        Field<uint16_t>(pWork, kSphericalYaw) = static_cast<uint16_t>(Field<uint16_t>(pWork, kSphericalYaw) + iDelta);
        return iDelta;
    }

    int __fastcall SphericalCameraHeight_hook_dest(uint8_t* pWork)
    {
        if (!UseMGS3RawCamera() || Field<int32_t>(pWork, 0x64) == 0)
        {
            return SphericalCameraHeight_hook.call<int>(pWork);
        }
        const float fVerticalDirection = RawMouseInput::bInvertThirdPersonY ? -1.0f : 1.0f;
        const float fDelta = ConsumeMGS3CameraAxis(sMGS3HeightAxis, pWork, ptLatestRawDelta.y, kMGS3HeightScale * RawMouseInput::fThirdPersonSensitivityY * fVerticalDirection);
        Field<float>(pWork, kSphericalHeight) = std::clamp(Field<float>(pWork, kSphericalHeight) + fDelta, Field<float>(pWork, kSphericalMinHeight), Field<float>(pWork, kSphericalMaxHeight));
        const int iHeightIndex = static_cast<int>(Field<float>(pWork, kSphericalHeight) * 3.0f);
        Field<int32_t>(pWork, kSphericalHeightIndex) = iHeightIndex;
        return iHeightIndex;
    }

}

void RawMouseInput::Initialize()
{
    if (!(eGameType & (MGS2 | MGS3)))
    {
        return;
    }

    if (eGameType & MGS2)
    {
        if (!MGS2_ThirdPersonFreecam::bEnabled || g_GameVars.gBP_3rdPersonCamera_Rot() == nullptr || g_GameVars.gBP_3rdPersonCamera_Override() == nullptr)
        {
            return;
        }

        pMouseStateLayout = &kMGS2MouseStateLayout;
        if (uint8_t* pad_read_act_mouse_Scan = Memory::PatternScan(baseModule, "40 53 48 83 EC ?? 80 79 ?? 00 48 8B D9 0F 84", "MGS 2: Raw Mouse Input: system\\libgv\\wpad.cpp | pad_read_act_mouse() | @ L759"))
        {
            pad_read_act_mouse_hook = safetyhook::create_inline(pad_read_act_mouse_Scan, pad_read_act_mouse_hook_dest);
            LOG_HOOK(pad_read_act_mouse_hook, "MGS 2: Raw Mouse Input: system\\libgv\\wpad.cpp | pad_read_act_mouse() | @ L759")
        }

        if (uint8_t* PutCamera_Scan = Memory::PatternScan(baseModule, "40 55 53 57 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 ?? 48 8B D9 B9", "MGS 2: Raw Mouse Input: user\\sonoyama\\raiden\\routine.c | PutCamera() | @ L685"))
        {
            PutCamera_hook = safetyhook::create_inline(PutCamera_Scan, PutCamera_hook_dest);
            LOG_HOOK(PutCamera_hook, "MGS 2: Raw Mouse Input: user\\sonoyama\\raiden\\routine.c | PutCamera() | @ L685")
        }
    }
    else if (eGameType & MGS3)
    {
        if (!bEnabled)
        {
            return;
        }

        pMouseStateLayout = &kMGS3MouseStateLayout;
        if (uint8_t* pad_read_act_mouse_Scan = Memory::PatternScan(baseModule, "40 53 48 83 EC ?? 80 79 ?? 00 48 8B D9 75", "MGS 3: Raw Mouse Input: system\\libgv\\wpad.cpp | pad_read_act_mouse() | roughly @ L759"))
        {
            pad_read_act_mouse_hook = safetyhook::create_inline(pad_read_act_mouse_Scan, pad_read_act_mouse_hook_dest);
            LOG_HOOK(pad_read_act_mouse_hook, "MGS 3: Raw Mouse Input: system\\libgv\\wpad.cpp | pad_read_act_mouse() | roughly @ L759")
        }
        if (!pad_read_act_mouse_hook)
        {
            return;
        }

        if (uint8_t* SphericalCameraYaw_Scan = Memory::PatternScan(baseModule, "40 53 48 83 EC ?? F3 0F 10 81 ?? ?? ?? ?? 48 8B D9 F3 0F 59 05", "MGS 3: Raw Mouse Input: NewSphericalCamera__Act() | yaw update"))
        {
            SphericalCameraYaw_hook = safetyhook::create_inline(SphericalCameraYaw_Scan, SphericalCameraYaw_hook_dest);
            LOG_HOOK(SphericalCameraYaw_hook, "MGS 3: Raw Mouse Input: NewSphericalCamera__Act() | yaw update")
        }
        if (uint8_t* SphericalCameraHeight_Scan = Memory::PatternScan(baseModule, "40 53 48 83 EC ?? 83 79 ?? 00 48 8B D9 74 ?? F3 0F 10 05", "MGS 3: Raw Mouse Input: NewSphericalCamera__Act() | height update"))
        {
            SphericalCameraHeight_hook = safetyhook::create_inline(SphericalCameraHeight_Scan, SphericalCameraHeight_hook_dest);
            LOG_HOOK(SphericalCameraHeight_hook, "MGS 3: Raw Mouse Input: NewSphericalCamera__Act() | height update")
        }
    }
}
