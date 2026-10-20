// revd.asi - more engine audio slots for GTA IV: The Complete Edition
// (1.2.0.59).
//
// THE PROBLEM
// -----------
// GTA IV plays a looping engine sound per distinct vehicle MODEL in earshot,
// not per vehicle, and it has a fixed table of slots to put them in. The audio
// init (RVA 0x58D4C3..0x58D512) probes STREAM_ENGINE_1.. and stores each slot
// into a 16-byte-entry table at RVA 0xE832B8 (count at 0xE83298). The loop
// stops at `cmp edi, 0x19` (RVA 0x58D50D) - twenty five slots.
//
// Stock traffic rarely shows twenty five distinct models at once, so nobody
// notices. Add a traffic pack or raise vehicle variety and you go straight past
// it: every model past the twenty fifth is simply silent. Cars roll by making
// no engine noise at all.
//
// WHAT THIS DOES
// --------------
//   1. Relocates the slot table into this DLL, because the statics immediately
//   after the stock table
//      are occupied and it cannot simply grow in place. Every reference to the
//      table is an absolute displacement onto entry 0's fields (+0 ptr, +4, +8,
//      +12): fourteen sites, listed below with the field each one addresses.
//      All fourteen are verified against their expected values before any write
//      happens.
//   2. Raises the probe bound at 0x58D50F from 25 to [revd] Slots.
//   3. Raises the physical audio heap the slot block is carved from (`mov esi,
//   imm32` at RVA 0x4C158C,
//      stock 126 MB) to [revd] AudioHeapMB. This is NOT optional: each slot
//      costs about 0.8 MB, and past roughly thirty slots the stock heap runs
//      out and the game crashes during audio init. If you turn the heap raise
//      off, keep Slots at 25.
//
//   4. Declares the extra slots in pc/audio/config/waveslots.xml, because the
//   engine probes them BY
//      NAME and a slot the file does not declare stays empty however high the
//      bound goes. Missing entries are appended, never replaced, so another
//      audio mod's edits survive, and the original is backed up first. Turn it
//      off with [revd] WriteWaveslots=0 and declare them yourself.
//
// SAFETY
// ------
// waveslots.xml is the one and only file touched, and only when WriteWaveslots
// is on. Every engine patch is made in memory at runtime. The Complete
// Edition's .text is encrypted at load, so this polls until each site reads its
// expected stock value and only then writes. If any site disagrees, nothing is
// written and the reason goes in revd.log. Patching is skipped entirely once
// the audio system has initialised (the count global is no longer 0), so a late
// load is a no-op rather than a crash.
//
// Population, traffic density and ped keep radius are NOT here. That is
// popctl's job: https://github.com/gutbash/popctl
//
// MIT licensed. See LICENSE.
#define WIN32_LEAN_AND_MEAN
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static char g_log[MAX_PATH], g_ini[MAX_PATH];
static HMODULE g_self = nullptr;

