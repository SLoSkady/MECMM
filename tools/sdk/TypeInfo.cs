// Reads Frostbite's runtime type information (TypeInfo) out of the running game.
// Used by sdkdump.ps1. Layout (Mirror's Edge Catalyst, found by research, see docs/research/findings.md):
//   TypeInfo   +0x00 info*, +0x08 next TypeInfo* (global linked list)
//   info       +0x00 const char* name, +0x08 u16 flags (kind = flags >> 4 & 0x1F), +0x0A u16 size,
//              +0x10 module info*, +0x18 u8 alignment, +0x1A u16 field count
//     class    +0x20 super TypeInfo*, +0x30 FieldInfo[]
//     value    +0x28 FieldInfo[]   (structs)
//     enum     +0x20 FieldInfo[]   (name at +0x00, u32 value at +0x10)
//     array    +0x20 element TypeInfo*
//   FieldInfo  (0x18 bytes) +0x00 name, +0x08 u16 flags, +0x0A u16 offset, +0x10 field TypeInfo*
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

public class Image {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);

    public readonly long Base, End;
    readonly byte[] data;
    readonly IntPtr process;

    public Image(int pid, long moduleBase, int moduleSize) {
        process = OpenProcess(0x0410, false, pid);
        Base = moduleBase;
        End = moduleBase + moduleSize;
        data = new byte[moduleSize];
        // Read page by page so unreadable pages (guard / discarded) just stay zero.
        var page = new byte[0x1000];
        for (long off = 0; off < moduleSize; off += page.Length) {
            int n = (int)Math.Min(page.Length, moduleSize - off); IntPtr got;
            if (ReadProcessMemory(process, new IntPtr(moduleBase + off), page, new IntPtr(n), out got))
                Buffer.BlockCopy(page, 0, data, (int)off, (int)got.ToInt64());
        }
    }

    // (written without a + 8: garbage values near long.MaxValue would overflow)
    public bool Contains(long a) { return a >= Base && a <= End - 8; }

    public long Q(long a) {
        if (Contains(a)) return BitConverter.ToInt64(data, (int)(a - Base));
        var b = new byte[8]; IntPtr got;
        return ReadProcessMemory(process, new IntPtr(a), b, new IntPtr(8), out got) ? BitConverter.ToInt64(b, 0) : 0;
    }
    public uint U32(long a) { return (uint)(Q(a) & 0xFFFFFFFF); }
    public ushort U16(long a) { return (ushort)(Q(a) & 0xFFFF); }
    public byte U8(long a) { return (byte)(Q(a) & 0xFF); }

    // Zero-terminated identifier-like string (type names may contain ::, <, >, comma, space for templates).
    public string Name(long a) {
        if (!Contains(a)) return null;
        int o = (int)(a - Base), n = 0;
        while (o + n < data.Length && n < 200 && data[o + n] != 0) {
            byte c = data[o + n];
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                      c == ':' || c == '<' || c == '>' || c == ',' || c == ' ' || c == '-';
            if (!ok) return null;
            n++;
        }
        if (n >= 200) return null;
        return Encoding.ASCII.GetString(data, o, n);  // may be "": array types have no name of their own
    }

    public int Size { get { return data.Length; } }
}

public class TypeEntry {
    public long Address, Info, Next;
    public string Name;
    public ushort Flags, Size;
    public int Kind { get { return (Flags >> 4) & 0x1F; } }
}

public static class Types {
    // Every 8-aligned spot in the image that looks like a TypeInfo: info -> name, next -> 0 or image.
    public static Dictionary<long, TypeEntry> Collect(Image img) {
        var all = new Dictionary<long, TypeEntry>();
        for (long a = img.Base; a + 16 <= img.End; a += 8) {
            long info = img.Q(a);
            if (!img.Contains(info)) continue;
            long next = img.Q(a + 8);
            if (next != 0 && !img.Contains(next)) continue;
            string name = img.Name(img.Q(info));
            if (name == null) continue;
            all[a] = new TypeEntry { Address = a, Info = info, Next = next, Name = name,
                                     Flags = img.U16(info + 8), Size = img.U16(info + 10) };
        }
        // Keep only nodes whose `next` is another node (or the end): real TypeInfos form long chains.
        var valid = new Dictionary<long, TypeEntry>();
        foreach (var t in all.Values) {
            int steps = 0;
            for (var c = t; c != null && steps < 4; steps++) {
                if (c.Next == 0) { steps = 4; break; }
                TypeEntry n;
                c = all.TryGetValue(c.Next, out n) ? n : null;
            }
            if (steps >= 4) valid[t.Address] = t;
        }
        return valid;
    }

    // Walks the global list from its head(s): nodes nobody points to as `next`. Returns the longest list.
    public static List<TypeEntry> Walk(Dictionary<long, TypeEntry> nodes) {
        var pointedTo = new HashSet<long>();
        foreach (var t in nodes.Values) if (t.Next != 0) pointedTo.Add(t.Next);
        List<TypeEntry> best = new List<TypeEntry>();
        foreach (var t in nodes.Values) {
            if (pointedTo.Contains(t.Address)) continue;
            var list = new List<TypeEntry>();
            var seen = new HashSet<long>();
            for (var c = t; c != null && seen.Add(c.Address);) {
                list.Add(c);
                TypeEntry n;
                c = c.Next != 0 && nodes.TryGetValue(c.Next, out n) ? n : null;
            }
            if (list.Count > best.Count) best = list;
        }
        return best;
    }
}

