# ReactX

Native Direct3D runtimes that load the vendor WDDM user mode driver and talk to
the kernel through D3DKMT. No Wine code.

# Direct3D 11
(`d3d11.dll`)

# Building

```
.\scripts\build.ps1 -Target win10
.\scripts\build-all.ps1 -Arch x64,x86
```

Targets: `win8`, `winblue`, `win10`, `win10_ni`, `win11`. Needs Windows SDK
10.0.28000 or newer for the UMD DDI headers.

# Testing

`rxprobe` loads the `d3d11.dll` next to it, or the system one with `-system`,
and probes every adapter. `-v` turns on the runtime's trace (`REACTX_LOG`).