static void Log(const char* fmt, ...)
{
    FILE* f = fopen(g_log, "a");
    if (!f)
        return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// ---- engine audio wave slots
// --------------------------------------------------------------------
static const DWORD BaseAdress_unused = 0xd90000;  // 1.8 0x400000
// Patterns are commented above RVA vars.
/* 0 offset patterns CE 1.2.0.59 "b8 32 c1 01" / 1.0.8.0 "70 b0 17 01" / matches
8b 81 3c 0a 00 00 83 f8 ff 74 0a c1 e0 04 8b 80 b8 32 c1 01 c3  CE
8b 81 3c 0a 00 00 83 f8 ff 74 0a c1 e0 04 8b 80 70 b0 17 01 c3  1.0.8.0
8b 81 3c 0a 00 00 83 f8 ff 74 0a c1 e0 04 8b 80 ^^ ?? ?? ?? c3

33 ff 83 cb ff 39 3d 98 32 c1 01 76 66 8b 6c 24 14 be b8 32 c1 01 8d 9b 00 00 00 00
33 ff 83 cd ff 39 3d 00 b2 17 01 76 6e 8b 5c 24 14 be 70 b0 17 01 8d 9b 00 00 00 00
33 ff 83 ?? ff 39 3d ?? ?? ?? 01 76 ?? 8b ?? 24 14 be ^^ ?? ?? ?? 8d 9b 00 00 00 00

83 c6 10 3b 3d 98 32 c1 01 72 d4 83 fb ff 74 26 8b f3 c1 e6 04 55 8b 8e b8 32 c1 01
47 83 c6 10 3b 3d 00 b2 17 01 72 d1 83 fd ff 74 2b 8b f5 c1 e6 04 8b 8e 70 b0 17 01
83 c6 10 3b 3d ?? ?? ?? 01 72 ?? 83 ?? ff 74 ?? 8b ?? c1 ?? 04 55 8b 8e ^^ ?? ?? ??                 CE
?? 83 c6 10 3b 3d ?? ?? ?? 01 72 ?? 83 ?? ff 74 ?? 8b ?? c1 ?? 04 8b 8e ^^ ?? ?? ??                 1.0.8.0

e8 69 53 fe ff 8b 87 3c 0a 00 00 83 f8 ff 74 0b c1 e0 04 8b 80 b8 32 c1 01 eb 02 33 c0
e8 57 6d fd ff 8b bf 3c 0a 00 00 83 ff ff 74 0b c1 e7 04 8b bf 70 b0 17 01 eb 02 33 ff
e8 ?? ?? ?? ff 8b ?? 3c 0a 00 00 83 ?? ff 74 0b c1 ?? 04 8b ?? ^^ ?? ?? ?? eb 02 33

e8 79 51 fe ff 8b 87 3c 0a 00 00 83 f8 ff 74 0b c1 e0 04 8b 80 b8 32 c1 01 eb 02 33 c0
e8 cb 6b fd ff 8b 87 3c 0a 00 00 83 f8 ff 74 0b c1 e0 04 8b 80 70 b0 17 01 eb 02 33 c0
e8 ?? ?? ?? ff 8b 87 3c 0a 00 00 83 f8 ff 74 0b c1 e0 04 8b 80 ^^ ?? ?? ?? eb 02 33 c0

ff b0 b8 32 c1 01 e8           // Substitute in the memory address after it's been found by one of the previous methods
8b 88 70 b0 17 01 53
ff ?? ^^ ?? ?? ?? e8                             CE
8b ?? ^^ ?? ?? ?? 53                             1.0.8.0
*/

/* +4 offset patterns CE 1.2.0.59 "bc 32 c1 01" / 1.0.8.0 "74 b0 17 01" / matches
e8 b8 9d ef ff ff 05 a0 32 c1 01 0f b7 c5 89 86 bc 32 c1 01
e8 b0 8f f6 ff 01 3d 0c b2 17 01 0f b7 d3 89 96 74 b0 17 01
e8 ?? ?? ?? ff ?? ?? ?? ?? ?? 01 0f b7 ?? 89 ?? ^^ ?? ?? ??
*/

/* +8 offset patterns CE 1.2.0.59 "c0 32 c1 01" / 1.0.8.0 "78 b0 17 01" / matches
8b 4c 24 04 c1 e1 04 8b 81 c0 32 c1 01 85 c0
8b 44 24 04 c1 e0 04 8b 88 78 b0 17 01 85 c9
8b ?? 24 04 c1 ?? 04 8b ?? ^^ ?? ?? ?? 85 ??

89 81 c0 32 c1 01 a1
89 88 78 b0 17 01 8b
89 ?? ^^ ?? ?? ?? a1                                CE
89 ?? ^^ ?? ?? ?? 8b                                1.0.8.0

a1 b4 35 b0 01 89 81 c4 32 c1 01 83 b9 c0 32 c1 01 00 75 06
0d 6c cb 0f 01 89 88 7c b0 17 01 83 b8 78 b0 17 01 00 75 07
0d ?? ?? ?? 01 89 ?? ?? ?? ?? 01 83 ?? ^^ ?? ?? ?? 00 75 ??

68 60 f0 81 01 b9 08 3f c1 01 e8 ed 00 f2 ff 33 ff be c0 32 c1 01 8d 9b 00 00 00 00
68 9c 04 d8 00 b9 a0 bc 17 01 e8 cd e1 f8 ff 33 c0 be 78 b0 17 01 8d 9b 00 00 00 00
68 ?? ?? ?? ?? b9 ?? ?? ?? 01 e8 ?? ?? ?? ff 33 ?? be ^^ ?? ?? ?? 8d 9b 00 00 00 00

8b 8e b8 32 c1 01 ff 86 c0 32 c1 01 e8 b8 9d ef ff ff
   bf 01 00 00 00 01 be 78 b0 17 01 53 e8 b0 8f f6 ff
                        ^^ ?? ?? ?? ?? ?? ?? ?? ?? ff

8b c7 c1 e0 04 ff 80 c0 32 c1 01
8b cf c1 e1 04 83 81 78 b0 17 01 01
8b ?? c1 ?? 04 ?? ?? ^^ ?? ?? ??

Extra for the last one in 1.0.8.0 only so far:
8b ?? c1 ?? 04 ?? ?? ** ?? ?? ?? 01 8d ?? ^^ ?? ?? ??
*/

/* +12 offset patterns CE 1.2.0.59 "c4 32 c1 01" / 1.0.8.0 "7c b0 17 01" / matches
89 81 c4 32 c1 01
89 88 7c b0 17 01
89 ?? ^^ ?? ?? ??
*/
static const DWORD kSlotTableRva = 0xE832B8; // 0x1C132B8 // 1.8 offset 0x117b070

/* Patterns CE "98 32 c1 01" | 1.0.8.0 "00 b2 17 01"
89 46 f8 c7 06 00 00 00 00 c7 46 04 00 00 00 00 85 c0 74 0e ff 05 98 32 c1 01 83 c6 10 83 ff 19 72 be
89 46 f8 c7 06 00 00 00 00 c7 46 04 00 00 00 00 74 11 83 05 00 b2 17 01 01 8b c7 83 c6 10 83 f8 19 72 b9
89 46 f8 c7 06 00 00 00 00 c7 46 04 00 00 00 00 85 ?? 74 ?? ?? 05 ^^ ?? ?? ?? 83 ?? 10 83 ?? 19 72
89 46 f8 c7 06 00 00 00 00 c7 46 04 00 00 00 00 74 ?? 83 05 ^^ ?? ?? ?? 01 8b ?? 83 ?? 10 83 ?? 19 72
*/
static const DWORD kSlotCountRva = 0xE83298; // 0x1C13298 // 1.8 offset 0x117b200

// Patterns: same as kSlotCountRva just target 19 (1 byte) in "?? 83 ?? 10 83 ?? 19 72"
static const DWORD kSlotBoundRva = 0x58D50F; // imm8 of `cmp edi, 0x19` // 0x131D50F // 1.8 offset 0x8254d4
struct SlotRef
{
    DWORD rva;
    DWORD field;
};
static const SlotRef kSlotRefs[] = { // relative CE 1.2.0.59
    {0x58B979, 8}, {0x58B984, 8}, {0x58B98F, 12}, {0x58B995, 8}, {0x58CBD0, 0},
    {0x58D4C6, 8}, {0x58ECC6, 0}, {0x58ED09, 0},  {0x58ED0F, 8}, {0x58ED23, 4},
    {0x58ED35, 8}, {0x599577, 0}, {0x599767, 0},  {0x828054, 0},
};
// CE 1.2.0.59 kSlotTableRva offsets    // 1.8 offsets:
// +4   0x1C132BC                       // +4   0x117B074
// +8   0x1C132C0                       // +8   0x117B078
// +12  0x1C132C4                       // +12  0x117B07C
//static const SlotRef kSlotRefs[] = { // CE 1.2.0.59
//    {0x131B979, 8}, {0x131B984, 8}, {0x131B98F, 12}, {0x131B995, 8}, {0x131CBD0, 0},
//    {0x131D4C6, 8}, {0x131ECC6, 0}, {0x131ED09, 0}, {0x131ED0F, 8}, {0x131ED23, 4},
//    {0x131ED35, 8}, {0x1329577 , 0}, {0x1329767, 0}, {0x15B8054, 0},
//};
static const int kNumSlotRefs = (int)(sizeof kSlotRefs / sizeof kSlotRefs[0]); // 14
static const int kStockSlots = 25;
static const int kMaxSlots = 64; // the relocated table's capacity

static int g_cfgSlots = 0; // 0 = leave stock
static __declspec(align(
    16)) DWORD g_slotTable[(kMaxSlots + 2) * 4]; // 32-byte header + entries
static bool g_slotsDone = false, g_slotsWarned = false;
static bool g_slotsMissedWindow =
    false; // audio init beat us to it; stop trying, but do not claim success

// ---- physical audio heap
// ------------------------------------------------------------------------
/* Patterns: addresses aren't referenced elsewhere, the value at the address: "00 00 E0 07"
be 00 00 e0 07 e8 cb 02 b4 ff
be 00 00 e0 07 e8 0a 7f c9 ff
be ^^ ** ** ** e8 ?? ?? ?? ff
*/
static const DWORD kAudioHeapRva = 0x4C158C;  // imm32 of `mov esi, 0x7E00000` // 0x125158C // 1.8 offset 0x7bc4ec
static const int kAudioHeapStock = 132120576; // 126 MB
static int g_cfgHeapMB = 0;                   // 0 = leave stock
static bool g_heapDone = false, g_heapWarned = false;

// ---- waveslots.xml
// ------------------------------------------------------------------------------
// The engine probes slots BY NAME, so raising the bound achieves nothing unless
// pc/audio/config/waveslots.xml also declares STREAM_ENGINE_1..Slots. This used
// to be a separate PowerShell script; Nexus quarantines any archive containing
// one, so it lives here instead.
//
// The file is read by the audio system, which initialises well after this DLL
// is attached, so writing it at load lands in time for the same session.
// Missing entries are APPENDED, never replaced, so another audio mod's edits
// survive. The original is copied to waveslots.xml.bak the first time, and
// never overwritten after that.
static const int kSlotBytes =
    794624; // every stock engine slot declares exactly this
static bool g_cfgWriteWaveslots = true;

static bool PatchAudioHeap(BYTE* base)
{
    BYTE* p = base + kAudioHeapRva;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof mbi) || !(mbi.State & MEM_COMMIT))
        return false;
    const int want = g_cfgHeapMB * 1024 * 1024;
    int cur = *(int*)p;
    if (cur == want)
    {
        g_heapDone = true;
        return true;
    }
    if (cur != kAudioHeapStock)
    {
        if (!g_heapWarned)
        {
            g_heapWarned = true;
            Log("audio heap: RVA %X reads %d, expected stock %d - waiting",
                kAudioHeapRva, cur, kAudioHeapStock);
        }
        return false;
    }
    __builtin_trap();
    DWORD old;
    if (!VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &old))
    {
        Log("audio heap: VirtualProtect failed %lu", GetLastError());
        return false;
    }
    //*(int*)p = want;
    VirtualProtect(p, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 4);
    g_heapDone = true;
    Log("audio heap: %d MB -> %d MB", kAudioHeapStock / (1024 * 1024),
        g_cfgHeapMB);
    return true;
}

