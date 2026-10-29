param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$Icon
)
# Replaces every icon group in a built LumeTaskbarCore.exe with the Lume icon,
# so Task Manager, Explorer and (hidden) tray entries no longer show the
# TranslucentTB artwork. Only RT_GROUP_ICON / RT_ICON resources change; code,
# version information and other resources are untouched. Run after building
# the core (see vendor/taskbar-runtime/2026.1/SOURCE.md), then refresh
# SHA256SUMS.txt.
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$Icon = (Resolve-Path -LiteralPath $Icon).Path
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
public static class IconBrand {
    const int RT_ICON = 3, RT_GROUP_ICON = 14;
    delegate bool NameProc(IntPtr module, IntPtr type, IntPtr name, IntPtr param);
    delegate bool LangProc(IntPtr module, IntPtr type, IntPtr name, ushort lang, IntPtr param);
    [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)] static extern IntPtr LoadLibraryEx(string file, IntPtr h, uint flags);
    [DllImport("kernel32")] static extern bool FreeLibrary(IntPtr m);
    [DllImport("kernel32")] static extern bool EnumResourceNames(IntPtr m, IntPtr type, NameProc cb, IntPtr p);
    [DllImport("kernel32")] static extern bool EnumResourceLanguages(IntPtr m, IntPtr type, IntPtr name, LangProc cb, IntPtr p);
    [DllImport("kernel32", SetLastError = true)] static extern IntPtr BeginUpdateResource(string file, bool deleteExisting);
    [DllImport("kernel32", SetLastError = true)] static extern bool UpdateResource(IntPtr h, IntPtr type, IntPtr name, ushort lang, byte[] data, int size);
    [DllImport("kernel32", SetLastError = true)] static extern bool EndUpdateResource(IntPtr h, bool discard);
    static List<KeyValuePair<int, ushort>> Entries(IntPtr m, int type) {
        var result = new List<KeyValuePair<int, ushort>>();
        EnumResourceNames(m, (IntPtr)type, (a, t, name, p) => {
            if (((long)name >> 16) != 0) throw new InvalidDataException("Named icon resources are not expected");
            int id = (int)(long)name;
            EnumResourceLanguages(a, t, name, (b, u, v, lang, q) => { result.Add(new KeyValuePair<int, ushort>(id, lang)); return true; }, IntPtr.Zero);
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static string Apply(string exe, string ico) {
        byte[] file = File.ReadAllBytes(ico);
        if (file.Length < 6 || BitConverter.ToUInt16(file, 0) != 0 || BitConverter.ToUInt16(file, 2) != 1)
            throw new InvalidDataException("Not an .ico file");
        int count = BitConverter.ToUInt16(file, 4);
        if (count == 0 || 6 + count * 16 > file.Length) throw new InvalidDataException("Malformed .ico directory");
        IntPtr module = LoadLibraryEx(exe, IntPtr.Zero, 2);   // LOAD_LIBRARY_AS_DATAFILE
        if (module == IntPtr.Zero) throw new Win32Exception();
        List<KeyValuePair<int, ushort>> groups, icons;
        try { groups = Entries(module, RT_GROUP_ICON); icons = Entries(module, RT_ICON); }
        finally { FreeLibrary(module); }
        if (groups.Count == 0) throw new InvalidDataException("No icon groups to replace");
        IntPtr update = BeginUpdateResource(exe, false);
        if (update == IntPtr.Zero) throw new Win32Exception();
        bool ok = false;
        try {
            foreach (var icon in icons) Check(UpdateResource(update, (IntPtr)RT_ICON, (IntPtr)icon.Key, icon.Value, null, 0));
            foreach (var group in groups) Check(UpdateResource(update, (IntPtr)RT_GROUP_ICON, (IntPtr)group.Key, group.Value, null, 0));
            ushort lang = groups[0].Value;
            int nextId = 1;
            foreach (var group in groups) {
                var dir = new MemoryStream();
                var w = new BinaryWriter(dir);
                w.Write((ushort)0); w.Write((ushort)1); w.Write((ushort)count);
                for (int i = 0; i < count; i++) {
                    int e = 6 + i * 16;
                    int size = BitConverter.ToInt32(file, e + 8), offset = BitConverter.ToInt32(file, e + 12);
                    if (size <= 0 || offset < 0 || offset + size > file.Length) throw new InvalidDataException("Malformed .ico image");
                    byte[] image = new byte[size];
                    Buffer.BlockCopy(file, offset, image, 0, size);
                    int id = nextId++;
                    Check(UpdateResource(update, (IntPtr)RT_ICON, (IntPtr)id, lang, image, size));
                    w.Write(file, e, 12);          // width, height, colors, reserved, planes, bit count, bytes
                    w.Write((ushort)id);
                }
                byte[] data = dir.ToArray();
                Check(UpdateResource(update, (IntPtr)RT_GROUP_ICON, (IntPtr)group.Key, group.Value, data, data.Length));
            }
            ok = true;
        } finally { Check(EndUpdateResource(update, !ok)); }
        return string.Format("replaced {0} icon groups ({1} images each)", groups.Count, count);
    }
    static void Check(bool ok) { if (!ok) throw new Win32Exception(); }
}
'@
[IconBrand]::Apply($Executable, $Icon)
