//if gc is used here then we're fucked
struct LuaZIO {
    size_t      n;        
    const char* p;        
    lua_Reader  reader;   
    void* data;     
    lua_State* L_field;  
};

struct LuaMbuffer {
    char* buffer;    
    size_t n;         
    size_t buffsize;  
};

struct LuaSParser {
    LuaZIO* z;      
    LuaMbuffer  buff;   
    const char* name;   
    int         extra;  
};

using tFParser = void(__cdecl*)(lua_State*, void*);
using tLuaDPCall = int(__cdecl*)(lua_State*, tFParser, void*, ptrdiff_t, ptrdiff_t);

static tFParser   g_f_parser = nullptr;
static tLuaDPCall g_luaD_pcall = nullptr;

struct LoadS { const char* s; size_t size; };

static const char* getS(lua_State* , void* ud, size_t* sz) {
    LoadS* ls = (LoadS*)ud;
    if (ls->size == 0) return nullptr;
    *sz = ls->size;
    ls->size = 0;
    return ls->s;
}

static void ResolveApi() {
    lua_newthread = (tLuaNewThread)R(ida::lua_newthread);
    lua_gettop = (tLuaGetTop)R(ida::lua_gettop);
    lua_settop = (tLuaSetTop)R(ida::lua_settop);
    lua_setfield = (tLuaSetField)R(ida::lua_setfield);
    lua_pushstring = (tLuaPushString)R(ida::lua_pushstring);
    lua_pushlstring = (tLuaPushLString)R(ida::lua_pushlstring);
    lua_pushnumber = (tLuaPushNumber)R(ida::lua_pushnumber);
    lua_pushcclosure = (tLuaPushCClosure)R(ida::lua_pushcclosure);
    lua_getfield = (tLuaGetField)R(ida::lua_getfield);
    lua_createtable = (tLuaCreateTable)R(ida::lua_createtable);
    lua_settable = (tLuaSetTable)R(ida::lua_settable);
    lua_tolstring = (tLuaToLString)R(ida::lua_tolstring);
    lua_gc = (tLuaGc)R(ida::lua_gc);
    lua_pcall = (tLuaPCall)R(ida::lua_pcall);
    lua_rawgeti = (tLuaRawGetI)R(ida::lua_rawgeti);
    lua_resume = (tLuaResume)R(ida::lua_resume);

    g_f_parser = (tFParser)R(ida::f_parser);
    g_luaD_pcall = (tLuaDPCall)R(0x0051EF80);   

    g_rbxLoadProto = (tRbxLoadProto)R(ida::rbx_load_proto);
    g_rbxNewClosure = (tRbxNewClosure)R(ida::rbx_new_closure);
    g_rbxNewUpvalue = (tRbxNewUpvalue)R(ida::rbx_new_upvalue);
    g_rbxMakeString = (tRbxMakeString)R(ida::rbx_make_string);
    g_index2adr = (tIndex2Adr)R(ida::index2adr);
    g_growstack = (tGrowStack)R(ida::luaD_growstack);
    g_currentSecurity = (tCurrentSecurity)R(ida::currentSecurity);
    g_checkPermission = (tCheckPermission)R(ida::checkPermission);

}

static inline uintptr_t slot_at(lua_State* L, int idx) {
    uintptr_t top = *(uintptr_t*)((uintptr_t)L + L_fld::top);
    return top + (uintptr_t)(idx * (int)TVALUE_SIZE);
}
static inline int my_type(lua_State* L, int idx) {
    return *(int32_t*)(slot_at(L, idx) + TVALUE_TT);
}

static inline double my_tonumber(lua_State* L, int idx) {
    uint64_t raw = *(uint64_t*)slot_at(L, idx) ^ 0x109E29EF109E29EFULL;
    double d; memcpy(&d, &raw, sizeof(d));
    return d;
}
static const char* TypeName(int t) {
    switch (t) {
    case 0: return "nil";
    case 1: return "?1";
    case 2: return "number";
    case 3: return "boolean";
    case 4: return "string";
    case 5: return "?5";
    case 6: return "function";
    case 7: return "table";
    case 8: return "userdata";
    default: return "?";
    }
}

static inline int my_type_abs(lua_State* L, int i) {
    uintptr_t b = *(uintptr_t*)((uintptr_t)L + L_fld::base);
    return *(int32_t*)(b + (uintptr_t)(i - 1) * TVALUE_SIZE + TVALUE_TT);
}

