// MGSHDFixVita: taiHEN plugin for the PS Vita MGS HD Collection.
// Port of the MGSHDFix caption fixes and MGS2 script patches by Lyall, ShizCalev and the MGSHDFix contributors (MIT).

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <taihen.h>

#include "game_strings.hpp"
#include <array>
#include <string_view>
#include "mgs2_status_flags.hpp"

// game_stages.hpp has a commented-out list line that ends in a backslash.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include "game_stages.hpp"
#pragma GCC diagnostic pop

#define LOG_DIR  "ux0:data/mgshdfixvita"
#define LOG_PATH LOG_DIR "/log.txt"
#define CONFIG_PATH LOG_DIR "/config.ini"

namespace
{
    // --- log

    void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

    void Log(const char* fmt, ...)
    {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        int len = sceClibVsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (len <= 0)
            return;
        if (len >= (int)sizeof(buf))
            len = sizeof(buf) - 1;

        SceUID fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
        if (fd < 0)
            return;
        sceIoWrite(fd, buf, len);
        sceIoClose(fd);
    }

    // --- config

    // Same section, key and value names as MGSHDFix.ini.
    struct Settings
    {
        bool skipStillman = false;
        bool skipOlgaTaunt = false;
        bool skipSwordTraining = false;
        bool restorePs2ChokeO2 = false;
        bool startInDevMenu = false;
        bool giveTestInventory = false;   // debug builds only
        bool fixIgtLoadingPause = true;
        bool keepSavedRadarType = true;
        bool fixPs2VisualEffects = true;
        bool restorePhoneJingle = true;
        bool restoreSolRadarRotation = false;
        bool snakeTalesRadar = false;
        bool useCustomLifebarName = false;
        char customLifebarName[16] = "LIFE";
        bool useCharacterLifebarNames = false;
        bool skipSplashscreens = false;
        bool restoreNodeScreen = true;
        bool restoreActionLevel = true;
        int sunglasses = 0;   // 0 = Normal, 1 = Always, 2 = Never
        int hostageType = 0;  // 0 = Normal, 1 = Kato-chan, 2 = Old Beauties, 3 = Jennifer
    };
    Settings g_Settings;

    const char kDefaultConfig[] =
        "; MGSHDFixVita settings. Change a value, then start the game again.\r\n"
        "; On/off values: true or false.\r\n"
        "\r\n"
        "[Skips]\r\n"
        "; MGS2: the two Peter Stillman cutscenes in the Shell 1 Core can be skipped in a New Game.\r\n"
        "Stillman = false\r\n"
        "; MGS2: Olga's two monologues during the fight can be skipped in a New Game.\r\n"
        "Olga Taunt = false\r\n"
        "; MGS2: removes the 45 second wait in Snake's sword training.\r\n"
        "Sword Training = false\r\n"
        "\r\n"
        "[Difficulty Restoration]\r\n"
        "; MGS2: Disabled, Life Reduction Only, Duration Increase Only or Full Restoration.\r\n"
        "; On Vita, all values except Disabled restore only the PS2 O2 gauge (drain 2, not 3; a press gives 1/17, not 1/19).\r\n"
        "Restore PS2 Solidus Choking Difficulty = Disabled\r\n"
        "\r\n"
        "[Bugfixes]\r\n"
        "; MGS2: loading a save keeps the radar setting from the save, also after you change options on the title screen.\r\n"
        "Keep Radar Type From Save = true\r\n"
        "; MGS2: PS2 timing for broken visual effects (codec CRT band).\r\n"
        "Fix Broken PS2 Visual Effects = true\r\n"
        "\r\n"
        "[Various]\r\n"
        "; MGS2: the phone rings with the MGS theme, as in the Japanese Sons of Liberty.\r\n"
        "Restore Japanese Phone Ringtone = true\r\n"
        "; MGS2: the Plant radar uses the Sons of Liberty rotation (east-west), not the Substance rotation.\r\n"
        "Restore SoL Radar Rotation = false\r\n"
        "; MGS2: the radar works in Snake Tales.\r\n"
        "Snake Tales Radar = false\r\n"
        "; MGS2: the life bar shows the name below instead of LIFE.\r\n"
        "Use Custom Lifebar Name = false\r\n"
        "Custom Lifebar Name = LIFE\r\n"
        "; MGS2: the life bar shows the character name (RAIDEN, SNAKE, ...) instead of LIFE.\r\n"
        "Use Character Names for Lifebar = false\r\n"
        "; MGS2: the name entry screen at the start of the Plant asks for sex, birth year and blood type again.\r\n"
        "Restore Node DoB && Bloodtype Entry = true\r\n"
        "; MGS2: New Game has \"1st Time\" again, with the PS2 action level questions before the difficulty.\r\n"
        "; Tanker, Plant and Tanker-Plant are in the menu after you clear Tanker or Plant one time.\r\n"
        "Restore Action Level Selection = true\r\n"
        "; MGS2: Normal = sunglasses only on the third and later games (each second clear). Always or Never.\r\n"
        "Force Sunglasses = Normal\r\n"
        "\r\n"
        "[Launcher and Splashscreens]\r\n"
        "; MGS2: skips the Konami, Kojima Productions, Armature and Bluepoint logos at start.\r\n"
        "Skip In-Game Splashscreens = false\r\n"
        "\r\n"
        "[Speedrunner Settings]\r\n"
        "; MGS2: the in-game timer stops during loading screens, as on PS2 (HD Collection bug).\r\n"
        "Fix In-Game Timer Loading Pause = true\r\n"
        "; MGS2: all Plant hostages (not Ames) are one type. Normal, Kato-chan, Old Beauties or Jennifer.\r\n"
        "Force RTC Hostage Type = Normal\r\n"
        "\r\n"
        "[Debugging]\r\n"
        "; MGS2: start the game in the developer menu (stage select), for tests.\r\n"
        "Start Game in Developer Menu = false\r\n";

    char ToLower(char c)
    {
        return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
    }

    bool IEquals(const char* a, const char* b)
    {
        while (*a && ToLower(*a) == ToLower(*b))
        {
            ++a;
            ++b;
        }
        return ToLower(*a) == ToLower(*b);
    }

    char* Trim(char* s)
    {
        while (*s == ' ' || *s == '\t')
            ++s;
        char* end = s;
        while (*end)
            ++end;
        while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
            *--end = 0;
        if (end - s >= 2 && *s == '"' && end[-1] == '"')
        {
            end[-1] = 0;
            ++s;
        }
        return s;
    }

    bool ParseBool(const char* value, bool& out)
    {
        static const char* const kTrue[] = { "1", "true", "yes", "on" };
        static const char* const kFalse[] = { "0", "false", "no", "off" };
        for (const char* t : kTrue)
            if (IEquals(value, t)) { out = true; return true; }
        for (const char* f : kFalse)
            if (IEquals(value, f)) { out = false; return true; }
        return false;
    }

    void ApplySetting(const char* section, const char* key, const char* value)
    {
        bool* flag = nullptr;
        if (IEquals(section, "Skips"))
        {
            if (IEquals(key, "Stillman")) flag = &g_Settings.skipStillman;
            else if (IEquals(key, "Olga Taunt")) flag = &g_Settings.skipOlgaTaunt;
            else if (IEquals(key, "Sword Training")) flag = &g_Settings.skipSwordTraining;
        }
        else if (IEquals(section, "Bugfixes"))
        {
            if (IEquals(key, "Keep Radar Type From Save")) flag = &g_Settings.keepSavedRadarType;
            else if (IEquals(key, "Fix Broken PS2 Visual Effects")) flag = &g_Settings.fixPs2VisualEffects;
        }
        else if (IEquals(section, "Various"))
        {
            if (IEquals(key, "Restore Japanese Phone Ringtone")) flag = &g_Settings.restorePhoneJingle;
            else if (IEquals(key, "Restore SoL Radar Rotation")) flag = &g_Settings.restoreSolRadarRotation;
            else if (IEquals(key, "Restore Node DoB && Bloodtype Entry")) flag = &g_Settings.restoreNodeScreen;
            else if (IEquals(key, "Restore Action Level Selection")) flag = &g_Settings.restoreActionLevel;
            else if (IEquals(key, "Snake Tales Radar")) flag = &g_Settings.snakeTalesRadar;
            else if (IEquals(key, "Use Custom Lifebar Name")) flag = &g_Settings.useCustomLifebarName;
            else if (IEquals(key, "Use Character Names for Lifebar")) flag = &g_Settings.useCharacterLifebarNames;
            else if (IEquals(key, "Custom Lifebar Name"))
            {
                sceClibStrncpy(g_Settings.customLifebarName, value, sizeof(g_Settings.customLifebarName) - 1);
                return;
            }
            else if (IEquals(key, "Force Sunglasses"))
            {
                if (IEquals(value, "Normal")) g_Settings.sunglasses = 0;
                else if (IEquals(value, "Always")) g_Settings.sunglasses = 1;
                else if (IEquals(value, "Never")) g_Settings.sunglasses = 2;
                else Log("config: bad value for %s: %s\n", key, value);
                return;
            }
        }
        else if (IEquals(section, "Launcher and Splashscreens"))
        {
            if (IEquals(key, "Skip In-Game Splashscreens")) flag = &g_Settings.skipSplashscreens;
        }
        else if (IEquals(section, "Speedrunner Settings"))
        {
            if (IEquals(key, "Fix In-Game Timer Loading Pause")) flag = &g_Settings.fixIgtLoadingPause;
            else if (IEquals(key, "Force RTC Hostage Type"))
            {
                if (IEquals(value, "Normal")) g_Settings.hostageType = 0;
                else if (IEquals(value, "Kato-chan")) g_Settings.hostageType = 1;
                else if (IEquals(value, "Old Beauties")) g_Settings.hostageType = 2;
                else if (IEquals(value, "Jennifer")) g_Settings.hostageType = 3;
                else Log("config: bad value for %s: %s\n", key, value);
                return;
            }
        }
        else if (IEquals(section, "Debugging"))
        {
            if (IEquals(key, "Start Game in Developer Menu")) flag = &g_Settings.startInDevMenu;
            else if (IEquals(key, "Give Test Inventory")) flag = &g_Settings.giveTestInventory;
        }
        else if (IEquals(section, "Difficulty Restoration") && IEquals(key, "Restore PS2 Solidus Choking Difficulty"))
        {
            if (IEquals(value, "Disabled"))
                g_Settings.restorePs2ChokeO2 = false;
            else if (IEquals(value, "Life Reduction Only") || IEquals(value, "Duration Increase Only") || IEquals(value, "Full Restoration"))
                g_Settings.restorePs2ChokeO2 = true;
            else
                Log("config: bad value for %s: %s\n", key, value);
            return;
        }

        if (flag && !ParseBool(value, *flag))
            Log("config: bad value for %s: %s\n", key, value);
    }

