//stock 5.1 to the old vm format
static const uint8_t kLua51Sig[] = { 0x1B, 'L', 'u', 'a' };
static const uint8_t kLua51Ver = 0x51;
static const uint64_t kRbxNumXor = 0x109E29EF109E29EFULL;
static const uintptr_t kRbxNumXorAddr = 0x0164F6E0;
static inline uint64_t RbxNumXor() {
    return *(volatile uint64_t*)kRbxNumXorAddr;
}

struct ByteReader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;

    ByteReader(const uint8_t* d, size_t n) : p(d), end(d + n) {}

    uint8_t  u8() { if (p + 1 > end) { ok = false; return 0; } return *p++; }
    uint32_t u32() {
        if (p + 4 > end) { ok = false; return 0; }
        uint32_t v; memcpy(&v, p, 4); p += 4; return v;
    }
    uint64_t u64() {
        if (p + 8 > end) { ok = false; return 0; }
        uint64_t v; memcpy(&v, p, 8); p += 8; return v;
    }
    
    std::string str() {
        uint32_t len = u32();
        if (!len) return "";
        if (p + len > end) { ok = false; return ""; }
        std::string s((char*)p, len - 1); 
        p += len;
        return s;
    }
};

struct ByteWriter {
    std::vector<uint8_t> buf;
    void u8(uint8_t v) { buf.push_back(v); }
    void u32(uint32_t v) { buf.insert(buf.end(), (uint8_t*)&v, (uint8_t*)&v + 4); }
    void u64(uint64_t v) { buf.insert(buf.end(), (uint8_t*)&v, (uint8_t*)&v + 8); }
    void bytes(const void* d, size_t n) {
        buf.insert(buf.end(), (uint8_t*)d, (uint8_t*)d + n);
    }
};

struct HeapBuf {
    uint8_t* data = nullptr;
    size_t   size = 0;
    void release() { delete[] data; data = nullptr; size = 0; }
};

struct L51Const {
    uint8_t     type;   
    bool        bval;
    uint64_t    nval;   
    std::string sval;
};
struct L51Local { std::string name; uint32_t spc, epc; };
struct L51Proto {
    std::string              source;
    uint32_t                 linedef, lastline;
    uint8_t                  nups, numparams, is_vararg, maxstack;
    std::vector<uint32_t>    code;
    std::vector<L51Const>    kk;
    std::vector<uint32_t>    lines;
    std::vector<L51Local>    locvars;
    std::vector<std::string> upnames;
    std::vector<L51Proto*>   subs;
    ~L51Proto() { for (auto* s : subs) delete s; }
};

static L51Proto* ReadL51Proto(ByteReader& r) {
    auto* p = new L51Proto();
    p->source = r.str();
    p->linedef = r.u32();
    p->lastline = r.u32();
    p->nups = r.u8();
    p->numparams = r.u8();
    p->is_vararg = r.u8();
    p->maxstack = r.u8();
    
    uint32_t n = r.u32(); p->code.resize(n);
    for (uint32_t i = 0; i < n; i++) p->code[i] = r.u32();
    
    n = r.u32(); p->kk.resize(n);
    for (auto& k : p->kk) {
        k.type = r.u8();
        if (k.type == 1) k.bval = r.u8() != 0;
        else if (k.type == 3) {
            k.nval = r.u64();
            double dd; memcpy(&dd, &k.nval, sizeof(dd));

        }
        else if (k.type == 4) k.sval = r.str();
    }
    
    n = r.u32(); p->subs.resize(n);
    for (auto*& s : p->subs) s = ReadL51Proto(r);
    
    n = r.u32(); p->lines.resize(n);
    for (uint32_t i = 0; i < n; i++) p->lines[i] = r.u32();
    
    n = r.u32(); p->locvars.resize(n);
    for (auto& lv : p->locvars) {
        lv.name = r.str(); lv.spc = r.u32(); lv.epc = r.u32();
    }
    
    n = r.u32(); p->upnames.resize(n);
    for (auto& u : p->upnames) u = r.str();

    return p;
}

struct RbxStringTable {
    std::vector<std::string>            strings; 
    std::unordered_map<std::string, uint32_t> idx; 

    uint32_t intern(const std::string& s) {
        if (s.empty()) return 0;
        auto it = idx.find(s);
        if (it != idx.end()) return it->second;
        strings.push_back(s);
        uint32_t i = (uint32_t)strings.size();
        idx[s] = i;
        return i;
    }
    void collect(const L51Proto* p) {
        intern(p->source);
        for (auto& k : p->kk)  if (k.type == 4) intern(k.sval);
        for (auto& lv : p->locvars)           intern(lv.name);
        for (auto& u : p->upnames)           intern(u);
        for (auto* s : p->subs)              collect(s);
    }
};

