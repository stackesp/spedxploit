//lua jobs stay on the right thread
using LuaJob = std::function<void(lua_State*)>;

static std::mutex          g_qMutex;
static std::deque<LuaJob>  g_queue;
static detour::Hook        g_waitHook;
static tLuaCFunction       g_origWait = nullptr;
static volatile LONG       g_inHook = 0;
static volatile LONG g_queueCount = 0;   
static volatile LONG g_waitCount = 0;
static DWORD g_startTick = 0;
static DWORD g_lastBeat = 0;

void Schedule(LuaJob job) {
    std::lock_guard<std::mutex> lk(g_qMutex);
    InterlockedIncrement(&g_queueCount);
    g_queue.push_back(std::move(job));
}

static DWORD  g_excCode = 0;
static void* g_excAddr = nullptr;
static void* g_excData = nullptr;
static volatile LONG g_jobFaulted = 0;

static int JobFilter(EXCEPTION_POINTERS* ep) {
    g_excCode = ep->ExceptionRecord->ExceptionCode;
    g_excAddr = ep->ExceptionRecord->ExceptionAddress;
    g_excData = (ep->ExceptionRecord->NumberParameters >= 2)
        ? (void*)ep->ExceptionRecord->ExceptionInformation[1]
        : nullptr;
    return EXCEPTION_EXECUTE_HANDLER;
}

static void InvokeJobSEH(LuaJob* job, lua_State* L) {
    __try {
        (*job)(L);
    }
    __except (JobFilter(GetExceptionInformation())) {
        InterlockedExchange(&g_jobFaulted, 1);
        Err("job fault %08x at %p, addr %p\n",
            (unsigned)g_excCode, g_excAddr, g_excData);
    }
}

static void RunQueue(lua_State* L) {
    for (;;) {
        LuaJob job;
        {
            std::lock_guard<std::mutex> lk(g_qMutex);
            if (g_queue.empty()) return;
            job = std::move(g_queue.front());
            g_queue.pop_front();
        }
#if RUNQUEUE_BALANCE
        int base = lua_gettop(L);
        uint32_t savedCi = *(uint32_t*)((uintptr_t)L + 0x0C);
        uint16_t savedNC = *(uint16_t*)((uintptr_t)L + 0x36);
        LDUMP("job:pre", L);
        InvokeJobSEH(&job, L);
        LDUMP("job:post", L);
        if (InterlockedCompareExchange(&g_jobFaulted, 0, 0) == 1) {
            
            
            *(uint32_t*)((uintptr_t)L + 0x0C) = savedCi;
            *(uint16_t*)((uintptr_t)L + 0x36) = savedNC;

        }
        InterlockedDecrement(&g_queueCount);
        lua_settop(L, base);
        LDUMP("job:restored", L);
#else
        LDUMP("job:pre", L);
        InvokeJobSEH(&job, L);
        LDUMP("job:post", L);
#endif
    }
}

static bool QueueHasWork() { return g_queueCount > 0; }

static int __cdecl hk_wait(lua_State* L) {
    if (!g_unloading && InterlockedCompareExchange(&g_inHook, 1, 0) == 0) {
        
        
        InterlockedIncrement(&g_waitCount);

        if (QueueHasWork()) {
            bool trace = (g_waitTrace != 0);

            int* idp = g_currentSecurity ? g_currentSecurity() : nullptr;
            int  idSave = idp ? *idp : -1;

            if (trace) LDUMP("wait:enter", L);
            __try { RunQueue(L); }
            __except (JobFilter(GetExceptionInformation())) {
                Err("queue fault %08x at %p, addr %p\n",
                    (unsigned)g_excCode, g_excAddr, g_excData);
                InterlockedExchange(&g_jobFaulted, 1);
            }
            if (trace) LDUMP("wait:exit", L);

            if (idp && *idp != idSave) {
                Err("identity leaked (%d -> %d), restoring\n", idSave, *idp);
                *idp = idSave;
            }
            if (InterlockedCompareExchange(&g_jobFaulted, 0, 1) == 1) {
                Err("thread state may be corrupt\n");
            }
        }
        InterlockedExchange(&g_inHook, 0);
    }
    return g_origWait(L);
}