    void LoadConfig()
    {
        static char buf[4096];
        SceUID fd = sceIoOpen(CONFIG_PATH, SCE_O_RDONLY, 0);
        if (fd < 0)
        {
            fd = sceIoOpen(CONFIG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
            if (fd >= 0)
            {
                sceIoWrite(fd, kDefaultConfig, sizeof(kDefaultConfig) - 1);
                sceIoClose(fd);
                Log("config: wrote default %s\n", CONFIG_PATH);
            }
            return;
        }
        int n = sceIoRead(fd, buf, sizeof(buf) - 1);
        sceIoClose(fd);
        buf[n > 0 ? n : 0] = 0;

        char section[64];
        section[0] = 0;
        for (char* line = buf; line && *line; )
        {
            char* next = line;
            while (*next && *next != '\n')
                ++next;
            if (*next)
                *next++ = 0;
            else
                next = nullptr;

            char* t = Trim(line);
            if (*t == '[')
            {
                char* end = t;
                while (*end && *end != ']')
                    ++end;
                *end = 0;
                sceClibStrncpy(section, Trim(t + 1), sizeof(section) - 1);
            }
            else if (*t && *t != ';' && *t != '#')
            {
                char* eq = t;
                while (*eq && *eq != '=')
                    ++eq;
                if (*eq)
                {
                    *eq = 0;
                    ApplySetting(section, Trim(t), Trim(eq + 1));
                }
            }
            line = next;
        }

        Log("config: Stillman %d, Olga Taunt %d, Sword Training %d, PS2 choke O2 %d, IGT loading pause %d, keep radar %d, PS2 visual effects %d\n",
            g_Settings.skipStillman, g_Settings.skipOlgaTaunt, g_Settings.skipSwordTraining, g_Settings.restorePs2ChokeO2,
            g_Settings.fixIgtLoadingPause, g_Settings.keepSavedRadarType, g_Settings.fixPs2VisualEffects);
        // Log has a 256 byte buffer, so the settings are on two lines.
        Log("config: phone ringtone %d, SoL radar %d, Snake Tales radar %d, lifebar custom %d '%s' characters %d, skip splash %d, sunglasses %d, hostages %d, developer menu %d\n",
            g_Settings.restorePhoneJingle, g_Settings.restoreSolRadarRotation, g_Settings.snakeTalesRadar, g_Settings.useCustomLifebarName,
            g_Settings.customLifebarName, g_Settings.useCharacterLifebarNames, g_Settings.skipSplashscreens, g_Settings.sunglasses, g_Settings.hostageType, g_Settings.startInDevMenu);
    }

    // --- hooks

    // TAI_CONTINUE casts to "type(*)()", which takes no arguments in C++.
    template <typename R, typename... Args>
    R CallNext(tai_hook_ref_t ref, Args... args)
    {
        using Fn = R (*)(Args...);
        _tai_hook_user* cur = (_tai_hook_user*)ref;
        _tai_hook_user* next = (_tai_hook_user*)cur->next;
        return next ? ((Fn)next->func)(args...) : ((Fn)cur->old)(args...);
    }


#ifdef MGSHDFIX_DEBUG_STRINGS
    using GmSdSetFn = void (*)(int);
    GmSdSetFn g_GmSdSet = nullptr;
    volatile int g_RingtoneRequest = 0;
#endif

    void UpdateSnakeTalesRadar(const char* stage);

    // --- pattern scan

    // Pattern text: hex bytes separated by spaces, "??" is a wildcard.
    struct Pattern
    {
        const char* name;
        const char* text;
    };

    int HexValue(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }

    // Returns the number of bytes, or -1 if the text is not valid. mask[i] = 0 for a wildcard.
    int ParsePattern(const char* text, uint8_t* bytes, uint8_t* mask, int max)
    {
        int n = 0;
        for (const char* p = text; *p; )
        {
            if (*p == ' ') { ++p; continue; }
            if (n >= max) return -1;
            if (p[0] == '?' && p[1] == '?')
            {
                bytes[n] = 0;
                mask[n] = 0;
            }
            else
            {
                int hi = HexValue(p[0]);
                int lo = HexValue(p[1]);
                if (hi < 0 || lo < 0) return -1;
                bytes[n] = (uint8_t)(hi << 4 | lo);
                mask[n] = 1;
            }
            ++n;
            p += 2;
        }
        return n;
    }

    // Returns the address of the only match, or 0 if there is no match or more than one.
    uintptr_t FindUnique(uintptr_t start, size_t size, const Pattern& pattern)
    {
        uint8_t bytes[128];
        uint8_t mask[128];
        int n = ParsePattern(pattern.text, bytes, mask, sizeof(bytes));
        if (n <= 0 || (size_t)n > size)
        {
            Log("pattern %s: bad pattern text\n", pattern.name);
            return 0;
        }

        uintptr_t found = 0;
        int hits = 0;
        // Thumb code is 2-byte aligned.
        for (size_t i = 0; i + n <= size; i += 2)
        {
            const uint8_t* p = (const uint8_t*)(start + i);
            int j = 0;
            while (j < n && (!mask[j] || p[j] == bytes[j]))
                ++j;
            if (j == n)
            {
                found = start + i;
                ++hits;
            }
        }

        if (hits != 1)
        {
            Log("pattern %s: %d matches\n", pattern.name, hits);
            return 0;
        }
        return found;
    }

    // --- MGS2 patterns, checked on PCSE00020 and PCSE00078

    const Pattern kGclLoadScript =
    {
        "GCL_LoadScript",
        "2D E9 F0 41 03 1C 9C 46 9C F8 02 00 00 04 9C F8 03 10 61 F3 1F 60 9C F8 01 10 61 F3 0F 20 9C F8 00 10 "
        "?? ?? ?? ?? ?? ?? ?? ?? 08 43 11 46 88 42"
    };

    const Pattern kBpLookupStringOveride =
    {
        "BP_LookupStringOveride",
        "70 B5 00 24 22 1C ?? ?? ?? ?? 13 1C ?? ?? ?? ?? ?? ?? ?? ?? 0D 68 85 42 06 D0 01 34 08 31 08 33 64 45 F7 D3 10 1C 70 BD"
    };

    const Pattern kBpGetOverrideString =
    {
        "BP_GetOverrideString",
        "10 B5 04 1C 38 B1 20 1C ?? ?? ?? ?? ?? ?? ?? ?? 00 28 00 D0 10 BD 20 1C 10 BD"
    };

    // Returns the English stage name for an index. The names are a table of string pointers in the
    // executable, so the pause menu and the area name banner do not use BP_LookupStringOveride.
    const Pattern kGetStageName =
    {
        "GetStageName",
        "3E 28 04 D3 ?? ?? ?? ?? ?? ?? ?? ?? 70 47 ?? ?? ?? ?? ?? ?? ?? ?? 09 68 80 00 B1 F9 14 10 07 29 05 D1 "
        "?? ?? ?? ?? ?? ?? ?? ?? 40 58 04 E0 ?? ?? ?? ?? ?? ?? ?? ?? 40 58 70 47"
    };
    const uint32_t kGetStageNameTableMovw = 0x2e;
    const uint32_t kStageNameCount = 62;

    // Thumb-2 MOVW/MOVT immediate.
    uint16_t ThumbMovImm(uintptr_t insn)
    {
        const uint16_t* hw = (const uint16_t*)insn;
        return (uint16_t)((hw[0] & 0xF) << 12 | ((hw[0] >> 10) & 1) << 11 | ((hw[1] >> 12) & 7) << 8 | (hw[1] & 0xFF));
    }

    // Thumb-2 BL target.
    uintptr_t ThumbBlTarget(uintptr_t insn)
    {
        const uint16_t* hw = (const uint16_t*)insn;
        uint32_t s = (hw[0] >> 10) & 1;
        uint32_t i1 = 1 - (((hw[1] >> 13) & 1) ^ s);
        uint32_t i2 = 1 - (((hw[1] >> 11) & 1) ^ s);
        int32_t imm = (int32_t)(s << 24 | i1 << 23 | i2 << 22 | (hw[0] & 0x3FF) << 12 | (hw[1] & 0x7FF) << 1);
        if (s)
            imm -= 1 << 25;
        return insn + 4 + imm;
    }

    // Same CRC32 as CCRC::CalculateCRC32AsString in the game (zlib polynomial).
    uint32_t Crc32(const char* str)
    {
        uint32_t crc = 0xFFFFFFFF;
        for (const uint8_t* p = (const uint8_t*)str; *p; ++p)
        {
            crc ^= *p;
            for (int k = 0; k < 8; ++k)
                crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
        }
        return ~crc;
    }

    // --- caption fixes

    tai_hook_ref_t g_LookupRef;
    SceUID g_LookupHook = -1;
    SceUID g_StageNameInject = -1;

    bool IsForceOriginal(uint32_t hash)
    {
        for (uint32_t h : kMGS2ForceOriginalHashes)
            if (h == hash)
                return true;
        return false;
    }

    const char* FindTypoFix(uint32_t hash)
    {
        for (const CaptionOverride& fix : kMGS2CaptionTypoFixes)
            if (fix.ps2_crc32 == hash)
                return fix.replacement;
        // Upstream keeps the MGS2 Harrier fixes in the MGS3 table. No MGS3 hash occurs in the MGS2 data.
        for (const CaptionOverride& fix : kMGS3CaptionTypoFixes)
            if (fix.ps2_crc32 == hash)
                return fix.replacement;
        return nullptr;
    }

#ifdef MGSHDFIX_DEBUG_STRINGS
    // Logs each hash one time, to find which text goes through the lookup.
    uint32_t g_SeenHashes[8192];

    void LogNewHash(uint32_t hash)
    {
        if (hash == 0)
            return;
        uint32_t i = hash & 8191;
        for (uint32_t n = 0; n < 8192; ++n, i = (i + 1) & 8191)
        {
            if (g_SeenHashes[i] == hash)
                return;
            if (g_SeenHashes[i] == 0)
            {
                g_SeenHashes[i] = hash;
                Log("hash %08x\n", (unsigned)hash);
                return;
            }
        }
    }
#endif

    // Same order as MGSHDFix: force original first, then typo fixes, then the Bluepoint table.
    const char* BP_LookupStringOveride_hk(uint32_t hash)
    {
#ifdef MGSHDFIX_DEBUG_STRINGS
        LogNewHash(hash);
#endif
        if (IsForceOriginal(hash))
            return nullptr;

        if (const char* fix = FindTypoFix(hash))
            return fix;

        return CallNext<const char*>(g_LookupRef, hash);
    }
}

// --- MGS2 script patches

namespace
{
    // GM_SetArea copies the area name to linkvarbuf. The movw/movt at +0x0a load the address of the
    // linkvarbuf pointer. Offsets in linkvarbuf are from linkvar.h.
    const Pattern kGmSetArea =
    {
        "GM_SetArea",
        "10 B5 0C 1C 20 1C ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 13 68 21 1C C3 F8 BC 00 10 68 2C 30 "
        "?? ?? ?? ?? ?? ?? ?? ?? 20 1C ?? ?? ?? ?? ?? ?? ?? ?? 00 28 01 D1"
    };
    const uint32_t kGmSetAreaLinkVarMovw = 0x0a;
    const uint32_t kLinkVarConfiguration = 6;   // GM_Configuration (short)
    const uint32_t kLinkVarResult = 18;     // GM_Result (short)
    const uint32_t kLinkVarAreaName = 44;   // GM_SaveAreaDir
    const uint32_t kLinkVarSaveArea = 188;  // GM_SaveArea (int)

    // Same patches as MGSHDFix (mgs2_difficulty.cpp, mgs2_ng_cutscene_skips.cpp).
    struct ScriptPatch
    {
        const char* stage;
        const bool* enabled;
        uint8_t find[16];
        uint8_t findLen;
        uint8_t at;
        uint8_t put[4];
        uint8_t putLen;
    };

    const ScriptPatch kScriptPatches[] =
    {
        // The choke's O2 gauge: HD drains 3 a frame and a press gives back 1/19; PS2 drains 2 and gives back 1/17.
        { "w51a", &g_Settings.restorePs2ChokeO2, { 0x3C, 0x11, 0x80, 0x00, 0xFE, 0x11, 0x80, 0x00, 0xFE, 0xC4, 0xA5, 0xB6, 0xA0 }, 13, 9, { 0xC3 }, 1 },
        { "w51a", &g_Settings.restorePs2ChokeO2, { 0x37, 0x11, 0x80, 0x01, 0x00, 0xD4, 0xA7, 0xA0 }, 8, 5, { 0xD2 }, 1 },
        // Each script asks "seen it before?" before it adds the skip button. These make the answer yes.
        { "w16a", &g_Settings.skipStillman, { 0x37, 0x11, 0x80, 0x00, 0x04, 0xC1, 0xAC, 0xA0 }, 8, 6, { 0xB0 }, 1 },
        { "w16b", &g_Settings.skipStillman, { 0x37, 0x11, 0x80, 0x00, 0x04, 0xC1, 0xAC, 0xA0 }, 8, 6, { 0xB0 }, 1 },
        { "w00b", &g_Settings.skipOlgaTaunt, { 0x35, 0x14, 0x04, 0x03, 0x6D, 0xA0 }, 6, 1, { 0xC2, 0xA0 }, 2 },
        { "w00b", &g_Settings.skipOlgaTaunt, { 0x35, 0x14, 0x07, 0x0C, 0x40, 0xA0 }, 6, 1, { 0xC2, 0xA0 }, 2 },
        // The 45 second wait after the first swing becomes one frame.
        { "w43a", &g_Settings.skipSwordTraining, { 0x54, 0x74, 0x01, 0x8C, 0x0A, 0x5D }, 6, 3, { 0x01, 0x00 }, 2 },
        // The questionnaire's start voice (vc126101.sdt in vox2) is 0 in the HD data. The end voice entry follows it.
        { "n_title", &g_Settings.restoreActionLevel, { 0x00, 0x00, 0x00, 0x00, 0x90, 0x48, 0x01, 0x21 }, 8, 0, { 0x7F, 0x49, 0x01, 0x21 }, 4 },
    };

    // MGSHDFix scans 1 MB from the script start. The same code is also in boss.gcx, which can be in memory
    // after the script, so here the scan stops at the script end from its header (see GCL_LoadScript).
    uint32_t ReadLE32(const uint8_t* p)
    {
        return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
    }

    // Returns the script length, or 0 if the header is not valid inside "size" bytes.
    size_t ScriptLength(const uint8_t* script, size_t size)
    {
        size_t off = 8;   // 'BGCL', variable version
        if (size < off || ReadLE32(script) != 0x4247434C)
            return 0;
        while (off + 4 <= size && ReadLE32(script + off) != 0)
            off += 8;     // proc table: id, offset
        off += 8;
        if (off + 4 > size)
            return 0;
        off += ReadLE32(script + off);   // string resource block
        if (off + 4 > size)
            return 0;
        off += 4 + ReadLE32(script + off);   // proc bodies
        if (off + 4 > size)
            return 0;
        off += 4 + ReadLE32(script + off);   // script body
        return off <= size ? off : 0;
    }

    tai_hook_ref_t g_LoadScriptRef;
    SceUID g_LoadScriptHook = -1;
    uintptr_t g_LinkVarBufPtr = 0;
    uintptr_t g_GmSetAreaAddr = 0;

    bool ResolveLinkVarBuf(uintptr_t text, size_t textSize)
    {
        g_GmSetAreaAddr = FindUnique(text, textSize, kGmSetArea);
        if (!g_GmSetAreaAddr)
            return false;
        uintptr_t movw = g_GmSetAreaAddr + kGmSetAreaLinkVarMovw;
        g_LinkVarBufPtr = (uintptr_t)ThumbMovImm(movw + 4) << 16 | ThumbMovImm(movw);
        return true;
    }

    uintptr_t LinkVarBuf()
    {
        return *(const uintptr_t*)g_LinkVarBufPtr;
    }

    const char* CurrentStage()
    {
        uintptr_t linkvar = LinkVarBuf();
        return linkvar ? (const char*)(linkvar + kLinkVarAreaName) : nullptr;
    }

    // --- developer menu (MGSHDFix game_funcs.cpp, EnterDeveloperMenu)

    // Sets GM_LoadRequest to 1, then calls GM_SetArea. Used only for the GM_LoadRequest address.
    const Pattern kGmLoadRequestUser =
    {
        "GM_LoadRequest",
        "70 B5 ?? ?? ?? ?? ?? ?? ?? ?? 15 1C ?? ?? ?? ?? 01 23 0B 60 ?? ?? ?? ?? 29 68 04 1C D1 F8 BC 60 11 F1 2C 00"
    };
    const uint32_t kGmLoadRequestMovw = 0x06;
    const uint32_t kGmLoadRequestMovt = 0x0c;