static const uint8_t kStockToRbx[38] = {
     6,  4,  0,  7,  2,  8,  1,  3,  5, 15,   
    13,  9, 16, 11, 17, 10, 12, 14, 24, 22,   
    18, 25, 20, 26, 19, 21, 23, 33, 31, 27,   
    34, 29, 35, 28, 30, 32, 37, 36            
};

static uint32_t EncodeRbxInstr(uint32_t ins) {
    uint32_t op = ins & 0x3F;
    uint32_t A = (ins >> 6) & 0xFF;
    uint32_t C = (ins >> 14) & 0x1FF;
    uint32_t B = (ins >> 23) & 0x1FF;
    uint32_t Bx = (ins >> 14) & 0x3FFFF;
    uint32_t rop = (op < 38) ? kStockToRbx[op] : op;

    bool usesBx = (op == 1 || op == 5 || op == 7 ||    
        op == 22 || op == 31 || op == 32 || 
        op == 36);                          
    uint32_t out = (rop << 26) | (A << 18);
    if (usesBx) out |= (Bx & 0x3FFFF);
    else        out |= ((C & 0x1FF) << 9) | (B & 0x1FF);
    return out;
}

static void WriteRbxProto(ByteWriter& w, const L51Proto* p, RbxStringTable& st) {
    w.u32((uint32_t)p->subs.size());     
    w.u32((uint32_t)p->kk.size());       
    w.u32((uint32_t)p->code.size());     
    w.u32((uint32_t)p->locvars.size());  
    w.u32((uint32_t)p->lines.size());    
    w.u32((uint32_t)p->upnames.size());  

    
    
    
    
    
    
    w.u8(p->maxstack);   
    w.u8(p->is_vararg);  
    w.u8(p->nups);       
    w.u8(p->numparams);  

    
    for (auto& k : p->kk) {
        uint8_t rtype = (k.type == 3) ? 3 :
            (k.type == 4) ? 4 :
            (k.type == 1) ? (k.bval ? 2 : 1) : 0;
        w.u8(rtype);
        if (k.type == 3) {
            double dd; memcpy(&dd, &k.nval, sizeof(dd));

            w.u64(k.nval);
        }
        if (k.type == 4) w.u32(st.intern(k.sval));
    }

    
    
    
    for (uint32_t ln : p->lines) w.u32(ln);

    
    for (auto& lv : p->locvars) {
        w.u32(lv.spc);
        w.u32(lv.epc);
        w.u32(st.intern(lv.name));
    }

    
    for (auto& u : p->upnames) w.u32(st.intern(u));

    
    for (uint32_t instr : p->code) w.u32(EncodeRbxInstr(instr));

    
    for (auto* s : p->subs) WriteRbxProto(w, s, st);
}

static std::vector<uint8_t> BuildRbxBytecode(const L51Proto* root) {
    RbxStringTable st;
    st.collect(root);

    ByteWriter w;
    
    w.u32((uint32_t)st.strings.size());          
    for (auto& s : st.strings) w.u32((uint32_t)s.size()); 
    for (auto& s : st.strings) w.bytes(s.data(), s.size()); 
    
    w.u32(0);
    
    WriteRbxProto(w, root, st);
    return w.buf;
}

static HeapBuf BuildRbxBytecodeHeap(const L51Proto* root) {
    RbxStringTable st;
    st.collect(root);

    
    
    
    ByteWriter protoW;
    WriteRbxProto(protoW, root, st);

    
    ByteWriter strW;
    strW.u32((uint32_t)st.strings.size());
    for (auto& s : st.strings) strW.u32((uint32_t)s.size());
    for (auto& s : st.strings) strW.bytes(s.data(), s.size());

    
    uint32_t stringTableOffset = 4 + (uint32_t)protoW.buf.size();

    ByteWriter finalW;
    finalW.u32(stringTableOffset);
    finalW.buf.insert(finalW.buf.end(), protoW.buf.begin(), protoW.buf.end());
    finalW.buf.insert(finalW.buf.end(), strW.buf.begin(), strW.buf.end());

    HeapBuf h;
    h.size = finalW.buf.size();
    h.data = new uint8_t[h.size];
    memcpy(h.data, finalW.buf.data(), h.size);
    return h;
}

