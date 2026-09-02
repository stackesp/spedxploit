//errors only
static void CmdPermMatrix() {
    if (!g_checkPermission) { Err("checkpermission missing\n"); return; }
    static const char* kPerm[7] = {
        "None", "Plugin", "RobloxPlace", "LocalUser",
        "WritePlayer", "RobloxScript", "Roblox"
    };

    for (int id = 0; id <= 7; ++id) {
        char line[128];
        int n = sprintf_s(line, sizeof(line), "[sec] %2d |", id);
        for (int p = 0; p <= 6; ++p)
            n += sprintf_s(line + n, sizeof(line) - n, "    %d",
                g_checkPermission(id, p) ? 1 : 0);

    }

    (void)kPerm;
}

static bool HandleCommand(char* line) {
    char* tok[4] = {};
    int   n = Tokenize(line, tok);
    if (n == 0) return true;

    const char* c = tok[0];
    Target t;
    double v = 0.0;

    
    if (!_stricmp(c, "help") || !_stricmp(c, "?")) { PrintHelp(); return true; }
    if (!_stricmp(c, "q") || !_stricmp(c, "exit")) { return false; }
    if (!_stricmp(c, "states")) { Report(); return true; }
    if (!_stricmp(c, "permmatrix")) { CmdPermMatrix(); return true; }
    if (!_stricmp(c, "estate")) {
        if (n >= 2) { g_execState = atoi(tok[1]); Say("state: %d\n", g_execState); }
        else          Say("state: %d\n", g_execState);
        return true;
    }
    if (!_stricmp(c, "gc")) {
        Schedule([](lua_State* L) {
            Say("gc: %d kb\n", lua_gc(L, LUA_GCCOUNT, 0));
            });
        return true;
    }

    
    if (!_stricmp(c, "fogstart") && n >= 2) { CmdServiceNumber("Lighting", "FogStart", atof(tok[1])); return true; }
    if (!_stricmp(c, "fogend") && n >= 2) { CmdServiceNumber("Lighting", "FogEnd", atof(tok[1])); return true; }
    if (!_stricmp(c, "brightness") && n >= 2) { CmdServiceNumber("Lighting", "Brightness", atof(tok[1])); return true; }
    if (!_stricmp(c, "gravity") && n >= 2) { CmdServiceNumber("Workspace", "Gravity", atof(tok[1])); return true; }
    if (!_stricmp(c, "time") && n >= 2) {
        char timebuf[16];
        int colons = 0;
        for (const char* p = tok[1]; *p; ++p) if (*p == ':') ++colons;
        if (colons == 1) snprintf(timebuf, sizeof(timebuf), "%s:00", tok[1]);
        else             strncpy_s(timebuf, tok[1], _TRUNCATE);
        CmdServiceString("Lighting", "TimeOfDay", timebuf);
        return true;
    }
    
    if (!_stricmp(c, "ws") || !_stricmp(c, "walkspeed")) {
        if (!ParseTargetValue(tok, n, t, v)) { (void)0; return true; }
        CmdHumanoidNumber(t, "WalkSpeed", v); return true;
    }
    if (!_stricmp(c, "hp") || !_stricmp(c, "health")) {
        if (!ParseTargetValue(tok, n, t, v)) { (void)0; return true; }
        CmdHumanoidNumber(t, "Health", v); return true;
    }
    if (!_stricmp(c, "maxhp")) {
        if (!ParseTargetValue(tok, n, t, v)) { (void)0; return true; }
        CmdHumanoidNumber(t, "MaxHealth", v); return true;
    }

    
    t = ParseTarget(n >= 2 ? tok[1] : "me");
    if (!_stricmp(c, "god")) { CmdGod(t);                                 return true; }
    if (!_stricmp(c, "kill")) { CmdHumanoidNumber(t, "Health", 0.0);    return true; }
    if (!_stricmp(c, "freeze")) { CmdHumanoidNumber(t, "WalkSpeed", 0.0);   return true; }
    if (!_stricmp(c, "thaw")) { CmdHumanoidNumber(t, "WalkSpeed", 16.0);   return true; }
    if (!_stricmp(c, "sit")) { CmdHumanoidBool(t, "Sit", true);        return true; }
    if (!_stricmp(c, "stand")) { CmdHumanoidBool(t, "Sit", false);       return true; }
    if (!_stricmp(c, "jump")) { CmdHumanoidBool(t, "Jump", true);        return true; }
    if (!_stricmp(c, "info")) { CmdInfo(t);                                return true; }
    if (!_stricmp(c, "btools")) { CmdBtools(t);                              return true; }
    if (!_stricmp(c, "ident")) {
        if (n >= 2) { g_targetIdentity = atoi(tok[1]); Say("identity: %d\n", g_targetIdentity); }
        else          Say("identity: %d\n", g_targetIdentity);
        return true;
    }
    if (!_stricmp(c, "hookprint")) {
        InterlockedExchange(&g_hookPrint,
            (n >= 2 && (!_stricmp(tok[1], "on") || !_stricmp(tok[1], "1"))) ? 1 : 0);
        Say("print hook: %s\n", g_hookPrint ? "on" : "off");
        return true;
    }

    if (!_stricmp(c, "waittrace")) {
        InterlockedExchange(&g_waitTrace,
            (n >= 2 && (!_stricmp(tok[1], "on") || !_stricmp(tok[1], "1"))) ? 1 : 0);
        Say("wait trace: %s\n", g_waitTrace ? "on" : "off");
        return true;
    }

    if (!_stricmp(c, "mode")) {
        if (n >= 2) InterlockedExchange(&g_execSpawn, _stricmp(tok[1], "direct") ? 1 : 0);
        Say("mode: %s\n", g_execSpawn ? "spawn" : "direct");
        return true;
    }

    if (!_stricmp(c, "lscheck")) {
        Schedule([](lua_State* L) {
            StackGuard g(L);

            
            uintptr_t s = (uintptr_t)L;
            uintptr_t owner_ptr = *(uintptr_t*)(s - 0x20);
            uintptr_t sc_ptr = owner_ptr ? *(uintptr_t*)(owner_ptr + 4) : 0;

            lua_getfield(L, LUA_GLOBALSINDEX, "load");

            if (my_type(L, -1) == LUA_TFUNCTION) {
                lua_pushstring(L, "return 1+1");
                int rc2 = lua_pcall(L, 1, 2, 0);

                if (rc2 != 0 && my_type(L, -1) == LUA_TSTRING)
                    Err("load failed: %s\n", lua_tolstring(L, -1, nullptr));
                else if (rc2 == 0 && my_type(L, -2) == LUA_TFUNCTION)

            }
            
            });
        return true;   
    }

    if (!_stricmp(c, "exec")) {
        CmdExec(n >= 2 ? tok[1] : nullptr);
        return true;
    }

    Err("unknown command: %s\n", c);
    return true;
}

