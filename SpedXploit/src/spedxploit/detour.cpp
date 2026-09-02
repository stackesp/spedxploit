//tiny x86 hook
namespace detour {

    
    static size_t InstrLen(const uint8_t* p) {
        switch (p[0]) {
        case 0x50: case 0x51: case 0x52: case 0x53:      
        case 0x54: case 0x55: case 0x56: case 0x57:
        case 0x58: case 0x59: case 0x5A: case 0x5B:      
        case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        case 0x90:                                       
            return 1;

        case 0x6A: return 2;                             
        case 0x68: return 5;                             
        case 0xA1: return 5;                             

        case 0x33:                                       
        case 0x8B:                                       
        case 0x89: {                                     
            uint8_t modrm = p[1];
            uint8_t mod = modrm >> 6, rm = modrm & 7;
            if (mod == 3) return 2; 
            size_t len = 2;
            if (rm == 4) ++len;                          
            if (mod == 1) len += 1;
            else if (mod == 2) len += 4;
            else if (mod == 0 && rm == 5) len += 4;
            return len;
        }

        case 0x83: {                                     
            uint8_t mod = p[1] >> 6, rm = p[1] & 7;
            if (mod == 3) return 3;
            size_t len = 3;
            if (rm == 4) ++len;
            if (mod == 1) len += 1;
            else if (mod == 2) len += 4;
            else if (mod == 0 && rm == 5) len += 4;
            return len;
        }

        case 0x81: {                                     
            uint8_t mod = p[1] >> 6, rm = p[1] & 7;
            if (mod == 3) return 6;
            size_t len = 6;
            if (rm == 4) ++len;
            if (mod == 1) len += 1;
            else if (mod == 2) len += 4;
            else if (mod == 0 && rm == 5) len += 4;
            return len;
        }

        case 0x64:                                       
            if (p[1] == 0xA1) return 6;                  
            return 0;

            
        case 0xE8: case 0xE9: case 0xEB:
        default:
            return 0;
        }
    }

    struct Hook {
        void* target = nullptr;
        uint8_t* trampoline = nullptr;
        uint8_t  original[32] = {};
        size_t   stolen = 0;
        bool     installed = false;
    };

    static bool Install(Hook& h, void* target, void* detourFn) {
        h.target = target;
        uint8_t* t = (uint8_t*)target;

        
        size_t len = 0;
        while (len < 5) {
            size_t n = InstrLen(t + len);
            if (n == 0 || len + n > sizeof(h.original)) {
                Err("bad hook prologue at %p\n", target);
                for (int i = 0; i < 16; ++i) (void)0;
                Err("hook install refused\n");
                return false;
            }
            len += n;
        }
        h.stolen = len;
        memcpy(h.original, t, len);

        
        h.trampoline = (uint8_t*)VirtualAlloc(nullptr, len + 5, MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE);
        if (!h.trampoline) { Err("virtualalloc failed\n"); return false; }

        memcpy(h.trampoline, t, len);
        h.trampoline[len] = 0xE9;
        *(int32_t*)(h.trampoline + len + 1) =
            (int32_t)((uintptr_t)t + len - ((uintptr_t)h.trampoline + len + 5));

        
        DWORD old = 0;
        if (!VirtualProtect(t, len, PAGE_EXECUTE_READWRITE, &old)) {
            Err("virtualprotect failed\n");
            VirtualFree(h.trampoline, 0, MEM_RELEASE);
            h.trampoline = nullptr;
            return false;
        }
        t[0] = 0xE9;
        *(int32_t*)(t + 1) = (int32_t)((uintptr_t)detourFn - ((uintptr_t)t + 5));
        for (size_t i = 5; i < len; ++i) t[i] = 0x90;
        VirtualProtect(t, len, old, &old);
        FlushInstructionCache(GetCurrentProcess(), t, len);

        h.installed = true;

        return true;
    }

    static void Remove(Hook& h) {
        if (!h.installed) return;
        DWORD old = 0;
        if (VirtualProtect(h.target, h.stolen, PAGE_EXECUTE_READWRITE, &old)) {
            memcpy(h.target, h.original, h.stolen);
            VirtualProtect(h.target, h.stolen, old, &old);
            FlushInstructionCache(GetCurrentProcess(), h.target, h.stolen);
        }
        if (h.trampoline) VirtualFree(h.trampoline, 0, MEM_RELEASE);
        h.trampoline = nullptr;
        h.installed = false;

    }

}
