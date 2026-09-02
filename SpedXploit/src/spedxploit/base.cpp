//old client stuff
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <deque>
#include <mutex>
#include <functional>
#include <string>
#include <memory>
#include <unordered_map>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#define LUAC_PATH "C:\\Lua\\luac5.1.exe"

struct lua_State;   

#define LOG_PATH        "C:\\Users\\Public\\SpedXploit.log"
#define SCAN_TIMEOUT_MS 60000

#define STAGE            3
#define RUNQUEUE_BALANCE 1   
#define JOB3_GETTOP      1   
#define JOB3_GC          0   
#define DLOG_SYNC        0   

static volatile LONG g_waitTrace = 0;   

static uintptr_t g_guardOldBase = 0;
static uintptr_t g_guardOldSize = 0;
static int g_targetIdentity = 7; 
static volatile LONG g_execSpawn = 1;   

namespace ida {
    constexpr uintptr_t ImageBase = 0x400000;

    constexpr uintptr_t ScriptContext_vftable = 0x00FB7BB4;

    constexpr uintptr_t wait_cfunc = 0x006A8D00;   
    constexpr uintptr_t print_cfunc = 0x006A24E0;   

    constexpr uintptr_t lua_newthread = 0x0051C920;
    constexpr uintptr_t lua_gettop = 0x0051C690;
    constexpr uintptr_t lua_settop = 0x0051D5D0;
    constexpr uintptr_t lua_setfield = 0x0051D3D0;
    constexpr uintptr_t lua_pushstring = 0x0051CEA0;
    constexpr uintptr_t lua_pushlstring = 0x0051CDC0;
    constexpr uintptr_t lua_pushnumber = 0x0051CE50;
    constexpr uintptr_t lua_pushcclosure = 0x0051CC00;
    constexpr uintptr_t lua_createtable = 0x0051C2F0;
    constexpr uintptr_t lua_settable = 0x0051D0B0;
    constexpr uintptr_t lua_tolstring = 0x0051D760;
    constexpr uintptr_t lua_gc = 0x0051C3E0;
    constexpr uintptr_t lua_getfield = 0x0051C560;
    constexpr uintptr_t lua_resume = 0x0051F650;

    constexpr uintptr_t rbx_load_proto = 0x00406FE0;  
    constexpr uintptr_t rbx_new_closure = 0x0051E830;  
    constexpr uintptr_t rbx_new_upvalue = 0x0051E940;  
    constexpr uintptr_t rbx_make_string = 0x0051E210;  

    constexpr uintptr_t lua_pcall = 0x0051CB30;
    constexpr uintptr_t lua_rawgeti = 0x0051C6B0;

    constexpr uintptr_t GuardBase = 0x00F9B4DC;   
    constexpr uintptr_t GuardSize = 0x00F9B4E0;   

    constexpr uintptr_t lua_load_internal = 0x0051EF80;  
    constexpr uintptr_t f_parser = 0x0051EC80;  

    constexpr uintptr_t index2adr = 0x0051BE30;
    constexpr uintptr_t luaD_growstack = 0x0051EF50;

    constexpr uintptr_t currentSecurity = 0x00605B70;  
    constexpr uintptr_t checkPermission = 0x00605C30;  

    
}

namespace sc_off {
    constexpr uintptr_t GlobalStates = 0x68;
    constexpr uintptr_t GlobalStateSize = 0x40;
    constexpr int       GlobalStateCount = 3;
}

namespace L_off {
    constexpr intptr_t PrefixStart = -0x2C;
    constexpr intptr_t OwnerPtr = -0x20;   
    constexpr intptr_t ScriptWeakPtr = -0x0C;
    constexpr intptr_t ThreadNode = -0x04;
}

#define LUA_REGISTRYINDEX (-10000)
#define LUA_ENVIRONINDEX  (-10001)
#define LUA_GLOBALSINDEX  (-10002)
#define LUA_GCSTOP        0
#define LUA_GCRESTART     1
#define LUA_GCCOUNT       3

#define LUA_TNIL       0
#define LUA_TNUMBER    2
#define LUA_TBOOLEAN   3
#define LUA_TSTRING    4
#define LUA_TFUNCTION  6
#define LUA_TTABLE     7
#define LUA_TUSERDATA  8

namespace L_fld {
    constexpr uintptr_t top = 0x10;   
    constexpr uintptr_t base = 0x1C;   
}

constexpr size_t TVALUE_SIZE = 16;     
constexpr size_t TVALUE_TT = 8;      

#ifndef LUA_TNIL
#define LUA_TNIL 0
#endif

static inline int my_gettop(lua_State* L) {
    uintptr_t s = (uintptr_t)L;
    return (int)((*(uintptr_t*)(s + L_fld::top) - *(uintptr_t*)(s + L_fld::base))
        / TVALUE_SIZE);
}

static inline void my_settop(lua_State* L, int idx) {
    uintptr_t  s = (uintptr_t)L;
    uintptr_t* top = (uintptr_t*)(s + L_fld::top);

    if (idx >= 0) {
        uintptr_t want = *(uintptr_t*)(s + L_fld::base) + (uintptr_t)idx * TVALUE_SIZE;
        while (*top < want) {                              
            *(uint32_t*)(*top + TVALUE_TT) = LUA_TNIL;
            *top += TVALUE_SIZE;
        }
        *top = want;
    }
    else {
        *top += (uintptr_t)(idx * (int)TVALUE_SIZE + (int)TVALUE_SIZE);
    }
}