static DWORD WINAPI DrainThread(LPVOID) {
    while (!g_unloading) {
        DrainDeferred();
        Sleep(200);
    }
    return 0;
}

static void ConsoleLoop() {
    PrintHelp();
    char line[512];
    for (;;) {
        fputs("> ", stdout);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;   
        if (!HandleCommand(line)) break;
    }
}

static void QueueWalkSpeed(double speed) {
    Schedule([speed](lua_State* L) {
        int base = my_gettop(L);

        static const char* kPath[] = {
            "Players", "LocalPlayer", "Character", "Humanoid"
        };

        lua_getfield(L, LUA_GLOBALSINDEX, "game");
        if (my_type(L, -1) == LUA_TNIL) {
            Err("game missing\n");
            my_settop(L, base);
            return;
        }

        for (int i = 0; i < 4; ++i) {
            lua_getfield(L, -1, kPath[i]);
            int t = my_type(L, -1);

            if (t == LUA_TNIL) {
                Err("%s missing\n", kPath[i]);
                my_settop(L, base);
                return;
            }
        }

        
        lua_getfield(L, -1, "WalkSpeed");
        if (my_type(L, -1) == LUA_TNUMBER)

        else

        my_settop(L, -2);                       

        lua_pushnumber(L, speed);
        lua_setfield(L, -2, "WalkSpeed");       

        lua_getfield(L, -1, "WalkSpeed");
        if (my_type(L, -1) == LUA_TNUMBER)

        my_settop(L, base);
        });
}

static bool FindEverything() {
    g_proc = GetCurrentProcess();
    g_base = (uintptr_t)GetModuleHandleA(nullptr);
    if (!g_base) { Err("getmodulehandle failed\n"); return false; }

    ResolveApi();

    for (int t = 0; t < SCAN_TIMEOUT_MS; t += 1000) {
        lua_State* found[sc_off::GlobalStateCount] = {};
        void* sc = FindScriptContext(found);
        if (sc) {
            g_scriptContext = sc;
            for (int i = 0; i < sc_off::GlobalStateCount; ++i) g_states[i] = found[i];
            for (int i = sc_off::GlobalStateCount - 1; i >= 0; --i)
                if (g_states[i]) { g_L = g_states[i]; g_LIndex = i; break; }
            return g_L != nullptr;
        }

        Sleep(1000);
    }
    return false;
}

static bool InstallScheduler() {
    void* target = (void*)R(ida::wait_cfunc);
    if (!detour::Install(g_waitHook, target, (void*)&hk_wait)) return false;
    g_origWait = (tLuaCFunction)g_waitHook.trampoline;

    return true;
}