    // GCL_InitFunc loads the script when its ID matches scenerio_code (GCL_ChangeSenerioCode sets it).
    const Pattern kGclInitFunc =
    {
        "GCL_InitFunc",
        "10 B5 ?? ?? ?? ?? ?? ?? ?? ?? 12 68 C1 F3 17 01 91 42 01 D1 ?? ?? ?? ?? 01 20 10 BD"
    };
    const uint32_t kGclInitFuncMovw = 0x02;
    const uint32_t kStrCodeScenerio = 0x180720;   // GV_StrCode("scenerio")

    using GmSetAreaFn = void (*)(int, const char*);
    GmSetAreaFn g_GmSetArea = nullptr;
    int* g_GmLoadRequest = nullptr;
    int* g_ScenerioCode = nullptr;
    bool g_DevMenuEntered = false;

    void EnterDeveloperMenu()
    {
        uintptr_t linkvar = LinkVarBuf();
        g_GmSetArea(*(const int*)(linkvar + kLinkVarSaveArea), "select");
        *g_ScenerioCode = kStrCodeScenerio;
        *(short*)(linkvar + kLinkVarResult) = 9999;
        *g_GmLoadRequest = 0x0002 | 0x1;
        Log("developer menu: load requested\n");
    }

    void ResolveDeveloperMenu(uintptr_t text, size_t textSize, uintptr_t setArea)
    {
        uintptr_t loadRequestUser = FindUnique(text, textSize, kGmLoadRequestUser);
        uintptr_t initFunc = FindUnique(text, textSize, kGclInitFunc);
        if (!loadRequestUser || !initFunc)
            return;
        g_GmSetArea = (GmSetAreaFn)(setArea | 1);
        g_GmLoadRequest = (int*)((uintptr_t)ThumbMovImm(loadRequestUser + kGmLoadRequestMovt) << 16 | ThumbMovImm(loadRequestUser + kGmLoadRequestMovw));
        g_ScenerioCode = (int*)((uintptr_t)ThumbMovImm(initFunc + kGclInitFuncMovw + 4) << 16 | ThumbMovImm(initFunc + kGclInitFuncMovw));
        Log("developer menu: GM_LoadRequest %08x, scenerio_code %08x\n", (unsigned)(uintptr_t)g_GmLoadRequest, (unsigned)(uintptr_t)g_ScenerioCode);
    }

    int GCL_LoadScript_hk(char* datatop)
    {
        int result = CallNext<int>(g_LoadScriptRef, datatop);
        const char* stage = CurrentStage();
        if (!datatop || !stage)
            return result;

#ifdef MGSHDFIX_DEBUG_STRINGS
        Log("script load: %s at %08x\n", stage, (unsigned)(uintptr_t)datatop);
#endif

#ifdef MGSHDFIX_DEBUG_STRINGS
        if (g_GmSdSet && g_RingtoneRequest)
        {
            g_RingtoneRequest = 0;
            g_GmSdSet(0xA0);
            Log("phone ringtone: played SD_S_PHONEMEL in %s\n", stage);
        }
#endif

        UpdateSnakeTalesRadar(stage);

        // The first title script load is the game start.
        if (g_GmLoadRequest && !g_DevMenuEntered && IEquals(stage, "n_title"))
        {
            g_DevMenuEntered = true;
            EnterDeveloperMenu();
            return result;
        }

        const ScriptPatch* wanted[sizeof(kScriptPatches) / sizeof(kScriptPatches[0])];
        int count = 0;
        size_t longest = 0;
        for (const ScriptPatch& patch : kScriptPatches)
        {
            if (*patch.enabled && IEquals(stage, patch.stage))
            {
                wanted[count++] = &patch;
                if (patch.findLen > longest)
                    longest = patch.findLen;
            }
        }
        if (count == 0)
            return result;

        SceKernelMemBlockInfo block;
        block.size = sizeof(block);
        if (sceKernelGetMemBlockInfoByAddr(datatop, &block) < 0)
        {
            Log("script patches: %s, no memory block info\n", stage);
            return result;
        }
        uint8_t* script = (uint8_t*)datatop;
        size_t size = ScriptLength(script, (uintptr_t)block.mappedBase + block.mappedSize - (uintptr_t)script);
        if (size == 0)
        {
            Log("script patches: %s, script header not valid\n", stage);
            return result;
        }

        int patched = 0;
        for (size_t off = 0; off + longest <= size; ++off)
        {
            for (int i = 0; i < count; ++i)
            {
                const ScriptPatch* patch = wanted[i];
                if (script[off] == patch->find[0] && sceClibMemcmp(script + off, patch->find, patch->findLen) == 0)
                {
                    sceClibMemcpy(script + off + patch->at, patch->put, patch->putLen);
                    ++patched;
                    break;
                }
            }
        }
        // Codec calls also load scripts through here, so most loads change nothing.
#ifndef MGSHDFIX_DEBUG_STRINGS
        if (patched)
#endif
            Log("script patches: %s, %d change(s)\n", stage, patched);
        return result;
    }

    bool AnyScriptPatchEnabled()
    {
        for (const ScriptPatch& patch : kScriptPatches)
            if (*patch.enabled)
                return true;
        return false;
    }

    void InstallScriptPatches(SceUID modid, uintptr_t text, size_t textSize, uintptr_t loadScript)
    {
#ifdef MGSHDFIX_DEBUG_STRINGS
        const bool logLoads = true;
#else
        const bool logLoads = false;
#endif
        // Debug builds hook it always, to log each script load.
        if (!AnyScriptPatchEnabled() && !g_Settings.startInDevMenu && !g_Settings.snakeTalesRadar && !logLoads)
            return;
        if (!g_LinkVarBufPtr)
            return;
        if (g_Settings.startInDevMenu)
            ResolveDeveloperMenu(text, textSize, g_GmSetAreaAddr);

        g_LoadScriptHook = taiHookFunctionOffset(&g_LoadScriptRef, modid, 0, loadScript - text, 1, (const void*)GCL_LoadScript_hk);
        if (g_LoadScriptHook < 0)
            Log("GCL_LoadScript hook: error %08x\n", (unsigned)g_LoadScriptHook);
        else
            Log("script patches: hook installed\n");
    }
}

// --- in-game timer (MGSHDFix playtime_fixes.cpp)

namespace
{
    // GM_StartLoader in game/loader.c. The game clears GM_CONFIG_PLAYTIME_STOP when a load is done, but the
    // HD Collection never sets it when a load starts, so the timer counts during loading screens.
    const Pattern kGmStartLoader =
    {
        "GM_StartLoader",
        "70 B5 04 1C 02 20 5F F4 00 51 B0 22 00 23 ?? ?? ?? ?? 05 1C 20 1C ?? ?? ?? ?? 28 62 ?? ?? ?? ?? ?? ?? ?? ?? "
        "28 1C ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 00 26 6E 62 01 20 A8 62 15 F1 30 00 21 1C ?? ?? ?? ?? 01 20 01 21"
    };
    const uint16_t kGmConfigPlaytimeStop = 0x4000;

    tai_hook_ref_t g_StartLoaderRef;
    SceUID g_StartLoaderHook = -1;

    void* GM_StartLoader_hk(char* dir)
    {
        void* work = CallNext<void*>(g_StartLoaderRef, dir);
        uintptr_t linkvar = LinkVarBuf();
        if (linkvar)
        {
            uint16_t* config = (uint16_t*)(linkvar + kLinkVarConfiguration);
#ifdef MGSHDFIX_DEBUG_STRINGS
            Log("IGT: load %s, stop flag before %d\n", dir ? dir : "?", (*config & kGmConfigPlaytimeStop) != 0);
#endif
            *config |= kGmConfigPlaytimeStop;
        }
        return work;
    }

    void InstallIgtFix(SceUID modid, uintptr_t text, size_t textSize)
    {
        if (!g_Settings.fixIgtLoadingPause || !g_LinkVarBufPtr)
            return;
        uintptr_t startLoader = FindUnique(text, textSize, kGmStartLoader);
        if (!startLoader)
            return;
        g_StartLoaderHook = taiHookFunctionOffset(&g_StartLoaderRef, modid, 0, startLoader - text, 1, (const void*)GM_StartLoader_hk);
        if (g_StartLoaderHook < 0)
            Log("GM_StartLoader hook: error %08x\n", (unsigned)g_StartLoaderHook);
        else
            Log("IGT loading pause: hook installed\n");
    }
}

// --- radar type from save (MGSHDFix mgs2_radar_fixes.cpp, bKeepSavedRadarType)

namespace
{
    // ComStoreLoadedVariable in kano/mcman/mcdata.c. After an options change on the title screen it keeps the
    // current GM_Configuration and takes only bits 0x3800 from the save. MGSHDFix adds the radar bits to the
    // mask. Here the compiler used one bfi for bits 11-13, so a hook puts the radar bits back afterwards.
    const Pattern kComStoreLoadedVariable =
    {
        "ComStoreLoadedVariable",
        "2D E9 F0 47 ?? ?? ?? ?? ?? ?? ?? ?? DA F8 00 90 09 F1 8A 00 B9 F9 06 80 10 F5 A8 54 D9 F8 08 70 B9 F9 18 60 "
        "B9 F9 1A 50 B4 F9 00 40 B9 F9 14 90 ?? ?? ?? ?? DA F8 00 00 14 F4 80 71 16 D0 B0 F9 06 10 4F EA F1 21 61 F3 CD 28"
    };
    const uint32_t kComStoreRestoreVarCall = 0x30;   // bl GCL_RestoreVar
    const uint32_t kRestoreVarSourceMovw = 0x06;     // movw/movt of the sv_linkvarbuf pointer (memcpy source)
    const uint32_t kRestoreVarSourceMovt = 0x10;
    const uint32_t kLinkVarTitleMenuStatus = 0x158a;
    const uint16_t kTitleMenuOptionChanged = 0x0100;
    const uint16_t kGmConfigRadarBits = 0x0004 | 0x0020;   // radar off, radar type 2

    tai_hook_ref_t g_ComStoreRef;
    SceUID g_ComStoreHook = -1;
    uintptr_t g_SvLinkVarBufPtr = 0;

    int ComStoreLoadedVariable_hk()
    {
        uintptr_t linkvar = LinkVarBuf();
        bool optionChanged = linkvar && (*(const uint16_t*)(linkvar + kLinkVarTitleMenuStatus) & kTitleMenuOptionChanged);
        int result = CallNext<int>(g_ComStoreRef);
        linkvar = LinkVarBuf();
        uintptr_t saved = *(const uintptr_t*)g_SvLinkVarBufPtr;
        if (optionChanged && linkvar && saved)
        {
            uint16_t* config = (uint16_t*)(linkvar + kLinkVarConfiguration);
            uint16_t fromSave = *(const uint16_t*)(saved + kLinkVarConfiguration) & kGmConfigRadarBits;
#ifdef MGSHDFIX_DEBUG_STRINGS
            Log("radar: load after options change, radar bits %02x -> %02x\n", *config & kGmConfigRadarBits, fromSave);
#endif
            *config = (uint16_t)((*config & ~kGmConfigRadarBits) | fromSave);
        }
        return result;
    }

    void InstallKeepSavedRadar(SceUID modid, uintptr_t text, size_t textSize)
    {
        if (!g_Settings.keepSavedRadarType || !g_LinkVarBufPtr)
            return;
        uintptr_t comStore = FindUnique(text, textSize, kComStoreLoadedVariable);
        if (!comStore)
            return;
        uintptr_t restoreVar = ThumbBlTarget(comStore + kComStoreRestoreVarCall);
        if (restoreVar < text || restoreVar >= text + textSize)
        {
            Log("keep radar: GCL_RestoreVar %08x is not in the text segment\n", (unsigned)restoreVar);
            return;
        }
        g_SvLinkVarBufPtr = (uintptr_t)ThumbMovImm(restoreVar + kRestoreVarSourceMovt) << 16 | ThumbMovImm(restoreVar + kRestoreVarSourceMovw);
        g_ComStoreHook = taiHookFunctionOffset(&g_ComStoreRef, modid, 0, comStore - text, 1, (const void*)ComStoreLoadedVariable_hk);
        if (g_ComStoreHook < 0)
            Log("ComStoreLoadedVariable hook: error %08x\n", (unsigned)g_ComStoreHook);
        else
            Log("keep radar: hook installed, sv_linkvarbuf pointer %08x\n", (unsigned)g_SvLinkVarBufPtr);
    }
}

// --- byte patches

namespace
{
    SceUID g_BytePatches[16];
    int g_BytePatchCount = 0;

    // Writes one byte after it checks that the bytes at addr are as expected (one whole instruction).
    template <size_t N>
    bool PatchByte(SceUID modid, uintptr_t text, uintptr_t addr, const uint8_t (&expected)[N], uint8_t value)
    {
        if (sceClibMemcmp((const void*)addr, expected, N) != 0 || g_BytePatchCount >= 16)
        {
            Log("byte patch %08x: unexpected bytes, not patched\n", (unsigned)addr);
            return false;
        }
        SceUID uid = taiInjectData(modid, 0, addr - text, &value, 1);
        if (uid < 0)
        {
            Log("byte patch %08x: inject error %08x\n", (unsigned)addr, (unsigned)uid);
            return false;
        }
        g_BytePatches[g_BytePatchCount++] = uid;
        return true;
    }
}

namespace
{
    // Writes n bytes after it checks that the n bytes at addr are as expected. Returns the inject UID or -1.
    SceUID PatchBytes(SceUID modid, uintptr_t text, uintptr_t addr, const uint8_t* expected, const uint8_t* value, size_t n)
    {
        if (sceClibMemcmp((const void*)addr, expected, n) != 0)
        {
            Log("patch %08x: unexpected bytes, not patched\n", (unsigned)addr);
            return -1;
        }
        SceUID uid = taiInjectData(modid, 0, addr - text, value, n);
        if (uid < 0)
            Log("patch %08x: inject error %08x\n", (unsigned)addr, (unsigned)uid);
        return uid;
    }