using tLuaCFunction = int(__cdecl*)(lua_State*);
using tLuaNewThread = lua_State * (__cdecl*)(lua_State*);
using tLuaGetTop = int(__cdecl*)(lua_State*);
using tLuaSetTop = void(__cdecl*)(lua_State*, int);
using tLuaSetField = void(__cdecl*)(lua_State*, int, const char*);
using tLuaPushString = void(__cdecl*)(lua_State*, const char*);
using tLuaPushLString = void(__cdecl*)(lua_State*, const char*, size_t);
using tLuaPushNumber = void(__cdecl*)(lua_State*, double);
using tLuaPushCClosure = void(__cdecl*)(lua_State*, tLuaCFunction, int);
using tLuaCreateTable = void(__cdecl*)(lua_State*, int, int);
using tLuaSetTable = void(__cdecl*)(lua_State*, int);
using tLuaToLString = const char* (__cdecl*)(lua_State*, int, size_t*);
using tLuaGc = int(__cdecl*)(lua_State*, int, int);
using tLuaGetField = void(__cdecl*)(lua_State*, int, const char*);
using tLuaPCall = int(__cdecl*)(lua_State*, int, int, int);
using tLuaRawGetI = int(__cdecl*)(lua_State*, int, int);
using tIndex2Adr = int(__cdecl*)(lua_State*, int);   
using tGrowStack = void(__cdecl*)(lua_State*, int);
using tLuaResume = int(__cdecl*)(lua_State*, int);

using tCurrentSecurity = int* (__cdecl*)();

using tCheckPermission = int(__cdecl*)(int, int);

static tLuaResume lua_resume = nullptr;
static tCurrentSecurity g_currentSecurity = nullptr;
static tCheckPermission g_checkPermission = nullptr;
static tGrowStack g_growstack = nullptr;
static tIndex2Adr g_index2adr = nullptr;
static tLuaPCall   lua_pcall = nullptr;
static tLuaRawGetI lua_rawgeti = nullptr;
static tLuaGetField lua_getfield = nullptr;
static tLuaNewThread    lua_newthread = nullptr;
static tLuaGetTop       lua_gettop = nullptr;
static tLuaSetTop       lua_settop = nullptr;
static tLuaSetField     lua_setfield = nullptr;
static tLuaPushString   lua_pushstring = nullptr;
static tLuaPushLString  lua_pushlstring = nullptr;
static tLuaPushNumber   lua_pushnumber = nullptr;
static tLuaPushCClosure lua_pushcclosure = nullptr;
static tLuaCreateTable  lua_createtable = nullptr;
static tLuaSetTable     lua_settable = nullptr;
static tLuaToLString    lua_tolstring = nullptr;
static tLuaGc           lua_gc = nullptr;

struct RbxStreamInfo { const uint8_t* base; const uint8_t* end; };
struct RbxStream { RbxStreamInfo* si; uint32_t pos; };

using tRbxLoadProto = int(__cdecl*)(RbxStream*, lua_State*, int, int);
using tRbxNewClosure = int(__cdecl*)(lua_State*, int, int);
using tRbxNewUpvalue = int(__cdecl*)(lua_State*);
using tRbxMakeString = int(__cdecl*)(lua_State*, const char*, size_t);

static tRbxLoadProto  g_rbxLoadProto = nullptr;
static tRbxNewClosure g_rbxNewClosure = nullptr;
static tRbxNewUpvalue g_rbxNewUpvalue = nullptr;
static tRbxMakeString g_rbxMakeString = nullptr;

static uintptr_t  g_base = 0;
static HANDLE     g_proc = nullptr;
static HMODULE    g_self = nullptr;
static void* g_scriptContext = nullptr;
static lua_State* g_states[sc_off::GlobalStateCount] = {};
static lua_State* g_L = nullptr;   
static int        g_LIndex = -1;
static volatile LONG g_unloading = 0;
static int g_execState = -1;

static std::mutex              g_defMutex;
static std::deque<std::string> g_defLines;

static inline uintptr_t R(uintptr_t ida) { return g_base + (ida - ida::ImageBase); }

static FILE* g_log = nullptr;
static std::mutex g_logMutex;
static volatile LONG g_haveConsole = 0;
static void GuiLogPush(const char* s);

static void Emit(const char* s) {
    if (!s || !*s) return;
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_haveConsole) { fputs(s, stdout); fflush(stdout); }
    if (g_log) { fputs(s, g_log); fflush(g_log); }
    GuiLogPush(s);
}

static void Err(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    Emit(buf);
}

static void Say(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    Emit(buf);
}

static void DrainDeferred() {}

static bool SafeRead(uintptr_t src, void* dst, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(g_proc, (LPCVOID)src, dst, n, &got) && got == n;
}

template <typename T>
static bool SafeReadT(uintptr_t addr, T& out) { return SafeRead(addr, &out, sizeof(T)); }

static bool LooksLikePointer(uintptr_t p) {
    return p >= 0x00010000 && p < 0x7FFF0000 && (p & 3) == 0;
}

static bool DisableCallerCheck() {
    uintptr_t* p = (uintptr_t*)R(ida::GuardBase);   

    uintptr_t oldBase = 0, oldSize = 0;
    if (!SafeReadT((uintptr_t)&p[0], oldBase) || !SafeReadT((uintptr_t)&p[1], oldSize)) {
        Err("guard read failed\n");
        return false;
    }

    if (oldBase < g_base || oldBase > g_base + 0x02000000) {
        Err("bad guard values\n");
        return false;
    }

    DWORD old = 0;
    g_guardOldBase = oldBase;
    g_guardOldSize = oldSize;
    if (!VirtualProtect(p, 8, PAGE_READWRITE, &old)) return false;
    p[0] = 0;
    p[1] = 0xFFFFFFFF;          
    VirtualProtect(p, 8, old, &old);

    return true;
}

#define LDUMP(tag, LS) (void)0