static inline void my_pushvalue_abs(lua_State* L, int i) {
    if (g_growstack) {
        uintptr_t sl = *(uintptr_t*)((uintptr_t)L + 0x20);
        uintptr_t tp = *(uintptr_t*)((uintptr_t)L + 0x10);
        if (sl - tp <= 16) g_growstack(L, 1);
    }
    uintptr_t  b = *(uintptr_t*)((uintptr_t)L + L_fld::base);
    uintptr_t* topp = (uintptr_t*)((uintptr_t)L + L_fld::top);
    memcpy((void*)*topp, (const void*)(b + (uintptr_t)(i - 1) * TVALUE_SIZE), TVALUE_SIZE);
    *topp += TVALUE_SIZE;
}

static volatile LONG g_hookPrint = 0;

static int __cdecl l_dprint(lua_State* L) {
    int n = my_gettop(L);
    std::string out;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) out += "\t";
        int tt = my_type_abs(L, i);
        if (tt == LUA_TSTRING || tt == LUA_TNUMBER) {
            const char* s = lua_tolstring(L, i, nullptr);
            out += s ? s : "?";
            continue;
        }
        if (tt == LUA_TNIL) { out += "nil"; continue; }
        if (tt == LUA_TBOOLEAN) {
            uintptr_t b = *(uintptr_t*)((uintptr_t)L + L_fld::base);
            out += *(int32_t*)(b + (uintptr_t)(i - 1) * TVALUE_SIZE) ? "true" : "false";
            continue;
        }
        int save = my_gettop(L);
        lua_getfield(L, LUA_GLOBALSINDEX, "tostring");
        bool done = false;
        if (my_type(L, -1) == LUA_TFUNCTION) {
            my_pushvalue_abs(L, i);
            if (lua_pcall(L, 1, 1, 0) == 0 && my_type(L, -1) == LUA_TSTRING) {
                const char* s = lua_tolstring(L, -1, nullptr);
                out += s ? s : "?";
                done = true;
            }
        }
        if (!done) out += TypeName(tt);
        my_settop(L, save);
    }
    return 0;
}

static void RegisterDPrint(lua_State* L) {
    if (!lua_pushcclosure || !lua_setfield) return;
    lua_pushcclosure(L, (tLuaCFunction)l_dprint, 0);
    lua_setfield(L, LUA_GLOBALSINDEX, "dprint");
    if (g_hookPrint) {
        lua_pushcclosure(L, (tLuaCFunction)l_dprint, 0);
        lua_setfield(L, LUA_GLOBALSINDEX, "print");
    }
}

struct StackGuard {
    lua_State* L;
    int        base;
    explicit StackGuard(lua_State* l) : L(l), base(my_gettop(l)) {}
    ~StackGuard() { my_settop(L, base); }
};

static inline void my_pushboolean(lua_State* L, int b) {
    uintptr_t* top = (uintptr_t*)((uintptr_t)L + L_fld::top);
    *(int32_t*)(*top + 0) = b ? 1 : 0;   
    *(int32_t*)(*top + TVALUE_TT) = LUA_TBOOLEAN;
    *top += TVALUE_SIZE;
}

static int GetMember(lua_State* L, const char* name) {
    lua_getfield(L, -1, name);
    return my_type(L, -1);
}

static bool PushGame(lua_State* L) {
    lua_getfield(L, LUA_GLOBALSINDEX, "game");
    return my_type(L, -1) != LUA_TNIL;
}

static bool PushService(lua_State* L, const char* service) {
    if (!PushGame(L)) return false;
    return GetMember(L, service) != LUA_TNIL;
}

static bool PushHumanoid(lua_State* L) {
    if (GetMember(L, "Character") == LUA_TNIL) return false;   
    if (GetMember(L, "Humanoid") == LUA_TNIL) return false;
    return true;
}

static int CompileScript(lua_State* L, const char* src, size_t len, const char* name) {
    if (!g_f_parser || !g_luaD_pcall) {
        Err("parser funcs missing\n");
        return -1;
    }

    
    
    
    
    LuaZIO z;
    z.n = len;      
    z.p = src;      
    z.reader = nullptr;  
    z.data = nullptr;
    z.L_field = L;

    LuaSParser sp;
    sp.z = &z;
    sp.buff = { nullptr, 0, 0 };
    sp.name = name;
    sp.extra = 1;

    uintptr_t L_top = *(uintptr_t*)((uintptr_t)L + 0x10);
    uintptr_t L_stack = *(uintptr_t*)((uintptr_t)L + 0x18);
    ptrdiff_t old_top = (ptrdiff_t)(L_top - L_stack);

    return g_luaD_pcall(L, g_f_parser, &sp, old_top, 0);
}
