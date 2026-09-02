//find the script context
static bool ValidateScriptContext(uintptr_t sc, lua_State* out[sc_off::GlobalStateCount]) {
    lua_State* found[sc_off::GlobalStateCount] = {};
    int valid = 0;

    for (int i = 0; i < sc_off::GlobalStateCount; ++i) {
        uintptr_t entry = sc + sc_off::GlobalStates + (uintptr_t)i * sc_off::GlobalStateSize;

        uintptr_t L = 0;
        if (!SafeReadT(entry, L)) return false;
        if (L == 0) continue;
        if (!LooksLikePointer(L)) return false;

        uintptr_t owner = 0;
        if (!SafeReadT(L + L_off::OwnerPtr, owner)) return false;
        if (!LooksLikePointer(owner)) return false;

        uintptr_t back = 0;
        if (!SafeReadT(owner + 4, back)) return false;
        if (back != sc) return false;

        found[i] = (lua_State*)L;
        ++valid;
    }

    if (!valid) return false;
    for (int i = 0; i < sc_off::GlobalStateCount; ++i) out[i] = found[i];
    return true;
}

static void* FindScriptContext(lua_State* out[sc_off::GlobalStateCount]) {
    const uintptr_t vft = R(ida::ScriptContext_vftable);

    SYSTEM_INFO si; GetSystemInfo(&si);
    uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;
    const uintptr_t end = (uintptr_t)si.lpMaximumApplicationAddress;

    const size_t kChunk = 0x10000;
    std::vector<uint8_t> buf(kChunk);

    size_t regions = 0, kb = 0, hits = 0;
    MEMORY_BASIC_INFORMATION mbi;

    while (addr < end && VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        uintptr_t regBase = (uintptr_t)mbi.BaseAddress;
        SIZE_T    regSize = mbi.RegionSize;

        bool usable =
            mbi.State == MEM_COMMIT &&
            mbi.Type == MEM_PRIVATE &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE) &&
            regSize >= sizeof(uintptr_t) && regSize <= 0x04000000;

        if (usable) {
            ++regions;
            for (SIZE_T off = 0; off < regSize; off += kChunk) {
                SIZE_T want = regSize - off;
                if (want > kChunk) want = kChunk;

                SIZE_T got = 0;
                if (!ReadProcessMemory(g_proc, (LPCVOID)(regBase + off),
                    buf.data(), want, &got) || got < sizeof(uintptr_t))
                    continue;

                kb += got / 1024;
                uintptr_t* p = (uintptr_t*)buf.data();
                size_t n = got / sizeof(uintptr_t);
                for (size_t i = 0; i < n; ++i) {
                    if (p[i] != vft) continue;
                    ++hits;
                    uintptr_t cand = regBase + off + i * sizeof(uintptr_t);
                    if (ValidateScriptContext(cand, out)) {

                        return (void*)cand;
                    }
                }
            }
        }

        uintptr_t next = regBase + regSize;
        if (next <= addr) break;
        addr = next;
    }

    Err("scriptcontext scan failed\n");
    return nullptr;
}

static lua_State* GetGlobalState(void* sc, int idx) {
    if (!sc || idx < 0 || idx >= sc_off::GlobalStateCount) return nullptr;
    uintptr_t L = 0;
    SafeReadT((uintptr_t)sc + sc_off::GlobalStates
        + (uintptr_t)idx * sc_off::GlobalStateSize, L);
    return (lua_State*)L;
}

static void* ScriptContextFromState(lua_State* L) {
    if (!L) return nullptr;
    uintptr_t owner = 0, sc = 0;
    if (!SafeReadT((uintptr_t)L + L_off::OwnerPtr, owner) || !owner) return nullptr;
    if (!SafeReadT(owner + 4, sc)) return nullptr;
    return (void*)sc;
}

using lua_Reader = const char* (__cdecl*)(lua_State*, void*, size_t*);
