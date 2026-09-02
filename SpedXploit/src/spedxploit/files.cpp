//source and bytecode input
static bool HandleCommand(char* line);
static void PrintHelp();
static void ConsoleLoop();

static std::string ToCrLf(const char* p, size_t n) {
    std::string out;
    out.reserve(n + n / 16 + 8);
    for (size_t i = 0; i < n; ++i) {
        char c = p[i];
        if (c == '\r') {
            out += "\r\n";
            if (i + 1 < n && p[i + 1] == '\n') ++i;
        }
        else if (c == '\n') {
            out += "\r\n";
        }
        else {
            out += c;
        }
    }
    return out;
}

static std::string ToLf(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\r') {
            if (i + 1 < in.size() && in[i + 1] == '\n') continue;
            out += '\n';
        }
        else {
            out += in[i];
        }
    }
    return out;
}

static bool ReadWholeFile(const char* path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 ||
        sz.QuadPart > 64ll * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize((size_t)sz.QuadPart);
    size_t done = 0;
    bool ok = true;
    while (done < out.size()) {
        DWORD got = 0;
        DWORD want = (DWORD)min((size_t)(1u << 20), out.size() - done);
        if (!ReadFile(h, out.data() + done, want, &got, nullptr) || got == 0) {
            ok = false;
            break;
        }
        done += got;
    }
    CloseHandle(h);
    if (ok) out.resize(done);
    return ok;
}

static bool WriteWholeFile(const char* path, const void* data, size_t n) {
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t done = 0;
    bool ok = true;
    while (done < n) {
        DWORD put = 0;
        DWORD want = (DWORD)min((size_t)(1u << 20), n - done);
        if (!WriteFile(h, (const uint8_t*)data + done, want, &put, nullptr)) {
            ok = false;
            break;
        }
        done += put;
    }
    CloseHandle(h);
    return ok && done == n;
}

static bool LooksLikeBytecode(const uint8_t* p, size_t n) {
    return n >= 5 && p[0] == 0x1B && p[1] == 'L' && p[2] == 'u' && p[3] == 'a' &&
        p[4] == kLua51Ver;
}

static std::string ResolveLuacPath() {
    DWORD a = GetFileAttributesA(LUAC_PATH);
    if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY))
        return LUAC_PATH;

    char self[MAX_PATH] = {};
    if (GetModuleFileNameA((HMODULE)g_self, self, MAX_PATH)) {
        char* slash = strrchr(self, '\\');
        if (slash) {
            *(slash + 1) = 0;
            std::string cand = std::string(self) + "luac5.1.exe";
            a = GetFileAttributesA(cand.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY))
                return cand;
        }
    }

    char cwd[MAX_PATH] = {};
    if (GetCurrentDirectoryA(MAX_PATH, cwd)) {
        std::string cand = std::string(cwd) + "\\luac5.1.exe";
        a = GetFileAttributesA(cand.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY))
            return cand;
    }
    return "";
}

