//nothing fancy
enum {
    ID_EXECUTE = 1001, ID_CLEAR, ID_OPEN, ID_CONSOLE, ID_CLEARLOG,
    ID_SCRIPT, ID_LOG,
    ID_ARG_OK = 1200, ID_ARG_CANCEL,
    IDM_QUICK_BASE = 2000
};

struct QuickSpec {
    const char* label;
    const char* cmd;
    int         nArgs;
    const char* argLabel[2];
    const char* argDefault[2];
};

static const QuickSpec kQuick[] = {
    { "WalkSpeed...",   "ws",         2, { "target", "speed"  }, { "me", "100"      } },
    { "Health...",      "hp",         2, { "target", "health" }, { "me", "100"      } },
    { "MaxHealth...",   "maxhp",      2, { "target", "value"  }, { "me", "1000"     } },
    { nullptr,          nullptr,      0, { nullptr,  nullptr  }, { nullptr, nullptr } },
    { "God...",         "god",        1, { "target", nullptr  }, { "me", nullptr    } },
    { "Kill...",        "kill",       1, { "target", nullptr  }, { "me", nullptr    } },
    { "Freeze...",      "freeze",     1, { "target", nullptr  }, { "me", nullptr    } },
    { "Thaw...",        "thaw",       1, { "target", nullptr  }, { "me", nullptr    } },
    { "Sit...",         "sit",        1, { "target", nullptr  }, { "me", nullptr    } },
    { "Stand...",       "stand",      1, { "target", nullptr  }, { "me", nullptr    } },
    { "Jump...",        "jump",       1, { "target", nullptr  }, { "me", nullptr    } },
    { "BTools...",      "btools",     1, { "target", nullptr  }, { "me", nullptr    } },
    { "Info...",        "info",       1, { "target", nullptr  }, { "me", nullptr    } },
    { nullptr,          nullptr,      0, { nullptr,  nullptr  }, { nullptr, nullptr } },
    { "FogStart...",    "fogstart",   1, { "studs",  nullptr  }, { "15000",  nullptr } },
    { "FogEnd...",      "fogend",     1, { "studs",  nullptr  }, { "100000", nullptr } },
    { "Brightness...",  "brightness", 1, { "value",  nullptr  }, { "1",      nullptr } },
    { "Gravity...",     "gravity",    1, { "value",  nullptr  }, { "196.2",  nullptr } },
    { "TimeOfDay...",   "time",       1, { "hh:mm",  nullptr  }, { "14:00",  nullptr } },
    { nullptr,          nullptr,      0, { nullptr,  nullptr  }, { nullptr, nullptr } },
    { "Lua heap (gc)",  "gc",         0, { nullptr,  nullptr  }, { nullptr, nullptr } },
    { "States",         "states",     0, { nullptr,  nullptr  }, { nullptr, nullptr } },
};
static const int kQuickCount = (int)(sizeof(kQuick) / sizeof(kQuick[0]));

static HWND    g_hwnd = nullptr;
static HWND    g_hScript = nullptr;
static HWND    g_hLog = nullptr;
static HWND    g_hExec = nullptr, g_hClr = nullptr, g_hOpen = nullptr,
g_hCon = nullptr, g_hClrLog = nullptr;
static HFONT   g_hMono = nullptr, g_hUi = nullptr;
static WNDPROC g_oldEditProc = nullptr;
static std::string g_chunkName = "GUI";

static std::mutex              g_guiMutex;
static std::deque<std::string> g_guiLines;

static void GuiLogPush(const char* s) {
    if (!s || !*s) return;
    std::lock_guard<std::mutex> lk(g_guiMutex);
    if (g_guiLines.size() > 4000) g_guiLines.pop_front();
    g_guiLines.emplace_back(s);
}

static std::vector<std::pair<HWND, std::string>> g_btnText;

static const char* BtnText(HWND h) {
    for (auto& p : g_btnText) if (p.first == h) return p.second.c_str();
    return "";
}

static HWND MakeButton(HWND parent, const char* text, int id) {
    HWND b = CreateWindowExA(0, "BUTTON", "",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, (HINSTANCE)g_self, nullptr);
    g_btnText.emplace_back(b, text ? text : "");
    return b;
}

static void DrawOdButton(DRAWITEMSTRUCT* d) {
    RECT r = d->rcItem;
    UINT st = DFCS_BUTTONPUSH | ((d->itemState & ODS_SELECTED) ? DFCS_PUSHED : 0);
    DrawFrameControl(d->hDC, &r, DFC_BUTTON, st);
    if (d->itemState & ODS_FOCUS) {
        RECT f = r;
        InflateRect(&f, -3, -3);
        DrawFocusRect(d->hDC, &f);
    }
    if (d->itemState & ODS_SELECTED) OffsetRect(&r, 1, 1);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, GetSysColor(COLOR_BTNTEXT));
    HFONT old = (HFONT)SelectObject(d->hDC,
        g_hUi ? g_hUi : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    DrawTextA(d->hDC, BtnText(d->hwndItem), -1, &r,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->hDC, old);
}

