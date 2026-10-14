// Finds live objects of an engine class:
//   1. GetType() of every reflected class is a tiny function "lea rax, [rip+TypeInfo]; ret"
//      (48 8D 05 rel32 C3). Find the ones returning the class's TypeInfo.
//   2. vtables contain pointers to those functions; the vtable start is a few slots before.
//   3. Objects start with a pointer to their vtable: scan memory for those values.
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class Instances {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr addr, out MBI mbi, IntPtr len);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);

    // Addresses of "lea rax,[rip+X]; ret" functions in the image that return `typeInfo`.
    public static List<long> GetTypeFunctions(Image img, long typeInfo) {
        var result = new List<long>();
        for (long a = img.Base; a + 8 <= img.End; a++) {
            long q = img.Q(a);
            if ((q & 0xFFFFFF) != 0x058D48) continue;           // 48 8D 05
            if (((q >> 56) & 0xFF) != 0xC3) continue;          // ret right after the 7-byte lea
            int rel = (int)((q >> 24) & 0xFFFFFFFF);
            if (a + 7 + rel == typeInfo) result.Add(a);
        }
        return result;
    }

    // Image locations holding pointers to any of `functions` (vtable slots).
    public static List<long> Slots(Image img, List<long> functions) {
        var set = new HashSet<long>(functions);
        var result = new List<long>();
        for (long a = img.Base; a + 8 <= img.End; a += 8)
            if (set.Contains(img.Q(a))) result.Add(a);
        return result;
    }

    // Heap objects whose first qword is one of `vtables`. Returns (object, vtable) pairs.
    public static List<long[]> Find(int pid, HashSet<long> vtables, int max) {
        var h = OpenProcess(0x0410, false, pid);
        var hits = new List<long[]>();
        var buf = new byte[16 << 20]; long addr = 0; MBI m;
        while (hits.Count < max && VirtualQueryEx(h, new IntPtr(addr), out m, new IntPtr(Marshal.SizeOf(typeof(MBI)))) != IntPtr.Zero) {
            long b = m.Base.ToInt64(), size = m.Size.ToInt64();
            bool writable = (m.Protect & (0x04 | 0x08 | 0x40 | 0x80)) != 0, guard = (m.Protect & 0x100) != 0;
            if (m.State == 0x1000 && writable && !guard) {
                for (long off = 0; off < size && hits.Count < max; off += buf.Length) {
                    long n = Math.Min(buf.Length, size - off); IntPtr got;
                    if (!ReadProcessMemory(h, new IntPtr(b + off), buf, new IntPtr(n), out got)) continue;
                    int cnt = (int)got.ToInt64();
                    for (int i = 0; i + 8 <= cnt; i += 8) {
                        long v = BitConverter.ToInt64(buf, i);
                        if (vtables.Contains(v)) { hits.Add(new[] { b + off + i, v }); if (hits.Count >= max) break; }
                    }
                }
            }
            addr = b + size;
            if (addr <= 0) break;
        }
        return hits;
    }
}
