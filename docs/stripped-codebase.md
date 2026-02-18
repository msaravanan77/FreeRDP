# FreeRDP Stripped Codebase

This branch (`claude/FreeRDP-stripped`) is a minimal version of the FreeRDP repository
stripped down to contain **only what is required to build and run the `freerdp-proxy`
binary** with mstshash-based credential injection.

Everything else — all client GUIs, the shadow server, platform-specific builds, CI
tooling, and packaging — has been removed.

---

## What Was Removed

### `client/X11/`
**Reason:** Builds the `xfreerdp` X11 desktop client binary. Not needed for the proxy.
The proxy is a server-side component; `xfreerdp` is only used as a *test client* to
connect to the proxy. A system-installed `xfreerdp` (e.g., `sudo apt install freerdp2-x11`)
serves that purpose without requiring it to be built from source.

### `client/SDL/`
**Reason:** Builds the `sdl3-freerdp` SDL-based GUI client. Not needed for any proxy
functionality.

### `client/Android/`
**Reason:** Android Studio project for the Android RDP client. Entirely unrelated to the
proxy.

### `client/Mac/`
**Reason:** macOS native RDP client. Not needed for the proxy.

### `client/iOS/`
**Reason:** iOS native RDP client. Not needed for the proxy.

### `client/Windows/`
**Reason:** Windows native RDP client (`wfreerdp`). Not needed for the proxy.

### `client/Wayland/`
**Reason:** Wayland RDP client (`wlfreerdp`). Not needed for the proxy.

### `client/Sample/`
**Reason:** Minimal sample client used as a developer template. Not needed.

> **What was kept:** `client/common/` — this builds **`libfreerdp-client3.so`**, the
> client-side RDP connection library. The proxy uses this internally to open the
> outbound connection from the proxy to the real RDP target server.

---

### `server/shadow/`
**Reason:** The shadow server is a separate FreeRDP product that mirrors a local desktop
session over RDP. It is completely unrelated to the MITM proxy. Its removal also
eliminates the need for `rdtk/` (see below).

### `server/Sample/`
**Reason:** Developer sample/template for building a custom RDP server. Not needed.

### `server/Mac/` and `server/Windows/`
**Reason:** Platform-specific server stubs, not needed on Linux and unrelated to the
proxy.

> **What was kept:** `server/common/` (builds `libfreerdp-server3.so`) and
> `server/proxy/` (builds the proxy library and `freerdp-proxy` binary).

---

### `rdtk/`
**Reason:** The Remote Desktop ToolKit — a UI widget library used exclusively by the
shadow server for rendering its control interface. With the shadow server removed,
`rdtk` has zero consumers.

### `uwac/`
**Reason:** "Using Wayland As Client" — a thin Wayland compositor library used
exclusively by the `client/Wayland/` client. With the Wayland client removed, this has
no consumers.

### `ci/`
**Reason:** CI/CD pipeline configuration files (GitHub Actions, CMake presets for
Android, iOS, macOS, Windows, FreeBSD, etc.). Build tooling — not source code.

### `packaging/`
**Reason:** Debian/RPM packaging scripts. Not needed for building or running the proxy.

### `scripts/`
**Reason:** Developer helper scripts (code style, release tooling). Not needed.

---

## CMakeLists.txt Changes

Three CMake files were updated to reflect the removed directories:

| File | Change |
|------|--------|
| `CMakeLists.txt` | Removed `uwac` and `rdtk` subdirectory blocks; defaulted `WITH_X11=OFF` |
| `client/CMakeLists.txt` | Removed all client application subdirectories; kept `common/` only |
| `server/CMakeLists.txt` | Removed shadow, Sample, Windows, Mac subdirectories; kept `common/` and `proxy/` only |

---

## What Remains (and Why)

| Directory | Library built | Why needed |
|-----------|--------------|------------|
| `winpr/` | `libwinpr3.so` | Windows Portable Runtime — threading, strings, data structures. Every other library depends on it. |
| `libfreerdp/` | `libfreerdp3.so` | Core RDP protocol engine — connection sequences, TLS, NLA/CredSSP, PDU codec. |
| `client/common/` | `libfreerdp-client3.so` | Client-side RDP session API. The proxy uses this to open the outbound connection to the real target server. |
| `server/common/` | `libfreerdp-server3.so` | Server-side peer/listener API. The proxy uses this to accept incoming client connections. |
| `server/proxy/` | `libfreerdp-server-proxy3.so` + `freerdp-proxy` | The proxy library and CLI binary itself. |
| `channels/` | Various `*.so` plugins | RDP channel plugins (rdpdr, drdynvc, cliprdr, rdpsnd, rdpgfx, …) loaded at runtime for channel forwarding. |
| `include/` | (headers) | Public C headers for all the above. |
| `cmake/` | (build modules) | CMake helper modules required by the build system. |
| `third-party/` | (conditional) | Optional third-party code included inline (e.g., cJSON). |
| `external/` | (conditional) | Optional external library wrappers. |

---

## Dependency Graph of the Proxy Binary

```
freerdp-proxy  (CLI, server/proxy/cli/)
    └── libfreerdp-server-proxy3.so  (server/proxy/)
            ├── libfreerdp-client3.so   (client/common/)    ← outbound to target
            ├── libfreerdp-server3.so   (server/common/)    ← inbound from client
            ├── libfreerdp3.so          (libfreerdp/)       ← core RDP protocol
            └── libwinpr3.so            (winpr/)            ← platform runtime
```

System libraries pulled in transitively (OpenSSL, Kerberos, FFmpeg, libjansson, etc.)
are system-installed and not part of this repository.