    template <size_t N>
    SceUID PatchBytes(SceUID modid, uintptr_t text, uintptr_t addr, const uint8_t (&expected)[N], const uint8_t (&value)[N])
    {
        return PatchBytes(modid, text, addr, expected, value, N);
    }
}

// --- codec CRT band (MGSHDFix mgs2_codec_band.cpp, wait part)

namespace
{
    // user/mode/codec/codeccrt.c. Off PS2 the band position is in pixels and UNSYNC_SPD is 0.5f, so the wait
    // unsync_height / UNSYNC_SPD became 2 * height frames (about 3 s); the PS2 divided 1/16 units by 8 (about
    // 0.2 s). The compiler made it "height * 2.0f"; the patch makes the constant 0.125f (same as MGSHDFix >> 4).
    const Pattern kCodecCrtAct =
    {
        "codeccrt Act",
        "30 B5 83 B0 04 1C 1D EE 70 5F E0 6B 01 30 E2 6F 10 F0 03 00 63 6F 59 1E E0 63 00 2B 50 6A 60 65 03 DD 61 67 "
        "?? ?? ?? ?? AD E0 00 21 A2 6E 94 ED 1C 0A 00 2A 61 67 59 D1 E1 6C F6 EE 00 0A 01 EE 10 1A 30 EE 20 0A"
    };
    const uint32_t kCodecCrtActWaitDown = 0x5a;
    const uint32_t kCodecCrtActWaitUp = 0x114;

    const Pattern kCodecCrtInit =
    {
        "codeccrt init_black_zone",
        "?? ?? ?? ?? 00 EB A4 00 00 EE 10 0A B0 EE 00 1A 00 EE 90 5A B8 6E B8 EE C0 0A 00 28 F8 EE E0 0A 20 EE 01 1A "
        "87 ED 1B 0A BD EE C1 1A 11 EE 10 0A 78 67 09 D0"
    };
    const uint32_t kCodecCrtInitWait = 0x0c;

    // vmov.f32 sN, #2.0 -> #0.125: only the imm4H nibble in the first byte changes.
    const uint8_t kVmovS1Two[4] = { 0xF0, 0xEE, 0x00, 0x0A };
    const uint8_t kVmovS2Two[4] = { 0xB0, 0xEE, 0x00, 0x1A };

#ifdef MGSHDFIX_DEBUG_STRINGS
    // Logs how many frames the band stays away, for each codec CRT work (debug builds only).
    const uint32_t kCodecCrtWorkWait = 0x74;     // unsync_wait_cnt
    const uint32_t kCodecCrtWorkBandHeight = 0x6c;   // unsync_height (float)

    struct BandTrack { uintptr_t work; int prevWait; uint32_t start; };
    BandTrack g_BandTracks[4];
    uint32_t g_BandFrames = 0;
    tai_hook_ref_t g_CodecCrtActRef;
    SceUID g_CodecCrtActHook = -1;

    void CodecCrtAct_hk(uintptr_t work)
    {
        CallNext<void>(g_CodecCrtActRef, work);
        ++g_BandFrames;
        BandTrack* t = nullptr;
        for (BandTrack& b : g_BandTracks)
            if (b.work == work || (!t && b.work == 0))
                t = &b;
        if (!t)
            return;
        if (t->work != work)
        {
            t->work = work;
            t->prevWait = 0;
        }
        int wait = *(const int*)(work + kCodecCrtWorkWait);
        if (t->prevWait <= 0 && wait > 0)
        {
            t->start = g_BandFrames;
            Log("band %08x: away, wait %d, band height %d\n", (unsigned)work, wait, (int)*(const float*)(work + kCodecCrtWorkBandHeight));
        }
        else if (t->prevWait > 0 && wait <= 0)
        {
            Log("band %08x: back after %u frames\n", (unsigned)work, (unsigned)(g_BandFrames - t->start));
        }
        t->prevWait = wait;
    }
#endif

    void InstallCodecBand(SceUID modid, uintptr_t text, size_t textSize)
    {
        if (!g_Settings.fixPs2VisualEffects)
            return;
        uintptr_t act = FindUnique(text, textSize, kCodecCrtAct);
        uintptr_t init = FindUnique(text, textSize, kCodecCrtInit);
        if (!act || !init)
            return;
        int done = 0;
        done += PatchByte(modid, text, act + kCodecCrtActWaitDown, kVmovS1Two, 0xF4);
        done += PatchByte(modid, text, act + kCodecCrtActWaitUp, kVmovS1Two, 0xF4);
        done += PatchByte(modid, text, init + kCodecCrtInitWait, kVmovS2Two, 0xB4);
        Log("codec band: %d of 3 waits patched\n", done);
#ifdef MGSHDFIX_DEBUG_STRINGS
        g_CodecCrtActHook = taiHookFunctionOffset(&g_CodecCrtActRef, modid, 0, act - text, 1, (const void*)CodecCrtAct_hk);
        Log("codec band: debug hook %s\n", g_CodecCrtActHook >= 0 ? "installed" : "failed");
#endif
    }
}

// --- Japanese phone ringtone (MGSHDFix mgs2_restore_phone_jingle.cpp)

namespace
{
    // mode/mobile/mobile.h: PHONE_CALL is PHONE_RING (SD_S_PHONE001, 0x9B) in English builds and PHONE_MELODY
    // (SD_S_PHONEMEL, 0xA0, the main theme) in Japanese builds. The two GM_SdSet(PHONE_CALL) calls get 0xA0.
    const Pattern kPhoneCallStart = { "phone call start", "06 D0 ?? ?? ?? ?? 9B 20 ?? ?? ?? ?? D4 F8 F4 10" };
    const Pattern kPhoneCallResume = { "phone call resume", "55 F0 08 01 C4 F8 F4 10 9B 20 ?? ?? ?? ?? 03 25" };
    const uint8_t kMovsR0Ring[2] = { 0x9B, 0x20 };   // movs r0, #0x9B

#ifdef MGSHDFIX_DEBUG_STRINGS
    // Debug builds: L + R + Start asks for the ringtone. A thread reads the buttons; the sound plays at the
    // next script load (next area), on the game thread. A hook on GM_SdSet crashed the developer menu.
    int RingtoneButtonThread(SceSize, void*)
    {
        const uint32_t kL = SCE_CTRL_LTRIGGER | SCE_CTRL_L1;
        const uint32_t kR = SCE_CTRL_RTRIGGER | SCE_CTRL_R1;
        uint32_t prev = 0;
        for (;;)
        {
            sceKernelDelayThread(50 * 1000);
            SceCtrlData pad;
            if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0)
                continue;
            uint32_t b = pad.buttons;
            if ((b & kL) && (b & kR) && (b & SCE_CTRL_START) && !(prev & SCE_CTRL_START))
            {
                g_RingtoneRequest = 1;
                Log("phone ringtone: requested, plays at the next area load\n");
            }
            prev = b;
        }
        return 0;
    }

    void InstallRingtoneButton(SceUID, uintptr_t, uintptr_t sdSet)
    {
        g_GmSdSet = (GmSdSetFn)(sdSet | 1);
        SceUID thid = sceKernelCreateThread("mgshdfix_ringtone", RingtoneButtonThread, 0x10000100, 0x1000, 0, 0, nullptr);
        if (thid >= 0)
            sceKernelStartThread(thid, 0, nullptr);
        Log("phone ringtone: L + R + Start test %s (GM_SdSet %08x)\n", thid >= 0 ? "ready" : "failed", (unsigned)sdSet);
    }
#endif


    void InstallPhoneJingle(SceUID modid, uintptr_t text, size_t textSize)
    {
        if (!g_Settings.restorePhoneJingle)
            return;
        uintptr_t start = FindUnique(text, textSize, kPhoneCallStart);
        uintptr_t resume = FindUnique(text, textSize, kPhoneCallResume);
        if (!start || !resume)
            return;
        int done = PatchByte(modid, text, start + 6, kMovsR0Ring, 0xA0) + PatchByte(modid, text, resume + 8, kMovsR0Ring, 0xA0);
        Log("phone ringtone: %d of 2 patched\n", done);
#ifdef MGSHDFIX_DEBUG_STRINGS
        InstallRingtoneButton(modid, text, ThumbBlTarget(start + 8));
#endif
    }
}

// --- radar options (MGSHDFix mgs2_restore_sol_radar.cpp, mgs2_snake_tales_radar.cpp)

namespace
{
    // kira/radar/radar.c GetResources: base_rot = GCL_GetOptionValue('d', 0) & 4095. Sons of Liberty had no
    // rotation; MGSHDFix sets the value to 0. Here "ubfx r0, r0, #0, #12" becomes "movs r0, #0; nop".
    const Pattern kRadarBaseRot =
    {
        "radar base_rot",
        "64 20 00 21 ?? ?? ?? ?? C0 F3 0B 00 ?? ?? ?? ?? 28 F8 01 00 00 B2 B0 F5 00 6F 05 DD"
    };
    const uint8_t kUbfxR0Low12[4] = { 0xC0, 0xF3, 0x0B, 0x00 };
    const uint8_t kMovsR0ZeroNop[4] = { 0x00, 0x20, 0x00, 0xBF };
    SceUID g_SolRadarPatch = -1;

    // game/gamed.c NewSetMenuStatus: "-n" on (GM_STRCODE_ON) sets MENU_NODE_ACCESSED, which turns the radar on.
    // In Snake Tales the bne after the compare becomes a nop, so the node state is always on (same as MGSHDFix,
    // which forces the value to GM_STRCODE_ON). The script load hook applies it only in Snake Tales areas.
    const Pattern kSetMenuStatusNode =
    {
        "NewSetMenuStatus node",
        "6E 20 00 21 ?? ?? ?? ?? ?? ?? ?? ?? 88 42 09 D1 ?? ?? ?? ?? ?? ?? ?? ?? 01 68 51 F4 80 41 01 60 00 20"
    };
    const uint8_t kBneNode[2] = { 0x09, 0xD1 };
    const uint8_t kNop16[2] = { 0x00, 0xBF };
    uintptr_t g_SnakeTalesBne = 0;
    SceUID g_SnakeTalesPatch = -1;
    SceUID g_TextModid = -1;
    uintptr_t g_Text = 0;

    const Stage kMgs2Stages[] =
    {
#define X(name, id, mode, disp) { id, mode, disp },
        MGS2_STAGE_LIST
#undef X
    };

    bool IsStageMode(const char* stage, const char* mode)
    {
        for (const Stage& s : kMgs2Stages)
            if (IEquals(stage, s.sStageId))
                return IEquals(s.sGameMode, mode);
        return false;
    }

    bool IsSnakeTalesStage(const char* stage)
    {
        return IsStageMode(stage, "Snake Tales");
    }

    void InstallRadarOptions(SceUID modid, uintptr_t text, size_t textSize)
    {
        if (g_Settings.restoreSolRadarRotation)
        {
            uintptr_t baseRot = FindUnique(text, textSize, kRadarBaseRot);
            if (baseRot)
                g_SolRadarPatch = PatchBytes(modid, text, baseRot + 8, kUbfxR0Low12, kMovsR0ZeroNop);
            Log("SoL radar rotation: %s\n", g_SolRadarPatch >= 0 ? "patched" : "failed");
        }
        if (g_Settings.snakeTalesRadar)
        {
            uintptr_t node = FindUnique(text, textSize, kSetMenuStatusNode);
            if (node)
            {
                g_SnakeTalesBne = node + 0x0e;
                g_TextModid = modid;
                g_Text = text;
            }
            Log("Snake Tales radar: %s\n", g_SnakeTalesBne ? "ready" : "failed");
        }
    }

    // Called on each script load (game thread).
    void UpdateSnakeTalesRadar(const char* stage)
    {
        if (!g_SnakeTalesBne)
            return;
        bool tales = IsSnakeTalesStage(stage);
        if (tales && g_SnakeTalesPatch < 0)
        {
            g_SnakeTalesPatch = PatchBytes(g_TextModid, g_Text, g_SnakeTalesBne, kBneNode, kNop16);
            Log("Snake Tales radar: on in %s\n", stage);
        }
        else if (!tales && g_SnakeTalesPatch >= 0)
        {
            taiInjectRelease(g_SnakeTalesPatch);
            g_SnakeTalesPatch = -1;
            Log("Snake Tales radar: off in %s\n", stage);
        }
    }
}

// --- skip splash screens (MGSHDFix intro_skip.cpp)

namespace
{
    // bp/shared/BP_SplashScreen.cpp BP_Splash_Start: if gState is kSS_Wait or kSS_Done, it sets kSS_Init (1).
    // MGSHDFix sets kSS_Done (3) instead, so no splash screen is shown. Here "movs r1, #1" becomes "movs r1, #3".
    const Pattern kBpSplashStart =
    {
        "BP_Splash_Start",
        "?? ?? ?? ?? ?? ?? ?? ?? 00 68 00 28 08 DC 00 28 05 DB ?? ?? ?? ?? ?? ?? ?? ?? 01 21 01 60 70 47 03 28 FC D1 F5 E7"
    };
    const uint8_t kMovsR1One[2] = { 0x01, 0x21 };

    void InstallSkipSplash(SceUID modid, uintptr_t text, size_t textSize)
    {
        if (!g_Settings.skipSplashscreens)
            return;
        uintptr_t start = FindUnique(text, textSize, kBpSplashStart);
        if (!start)
            return;
        Log("skip splash screens: %s\n", PatchByte(modid, text, start + 0x1a, kMovsR1One, 0x03) ? "patched" : "failed");
    }
}

// --- sunglasses (MGSHDFix mgs2_sunglasses.cpp)