static L51Proto* ParseLuac(const char* buf, size_t len) {
    ByteReader r((uint8_t*)buf, len);

    
    if (len < 12 || memcmp(buf, kLua51Sig, 4) != 0) {
        Err("bad lua 5.1 bytecode\n");

        return nullptr;
    }
    r.p += 4;               
    if (r.u8() != kLua51Ver) { Err("lua 5.1 required\n"); return nullptr; }
    r.p += 7;               

    L51Proto* proto = ReadL51Proto(r);
    if (!r.ok) { Err("bytecode truncated\n"); delete proto; return nullptr; }
    return proto;
}

struct RbxLoadArgs {
    RbxStream* stream;
    lua_State* L;
    int         chunkname;
    int         flag;
    int         result;
};

static bool SafeBuildRbxBytecode(const L51Proto* root, HeapBuf& out) {
    
    
    out = HeapBuf{};
    __try {
        out = BuildRbxBytecodeHeap(root);
        return out.data != nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Err("bytecode conversion raised 0x%08x\n", GetExceptionCode());
        return false;
    }
}
static bool SafeLoadProto(RbxLoadArgs& a) {
    __try {
        a.result = g_rbxLoadProto(a.stream, a.L, a.chunkname, a.flag);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Err("loader fault 0x%08x\n",
            GetExceptionCode());
        return false;
    }
}

static uint32_t ModInv32(uint32_t a) {          
    uint32_t x = 1;
    for (int i = 0; i < 5; ++i) x *= 2u - a * x;  
    return x;
}

static uint32_t GetInstrMultiplier(lua_State* L) {
    uintptr_t p = (uintptr_t)L + (uintptr_t) * (int32_t*)((uintptr_t)L + 8) + 36;
    return (uint32_t)(p + (uint32_t) * (int32_t*)p);
}
static int ResolveEnvTable(lua_State* L)
{
    uintptr_t gt = (uintptr_t)L + 0x68;
    int tt = *(int*)(gt + TVALUE_TT);
    int val = *(int*)gt;

    if (tt != LUA_TTABLE || !val) return 0;
    return val;
}

static void PatchNumberConstants(int proto, const L51Proto* src, int depth) {
    int sizek = *(int*)(proto + 0x1C);
    if (sizek != (int)src->kk.size()) {
        Err("d%d sizek mismatch: loaded=%d parsed=%d\n",
            depth, sizek, (int)src->kk.size());
        return;
    }
    uintptr_t k = (uintptr_t)(proto + 0x20 + *(int*)(proto + 0x20));
    for (int i = 0; i < sizek; ++i) {
        if (src->kk[i].type != 3) continue;
        uintptr_t tv = k + 16 * i;
        if (*(int*)(tv + 8) != LUA_TNUMBER) {

            continue;
        }
        *(uint64_t*)tv = src->kk[i].nval ^ kRbxNumXor;
        double d; memcpy(&d, &src->kk[i].nval, sizeof(d));

    }

    int sizep = *(int*)(proto + 0x2C);
    if (sizep != (int)src->subs.size()) {
        Err("d%d sizep mismatch: loaded=%d parsed=%d\n",
            depth, sizep, (int)src->subs.size());
        return;
    }
    if (!sizep) return;

    uintptr_t arr = (uintptr_t)(proto + 0x08 + *(int*)(proto + 0x08));
    for (int i = 0; i < sizep; ++i) {
        uintptr_t slot = arr + 4 * i;
        int raw = *(int*)slot;
        int wantK = (int)src->subs[i]->kk.size();
        
        
        int cand = 0;
        if (raw > 0x10000 && *(int*)(raw + 0x1C) == wantK)                cand = raw;
        else if (*(int*)((int)(slot + raw) + 0x1C) == wantK)              cand = (int)(slot + raw);
        if (!cand) {
            Err("d%d sub%d: cannot resolve (raw=%08x)\n", depth, i, raw);
            continue;
        }
        PatchNumberConstants(cand, src->subs[i], depth + 1);
    }
}

static int RaiseIdentity(int to) {
    if (!g_currentSecurity) return -1;
    int* idp = g_currentSecurity();
    if (!idp) return -1;
    int old = *idp;
    *idp = to;

    return old;
}

static void SetIdentity(int v) {
    if (!g_currentSecurity) return;
    int* idp = g_currentSecurity();
    if (idp) *idp = v;
}