static bool PatchEngineSlots(BYTE* base)
{
    if (*(DWORD*)(base + kSlotCountRva) != 0)
    {
        Log("engine slots: audio already initialised (count %u) - not touched",
            *(DWORD*)(base + kSlotCountRva));
        return false;
    }
    if (base[kSlotBoundRva] != 0x19)
    {
        //__builtin_trap();
        if (!g_slotsWarned)
        {
            g_slotsWarned = true;
            Log("engine slots: bound byte reads %02x, expected 19 - waiting",
                base[kSlotBoundRva]);
        }
        return false;
    }
    const DWORD oldBase = (DWORD)base + kSlotTableRva;
    for (int i = 0; i < kNumSlotRefs; i++)
    {
        DWORD v = *(DWORD*)(base + kSlotRefs[i].rva);
        if (v != oldBase + kSlotRefs[i].field)
        {
            if (!g_slotsWarned)
            {
                g_slotsWarned = true;
                Log("engine slots: site %X reads %08X, expected %08X - waiting",
                    kSlotRefs[i].rva, v, oldBase + kSlotRefs[i].field);
            }
            return false;
        }
    }
    memset(g_slotTable, 0, sizeof g_slotTable);
    const DWORD newBase = (DWORD)&g_slotTable[8];
    //__builtin_trap();
    for (int i = 0; i < kNumSlotRefs; i++)
    {
        BYTE* p = base + kSlotRefs[i].rva;
        DWORD old;
        if (!VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &old))
        {
            Log("engine slots: VirtualProtect failed at %X", kSlotRefs[i].rva);
            return false;
        }
        //*(DWORD*)p = newBase + kSlotRefs[i].field;
        VirtualProtect(p, 4, old, &old);
    }
    DWORD old;
    VirtualProtect(base + kSlotBoundRva, 1, PAGE_EXECUTE_READWRITE, &old);
    base[kSlotBoundRva] = (BYTE)g_cfgSlots;
    VirtualProtect(base + kSlotBoundRva, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), base + 0x58B000, 0x2E0000);
    g_slotsDone = true;
    Log("engine slots: table relocated to %08X (%d sites), probe bound %d -> "
        "%d",
        newBase, kNumSlotRefs, kStockSlots, g_cfgSlots);
    return true;
}