namespace
{
    // The n_title script sets GM_CLEARED_SUNGLASSES_PLAYING (0x10) in GM_ClearFlag (saved). Two code checks use it:
    // sonoyama/etc/sunglasses.c GM_SunGlassesManager and satoyoshi/harrier/har_kasacka.c (Pliskin in the Harrier fight).
    // We patch the "beq" after each bit test, not the flag, so that saves do not change. Always = nop, Never = b.
    const Pattern kSunGlassesManager =
    {
        "GM_SunGlassesManager",
        "10 B5 ?? ?? ?? ?? ?? ?? ?? ?? 00 68 B0 F9 16 00 10 F0 10 00 08 D0 0A 20 5F F4 00 51 34 22 30 23 ?? ?? ?? ?? 04 1C 01 D1 00 20"
    };
    const Pattern kHarrierSunglasses =
    {
        "Harrier sunglasses",
        "?? ?? ?? ?? ?? ?? ?? ?? 00 68 09 F5 E0 61 B0 F9 16 00 09 F5 1C 74 09 91 10 F0 10 00 0E D0 0C 20 00 90 ?? ?? ?? ?? 59 46 ?? ?? ?? ?? 00 20 00 23"
    };

    bool PatchSunglassesBranch(SceUID modid, uintptr_t text, uintptr_t addr, uint8_t offset)
    {
        const uint8_t beq[2] = { offset, 0xD0 };
        if (g_Settings.sunglasses == 1)
            return PatchBytes(modid, text, addr, beq, { 0x00, 0xBF }) >= 0;
        return PatchByte(modid, text, addr + 1, { 0xD0 }, 0xE0);
    }

    void InstallSunglasses(SceUID modid, uintptr_t text, size_t textSize)
    {
        if (!g_Settings.sunglasses)
            return;
        uintptr_t manager = FindUnique(text, textSize, kSunGlassesManager);
        if (manager)
            Log("sunglasses: manager %s\n", PatchSunglassesBranch(modid, text, manager + 0x14, 0x08) ? "patched" : "failed");
        uintptr_t harrier = FindUnique(text, textSize, kHarrierSunglasses);
        if (harrier)
            Log("sunglasses: Harrier %s\n", PatchSunglassesBranch(modid, text, harrier + 0x1c, 0x0E) ? "patched" : "failed");
    }
}

// --- lifebar name (MGSHDFix custom_player_name.cpp)

namespace
{
    // sonoyama/etc/gaged.c GM_InitGageSet(gs, name, x, w, h, value, max, min, delay, level). The player life
    // bars pass "LIFE"; other bars ("SNAKE", "FATMAN", ...) are not changed.
    const Pattern kGmInitGageSet =
    {
        "GM_InitGageSet",
        "2D E9 B0 4F 2D ED 0C 8A 84 B0 02 93 1A 9B 17 1C 1B 9C 8A 46 DD F8 64 90 80 46 1D 9D 10 22 DD F8"
    };
    const uint32_t kLinkVarResidentDir = 28;   // GM_SaveResidentDir

    using GmInitGageSetFn = void (*)(void*, const char*, int, int, int, int, int, int, int, int);
    tai_hook_ref_t g_InitGageSetRef;
    SceUID g_InitGageSetHook = -1;

    // Same names as MGSHDFix (mgs2_status_flags.cpp GetPlayerCharacterName).
    const char* CharacterName(MGS2_Characters::PlayerCharacter c)
    {
        using MGS2_Characters::PlayerCharacter;
        switch (c)
        {
        case PlayerCharacter::NormalRaiden:
        case PlayerCharacter::NakedRaiden: return "RAIDEN";
        case PlayerCharacter::NinjaRaiden: return "NINJA RAIDEN";
        case PlayerCharacter::NormalSnake: return "SNAKE";
        case PlayerCharacter::TuxedoSnake: return "TUXEDO SNAKE";
        case PlayerCharacter::MGS1Snake: return "SOLID SNAKE";
        case PlayerCharacter::Pliskin: return "PLISKIN";
        default: return nullptr;
        }
    }

    const char* CurrentCharacterName()
    {
        uintptr_t linkvar = LinkVarBuf();
        if (!linkvar)
            return nullptr;
        const char* resident = (const char*)(linkvar + kLinkVarResidentDir);
        const char* stage = CurrentStage();
        if (IEquals(resident, "r_plt0") && stage && IEquals(stage, "w61a"))
            return "JACK";
        for (const auto& e : MGS2_Characters::PlayerResidentEntries)
            if (IEquals(e.residentDirectory.data(), resident))   // literals, so data() ends in NUL
                return CharacterName(e.character);
        return nullptr;
    }

    void GM_InitGageSet_hk(void* gs, const char* name, int x, int w, int h, int value, int max, int min, int delay, int level)
    {
        if (name && sceClibStrncmp(name, "LIFE", 5) == 0)
        {
            const char* replacement = nullptr;
            if (g_Settings.useCustomLifebarName)
                replacement = g_Settings.customLifebarName[0] ? g_Settings.customLifebarName : nullptr;
            else if (g_Settings.useCharacterLifebarNames)
                replacement = CurrentCharacterName();
            if (replacement)
                name = replacement;
        }
        CallNext<void>(g_InitGageSetRef, gs, name, x, w, h, value, max, min, delay, level);
    }

    void InstallLifebarName(SceUID modid, uintptr_t text, size_t textSize)
    {
        if (!g_Settings.useCustomLifebarName && !g_Settings.useCharacterLifebarNames)
            return;
        uintptr_t fn = FindUnique(text, textSize, kGmInitGageSet);
        if (!fn)
            return;
        g_InitGageSetHook = taiHookFunctionOffset(&g_InitGageSetRef, modid, 0, fn - text, 1, (const void*)GM_InitGageSet_hk);
        Log("lifebar name: hook %s\n", g_InitGageSetHook >= 0 ? "installed" : "failed");
    }
}

// --- RTC hostage type (MGSHDFix mgs2_hostage_type_easter_egg.cpp)

namespace
{
    // korekado/hostage/hostage.c GetResources is inlined in NewHostage. It reads the script options 'm' (motion),
    // 'k' (model_num) and 's' (status) in this order. MGSHDFix changes the three values after 's' is read. A taiHEN
    // hook cannot go in the middle of a function, so GCL_GetOptionValue returns the new values while NewHostage runs.
    const Pattern kNewHostage =
    {
        "NewHostage",
        "2D E9 F0 4F 91 B0 EB 46 0B F1 08 0B 2B F0 0F 0B 04 1C 06 20 5F F4 00 51 ?? ?? ?? ?? 00 23 ?? ?? ?? ?? 05 1C 00 F0 21 82"
    };
    const Pattern kGclGetOptionValue =
    {
        "GCL_GetOptionValue",
        "30 B5 85 B0 ?? ?? ?? ?? ?? ?? ?? ?? D2 68 45 B2 52 F8 04 0C 0C 1C 02 A9 03 AA ?? ?? ?? ?? 02 99 69 B1 11 F0 F0 02 50 2A F5 D1"
    };

    const uint32_t kHstgStatusPee = 0x08, kHstgStatusSoppo = 0x10, kHstgStatusLod = 0x20;
    const uint32_t kHstgStatusJitabata2 = 0x400, kHstgStatusNoEimsOl = 0x8000;

    struct HostageType
    {
        int model;
        int motion;   // GV_StrCode of the motion name
        int status;
    };
    // Index = hostageType - 1. Same values as MGSHDFix.
    const HostageType kHostageTypes[3] =
    {
        { 3, 0xb95848, (int)(kHstgStatusJitabata2 | kHstgStatusLod) },   // Kato-chan, "hos_b"
        { 12, 0x2b251b, (int)(kHstgStatusPee | kHstgStatusLod) },        // Cathy, "hosf_d"
        { 11, 0x2b251c, (int)(kHstgStatusSoppo | kHstgStatusNoEimsOl) }, // Jennifer, "hosf_e" (LOD bit from the script)
    };

    using NewHostageFn = void* (*)(int, int, int, char**);
    using GclGetOptionValueFn = int (*)(int, int);
    tai_hook_ref_t g_NewHostageRef, g_GetOptionValueRef;
    SceUID g_NewHostageHook = -1, g_GetOptionValueHook = -1;
    volatile bool g_HostageOverride = false;

    int GCL_GetOptionValue_hk(int option, int def)
    {
        int value = CallNext<int>(g_GetOptionValueRef, option, def);
        if (!g_HostageOverride)
            return value;
        // Ames (model_num 0) keeps the script values.
        if (CallNext<int>(g_GetOptionValueRef, (int)'k', -1) == 0)
            return value;
        const HostageType& t = kHostageTypes[g_Settings.hostageType - 1];
        switch ((char)option)
        {
        case 'm':
            return t.motion;
        case 'k':
            return t.model;
        case 's':
            g_HostageOverride = false;   // later option reads in NewHostage are not changed
#ifdef MGSHDFIX_DEBUG_STRINGS
            Log("hostage: model %d -> %d\n", CallNext<int>(g_GetOptionValueRef, (int)'k', -1), t.model);
#endif
            return g_Settings.hostageType == 3 ? t.status | (value & (int)kHstgStatusLod) : t.status;
        default:
            return value;
        }
    }

    void* NewHostage_hk(int name, int where, int argc, char** argv)
    {
        const char* stage = CurrentStage();
        g_HostageOverride = stage && IsStageMode(stage, "Plant");
        void* work = CallNext<void*>(g_NewHostageRef, name, where, argc, argv);
        g_HostageOverride = false;
        return work;
    }

    void InstallHostageType(SceUID modid, uintptr_t text, size_t textSize)
    {
        if (!g_Settings.hostageType)
            return;
        uintptr_t newHostage = FindUnique(text, textSize, kNewHostage);
        uintptr_t getOption = FindUnique(text, textSize, kGclGetOptionValue);
        if (!newHostage || !getOption)
            return;
        g_GetOptionValueHook = taiHookFunctionOffset(&g_GetOptionValueRef, modid, 0, getOption - text, 1, (const void*)GCL_GetOptionValue_hk);
        if (g_GetOptionValueHook >= 0)
            g_NewHostageHook = taiHookFunctionOffset(&g_NewHostageRef, modid, 0, newHostage - text, 1, (const void*)NewHostage_hk);
        Log("hostage type: hooks %s\n", g_GetOptionValueHook >= 0 && g_NewHostageHook >= 0 ? "installed" : "failed");
    }
}

// --- parrot radar cone (MGSHDFix mgs2_parrot_radar_fix.cpp)

namespace
{
    // okuta/parrot: AfterProcess sets npc.action.face_dir from the head bone matrix, which the HD port does not
    // update (the value is always 0), so the radar cone does not turn. Same as MGSHDFix:
    // face_dir = ctrl->rot.vy + svecAdjust.vy. Act calls this parrot function (think + AfterProcess), then
    // NPC_AfterProcess, which gives face_dir to the radar, so the new value goes in between.
    const Pattern kParrotThink =
    {
        "parrot think",
        "2D E9 F0 4F 9F B0 EB 46 0B F1 08 0B 2B F0 0F 0B B0 F8 A6 10 00 F5 1C 78 D0 F8 A8 60 4D 1E 04 1C 00 29 "
        "4F F0 00 07 40 F3 EF 80 04 F1 30 09 D6 F8 08 E0 DE F8 00 00 08 28 0D D8 DF E8 10 F0 09 00"
    };
    // PARROT (parrot.h) with 32-bit pointers: npc at 0x270 (NPCWORK: body, ctrl, lights, lod, action),
    // action.face_dir at npc + 0x10 + 0x3c, svecAdjust at 0x7c0. Same values as the code in Act and PAR_AdjustAimPoint.
    const uint32_t kParrotCtrl = 0x274;
    const uint32_t kParrotFaceDir = 0x2bc;
    const uint32_t kParrotSvecAdjustVy = 0x7c2;
    const uint32_t kControlRotVy = 0x22;

    tai_hook_ref_t g_ParrotThinkRef;
    SceUID g_ParrotThinkHook = -1;

    void ParrotThink_hk(uintptr_t work)
    {
        CallNext<void>(g_ParrotThinkRef, work);
        uintptr_t ctrl = *(const uintptr_t*)(work + kParrotCtrl);
        if (ctrl)
            *(int16_t*)(work + kParrotFaceDir) = (int16_t)(*(const int16_t*)(ctrl + kControlRotVy) + *(const int16_t*)(work + kParrotSvecAdjustVy));
    }

    void InstallParrotRadar(SceUID modid, uintptr_t text, size_t textSize)
    {
        uintptr_t think = FindUnique(text, textSize, kParrotThink);
        if (!think)
            return;
        g_ParrotThinkHook = taiHookFunctionOffset(&g_ParrotThinkRef, modid, 0, think - text, 1, (const void*)ParrotThink_hk);
        if (g_ParrotThinkHook < 0)
            Log("parrot think hook: error %08x\n", (unsigned)g_ParrotThinkHook);
        else
            Log("parrot radar: hook installed\n");
    }
}

// --- node screen: sex, birth year and blood type entry (MGSHDFix mgs2_restore_dogtag_viewer.cpp, node_screen)

namespace
{
    // skoba/etc/name_layout.c. Bluepoint skips the sex and blood type fields ("BP JG") and the birth year (phase 1,
    // "????"). Fields: 0 name, 1 sex, 2 birthday, 3 blood, 4 nationality, 5 OK. Most functions are inlined; the
    // code is the same in PCSE00078 and PCSE00020, so all sites are offsets from PadControlNormal.
    const Pattern kNamePadControlNormal =
    {
        "name_layout PadControlNormal",
        "2D E9 F0 41 04 1C D4 F8 34 02 10 F4 00 11 D4 F8 94 00 00 F0 E4 80 D4 F8 F0 11 0B 1C 13 F4 00 52"
    };

    struct NodePatch
    {
        int32_t offset;
        uint8_t size;
        uint8_t expected[12];
        uint8_t value[12];
    };

