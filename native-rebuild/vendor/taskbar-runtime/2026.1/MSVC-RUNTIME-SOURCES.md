# Microsoft Visual C++ runtime dependency

The taskbar core, `ProgramLog.dll`, and `Xaml.dll` dynamically import the
Microsoft Visual C++ runtime. The following unmodified, release-mode DLLs are
copied from the official Visual Studio Build Tools redistributable directory:

`C:\BuildTools\VC\Redist\MSVC\14.44.35112\<architecture>\Microsoft.VC143.CRT`

The installed directory version is **14.44.35112**. The actual file and product
version of every distributed runtime DLL is **14.44.35211.0**. No files are
copied from Windows System32, an installed application, or `debug_nonredist`.
Authenticode validation returned `Valid` for all seven source files and their
byte-identical vendored copies.

| Architecture | File | SHA-256 |
| --- | --- | --- |
| x64 | msvcp140.dll | `0F885B509A685D2BBFA652FED26B5FB31D88FBDAB0A978C641D1C7B8AA460AA9` |
| x64 | msvcp140_atomic_wait.dll | `640B2AEFCED484D0368EEA5BDD06ADDD0658A3A70A49256E560D6923B404A479` |
| x64 | vcruntime140.dll | `D5E4D9A3E835FA679450145D6A7D94E36573A509317111904D9B3712C30D9066` |
| x64 | vcruntime140_1.dll | `1F2D41C4AA5DB0BC33EBF7B66D72943A817D7CE6CBE880502A9403823633093F` |
| arm64 | msvcp140.dll | `045FAEAB0B5710816AE160EA20DAE8495FFC11BB51FECA15D22AAF7FF3752CB3` |
| arm64 | msvcp140_atomic_wait.dll | `4384E35592B7BE1F0B80718EBA2F779060D73C3E232EB1BFADF8A4CB9B14601F` |
| arm64 | vcruntime140.dll | `D8A8513921544569837E400D37CC71302819967AE6DEFA932266A7ECB41DBAF9` |

The x64 CRT files are signed by Microsoft Windows, except
`msvcp140_atomic_wait.dll`, signed by Microsoft Windows Software Compatibility
Publisher. The ARM64 CRT files are signed by Microsoft Corporation.
`verify-dependencies.mjs` additionally checks the actual PE machine field:
`0x8664` for x64 and `0xAA64` for ARM64. Architecture is not inferred from the
directory name or signature.

## Distribution terms

These DLLs are Microsoft software and are not relicensed under the taskbar
core's GPL license. The Visual Studio 2022 Distributable Code list identifies
the `Microsoft.VC143.CRT` files for redistribution with applications:

https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution

The accompanying `MSVC-RUNTIME-LICENSE.docx` is the unmodified Microsoft
Visual C++ 2015-2022 Runtime license document linked by the official terms page:

- Terms page: https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/
- Original document: https://visualstudio.microsoft.com/wp-content/uploads/2021/09/Visual-C-Runtime-2015-2022-License-1.docx
- Document size: 39,644 bytes
- Document SHA-256: `F1E3D56CEB2AD68AAE0711B910375009E651AC5530FA0760F0DEA6E81E54FAE1`

Keep this source record and the original license document with the distributed
taskbar payload.

## Dependency boundary and verification

`MSVC-RUNTIME-DEPENDENCIES.json` records a read-only PE audit of **every** EXE
and DLL in both architecture directories. The audit follows normal imports,
delay imports, imported app-local symbol names/ordinals, and export forwarders.
All non-Windows dependencies must exist alongside the requesting image with
the correct architecture and required exported symbols. API-set contracts and
an explicit list of Windows operating-system DLLs remain system dependencies.
The Windows 10/11 Universal CRT is not copied into the application directory.

For this exact pinned payload, x64 needs the four CRT files above; ARM64 needs
only the three listed ARM64 files. In particular, no ARM64 import or export
forwarder requires `vcruntime140_1.dll`. The other VC CRT, MFC, OpenMP, debug,
Electron, and Node files are not included.

From any working directory:

```powershell
node <repository>/native-rebuild/vendor/taskbar-runtime/verify-dependencies.mjs <payload-root>
```

The audit only reads PE bytes and never loads or runs the core. The same audit
failed on the original payload with missing CRT imports, then passed after
these seven files were added. It requires Node only as a development tool;
neither the audit script nor a Node runtime is installed with Lume.

`taskbar_payload_test.cpp` also stages both architectures and verifies runtime
repair. Its host-architecture loader check loads only the verified Microsoft
CRT DLLs by absolute path, using application-directory and System32 search
flags. It does not execute `LumeTaskbarCore.exe`, `ExplorerHooks.dll`,
`ExplorerTAP.dll`, `ProgramLog.dll`, or `Xaml.dll`, so it cannot apply taskbar
effects. ARM64 loader execution still requires an ARM64-native test build on
an ARM64 machine; the portable static audit covers ARM64 imports and symbols
on the current x64 machine.

All executable and runtime hashes remain compiled into
`platform/windows/taskbar/taskbar_payload.cpp`; `SHA256SUMS.txt` is itself
pinned there. Editing a manifest cannot authorize a replacement binary.
