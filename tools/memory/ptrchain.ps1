# Multi-level pointer search: walks backwards from target addresses through pointers
# (each step: some pointer P points to an object start S with target = S + offset,
# offset <= MaxOffset) until it reaches a static location inside the game's .exe.
# Prints restart-stable paths like:  [exe+1234] +0x10 -> +0x200 -> target
#   ptrchain.ps1 -Targets 14F222620,3484A1B0 -Depth 4 -MaxOffset 0x800
param(
    [Parameter(Mandatory)][string[]]$Targets,
    [int]$Depth = 4,
    [int]$MaxOffset = 0x800,
    [int]$MaxNodesPerLevel = 50000,
    [int]$Show = 30,
    [string]$Process = "MirrorsEdgeCatalyst"
)

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class Chain {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr addr, out MBI mbi, IntPtr len);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);

    // node: an address that (via a chain of pointers) leads to a target
    class Node { public long Addr; public long Offset; public Node Next; }

    public static List<string> Run(IntPtr h, long[] targets, int depth, int maxOff, int maxNodes,
                                   long modBase, long modEnd, Action<string> log) {
        var results = new List<string>();
        var frontier = new List<Node>();
        foreach (var t in targets) frontier.Add(new Node { Addr = t });
        var seen = new HashSet<long>(targets);

        for (int level = 1; level <= depth && frontier.Count > 0; level++) {
            frontier.Sort((a, b) => a.Addr.CompareTo(b.Addr));
            var keys = new long[frontier.Count];
            for (int i = 0; i < keys.Length; i++) keys[i] = frontier[i].Addr;
            long lo = keys[0] - maxOff, hi = keys[keys.Length - 1];

            var next = new List<Node>();
            var buf = new byte[16 << 20]; long addr = 0; MBI m;
            while (VirtualQueryEx(h, new IntPtr(addr), out m, new IntPtr(Marshal.SizeOf(typeof(MBI)))) != IntPtr.Zero) {
                long b = m.Base.ToInt64(), size = m.Size.ToInt64();
                bool readable = (m.Protect & (0x02 | 0x04 | 0x08 | 0x20 | 0x40 | 0x80)) != 0, bad = (m.Protect & 0x101) != 0;
                if (m.State == 0x1000 && readable && !bad) {
                    for (long off = 0; off < size; off += buf.Length) {
                        long n = Math.Min(buf.Length, size - off); IntPtr got;
                        if (!ReadProcessMemory(h, new IntPtr(b + off), buf, new IntPtr(n), out got)) continue;
                        int cnt = (int)got.ToInt64();
                        for (int i = 0; i + 8 <= cnt; i += 8) {
                            long v = BitConverter.ToInt64(buf, i);
                            if (v < lo || v > hi) continue;
                            // smallest frontier address >= v
                            int k = Array.BinarySearch(keys, v);
                            if (k < 0) k = ~k;
                            if (k >= keys.Length || keys[k] - v > maxOff) continue;
                            long loc = b + off + i;
                            var node = new Node { Addr = loc, Offset = keys[k] - v, Next = frontier[k] };
                            if (loc >= modBase && loc < modEnd) {
                                results.Add(Describe(node, modBase));
                            } else if (next.Count < maxNodes && seen.Add(loc)) {
                                next.Add(node);
                            }
                        }
                    }
                }
                addr = b + size;
                if (addr <= 0) break;
            }
            log(string.Format("level {0}: {1} pointers, {2} static paths so far", level, next.Count, results.Count));
            frontier = next;
        }
        return results;
    }

    static string Describe(Node n, long modBase) {
        var sb = new StringBuilder();
        sb.AppendFormat("[exe+{0:X}]", n.Addr - modBase);
        for (var c = n; c.Next != null; c = c.Next) sb.AppendFormat(" +0x{0:X}{1}", c.Offset, c.Next.Next != null ? " ->" : "");
        for (var c = n; ; c = c.Next) if (c.Next == null) { sb.AppendFormat("   => {0:X}", c.Addr); break; }
        return sb.ToString();
    }
}
"@

$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$h = [Chain]::OpenProcess(0x0410, $false, $p.Id)
$modBase = $p.MainModule.BaseAddress.ToInt64()
$modEnd = $modBase + $p.MainModule.ModuleMemorySize
$t = [long[]]($Targets | ForEach-Object { [Convert]::ToInt64($_, 16) })

$results = [Chain]::Run($h, $t, $Depth, $MaxOffset, $MaxNodesPerLevel, $modBase, $modEnd, [Action[string]] { param($s) Write-Host $s })
"Static paths: $($results.Count)  (shortest first)"
$results | Sort-Object { ($_ -split '->').Count }, { $_ } | Select-Object -First $Show
