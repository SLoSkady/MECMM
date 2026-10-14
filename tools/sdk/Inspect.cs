// Shows the contents of a live engine object: every qword, with pointers resolved to the class
// of the object they point to (via vtable -> GetType() -> TypeInfo name) or to a string.
using System;
using System.Collections.Generic;
using System.Text;

public static class Inspect {
    // TypeInfo address -> class name (from index.csv)
    public static Dictionary<long, string> Names = new Dictionary<long, string>();

    // Class of the object at `obj`, or null: obj -> vtable (in image) -> slot 0 = GetType() = lea rax,[rip+X]; ret
    public static string ClassOf(Image img, long obj) {
        long vtable = img.Q(obj);
        if (!img.Contains(vtable)) return null;
        long getType = img.Q(vtable);
        if (!img.Contains(getType)) return null;
        long code = img.Q(getType);
        if ((code & 0xFFFFFF) != 0x058D48 || ((code >> 56) & 0xFF) != 0xC3) return null;
        long typeInfo = getType + 7 + (int)((code >> 24) & 0xFFFFFFFF);
        string name;
        return Names.TryGetValue(typeInfo, out name) ? name : string.Format("<TypeInfo {0:X}>", typeInfo);
    }

    static bool LooksLikePointer(long v) { return v > 0x10000 && v < 0x7FFFFFFFFFFF && (v & 7) == 0; }

    public static string Dump(Image img, long obj, int size) {
        var sb = new StringBuilder();
        sb.AppendFormat("{0:X}  {1}\n", obj, ClassOf(img, obj) ?? "(unknown class)");
        for (int o = 0; o < size; o += 8) {
            long v = img.Q(obj + o);
            string note = "";
            if (img.Contains(v)) {
                string s = img.Name(v);
                note = string.Format("exe+{0:X}", v - img.Base) + (string.IsNullOrEmpty(s) || s.Length < 3 ? "" : "  \"" + s + "\"");
            } else if (LooksLikePointer(v)) {
                string cls = ClassOf(img, v);
                if (cls != null) note = "-> " + cls;
            }
            float f0 = BitConverter.ToSingle(BitConverter.GetBytes(v), 0), f1 = BitConverter.ToSingle(BitConverter.GetBytes(v), 4);
            string floats = (Math.Abs(f0) > 1e-4 && Math.Abs(f0) < 1e5) || (Math.Abs(f1) > 1e-4 && Math.Abs(f1) < 1e5)
                ? string.Format("  f({0:0.###}, {1:0.###})", f0, f1) : "";
            sb.AppendFormat("  +{0:X3}  {1:X16}  {2}{3}\n", o, v, note, note.Length == 0 ? floats : "");
        }
        return sb.ToString();
    }
}