// First fatal exception in the process, logged and passed straight on to the
// game's own handler. This only ever reads and logs; it never swallows a crash.
static LONG g_excLogged = 0;
static LONG CALLBACK ExcLogger(EXCEPTION_POINTERS* ep)
{
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&g_excLogged) > 6)
        return EXCEPTION_CONTINUE_SEARCH;
    BYTE* base = (BYTE*)GetModuleHandleA(NULL);
    DWORD exeSize = 0x1BE6400; // 1.2.0.59 SizeOfImage
    CONTEXT* c = ep->ContextRecord;
    DWORD eip = c->Eip;
    HMODULE owner = nullptr;
    char modName[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)eip, &owner) &&
        owner)
    {
        GetModuleFileNameA(owner, modName, sizeof modName);
        const char* slash = strrchr(modName, '\\');
        if (slash)
            memmove(modName, slash + 1, strlen(slash));
    }
    Log("EXCEPTION %08X at %08X (%s+0x%X) tid %lu", code, eip, modName,
        eip - (DWORD)(owner ? owner : (HMODULE)base), GetCurrentThreadId());
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->NumberParameters >= 2)
        Log("  %s at %08X",
            ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
            (DWORD)ep->ExceptionRecord->ExceptionInformation[1]);
    Log("  eax %08X ebx %08X ecx %08X edx %08X esi %08X edi %08X ebp %08X esp "
        "%08X",
        c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    Log("  slot table at %08X, engine slot count %u", (DWORD)&g_slotTable[8],
        *(DWORD*)(base + kSlotCountRva));
    DWORD* sp = (DWORD*)(c->Esp & ~3u);
    int shown = 0;
    for (int i = 0; i < 512 && shown < 24; i++)
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(sp + i, &mbi, sizeof mbi) ||
            !(mbi.State & MEM_COMMIT) ||
            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            break;
        DWORD v = sp[i];
        if (v >= (DWORD)base && v < (DWORD)base + exeSize)
        {
            Log("  stack[%3d] %08X  GTAIV.exe+0x%X", i, v, v - (DWORD)base);
            shown++;
        }
        else if (g_self && v >= (DWORD)g_self && v < (DWORD)g_self + 0x40000)
        {
            Log("  stack[%3d] %08X  revd+0x%X", i, v, v - (DWORD)g_self);
            shown++;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Both patches have to land in the window between .text being decrypted and the
// audio system initialising, so poll quickly rather than waiting on one
// signature.
static DWORD WINAPI Worker(LPVOID)
{
    BYTE* base = (BYTE*)GetModuleHandleA(NULL);
    const bool wantHeap = g_cfgHeapMB > 0;
    const bool wantSlots = g_cfgSlots > kStockSlots;
    Log("base %p; waiting for decrypted .text (heap %s, slots %s)", base,
        wantHeap ? "on" : "off", wantSlots ? "on" : "off");
    if (!wantHeap && !wantSlots)
    {
        Log("nothing to do; both features off");
        return 0;
    }

    // Do this first and only once: the audio system parses waveslots.xml during
    // the same init the patches below are racing, so the file has to be right
    // before that starts.
    if (wantSlots)
    {
        Log("waveslots.xml editing disabled in source");
    }

    for (int i = 0; i < 2400; i++)
    { // up to 2 minutes at 50 ms
        if (wantHeap && !g_heapDone)
            PatchAudioHeap(base);
        if (wantSlots && !g_slotsDone)
        {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(base + kSlotBoundRva, &mbi, sizeof mbi) &&
                (mbi.State & MEM_COMMIT) && !(mbi.Protect & PAGE_NOACCESS) &&
                !(mbi.Protect & PAGE_GUARD))
            {
                if (*(DWORD*)(base + kSlotCountRva) != 0)
                {
                    Log("engine slots: audio initialised before the patch "
                        "could land - "
                        "left stock");
                    g_slotsMissedWindow =
                        true; // stop trying; the window is gone
                }
                else
                {
                    PatchEngineSlots(base);
                }
            }
        }
        if ((!wantHeap || g_heapDone) &&
            (!wantSlots || g_slotsDone || g_slotsMissedWindow))
        {
            Log("finished after %d polls; heap %s, slots %s", i,
                !wantHeap ? "off" : (g_heapDone ? "patched" : "NOT patched"),
                !wantSlots ? "off" : (g_slotsDone ? "patched" : "NOT patched"));
            return 0;
        }
        Sleep(50);
    }
    Log("timed out; heap %s, slots %s",
        !wantHeap ? "off" : (g_heapDone ? "patched" : "NOT patched"),
        !wantSlots ? "off" : (g_slotsDone ? "patched" : "NOT patched"));
    return 0;
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
        g_self = mod;
        AddVectoredExceptionHandler(1, ExcLogger);
        char path[MAX_PATH];
        GetModuleFileNameA(mod, path, MAX_PATH);
        char* dot = strrchr(path, '.');
        if (dot)
            *dot = 0;
        sprintf(g_log, "%s.log", path);
        sprintf(g_ini, "%s.ini", path);

        g_cfgSlots = GetPrivateProfileIntA("revd", "Slots", 0, g_ini);
        g_cfgHeapMB = GetPrivateProfileIntA("revd", "AudioHeapMB", 0, g_ini);
        g_cfgWriteWaveslots =
            GetPrivateProfileIntA("revd", "WriteWaveslots", 1, g_ini) != 0;

        if (g_cfgSlots != 0 && g_cfgSlots <= kStockSlots)
        {
            Log("Slots=%d is at or below stock %d - nothing to raise",
                g_cfgSlots, kStockSlots);
            g_cfgSlots = 0;
        }
        if (g_cfgSlots > kMaxSlots)
        {
            Log("Slots=%d exceeds the table capacity - clamped to %d",
                g_cfgSlots, kMaxSlots);
            g_cfgSlots = kMaxSlots;
        }
        if (g_cfgHeapMB != 0 && g_cfgHeapMB < 126)
        {
            Log("AudioHeapMB=%d is below stock 126 - ignored", g_cfgHeapMB);
            g_cfgHeapMB = 0;
        }
        if (g_cfgHeapMB > 512)
        {
            Log("AudioHeapMB=%d clamped to 512", g_cfgHeapMB);
            g_cfgHeapMB = 512;
        }
        if (g_cfgSlots > 30 && g_cfgHeapMB == 0)
            Log("WARNING: Slots=%d with the stock 126 MB audio heap will very "
                "likely "
                "crash during audio init. Set AudioHeapMB.",
                g_cfgSlots);

        Log("revd loaded: Slots=%d AudioHeapMB=%d WriteWaveslots=%d (stock 25 "
            "/ "
            "126)",
            g_cfgSlots, g_cfgHeapMB, g_cfgWriteWaveslots ? 1 : 0);
        HANDLE t = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
        if (t)
            CloseHandle(t);
    }
    return TRUE;
}
