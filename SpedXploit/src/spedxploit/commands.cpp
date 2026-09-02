//small cmd layer
static Target ParseTarget(const char* s) {
    Target t{};
    t.kind = TGT_NAME;
    t.name[0] = 0;
    if (!s || !*s) { t.kind = TGT_ME;     return t; }
    if (!_stricmp(s, "me")) { t.kind = TGT_ME;     return t; }
    if (!_stricmp(s, "all")) { t.kind = TGT_ALL;    return t; }
    if (!_stricmp(s, "others")) { t.kind = TGT_OTHERS; return t; }
    strncpy_s(t.name, s, _TRUNCATE);
    return t;
}

static void ForEachPlayer(lua_State* L, const Target& t,
    const std::function<void(lua_State*)>& fn)
{
    StackGuard g(L);   

    
    if (!PushService(L, "Players")) {
        Err("players service unavailable\n");
        return;
    }
    

    if (t.kind == TGT_ME || t.kind == TGT_NAME) {
        
        
        
        
        
        const char* key = (t.kind == TGT_ME) ? "LocalPlayer" : t.name;

        if (t.kind == TGT_ME) {
            
            if (GetMember(L, "LocalPlayer") == LUA_TNIL) {
                Err("localplayer missing\n");
                return;
            }
            fn(L);
        }
        else {
            
            
            
            
            
            
            
            
            
            
            

            int top_before = my_gettop(L);
            lua_rawgeti(L, -1, 0);   
            my_settop(L, top_before);

            
            
            
            
            
            
            
            lua_getfield(L, LUA_GLOBALSINDEX, "rawget");  
            my_settop(L, top_before);   

            
            
            
            
            
            
            
            
            
            
            
            

            
            
            
            if (GetMember(L, t.name) == LUA_TNIL) {
                Err("player not found: %s\n", t.name);
                return;
            }
            fn(L);
        }
        return;
    }

    
    
    
    
    
    
    
    
    

    int players_slot = my_gettop(L);   

    
    if (GetMember(L, "GetPlayers") != LUA_TFUNCTION) {

        return;
    }
    

    
    lua_rawgeti(L, LUA_REGISTRYINDEX, 0);   
    
    
    my_settop(L, players_slot);   

    
    
    
    
    
    lua_getfield(L, players_slot, "GetPlayers");   
    

    
    
    lua_getfield(L, players_slot - 1, "Players");
    

    int rc = lua_pcall(L, 1, 1, 0);
    
    

    if (rc != 0) {
        Err("getplayers failed %d: %s\n", rc,
            my_type(L, -1) == LUA_TSTRING
            ? lua_tolstring(L, -1, nullptr) : "?");
        return;
    }

    
    
    int result_slot = my_gettop(L);   

    
    
    
    
    char localName[64] = {};
    if (t.kind == TGT_OTHERS) {
        int snap = my_gettop(L);
        lua_getfield(L, players_slot, "LocalPlayer");
        if (my_type(L, -1) != LUA_TNIL) {
            lua_getfield(L, -1, "Name");
            if (my_type(L, -1) == LUA_TSTRING) {
                const char* s = lua_tolstring(L, -1, nullptr);
                if (s) strncpy_s(localName, s, _TRUNCATE);
            }
        }
        my_settop(L, snap);
    }

    for (int i = 1; ; ++i) {
        lua_rawgeti(L, result_slot, i);
        

        if (my_type(L, -1) == LUA_TNIL) {
            my_settop(L, -2);   
            break;
        }

        
        if (t.kind == TGT_OTHERS && localName[0]) {
            int snap = my_gettop(L);
            lua_getfield(L, -1, "Name");
            bool isLocal = false;
            if (my_type(L, -1) == LUA_TSTRING) {
                const char* pname = lua_tolstring(L, -1, nullptr);
                isLocal = pname && !strcmp(pname, localName);
            }
            my_settop(L, snap);   
            if (isLocal) {
                my_settop(L, -2);   
                continue;
            }
        }

        
        fn(L);
        my_settop(L, result_slot);   
    }
}

static void CmdHumanoidNumber(const Target& t, const char* member, double v) {
    Target      tc = t;
    std::string m = member;
    Schedule([tc, m, v](lua_State* L) {
        ForEachPlayer(L, tc, [&](lua_State* Lp) {
            StackGuard g(Lp);
            if (!PushHumanoid(Lp)) { Err("humanoid missing\n"); return; }
            lua_pushnumber(Lp, v);
            lua_setfield(Lp, -2, m.c_str());          

            });
        });
}

static void CmdHumanoidBool(const Target& t, const char* member, bool v) {
    Target      tc = t;
    std::string m = member;
    Schedule([tc, m, v](lua_State* L) {
        ForEachPlayer(L, tc, [&](lua_State* Lp) {
            StackGuard g(Lp);
            if (!PushHumanoid(Lp)) { Err("humanoid missing\n"); return; }
            my_pushboolean(Lp, v ? 1 : 0);
            lua_setfield(Lp, -2, m.c_str());

            });
        });
}

static void CmdGod(const Target& t) {
    CmdHumanoidNumber(t, "MaxHealth", 1e9);
    CmdHumanoidNumber(t, "Health", 1e9);
}