public static class Sdk {
    public static readonly string[] KindNames = {
        "Void", "DbObject", "ValueType", "Class", "Array", "FixedArray", "String", "CString", "Enum", "FileRef",
        "Boolean", "Int8", "Uint8", "Int16", "Uint16", "Int32", "Uint32", "Int64", "Uint64", "Float32", "Float64",
        "Guid", "SHA1", "ResourceRef", "Function", "TypeRef", "BoxedValueRef", "Interface", "Delegate", "k29", "k30", "k31" };

    class Field { public string Name, Type; public int Offset; public uint Value; }

    static Image img;
    static Dictionary<long, TypeEntry> byAddress;

    static string TypeName(long typeInfo) {
        TypeEntry t;
        if (!byAddress.TryGetValue(typeInfo, out t)) return string.Format("<?{0:X}>", typeInfo);
        if (t.Kind == 4 || t.Kind == 5) return TypeName(img.Q(t.Info + 0x20)) + "[]";
        return t.Name.Length > 0 ? t.Name : KindNames[t.Kind];
    }

    static string Module(TypeEntry t) {
        long module = img.Q(t.Info + 0x10);
        return img.Contains(module) ? (img.Name(img.Q(module)) ?? "?") : "?";
    }

    static List<Field> Fields(TypeEntry t, long array) {
        var list = new List<Field>();
        int count = img.U16(t.Info + 0x1A);
        if (!img.Contains(array) || count > 1024) return list;
        for (int i = 0; i < count; i++) {
            long f = array + i * 0x18;
            list.Add(new Field { Name = img.Name(img.Q(f)) ?? "?", Offset = img.U16(f + 0x0A), Value = img.U32(f + 0x10),
                                 Type = t.Kind == 8 ? "" : TypeName(img.Q(f + 0x10)) });
        }
        return list;
    }

    // Writes classes.txt, valuetypes.txt, enums.txt and index.csv into `dir`.
    public static string Dump(Image image, List<TypeEntry> types, string dir) {
        img = image;
        byAddress = types.ToDictionary(t => t.Address);
        Directory.CreateDirectory(dir);
        var classes = new StringBuilder();
        var values = new StringBuilder();
        var enums = new StringBuilder();
        var index = new StringBuilder("name,kind,size,module,typeinfo,super\n");

        foreach (var t in types.Where(t => t.Name.Length > 0).OrderBy(t => t.Name, StringComparer.Ordinal)) {
            string kind = KindNames[t.Kind];
            string super = "";
            if (t.Kind == 3) {
                long s = img.Q(t.Info + 0x20);
                if (s != 0 && s != t.Address) super = TypeName(s);
            }
            index.AppendFormat("{0},{1},0x{2:X},{3},exe+{4:X},{5}\n", t.Name, kind, t.Size, Module(t), t.Address - img.Base, super);

            if (t.Kind == 3) {
                classes.AppendFormat("class {0}{1}  // size 0x{2:X}, module {3}, TypeInfo exe+{4:X}\n{{\n", t.Name,
                                     super.Length > 0 ? " : " + super : "", t.Size, Module(t), t.Address - img.Base);
                foreach (var f in Fields(t, img.Q(t.Info + 0x30)).OrderBy(f => f.Offset))
                    classes.AppendFormat("    /* 0x{0:X4} */ {1,-32} {2};\n", f.Offset, f.Type, f.Name);
                classes.Append("}\n\n");
            } else if (t.Kind == 2) {
                values.AppendFormat("struct {0}  // size 0x{1:X}, module {2}\n{{\n", t.Name, t.Size, Module(t));
                foreach (var f in Fields(t, img.Q(t.Info + 0x28)).OrderBy(f => f.Offset))
                    values.AppendFormat("    /* 0x{0:X4} */ {1,-32} {2};\n", f.Offset, f.Type, f.Name);
                values.Append("}\n\n");
            } else if (t.Kind == 8) {
                enums.AppendFormat("enum {0}  // module {1}\n{{\n", t.Name, Module(t));
                foreach (var f in Fields(t, img.Q(t.Info + 0x20)))
                    enums.AppendFormat("    {0} = {1},\n", f.Name, (int)f.Value);
                enums.Append("}\n\n");
            }
        }
        File.WriteAllText(Path.Combine(dir, "classes.txt"), classes.ToString());
        File.WriteAllText(Path.Combine(dir, "valuetypes.txt"), values.ToString());
        File.WriteAllText(Path.Combine(dir, "enums.txt"), enums.ToString());
        File.WriteAllText(Path.Combine(dir, "index.csv"), index.ToString());
        return string.Format("{0} types: {1} classes, {2} value types, {3} enums",
                             types.Count, types.Count(t => t.Kind == 3), types.Count(t => t.Kind == 2),
                             types.Count(t => t.Kind == 8));
    }
}