    const NodePatch kNodePatches[] =
    {
        // SoftKey: name entry done, normal_mode.position += 2 -> += 1 (5 paths: keyboard and pad).
        { -0x1072, 2, { 0x81, 0x1C }, { 0x41, 0x1C } },
        { -0x0f12, 2, { 0x81, 0x1C }, { 0x41, 0x1C } },
        { -0x0a80, 2, { 0x81, 0x1C }, { 0x41, 0x1C } },
        { -0x0938, 2, { 0x81, 0x1C }, { 0x41, 0x1C } },
        { -0x089a, 2, { 0x81, 0x1C }, { 0x41, 0x1C } },
        // PadControlBirthEntry: confirm +2 -> +1, cancel -2 -> -1, Start +2 -> +1.
        { -0x041a, 2, { 0x81, 0x1C }, { 0x41, 0x1C } },
        { -0x0312, 2, { 0x81, 0x1E }, { 0x41, 0x1E } },
        { -0x021a, 2, { 0x81, 0x1C }, { 0x41, 0x1C } },
        // PadControlBirthEntry: left or cancel on the month goes back to the year ("case 1" moved to the "case 2" path).
        { -0x03a6, 2, { 0x01, 0x29 }, { 0x00, 0x29 } },
        // PadControlNormal: cancel -2 -> -1; up and down do not skip fields 1 and 3 (bne -> b).
        { +0x007e, 2, { 0x91, 0x1E }, { 0x51, 0x1E } },
        { +0x0114, 2, { 0x02, 0xD1 }, { 0x02, 0xE0 } },
        { +0x011e, 2, { 0x02, 0xD1 }, { 0x02, 0xE0 } },
        { +0x0162, 2, { 0x02, 0xD1 }, { 0x02, 0xE0 } },
        { +0x016c, 2, { 0x02, 0xD1 }, { 0x02, 0xE0 } },
        // PadControlRegionEntry: cancel -2 -> -1.
        { +0x0574, 2, { 0x81, 0x1E }, { 0x41, 0x1E } },
        // PositionAct: birth.phase = 1 -> 0 (start on the year).
        { +0x0af4, 2, { 0x01, 0x23 }, { 0x00, 0x23 } },
        // DotUpdate: cursor of field 2 STR_CUR_SEX -> STR_CUR_BIRTH, field 4 STR_CUR_BIRTH -> STR_CUR_REG.
        { +0x1cda, 12, { 0x45, 0xF2, 0xC5, 0x52, 0xC4, 0xF8, 0x20, 0x12, 0xC0, 0xF2, 0x8A, 0x02 },
                       { 0x5F, 0xF4, 0x0F, 0x62, 0xC4, 0xF8, 0x20, 0x12, 0xC0, 0xF2, 0x49, 0x02 } },
        { +0x1caa, 12, { 0x5F, 0xF4, 0x0F, 0x62, 0xC4, 0xF8, 0x20, 0x12, 0xC0, 0xF2, 0x49, 0x02 },
                       { 0x45, 0xF2, 0xB4, 0x12, 0xC4, 0xF8, 0x20, 0x12, 0xC0, 0xF2, 0x8A, 0x02 } },
        // SprInit: name text x 182 -> 127, nationality text and kcej y 116 -> 160, birthday text y 94 -> 116 (movt of
        // the float; the low half is 0).
        { +0x1ea2, 4, { 0xC4, 0xF2, 0x36, 0x32 }, { 0xC4, 0xF2, 0xFE, 0x22 } },
        { +0x1f78, 4, { 0xC4, 0xF2, 0xE8, 0x20 }, { 0xC4, 0xF2, 0x20, 0x30 } },
        { +0x2034, 4, { 0xC4, 0xF2, 0xE8, 0x20 }, { 0xC4, 0xF2, 0x20, 0x30 } },
        { +0x20b0, 4, { 0xC4, 0xF2, 0xBC, 0x21 }, { 0xC4, 0xF2, 0xE8, 0x21 } },
        // BirthDayUpdate: year highlight width 4 -> 45 pixels.
        // ponytail: fixed width (MGSHDFix: 45 for 19x0..19x9 without a 1); compute it from the digits if it looks wrong.
        { +0x19ca, 2, { 0x04, 0x22 }, { 0x2D, 0x22 } },
    };
    const uint32_t kNodeYearFormatMovw = 0x195c;   // BirthDayUpdate: sprintf(buf, "????")
    const uint32_t kNodeNumFormatMovw = 0x1924;    // r8 = "%02d", 8 bytes before "????"

    SceUID g_NodePatches[sizeof(kNodePatches) / sizeof(kNodePatches[0]) + 2];
    int g_NodePatchCount = 0;

    // The layout file (kira/2D_action) has the sex and blood parts at x + 6000 and the birthday and nationality
    // labels one line up. Same values as MGSHDFix, in the 512 x 400 layout space. Vita structs (32-bit):
    // l2dSprite vertex_num +0x0c, status_num +0x48, stat +0x4c; l2dStatus (0x50 bytes) pos_u +0x0c, size_u +0x14,
    // vertex +0x1c, size_w +0x20; l2dVertex is 16 bytes, x and y first.
    const Pattern kL2dSetupLayout2 =
    {
        "L2D_SetupLayout2",
        "2D E9 F0 41 ?? ?? ?? ?? 06 9C ?? ?? ?? ?? D5 F8 00 E0 BE F1 00 0F 03 D1 7F F0 00 00 BD E8 F0 81"
    };
    const Pattern kL2dGetParts =
    {
        "L2D_GetParts",
        "10 B5 ?? ?? ?? ?? ?? ?? ?? ?? 12 68 0A B9 00 20 10 BD 00 28 01 DA 00 20 10 BD 20 28 01 DB 00 20"
    };
    const uint32_t kL2dGetPartsFromSetup = 0x6e4;   // L2D_GetObject has the same start, so use the distance

    const int kPartsNameName = 8133413;    // "CODENAME" label
    const int kPartsNameSex = 258369;
    const int kPartsNameBirth = 11990742;
    const int kPartsNameBld = 241165;
    const int kPartsNameReg = 257328;
    const int kPartsSex = 7132415;
    const int kPartsBld = 7115211;
    const int kUnderline = 13646838;
    const float kNameDataOffsetX = -55.0f;
    const float kNameLabelUvTrim = 0.455f;   // cuts "CODE" from "CODENAME"

    using L2dGetPartsFn = uint8_t* (*)(int, int);
    L2dGetPartsFn g_L2dGetParts = nullptr;
    tai_hook_ref_t g_L2dSetupRef;
    SceUID g_L2dSetupHook = -1;

    // Calls f(x, y) for each vertex of each status of the part. Returns the number of vertices that f changed.
    template <typename F>
    int ForEachPartVertex(int handle, int code, F f)
    {
        uint8_t* spr = g_L2dGetParts(handle, code);
        if (!spr)
            return 0;
        int vnum = *(const int*)(spr + 0x0c);
        int snum = *(const int16_t*)(spr + 0x48);
        uint8_t* stat = *(uint8_t**)(spr + 0x4c);
        int changed = 0;
        for (int si = 0; si < snum; ++si)
        {
            float* v = *(float**)(stat + si * 0x50 + 0x1c);
            for (int vi = 0; v && vi < vnum; ++vi)
                changed += f(v[vi * 4], v[vi * 4 + 1]);
        }
        return changed;
    }

    void FixNodeLayout(int handle)
    {
        auto unhide = [](float& x, float&) { if (x < 6000.0f) return 0; x -= 6000.0f; return 1; };
        auto moveY = [](float from, float to) { return [=](float&, float& y) { if (y != from) return 0; y = to; return 1; }; };
        int n = ForEachPartVertex(handle, kPartsNameBld, unhide) + ForEachPartVertex(handle, kPartsBld, unhide)
              + ForEachPartVertex(handle, kPartsNameSex, unhide) + ForEachPartVertex(handle, kPartsSex, unhide);
        int birth = ForEachPartVertex(handle, kPartsNameBirth, moveY(94.0f, 116.0f));
        int reg = ForEachPartVertex(handle, kPartsNameReg, moveY(116.0f, 160.0f));
        int line = ForEachPartVertex(handle, kUnderline, [](float& x, float&) { x += kNameDataOffsetX; return 1; });

        int trimmed = 0;
        if (uint8_t* spr = g_L2dGetParts(handle, kPartsNameName))
        {
            uint8_t* stat = *(uint8_t**)(spr + 0x4c);
            for (int si = 0; si < *(const int16_t*)(spr + 0x48); ++si)
            {
                float& posU = *(float*)(stat + si * 0x50 + 0x0c);
                float& sizeU = *(float*)(stat + si * 0x50 + 0x14);
                float& sizeW = *(float*)(stat + si * 0x50 + 0x20);
                if (posU >= 0.0f && sizeU > 0.0f)
                {
                    posU += sizeU * kNameLabelUvTrim;
                    sizeU *= 1.0f - kNameLabelUvTrim;
                    ++trimmed;
                }
                if (sizeW > 0.0f)
                    sizeW *= 1.0f - kNameLabelUvTrim;
            }
        }
        Log("node layout %d: unhidden %d, birthday %d, nationality %d, underline %d, label %d\n", handle, n, birth, reg, line, trimmed);
    }

    int L2D_SetupLayout2_hk(void* entry, int chanl, int basePri, int addFlag, int pauseLevel, float safeZoneOffsetY)
    {
        int handle = CallNext<int>(g_L2dSetupRef, entry, chanl, basePri, addFlag, pauseLevel, safeZoneOffsetY);
        const char* stage = CurrentStage();
        if (handle >= 0 && stage && (IEquals(stage, "w11a") || IEquals(stage, "mselect")))
            FixNodeLayout(handle);
        return handle;
    }

    void InstallNodeScreen(SceUID modid, uintptr_t text, size_t textSize)
    {
        if (!g_Settings.restoreNodeScreen)
            return;
        uintptr_t anchor = FindUnique(text, textSize, kNamePadControlNormal);
        if (!anchor)
            return;

        int done = 0;
        for (const NodePatch& p : kNodePatches)
        {
            SceUID uid = PatchBytes(modid, text, anchor + p.offset, p.expected, p.value, p.size);
            if (uid >= 0)
            {
                g_NodePatches[g_NodePatchCount++] = uid;
                ++done;
            }
        }

        // sprintf(buf, "????") -> sprintf(buf, "%04d", birth.year): r2 = year (work +0xd0), r1 = r8 + 8 (the same
        // string), and the string becomes "%04d".
        uintptr_t movw = anchor + kNodeYearFormatMovw;
        uintptr_t r8Movw = anchor + kNodeNumFormatMovw;
        uintptr_t yearFmt = (uintptr_t)ThumbMovImm(movw + 4) << 16 | ThumbMovImm(movw);
        uintptr_t numFmt = (uintptr_t)ThumbMovImm(r8Movw + 10) << 16 | ThumbMovImm(r8Movw);
        static const uint8_t kYearCode[] = { 0xD4, 0xF8, 0xD0, 0x20, 0x08, 0xF1, 0x08, 0x01 };   // ldr.w r2, [r4, #0xd0]; add.w r1, r8, #8
        if (yearFmt == numFmt + 8 && yearFmt >= text && yearFmt + 5 <= text + textSize && sceClibMemcmp((const void*)yearFmt, "????", 5) == 0)
        {
            SceUID code = taiInjectData(modid, 0, movw - text, kYearCode, sizeof(kYearCode));
            SceUID str = code >= 0 ? taiInjectData(modid, 0, yearFmt - text, "%04d", 4) : -1;
            if (code >= 0)
                g_NodePatches[g_NodePatchCount++] = code;
            if (str >= 0)
            {
                g_NodePatches[g_NodePatchCount++] = str;
                ++done;
            }
        }
        else
            Log("node screen: year format at %08x not as expected\n", (unsigned)movw);

        Log("node screen: %d of %d patched at %08x\n", done, (int)(sizeof(kNodePatches) / sizeof(kNodePatches[0])) + 1, (unsigned)anchor);

        uintptr_t setup = FindUnique(text, textSize, kL2dSetupLayout2);
        uintptr_t getParts = setup ? FindUnique(setup + kL2dGetPartsFromSetup, 32, kL2dGetParts) : 0;
        if (!getParts)
            return;
        g_L2dGetParts = (L2dGetPartsFn)(getParts | 1);
        g_L2dSetupHook = taiHookFunctionOffset(&g_L2dSetupRef, modid, 0, setup - text, 1, (const void*)L2D_SetupLayout2_hk);
        if (g_L2dSetupHook < 0)
            Log("node layout: hook error %08x at %08x\n", (unsigned)g_L2dSetupHook, (unsigned)setup);
        else
            Log("node layout: hook installed\n");
    }
}

// --- end-game dogtag (MGSHDFix mgs2_restore_dogtag_viewer.cpp, endgame_dogtag)

namespace
{
    // takabe/object/eddogtag.c: Bluepoint removed the blood type, forced MALE, wrote "XXXX" for the birth year and
    // changed "Name" to "Code Name" (BP_DOGTAG_TCR_011). CreateDogtagTexture writes all text with this static
    // function, so the hook changes the text and adds the blood type. Same positions as MGSHDFix.
    const Pattern kDogtagWriteString =
    {
        "eddogtag WriteString",
        "2D E9 F0 41 8A B0 45 6D 0F 1C 81 6D 12 F0 03 03 CD E9 07 13 00 2B 05 DD 01 2B 40 F3 09 81 02 2B"
    };
    const uint32_t kDogtagX = 0x54;
    const uint32_t kDogtagY = 0x58;
    const int kDogtagSexY = 97;
    const int kDogtagBirthdayY = 119;
    const uint32_t kLinkVarMySex = 5480;
    const uint32_t kLinkVarMyYear = 5484;
    const uint32_t kLinkVarMyBlood = 5496;

    // taiHEN rejects a hook here (TAI_ERROR_HOOK_ERROR), so the plugin writes its own jump. The first 8 bytes
    // (push, sub sp, ldr r5) become "ldr.w pc, [pc]" + hook address. DogtagWriteStringOrig runs the 3 old
    // instructions and continues at WriteString + 8. r12 is a scratch register at function entry (AAPCS).
    SceUID g_DogtagWriteStringPatch = -1;
    extern "C" uintptr_t g_DogtagWriteStringResume __asm__("mgshdfix_dogtag_resume");
    uintptr_t g_DogtagWriteStringResume = 0;