static void CmdServiceNumber(const char* service, const char* member, double v) {
    std::string s = service, m = member;
    Schedule([s, m, v](lua_State* L) {
        StackGuard g(L);
        if (!PushService(L, s.c_str())) { Err("%s missing\n", s.c_str()); return; }
        lua_pushnumber(L, v);
        lua_setfield(L, -2, m.c_str());

        });
}

static void CmdServiceString(const char* service, const char* member, const char* v) {
    std::string s = service, m = member, val = v;
    Schedule([s, m, val](lua_State* L) {
        StackGuard g(L);
        if (!PushService(L, s.c_str())) { Err("%s missing\n", s.c_str()); return; }
        lua_pushstring(L, val.c_str());
        lua_setfield(L, -2, m.c_str());

        });
}

static void CmdInfo(const Target& t) {
    Target tc = t;
    Schedule([tc](lua_State* L) {
        ForEachPlayer(L, tc, [&](lua_State* Lp) {
            StackGuard g(Lp);

            
            if (GetMember(Lp, "Name") == LUA_TSTRING)
                Say("player: %s\n", lua_tolstring(Lp, -1, nullptr));
            my_settop(Lp, -2);

            if (!PushHumanoid(Lp)) { Say("humanoid missing\n"); return; }

            static const char* kFields[] = { "WalkSpeed", "Health", "MaxHealth" };
            for (int i = 0; i < 3; ++i) {
                if (GetMember(Lp, kFields[i]) == LUA_TNUMBER)
                    Say("%-9s: %.2f\n", kFields[i], my_tonumber(Lp, -1));
                else
                    Say("%-9s: <%s>\n", kFields[i], TypeName(my_type(Lp, -1)));
                my_settop(Lp, -2);                     
            }
            });
        });
}

static void CmdBtools(const Target& t) {
    Target tc = t;
    Schedule([tc](lua_State* L) {
        ForEachPlayer(L, tc, [&](lua_State* Lp) {
            StackGuard g(Lp);

            int player_slot = my_gettop(Lp);
            if (GetMember(Lp, "Backpack") == LUA_TNIL) {
                Err("backpack missing\n");
                return;
            }
            

            
            lua_getfield(Lp, LUA_GLOBALSINDEX, "Instance");
            if (my_type(Lp, -1) == LUA_TNIL) {
                Err("_g.instance is nil\n");
                return;
            }
            

            lua_getfield(Lp, -1, "new");
            if (my_type(Lp, -1) != LUA_TFUNCTION) {

                return;
            }
            

            lua_pushstring(Lp, "HopperBin");
            

            int rc = lua_pcall(Lp, 1, 1, 0);
            if (rc != 0) {
                
                my_settop(Lp, player_slot + 1); 
                const char* code = "return Instance.new('HopperBin')";
                int rc2 = CompileScript(Lp, code, strlen(code), "btools");
                if (rc2 != 0) {
                    Err("compile failed (rc=%d): %s\n", rc2,
                        my_type(Lp, -1) == LUA_TSTRING
                        ? lua_tolstring(Lp, -1, nullptr) : "?");
                    return;
                }
                rc = lua_pcall(Lp, 0, 1, 0);
                if (rc != 0) {
                    Err("chunk() failed (rc=%d): %s\n", rc,
                        my_type(Lp, -1) == LUA_TSTRING
                        ? lua_tolstring(Lp, -1, nullptr) : "?");
                    return;
                }
            }

            if (my_type(Lp, -1) == LUA_TNIL) {
                Err("instance.new returned nil\n");
                return;
            }
            int bin_slot = my_gettop(Lp);
            

            
            lua_getfield(Lp, player_slot, "Backpack");
            lua_setfield(Lp, bin_slot, "Parent");

            });
        });
}

static int Tokenize(char* line, char* tok[4]) {
    int n = 0;
    char* ctx = nullptr;
    for (char* p = strtok_s(line, " \t\r\n", &ctx); p && n < 4;
        p = strtok_s(nullptr, " \t\r\n", &ctx))
        tok[n++] = p;
    return n;
}

static void PrintHelp() {
    Say("spedxploit commands:\n"
       "  exec [file] | ws [target] <n> | hp [target] <n> | maxhp [target] <n>\n"
       "  god | kill | freeze | thaw | sit | stand | jump | btools | info\n"
       "  fogstart | fogend | brightness | gravity | time | states | gc | exit\n");
}

static bool ParseTargetValue(char* tok[4], int n, Target& outT, double& outV) {
    if (n == 2) {                       
        outT = ParseTarget("me");
        outV = atof(tok[1]);
        return true;
    }
    if (n >= 3) {                       
        outT = ParseTarget(tok[1]);
        outV = atof(tok[2]);
        return true;
    }
    return false;
}

static void Report() {
    Say("base: %08x\n", (unsigned)g_base);
    Say("sc: %08x\n", (unsigned)g_scriptContext);
    for (int i = 0; i < 3; ++i) {
        if (g_states[i])
            Say("state %d: %08x %s\n", i, (unsigned)g_states[i],
                i == g_LIndex ? "<-- chosen" : "");
        else
            Say("state %d: null\n", i);
    }
    Say("l: %08x (%d)\n", (unsigned)g_L, g_LIndex);
}