static bool LoadRbxChunk(lua_State* L, const char* buf, size_t len,
    const char* chunkname) {
    if (!g_rbxLoadProto || !g_rbxNewClosure || !g_rbxNewUpvalue || !g_rbxMakeString) {
        Err("loader funcs missing\n");
        return false;
    }

    std::unique_ptr<L51Proto> root(ParseLuac(buf, len));
    if (!root) return false;

    HeapBuf rbx;
    bool cvt_ok = SafeBuildRbxBytecode(root.get(), rbx);
    if (!cvt_ok) return false;

    
    RbxStreamInfo si{ rbx.data, rbx.data + rbx.size };
    RbxStream     stream{ &si, 0 };

    
    
    int gcWasAt = lua_gc(L, LUA_GCCOUNT, 0);
    lua_gc(L, LUA_GCSTOP, 0);

    
    int cnameStr = g_rbxMakeString(L, chunkname, strlen(chunkname));

    
    uint32_t M = GetInstrMultiplier(L);
    uint32_t inv = ModInv32(M);
    if (M * inv != 1) {
        Err("bad instruction multiplier\n");
        lua_gc(L, LUA_GCRESTART, 0);
        return false;
    }

    RbxLoadArgs la{ &stream, L, cnameStr, (int)inv, 0 };
    bool load_ok = SafeLoadProto(la);
    rbx.release();
    if (!load_ok) { lua_gc(L, LUA_GCRESTART, 0); return false; }

    int proto = la.result;
    if (!proto) {
        Err("sub_406fe0 returned null proto\n");
        lua_gc(L, LUA_GCRESTART, 0);
        return false;
    }

    {   
        int sizek = *(int*)(proto + 0x1C);
        uintptr_t k = (uintptr_t)(proto + 0x20 + *(int*)(proto + 0x20));
        for (int i = 0; i < sizek; ++i) {
            uintptr_t tv = k + 16 * i;
            int tt = *(int*)(tv + 8);
            uint32_t v = *(uint32_t*)tv;
            if (tt == LUA_TNUMBER) {
                uint64_t raw = *(uint64_t*)tv;
                uint64_t bits = raw ^ RbxNumXor();
                double   d;
                memcpy(&d, &bits, sizeof(d));

            }
            else if (tt == LUA_TSTRING) {

                char hex[128] = { 0 }; int n = 0;
                for (int b = 0; b < 32; ++b)
                    n += sprintf_s(hex + n, sizeof(hex) - n, "%02X ",
                        *(uint8_t*)((uintptr_t)v + b));

            }
            else {
                Err("tt=%d (%s) val=%08x unexpected\n", i, tt, TypeName(tt), v);
            }
        }
        int sizecode = *(int*)(proto + 0x34);
        uintptr_t code = (uintptr_t)(proto + 0x18 + *(int*)(proto + 0x18));
        for (int i = 0; i < sizecode; ++i) {
            uint32_t ins = M * *(uint32_t*)(code + 4 * i);   

        }
    }

    
    
    
    
    int nups = *(uint8_t*)(proto + 74);

    
    
    int envTable = ResolveEnvTable(L);
    if (!envTable) {
        Err("globals missing\n");
        lua_gc(L, LUA_GCRESTART, 0);
        return false;
    }

    int closure = g_rbxNewClosure(L, nups, envTable);
    if (!closure) {
        Err("sub_51e830 returned null closure\n");
        lua_gc(L, LUA_GCRESTART, 0);
        return false;
    }

    *(int*)(closure + 16) = proto - (closure + 16);   

    int* upv = (int*)(closure + 20);
    for (int i = 0; i < nups; i++) upv[i] = g_rbxNewUpvalue(L);

    if (g_growstack) {
        uintptr_t sl = *(uint32_t*)((uintptr_t)L + 0x20);
        uintptr_t tp = *(uint32_t*)((uintptr_t)L + 0x10);
        if (sl - tp <= 16) g_growstack(L, 1);
    }
    uintptr_t top = *(uintptr_t*)((uintptr_t)L + 0x10);
    *(int*)(top + 0) = closure;
    *(int*)(top + 8) = LUA_TFUNCTION;        
    *(uintptr_t*)((uintptr_t)L + 0x10) = top + 16;

    lua_gc(L, LUA_GCRESTART, 0);

    return true;
}

static void RestoreCallerCheck() {
    if (!g_guardOldBase && !g_guardOldSize) return;   

    uintptr_t* p = (uintptr_t*)R(ida::GuardBase);
    DWORD old;
    if (!VirtualProtect(p, 8, PAGE_READWRITE, &old)) {
        Err("guard restore failed\n");
        return;
    }
    p[0] = g_guardOldBase;
    p[1] = g_guardOldSize;
    VirtualProtect(p, 8, old, &old);

}

enum TargetKind { TGT_ME, TGT_ALL, TGT_OTHERS, TGT_NAME };

struct Target {
    TargetKind kind;
    char       name[64];
};