    __attribute__((naked)) void DogtagWriteStringOrig(uintptr_t, const char*, int)
    {
        asm volatile(
            "push.w {r4, r5, r6, r7, r8, lr}\n"
            "sub sp, #40\n"
            "ldr r5, [r0, #84]\n"
            "ldr ip, =mgshdfix_dogtag_resume\n"
            "ldr ip, [ip]\n"
            "bx ip\n"
            ".ltorg\n");
    }

    void DogtagWriteAt(uintptr_t work, int x, int y, const char* str, int flag)
    {
        *(int*)(work + kDogtagX) = x;
        *(int*)(work + kDogtagY) = y;
        DogtagWriteStringOrig(work, str, flag);
    }

    void DogtagWriteString_hk(uintptr_t work, const char* str, int flag)
    {
        static const char* const kBlood[] = { "?", "A", "B", "AB", "O" };
        uintptr_t linkvar = LinkVarBuf();
        int y = *(const int*)(work + kDogtagY);

        if (flag == 4 && sceClibStrcmp(str, "Code Name") == 0)
            str = "Name";

        if (linkvar && flag == 2 && y == kDogtagSexY)
        {
            str = *(const int*)(linkvar + kLinkVarMySex) == 1 ? "FEMALE" : "MALE";
            DogtagWriteStringOrig(work, str, flag);
            uint32_t blood = *(const uint32_t*)(linkvar + kLinkVarMyBlood);
            DogtagWriteAt(work, 238, kDogtagSexY, kBlood[blood < 5 ? blood : 0], 1);
            return;
        }

        char date[16];
        if (linkvar && flag == 1 && y == kDogtagBirthdayY && sceClibStrnlen(str, sizeof(date)) == 10 && sceClibStrcmp(str + 6, "XXXX") == 0)
        {
            sceClibSnprintf(date, sizeof(date), "%.6s%04d", str, *(const int*)(linkvar + kLinkVarMyYear));
            str = date;
        }

        DogtagWriteStringOrig(work, str, flag);

        if (flag == 4 && sceClibStrcmp(str, "Sex") == 0)
            DogtagWriteAt(work, 166, kDogtagSexY, "Blood", 4);
    }

    void InstallEndDogtag(SceUID modid, uintptr_t text, size_t textSize)
    {
        if (!g_LinkVarBufPtr)
            return;
        uintptr_t fn = FindUnique(text, textSize, kDogtagWriteString);
        if (!fn || (fn & 3))
            return;
        uint32_t jump[2] = { 0xf000f8df, (uint32_t)(uintptr_t)DogtagWriteString_hk };   // ldr.w pc, [pc]; Thumb address
        g_DogtagWriteStringResume = (fn + 8) | 1;
        g_DogtagWriteStringPatch = taiInjectData(modid, 0, fn - text, jump, sizeof(jump));
        if (g_DogtagWriteStringPatch < 0)
            Log("end dogtag: inject error %08x at %08x\n", (unsigned)g_DogtagWriteStringPatch, (unsigned)fn);
        else
            Log("end dogtag: jump installed at %08x\n", (unsigned)fn);
    }
}

// --- action level selection (MGSHDFix mgs2_restore_action_level_selection.cpp)

namespace
{
    // kano/titlescr/newgame.c: Bluepoint hides "1st Time" (game[0]), shows Tanker-Plant (game[3]) in its row as
    // cursor 0 and sends cursor 0 to the difficulty, not to the questionnaire (skoba/etc/encute.c). GameSubStep and
    // Step are inlined in one function; ShowGameMenu, Act and Die are near it. Same code in PCSE00078 and PCSE00020.
    const Pattern kNewGameStep =
    {
        "newgame Step",
        "2D E9 F0 41 9E B0 04 1C E0 6C 10 28 4A D8 DF E8 10 F0 11 00 68 04 48 07 BA 04 50 01 43 03 13 02"
    };
    const int32_t kNewGameShowMenu = -0x806;
    const int32_t kNewGameAct = 0x1080;
    const int32_t kNewGameDie = 0x1190;

    struct NewGamePatch
    {
        int32_t offset;
        uint8_t expected[2];
        uint8_t value[2];
    };
    const NewGamePatch kNewGamePatches[] =
    {
        { 0x1028, { 0x03, 0x20 }, { 0x00, 0x20 } },   // GameSubStep 1: cursor 0 selects game[0], not game[3]
        { 0x0f38, { 0x03, 0x25 }, { 0x00, 0x25 } },   // GameSubStep 2: the same for the old and new cursor
        { 0x0f54, { 0x02, 0x20 }, { 0x03, 0x20 } },   // up from 0 wraps to 3 (N_GAME_ITEMS 3 -> 4)
        { 0x0ff4, { 0x03, 0x29 }, { 0x04, 0x29 } },   // down wraps after 3
    };

    // Vita Work (32-bit) and SPR_OBJ offsets.
    const uint32_t kNgBusy = 0x38, kNgStep = 0x4c, kNgSubStep = 0x50, kNgAns = 0x64;
    const uint32_t kNgGameCursor = 0x68, kNgQuestionCursor = 0x6e, kNgDispGameItem = 0x74;
    const uint32_t kNgCursor = 0x7c, kNgGame = 0x84, kNgTargetY = 0xd4;
    const uint32_t kSprParent = 0x00, kSprFlags = 0x1c, kSprEmptyY = 0x4c, kSprSpriteY = 0x7c;
    const uint32_t kSprHidden = 0x8000;
    const int kStepGameSelect = 0, kStepQuestion = 1, kStepLevelSelect = 2;
    const int kTitleMsgModeEnd = 1;
    const uint16_t kTitleMenuCleared = 0x0004 | 0x0008, kTitleMenuStorySelMask = 0x0003;
    const uint8_t kDispFirstTimeOnly = 0x01, kDispAll = 0x0f;

    // game/jimaku.c GM_JimakuSetPosY(posy): posy < 0 sets the default (YTOP, 342 of 448). Two-line questionnaire
    // captions go below the screen at the default.
    const Pattern kGmJimakuSetPosY =
    {
        "GM_JimakuSetPosY",
        "10 B5 ?? ?? ?? ?? ?? ?? ?? ?? 09 68 00 28 01 DA 40 F2 56 10 48 62 08 1C ?? ?? ?? ?? 10 BD"
    };
    const int kQuestionCaptionY = 336;
    using GmJimakuSetPosYFn = void (*)(int);
    GmJimakuSetPosYFn g_GmJimakuSetPosY = nullptr;
    bool g_CaptionRaised = false;

    void RaiseCaption(bool raise)
    {
        if (!g_GmJimakuSetPosY || raise == g_CaptionRaised)
            return;
        g_GmJimakuSetPosY(raise ? kQuestionCaptionY : -1);
        g_CaptionRaised = raise;
    }

    SceUID g_NewGamePatches[sizeof(kNewGamePatches) / sizeof(kNewGamePatches[0])];
    int g_NewGamePatchCount = 0;
    tai_hook_ref_t g_NgShowMenuRef, g_NgActRef, g_NgDieRef;
    SceUID g_NgShowMenuHook = -1, g_NgActHook = -1, g_NgDieHook = -1;

    template <typename T>
    T& NgField(uintptr_t base, uint32_t offset)
    {
        return *(T*)(base + offset);
    }

    uintptr_t NgGame(uintptr_t work, int i)
    {
        return NgField<uintptr_t>(work, kNgGame + i * 4);
    }

    float ParentWorldY(uintptr_t spr)
    {
        float y = 0.0f;
        uintptr_t parent = NgField<uintptr_t>(spr, kSprParent);
        for (int i = 0; i < 8 && parent; ++i)
        {
            y += NgField<float>(parent, kSprEmptyY);
            parent = NgField<uintptr_t>(parent, kSprParent);
        }
        return y;
    }

    // Row 4 = row 3 + (row 3 - row 2). Returns 0 if the rows look wrong.
    float FourthRowY(float y0, float y1, float y2)
    {
        float step = y2 - y1;
        if (!(step > 1.0f && step < 200.0f))
            step = y1 - y0;
        return (step > 1.0f && step < 200.0f) ? y2 + step : 0.0f;
    }

    float NgLocalFourthRowY(uintptr_t work)
    {
        return FourthRowY(NgField<float>(NgGame(work, 0), kSprSpriteY), NgField<float>(NgGame(work, 1), kSprSpriteY),
                          NgField<float>(NgGame(work, 2), kSprSpriteY));
    }

    // Replaces ShowGameMenu. Hides the items that are not in disp_game_item and shows the others, except while the
    // menu opens (sub_step 1, layout busy). Tanker-Plant goes to row 4; it has a different parent from rows 1 to 3.
    void NgShowGameMenu_hk(uintptr_t work)
    {
        uint8_t mask = NgField<uint8_t>(work, kNgDispGameItem);
        bool canShow = !(NgField<int>(work, kNgSubStep) == 1 && NgField<int>(work, kNgBusy));
        for (int i = 0; i < 4; ++i)
        {
            uintptr_t spr = NgGame(work, i);
            if (!spr)
                continue;
            if (!(mask & (1 << i)))
                NgField<uint32_t>(spr, kSprFlags) |= kSprHidden;
            else if (canShow)
                NgField<uint32_t>(spr, kSprFlags) &= ~kSprHidden;
        }
        if (mask != kDispAll)
            return;
        uintptr_t g0 = NgGame(work, 0), g1 = NgGame(work, 1), g2 = NgGame(work, 2), g3 = NgGame(work, 3);
        float y = FourthRowY(NgField<float>(g0, kSprSpriteY) + ParentWorldY(g0), NgField<float>(g1, kSprSpriteY) + ParentWorldY(g1),
                             NgField<float>(g2, kSprSpriteY) + ParentWorldY(g2));
        if (y > 0.0f)
            NgField<float>(g3, kSprSpriteY) = y - ParentWorldY(g3);
    }

    void NgAct_hk(uintptr_t work)
    {
        int step0 = NgField<int>(work, kNgStep);
        int sub0 = NgField<int>(work, kNgSubStep);
        if (step0 == kStepGameSelect && sub0 == 0)
        {
            uintptr_t linkvar = LinkVarBuf();
            bool cleared = linkvar && (*(const uint16_t*)(linkvar + kLinkVarTitleMenuStatus) & kTitleMenuCleared);
            NgField<uint8_t>(work, kNgDispGameItem) = cleared ? kDispAll : kDispFirstTimeOnly;
        }

        CallNext<void>(g_NgActRef, work);

        // "1st Time" goes to the questionnaire, and cancel on the difficulty goes back to it.
        int step = NgField<int>(work, kNgStep);
        int16_t cursor = NgField<int16_t>(work, kNgGameCursor);
        if (cursor == 0 && ((step0 == kStepGameSelect && step == kStepLevelSelect) || (step0 == kStepLevelSelect && step == kStepGameSelect)))
        {
            NgField<int>(work, kNgStep) = kStepQuestion;
            NgField<int>(work, kNgSubStep) = 0;
#ifdef MGSHDFIX_DEBUG_STRINGS
            Log("action level: step %d -> questionnaire\n", step0);
#endif
        }

        step = NgField<int>(work, kNgStep);
        if (step == kStepQuestion)
            RaiseCaption(true);
        else if (step == kStepGameSelect)
            RaiseCaption(false);

        // The cursor target for Tanker-Plant is row 4, not the y of game[3] under its own parent.
        if (step == kStepGameSelect && cursor == 3 && NgField<uint8_t>(work, kNgDispGameItem) == kDispAll)
        {
            float y = NgLocalFourthRowY(work);
            if (y > 0.0f)
            {
                NgField<float>(work, kNgTargetY) = y;
                if (sub0 == 1 && NgField<int>(work, kNgSubStep) == 2)
                    NgField<float>(NgField<uintptr_t>(work, kNgCursor), kSprEmptyY) = y;
            }
        }
    }

    // Die saves Tanker-Plant (3) for cursor 0. The PS2 code saved Plant (2) for answers 4 and 5.
    void NgDie_hk(uintptr_t work)
    {
        bool plantOnly = NgField<int>(work, kNgAns) == kTitleMsgModeEnd && NgField<int16_t>(work, kNgGameCursor) == 0 &&
                         NgField<int16_t>(work, kNgQuestionCursor) >= 3;
        CallNext<void>(g_NgDieRef, work);
        RaiseCaption(false);
        uintptr_t linkvar = LinkVarBuf();
        if (plantOnly && linkvar)
        {
            uint16_t& status = *(uint16_t*)(linkvar + kLinkVarTitleMenuStatus);
            status = (status & ~kTitleMenuStorySelMask) | 2;
        }
    }

    void InstallActionLevel(SceUID modid, uintptr_t text, size_t textSize)
    {
        if (!g_Settings.restoreActionLevel || !g_LinkVarBufPtr)
            return;
        uintptr_t step = FindUnique(text, textSize, kNewGameStep);
        if (!step)
            return;
        for (const NewGamePatch& p : kNewGamePatches)
        {
            SceUID uid = PatchBytes(modid, text, step + p.offset, p.expected, p.value);
            if (uid < 0)
                break;
            g_NewGamePatches[g_NewGamePatchCount++] = uid;
        }
        if (g_NewGamePatchCount != (int)(sizeof(kNewGamePatches) / sizeof(kNewGamePatches[0])))
        {
            Log("action level: %d patches, not installed\n", g_NewGamePatchCount);
            for (int i = g_NewGamePatchCount - 1; i >= 0; --i)
                taiInjectRelease(g_NewGamePatches[i]);
            g_NewGamePatchCount = 0;
            return;
        }
        if (uintptr_t setPosY = FindUnique(text, textSize, kGmJimakuSetPosY))
            g_GmJimakuSetPosY = (GmJimakuSetPosYFn)(setPosY | 1);
        else
            Log("action level: GM_JimakuSetPosY not found, captions stay at the default position\n");
        g_NgShowMenuHook = taiHookFunctionOffset(&g_NgShowMenuRef, modid, 0, step + kNewGameShowMenu - text, 1, (const void*)NgShowGameMenu_hk);
        g_NgActHook = taiHookFunctionOffset(&g_NgActRef, modid, 0, step + kNewGameAct - text, 1, (const void*)NgAct_hk);
        g_NgDieHook = taiHookFunctionOffset(&g_NgDieRef, modid, 0, step + kNewGameDie - text, 1, (const void*)NgDie_hk);
        Log("action level: hooks %08x %08x %08x\n", (unsigned)g_NgShowMenuHook, (unsigned)g_NgActHook, (unsigned)g_NgDieHook);
    }
}

