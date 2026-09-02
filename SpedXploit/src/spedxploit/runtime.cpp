//start and clean up
static void QueueDemo() {}
static LONG WINAPI CrashLogger(EXCEPTION_POINTERS* ep) {
    Err("code=%08x addr=%08x\n",
        (unsigned)ep->ExceptionRecord->ExceptionCode,
        (unsigned)(uintptr_t)ep->ExceptionRecord->ExceptionAddress);
    if (ep->ExceptionRecord->NumberParameters >= 2)
        Err("touching %08x\n",
            (unsigned)ep->ExceptionRecord->ExceptionInformation[1]);
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI MainThread(LPVOID) {
    fopen_s(&g_log, LOG_PATH, "w");
    g_startTick = GetTickCount();
    Sleep(1500);

    if (!FindEverything()) {
        Err("scriptcontext not found\n");
        MessageBoxA(nullptr,
            "Could not find ScriptContext.\nJoin a place first, then inject.",
            "SpedXploit", MB_ICONERROR | MB_OK);
        return 0;
    }

    DisableCallerCheck();

    if (!InstallScheduler()) {
        Err("scheduler hook failed\n");
        MessageBoxA(nullptr, "Failed to install the wait() scheduler hook.",
            "SpedXploit", MB_ICONERROR | MB_OK);
        return 0;
    }

    SetUnhandledExceptionFilter(CrashLogger);

    CreateThread(nullptr, 0, DrainThread, nullptr, 0, nullptr);

    GuiRun();
    

    InterlockedExchange(&g_unloading, 1);
    detour::Remove(g_waitHook);
    Sleep(500);
    { std::lock_guard<std::mutex> lk(g_qMutex); g_queue.clear(); }
    RestoreCallerCheck();

    if (g_log) { fclose(g_log); g_log = nullptr; }
    if (g_haveConsole) FreeConsole();
    FreeLibraryAndExitThread(g_self, 0);
    return 0;
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = hModule;
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