static void EditSetText(HWND h, const char* s) {
    SendMessageA(h, EM_SETSEL, 0, -1);
    SendMessageA(h, EM_REPLACESEL, FALSE, (LPARAM)(s ? s : ""));
}

static std::string EditGetText(HWND h) {
    int len = GetWindowTextLengthA(h);
    if (len <= 0) return "";
    std::vector<char> buf((size_t)len + 1, 0);
    GetWindowTextA(h, buf.data(), len + 1);
    return std::string(buf.data(), (size_t)len);
}

static void GuiAppendLog(const std::string& text) {
    if (!g_hLog) return;
    int len = GetWindowTextLengthA(g_hLog);
    if (len > 400000) { EditSetText(g_hLog, ""); len = 0; }
    SendMessageA(g_hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageA(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    SendMessageA(g_hLog, EM_SCROLLCARET, 0, 0);
}

static void GuiDrainLog() {
    std::string batch;
    {
        std::lock_guard<std::mutex> lk(g_guiMutex);
        if (g_guiLines.empty()) return;
        for (auto& s : g_guiLines) batch += s;
        g_guiLines.clear();
    }
    GuiAppendLog(ToCrLf(batch.data(), batch.size()));
}

static DWORD WINAPI ConsoleThread(LPVOID) {
    PrintHelp();
    ConsoleLoop();
    if (g_hwnd) PostMessageA(g_hwnd, WM_CLOSE, 0, 0);
    return 0;
}

static void OpenConsole() {
    if (InterlockedCompareExchange(&g_haveConsole, 1, 0) != 0) {
        HWND c = GetConsoleWindow();
        if (c) { ShowWindow(c, SW_SHOW); SetForegroundWindow(c); }
        return;
    }
    AllocConsole();
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONIN$", "r", stdin);
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetConsoleTitleA("SpedXploit");
    CreateThread(nullptr, 0, ConsoleThread, nullptr, 0, nullptr);
}

struct ArgDlgState {
    const QuickSpec* spec;
    HWND        edit[2];
    HWND        btnOk, btnCancel;
    bool        ok;
    std::string val[2];
};
static ArgDlgState g_arg{};

static LRESULT CALLBACK ArgWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        HFONT old = (HFONT)SelectObject(dc, g_hUi);
        SetBkMode(dc, TRANSPARENT);
        if (g_arg.spec) {
            for (int i = 0; i < g_arg.spec->nArgs; ++i) {
                RECT r{ 12, 14 + i * 32, 96, 34 + i * 32 };
                DrawTextA(dc, g_arg.spec->argLabel[i], -1, &r,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }
        SelectObject(dc, old);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        DrawOdButton((DRAWITEMSTRUCT*)l);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(w) == ID_ARG_OK) {
            for (int i = 0; i < g_arg.spec->nArgs; ++i)
                g_arg.val[i] = EditGetText(g_arg.edit[i]);
            g_arg.ok = true;
            DestroyWindow(h);
            return 0;
        }
        if (LOWORD(w) == ID_ARG_CANCEL) {
            g_arg.ok = false;
            DestroyWindow(h);
            return 0;
        }
        return 0;

    case WM_CLOSE:
        g_arg.ok = false;
        DestroyWindow(h);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static void RunQuickSpec(const QuickSpec& qs) {
    if (qs.nArgs == 0) {
        char tmp[160];
        strncpy_s(tmp, qs.cmd, _TRUNCATE);

        HandleCommand(tmp);
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSA wc{};
        wc.lpfnWndProc = ArgWndProc;
        wc.hInstance = (HINSTANCE)g_self;
        wc.lpszClassName = "SpedXploitArg";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(COLOR_BTNFACE + 1);
        RegisterClassA(&wc);
        registered = true;
    }

    g_arg = ArgDlgState{};
    g_arg.spec = &qs;

    int bodyH = 14 + qs.nArgs * 32 + 46;
    RECT pr{};
    GetWindowRect(g_hwnd, &pr);
    int wx = pr.left + (pr.right - pr.left) / 2 - 150;
    int wy = pr.top + 140;

    HWND dlg = CreateWindowExA(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        "SpedXploitArg", "", WS_POPUP | WS_CAPTION | WS_SYSMENU,
        wx, wy, 300, bodyH + 34, g_hwnd, nullptr, (HINSTANCE)g_self, nullptr);
    if (!dlg) return;
    SendMessageA(dlg, WM_SETTEXT, 0, (LPARAM)qs.label);

    for (int i = 0; i < qs.nArgs; ++i) {
        g_arg.edit[i] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            104, 12 + i * 32, 172, 22, dlg, nullptr, (HINSTANCE)g_self, nullptr);
        SendMessageA(g_arg.edit[i], WM_SETFONT, (WPARAM)g_hMono, TRUE);
        SendMessageA(g_arg.edit[i], EM_SETLIMITTEXT, 120, 0);
        EditSetText(g_arg.edit[i], qs.argDefault[i] ? qs.argDefault[i] : "");
    }

    int by = 14 + qs.nArgs * 32 + 8;
    g_arg.btnOk = MakeButton(dlg, "Run", ID_ARG_OK);
    g_arg.btnCancel = MakeButton(dlg, "Cancel", ID_ARG_CANCEL);
    MoveWindow(g_arg.btnOk, 100, by, 84, 28, TRUE);
    MoveWindow(g_arg.btnCancel, 192, by, 84, 28, TRUE);

    ShowWindow(dlg, SW_SHOW);
    SetFocus(g_arg.edit[0]);
    SendMessageA(g_arg.edit[0], EM_SETSEL, 0, -1);

    EnableWindow(g_hwnd, FALSE);
    MSG msg;
    while (IsWindow(dlg)) {
        BOOL r = GetMessageA(&msg, nullptr, 0, 0);
        if (r <= 0) { PostQuitMessage(0); break; }
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_RETURN) {
                PostMessageA(dlg, WM_COMMAND, ID_ARG_OK, 0);
                continue;
            }
            if (msg.wParam == VK_ESCAPE) {
                PostMessageA(dlg, WM_COMMAND, ID_ARG_CANCEL, 0);
                continue;
            }
            if (msg.wParam == VK_TAB) {
                HWND nx = GetNextDlgTabItem(dlg, GetFocus(), FALSE);
                if (nx) SetFocus(nx);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    EnableWindow(g_hwnd, TRUE);
    SetForegroundWindow(g_hwnd);

    if (!g_arg.ok) return;

    std::string line = qs.cmd;
    for (int i = 0; i < qs.nArgs; ++i) {
        if (g_arg.val[i].empty()) {
            Err("%s missing\n", qs.argLabel[i]);
            return;
        }
        line += " ";
        line += g_arg.val[i];
    }

    char tmp[256];
    strncpy_s(tmp, line.c_str(), _TRUNCATE);

    HandleCommand(tmp);
}

static void GuiOpenFile() {
    char path[MAX_PATH] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = "Lua scripts\0*.lua;*.luac;*.txt\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&ofn)) return;

    std::vector<uint8_t> blob;
    if (!ReadWholeFile(path, blob)) {
        Err("read failed: %s (%lu)\n", path, GetLastError());
        return;
    }
    const char* base = strrchr(path, '\\');
    g_chunkName = base ? base + 1 : path;

    
    if (LooksLikeBytecode(blob.data(), blob.size())) {

        CmdExecBuffer(blob.data(), blob.size(), g_chunkName.c_str());
        return;
    }
    if (memchr(blob.data(), 0, blob.size())) {
        Err("bad bytecode file: %s\n",
            g_chunkName.c_str());
        return;
    }
    std::string crlf = ToCrLf((const char*)blob.data(), blob.size());
    EditSetText(g_hScript, crlf.c_str());

}

static void GuiExecute() {
    std::string src = EditGetText(g_hScript);
    if (src.empty()) { Err("no script\n"); return; }
    std::vector<uint8_t> blob(src.begin(), src.end());
    ExecSourceOrBytecode(blob, g_chunkName.c_str());
}

static LRESULT CALLBACK ScriptEditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN && w == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000)) {
        PostMessageA(g_hwnd, WM_COMMAND, ID_EXECUTE, 0);
        return 0;
    }
    if (m == WM_CHAR && w == '\t') {
        SendMessageA(h, EM_REPLACESEL, TRUE, (LPARAM)"    ");
        return 0;
    }
    return CallWindowProcA(g_oldEditProc, h, m, w, l);
}