// --- test inventory (debug builds only)

#ifdef MGSHDFIX_DEBUG_STRINGS
namespace
{
    // linkvar.h: weapon and item counts (short) for Snake and Raiden. Index values are from g_define.h.
    const uint32_t kLinkVarWeapons = 348, kLinkVarWeaponsMax = 420, kLinkVarItems = 492, kLinkVarItemsMax = 588;
    const uint32_t kLinkVarWeaponsR = 684, kLinkVarWeaponsMaxR = 756, kLinkVarItemsR = 828, kLinkVarItemsMaxR = 924;

    struct Stock { uint8_t index; int16_t count; };

    // All player weapons from WP_m92 to WP_Book, except WP_DemoMic (20).
    const Stock kTestWeapons[] =
    {
        { 1, 99 }, { 2, 99 }, { 3, 99 }, { 4, 99 }, { 5, 99 }, { 6, 99 }, { 7, 99 }, { 8, 99 }, { 9, 99 },
        { 10, 99 }, { 11, 99 }, { 12, 1 }, { 13, 1 }, { 14, 99 }, { 15, 99 }, { 16, 99 }, { 17, 99 }, { 18, 99 },
        { 19, 99 }, { 21, 99 },
    };

    // Equipment only: no story items, ID card, clear rewards or dummies.
    const Stock kTestItems[] =
    {
        { 1, 5 },   // ration
        { 3, 5 }, { 4, 5 }, { 5, 5 },   // medicine, bandage, pentazemin
        { 6, 1 },   // uniform
        { 7, 1 }, { 9, 1 }, { 10, 1 }, { 11, 1 }, { 12, 1 }, { 13, 1 }, { 14, 1 }, { 15, 1 },
        { 16, 1 }, { 22, 1 }, { 23, 1 }, { 26, 1 }, { 27, 1 },   // boxes
        { 29, 1 }, { 30, 1 }, { 35, 1 },   // suppressors
    };

    void TopUp(uintptr_t linkvar, uint32_t counts, uint32_t maxima, const Stock* list, int n)
    {
        int16_t* c = (int16_t*)(linkvar + counts);
        int16_t* m = (int16_t*)(linkvar + maxima);
        for (int i = 0; i < n; ++i)
        {
            if (m[list[i].index] < list[i].count)
                m[list[i].index] = list[i].count;
            if (c[list[i].index] < list[i].count)
                c[list[i].index] = list[i].count;
        }
    }

    int TestInventoryThread(SceSize, void*)
    {
        for (;;)
        {
            sceKernelDelayThread(1000 * 1000);
            uintptr_t linkvar = LinkVarBuf();
            if (!linkvar)
                continue;
            static int tick = 0;
            if (tick++ % 5 == 0)
            {
                const int16_t* lv = (const int16_t*)linkvar;
                const char* stage = CurrentStage();
                Log("test inventory: %s linkvar %08x uniform S %d R %d, M9 S %d R %d\n", stage ? stage : "?", (unsigned)linkvar,
                    lv[(kLinkVarItems >> 1) + 6], lv[(kLinkVarItemsR >> 1) + 6], lv[(kLinkVarWeapons >> 1) + 1], lv[(kLinkVarWeaponsR >> 1) + 1]);
            }
            const int nw = sizeof(kTestWeapons) / sizeof(kTestWeapons[0]);
            const int ni = sizeof(kTestItems) / sizeof(kTestItems[0]);
            TopUp(linkvar, kLinkVarWeapons, kLinkVarWeaponsMax, kTestWeapons, nw);
            TopUp(linkvar, kLinkVarWeaponsR, kLinkVarWeaponsMaxR, kTestWeapons, nw);
            TopUp(linkvar, kLinkVarItems, kLinkVarItemsMax, kTestItems, ni);
            TopUp(linkvar, kLinkVarItemsR, kLinkVarItemsMaxR, kTestItems, ni);
        }
        return 0;
    }

    void StartTestInventory()
    {
        if (!g_Settings.giveTestInventory || !g_LinkVarBufPtr)
            return;
        SceUID thid = sceKernelCreateThread("mgshdfix_inventory", TestInventoryThread, 0x10000100, 0x1000, 0, 0, nullptr);
        if (thid >= 0)
            sceKernelStartThread(thid, 0, nullptr);
        Log("test inventory: thread %s\n", thid >= 0 ? "started" : "failed");
    }
}
#endif

#ifdef MGSHDFIX_SELFTEST
#include "selftest_originals.h"

// Calls the game's BP_GetOverrideString with the original text of each fix and checks the result.
namespace
{
    void RunSelfTest(uintptr_t text, size_t textSize)
    {
        uintptr_t getOverride = FindUnique(text, textSize, kBpGetOverrideString);
        if (!getOverride)
            return;
        using Fn = char* (*)(char*);
        Fn gameGetOverride = (Fn)(getOverride | 1);

        unsigned pass = 0, fail = 0;
        for (const SelfTestString& t : kSelfTestStrings)
        {
            char* input = (char*)t.text;
            const char* expected = IsForceOriginal(t.hash) ? input : FindTypoFix(t.hash);
            const char* result = gameGetOverride(input);
            if (result == expected)
            {
                ++pass;
            }
            else
            {
                ++fail;
                Log("selftest FAIL %08x\n", (unsigned)t.hash);
            }
        }
        Log("selftest: %u pass, %u fail\n", pass, fail);
    }
}
#endif

namespace
{
    void FixStageNames(SceUID modid, uintptr_t text, size_t textSize)
    {
        uintptr_t getStageName = FindUnique(text, textSize, kGetStageName);
        if (!getStageName)
            return;

        uintptr_t movw = getStageName + kGetStageNameTableMovw;
        uintptr_t table = (uintptr_t)ThumbMovImm(movw + 4) << 16 | ThumbMovImm(movw);
        if (table < text || table + kStageNameCount * 4 > text + textSize)
        {
            Log("stage names: table %08x is not in the text segment\n", (unsigned)table);
            return;
        }

        const char* names[kStageNameCount];
        sceClibMemcpy(names, (const void*)table, sizeof(names));

        unsigned fixed = 0;
        for (uint32_t i = 0; i < kStageNameCount; ++i)
        {
            if (!names[i])
                continue;
            if (const char* fix = FindTypoFix(Crc32(names[i])))
            {
                names[i] = fix;
                ++fixed;
            }
        }
        if (fixed == 0)
        {
            Log("stage names: table %08x, no fixes\n", (unsigned)table);
            return;
        }

        g_StageNameInject = taiInjectData(modid, 0, table - text, names, sizeof(names));
        if (g_StageNameInject < 0)
            Log("stage names: inject error %08x\n", (unsigned)g_StageNameInject);
        else
            Log("stage names: table %08x, %u of %u fixed\n", (unsigned)table, fixed, (unsigned)kStageNameCount);
    }
}

extern "C"
{
    int module_start(SceSize, const void*);
    int _start(SceSize, const void*) __attribute__((weak, alias("module_start")));

    int module_start(SceSize, const void*)
    {
        sceIoMkdir(LOG_DIR, 0777);
        Log("MGSHDFixVita: start, build " __DATE__ " " __TIME__ "\n");

        tai_module_info_t tai_info;
        tai_info.size = sizeof(tai_info);
        if (taiGetModuleInfo((const char*)TAI_MAIN_MODULE, &tai_info) < 0)
        {
            Log("main module: not found\n");
            return SCE_KERNEL_START_SUCCESS;
        }

        SceKernelModuleInfo info;
        sceClibMemset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        if (sceKernelGetModuleInfo(tai_info.modid, &info) < 0)
        {
            Log("main module %s: no module info\n", tai_info.name);
            return SCE_KERNEL_START_SUCCESS;
        }

        uintptr_t text = (uintptr_t)info.segments[0].vaddr;
        size_t textSize = info.segments[0].memsz;
        Log("main module %s: text %08x size %08x\n", tai_info.name, (unsigned)text, (unsigned)textSize);

        // MGS2 and MGS3 both use the module name "main", so the patterns identify the game.
        uintptr_t loadScript = FindUnique(text, textSize, kGclLoadScript);
        uintptr_t lookup = FindUnique(text, textSize, kBpLookupStringOveride);
        if (!loadScript || !lookup)
        {
            Log("not MGS2: no fixes applied\n");
            return SCE_KERNEL_START_SUCCESS;
        }
        Log("MGS2: GCL_LoadScript %08x, BP_LookupStringOveride %08x\n", (unsigned)loadScript, (unsigned)lookup);
        LoadConfig();

        g_LookupHook = taiHookFunctionOffset(&g_LookupRef, tai_info.modid, 0, lookup - text, 1, (const void*)BP_LookupStringOveride_hk);
        if (g_LookupHook < 0)
        {
            Log("BP_LookupStringOveride hook: error %08x\n", (unsigned)g_LookupHook);
            return SCE_KERNEL_START_SUCCESS;
        }

        Log("caption fixes: %u typo fixes, %u force original\n",
            (unsigned)(sizeof(kMGS2CaptionTypoFixes) / sizeof(kMGS2CaptionTypoFixes[0]) +
                       sizeof(kMGS3CaptionTypoFixes) / sizeof(kMGS3CaptionTypoFixes[0])),
            (unsigned)(sizeof(kMGS2ForceOriginalHashes) / sizeof(kMGS2ForceOriginalHashes[0])));

        FixStageNames(tai_info.modid, text, textSize);
        if (!ResolveLinkVarBuf(text, textSize))
            Log("linkvarbuf: not found, script patches and IGT fix off\n");
        InstallScriptPatches(tai_info.modid, text, textSize, loadScript);
        InstallIgtFix(tai_info.modid, text, textSize);
        InstallKeepSavedRadar(tai_info.modid, text, textSize);
        InstallCodecBand(tai_info.modid, text, textSize);
        InstallParrotRadar(tai_info.modid, text, textSize);
        InstallPhoneJingle(tai_info.modid, text, textSize);
        InstallRadarOptions(tai_info.modid, text, textSize);
        InstallLifebarName(tai_info.modid, text, textSize);
        InstallSkipSplash(tai_info.modid, text, textSize);
        InstallSunglasses(tai_info.modid, text, textSize);
        InstallHostageType(tai_info.modid, text, textSize);
        InstallEndDogtag(tai_info.modid, text, textSize);
        InstallNodeScreen(tai_info.modid, text, textSize);
        InstallActionLevel(tai_info.modid, text, textSize);
#ifdef MGSHDFIX_DEBUG_STRINGS
        StartTestInventory();
#endif
#ifdef MGSHDFIX_SELFTEST
        RunSelfTest(text, textSize);
#endif
        return SCE_KERNEL_START_SUCCESS;
    }

    int module_stop(SceSize, const void*)
    {
#ifdef MGSHDFIX_DEBUG_STRINGS
        if (g_CodecCrtActHook >= 0)
            taiHookRelease(g_CodecCrtActHook, g_CodecCrtActRef);
#endif
        for (int i = 0; i < g_BytePatchCount; ++i)
            taiInjectRelease(g_BytePatches[i]);
        if (g_NgDieHook >= 0)
            taiHookRelease(g_NgDieHook, g_NgDieRef);
        if (g_NgActHook >= 0)
            taiHookRelease(g_NgActHook, g_NgActRef);
        if (g_NgShowMenuHook >= 0)
            taiHookRelease(g_NgShowMenuHook, g_NgShowMenuRef);
        for (int i = g_NewGamePatchCount - 1; i >= 0; --i)
            taiInjectRelease(g_NewGamePatches[i]);
        if (g_L2dSetupHook >= 0)
            taiHookRelease(g_L2dSetupHook, g_L2dSetupRef);
        for (int i = g_NodePatchCount - 1; i >= 0; --i)
            taiInjectRelease(g_NodePatches[i]);
        if (g_DogtagWriteStringPatch >= 0)
            taiInjectRelease(g_DogtagWriteStringPatch);
        if (g_InitGageSetHook >= 0)
            taiHookRelease(g_InitGageSetHook, g_InitGageSetRef);
        if (g_SnakeTalesPatch >= 0)
            taiInjectRelease(g_SnakeTalesPatch);
        if (g_SolRadarPatch >= 0)
            taiInjectRelease(g_SolRadarPatch);
        if (g_ParrotThinkHook >= 0)
            taiHookRelease(g_ParrotThinkHook, g_ParrotThinkRef);
        if (g_ComStoreHook >= 0)
            taiHookRelease(g_ComStoreHook, g_ComStoreRef);
        if (g_StartLoaderHook >= 0)
            taiHookRelease(g_StartLoaderHook, g_StartLoaderRef);
        if (g_LoadScriptHook >= 0)
            taiHookRelease(g_LoadScriptHook, g_LoadScriptRef);
        if (g_StageNameInject >= 0)
            taiInjectRelease(g_StageNameInject);
        if (g_LookupHook >= 0)
            taiHookRelease(g_LookupHook, g_LookupRef);
        return SCE_KERNEL_STOP_SUCCESS;
    }
}