static bool CompileWithLuac(const std::string& source,
    std::vector<uint8_t>& outBytecode,
    std::string& diag) {
    diag.clear();
    outBytecode.clear();

    std::string luac = ResolveLuacPath();
    if (luac.empty()) {
        diag = "luac5.1.exe not found (looked at " LUAC_PATH
            ", next to the DLL, and in the working directory)";
        return false;
    }

    char tmpDir[MAX_PATH] = {};
    if (!GetTempPathA(MAX_PATH, tmpDir)) { diag = "GetTempPath failed"; return false; }
    std::string inPath = std::string(tmpDir) + "rbx_gui_in.lua";
    std::string outPath = std::string(tmpDir) + "rbx_gui_out.luac";
    DeleteFileA(outPath.c_str());

    
    
    std::string lf = ToLf(source);
    if (!WriteWholeFile(inPath.c_str(), lf.data(), lf.size())) {
        diag = "could not write " + inPath;
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { diag = "CreatePipe failed"; return false; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    std::string cmd = "\"" + luac + "\" -o \"" + outPath + "\" \"" + inPath + "\"";
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi{};
    BOOL launched = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, tmpDir, &si, &pi);

    
    CloseHandle(wr);

    if (!launched) {
        CloseHandle(rd);
        char b[256];
        sprintf_s(b, "CreateProcess failed (err %lu) for %s", GetLastError(), luac.c_str());
        diag = b;
        return false;
    }

    char chunk[1024];
    DWORD got = 0;
    while (ReadFile(rd, chunk, sizeof(chunk) - 1, &got, nullptr) && got > 0) {
        chunk[got] = 0;
        diag += chunk;
        if (diag.size() > 64 * 1024) break;
    }
    CloseHandle(rd);

    DWORD code = 1;
    if (WaitForSingleObject(pi.hProcess, 15000) == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        diag += "\nluac timed out after 15s";
    }
    else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (code != 0) {
        if (diag.empty()) diag = "luac exited with a nonzero status";
        return false;
    }
    if (!ReadWholeFile(outPath.c_str(), outBytecode) || outBytecode.empty()) {
        diag += "\nluac reported success but produced no output file";
        return false;
    }
    if (!LooksLikeBytecode(outBytecode.data(), outBytecode.size())) {
        outBytecode.clear();
        diag += "\nluac output is not Lua 5.1 bytecode - you need a real 5.1 build,"
            " not any other version. this is 2015 bro";
        return false;
    }
    return true;
}

static void CmdExecBuffer(const uint8_t* data, size_t len, const char* name) {
    if (!data || len == 0) { Err("no input\n"); return; }
    if (!LooksLikeBytecode(data, len)) {
        Err("bad bytecode buffer\n");
        return;
    }

    auto script = std::make_shared<std::vector<uint8_t>>(data, data + len);
    std::string chunkname = name && *name ? name : "GUI";

    Schedule([script, chunkname](lua_State* L) {
        StackGuard guard(L);
        int base = my_gettop(L);

        int idOld = RaiseIdentity(g_targetIdentity);
        RegisterDPrint(L);

        int rc = 0;
        bool ok = true;

        if (!g_execSpawn) {
            
            if (!LoadRbxChunk(L, (const char*)script->data(), script->size(),
                chunkname.c_str())) {
                Err("load failed\n"); ok = false;
            }
            else {
                rc = lua_pcall(L, 0, 0, 0);
            }
        }
        else {
            
            lua_getfield(L, LUA_GLOBALSINDEX, "spawn");
            if (my_type(L, -1) != LUA_TFUNCTION) {
                my_settop(L, base);
                lua_getfield(L, LUA_GLOBALSINDEX, "Spawn");
            }
            if (my_type(L, -1) != LUA_TFUNCTION) {
                Err("no spawn global\n"); ok = false;
            }
            else if (!LoadRbxChunk(L, (const char*)script->data(), script->size(),
                chunkname.c_str())) {
                Err("load failed\n"); ok = false;
            }
            else {
                rc = lua_pcall(L, 1, 0, 0);   
                if (rc == 0) (void)0;
            }
        }

        if (idOld >= 0) SetIdentity(idOld);   

        if (ok && rc != 0) {
            const char* msg = (my_type(L, -1) == LUA_TSTRING)
                ? lua_tolstring(L, -1, nullptr) : "(no message)";
            Err("run failed %d: %s\n", rc, msg);
        }
        my_settop(L, base);
        });
}

static void ExecSourceOrBytecode(const std::vector<uint8_t>& blob, const char* name) {
    if (blob.empty()) { Err("empty input\n"); return; }

    if (LooksLikeBytecode(blob.data(), blob.size())) {
        CmdExecBuffer(blob.data(), blob.size(), name);
        return;
    }

    std::string src((const char*)blob.data(), blob.size());
    std::vector<uint8_t> bc;
    std::string diag;

    if (!CompileWithLuac(src, bc, diag)) {
        Err("compile failed\n%s\n", diag.c_str());
        return;
    }
    if (!diag.empty()) (void)0;

    CmdExecBuffer(bc.data(), bc.size(), name);
}

static void CmdExec(const char* path_override) {
    const char* p = (path_override && *path_override)
        ? path_override : "C:\\rbx_exec.luac";
    std::vector<uint8_t> blob;
    if (!ReadWholeFile(p, blob)) {
        Err("read failed: %s (%lu)\n", p, GetLastError());
        return;
    }
    const char* base = strrchr(p, '\\');
    ExecSourceOrBytecode(blob, base ? base + 1 : p);
}