static void GuiLayout(int cx, int cy) {
    const int PAD = 8, BH = 30, GAP = 6;
    int y = PAD;
    MoveWindow(g_hExec, PAD, y, 104, BH, TRUE);
    MoveWindow(g_hOpen, PAD + 110, y, 104, BH, TRUE);
    MoveWindow(g_hClr, PAD + 220, y, 94, BH, TRUE);
    MoveWindow(g_hCon, PAD + 320, y, 94, BH, TRUE);
    MoveWindow(g_hClrLog, PAD + 420, y, 94, BH, TRUE);
    y += BH + GAP * 2;

    int avail = cy - y - PAD;
    if (avail < 140) avail = 140;
    int scriptH = (avail * 64) / 100;
    int logH = avail - scriptH - GAP;
    MoveWindow(g_hScript, PAD, y, cx - PAD * 2, scriptH, TRUE);
    MoveWindow(g_hLog, PAD, y + scriptH + GAP, cx - PAD * 2, logH, TRUE);
}

static LRESULT CALLBACK GuiWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE:     GuiLayout(LOWORD(l), HIWORD(l));  return 0;
    case WM_TIMER:    GuiDrainLog();                    return 0;
    case WM_DRAWITEM: DrawOdButton((DRAWITEMSTRUCT*)l);  return TRUE;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)l;
        mmi->ptMinTrackSize.x = 560;
        mmi->ptMinTrackSize.y = 420;
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id >= IDM_QUICK_BASE && id < IDM_QUICK_BASE + kQuickCount) {
            const QuickSpec& qs = kQuick[id - IDM_QUICK_BASE];
            if (qs.cmd) RunQuickSpec(qs);
            return 0;
        }
        switch (id) {
        case ID_EXECUTE:  GuiExecute();            return 0;
        case ID_OPEN:     GuiOpenFile();           return 0;
        case ID_CLEAR:    EditSetText(g_hScript, "");
            g_chunkName = "GUI";     return 0;
        case ID_CONSOLE:  OpenConsole();           return 0;
        case ID_CLEARLOG: EditSetText(g_hLog, ""); return 0;
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        g_hwnd = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static void GuiRun() {
    WNDCLASSA wc{};
    wc.lpfnWndProc = GuiWndProc;
    wc.hInstance = (HINSTANCE)g_self;
    wc.lpszClassName = "SpedXploitWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(COLOR_BTNFACE + 1);
    if (!RegisterClassA(&wc)) { Err("registerclass failed\n"); return; }

    g_hMono = CreateFontA(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
    g_hUi = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");

    HMENU bar = CreateMenu();
    HMENU quick = CreatePopupMenu();
    for (int i = 0; i < kQuickCount; ++i) {
        if (!kQuick[i].cmd) AppendMenuA(quick, MF_SEPARATOR, 0, nullptr);
        else AppendMenuA(quick, MF_STRING, IDM_QUICK_BASE + i, kQuick[i].label);
    }
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)quick, "Quick CMD");

    g_hwnd = CreateWindowExA(0, "SpedXploitWnd", "",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 940, 700,
        nullptr, bar, (HINSTANCE)g_self, nullptr);
    if (!g_hwnd) { Err("createwindow failed\n"); return; }
    SendMessageA(g_hwnd, WM_SETTEXT, 0, (LPARAM)"SpedXploit");

    g_hExec = MakeButton(g_hwnd, "Execute", ID_EXECUTE);
    g_hOpen = MakeButton(g_hwnd, "Open File", ID_OPEN);
    g_hClr = MakeButton(g_hwnd, "Clear", ID_CLEAR);
    g_hCon = MakeButton(g_hwnd, "Console", ID_CONSOLE);
    g_hClrLog = MakeButton(g_hwnd, "Clear Log", ID_CLEARLOG);

    g_hScript = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP |
        ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL,
        0, 0, 10, 10, g_hwnd, (HMENU)ID_SCRIPT, (HINSTANCE)g_self, nullptr);

    g_hLog = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
        0, 0, 10, 10, g_hwnd, (HMENU)ID_LOG, (HINSTANCE)g_self, nullptr);

    
    SendMessageA(g_hScript, EM_SETLIMITTEXT, 0, 0);
    SendMessageA(g_hLog, EM_SETLIMITTEXT, 0, 0);
    SendMessageA(g_hScript, WM_SETFONT, (WPARAM)g_hMono, TRUE);
    SendMessageA(g_hLog, WM_SETFONT, (WPARAM)g_hMono, TRUE);

    g_oldEditProc = (WNDPROC)SetWindowLongPtrA(g_hScript, GWLP_WNDPROC,
        (LONG_PTR)ScriptEditProc);

    EditSetText(g_hScript,
        "print(\"hello, world!\")\r\n");

    RECT rc;
    GetClientRect(g_hwnd, &rc);
    GuiLayout(rc.right, rc.bottom);
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    SetTimer(g_hwnd, 1, 120, nullptr);
    SetFocus(g_hScript);

    std::string lp = ResolveLuacPath();
    if (lp.empty()) Err("luac not found\n");

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (g_hMono) DeleteObject(g_hMono);
    if (g_hUi)   DeleteObject(g_hUi);
}
