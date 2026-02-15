# FreeRDP MITM Proxy — Architecture & Implementation Guide

## Confirmation: Yes, FreeRDP Implements MS-RDPBCGR

FreeRDP is a **complete, open-source implementation** of Microsoft's MS-RDPBCGR (Remote Desktop Protocol: Basic Connectivity and Graphics Remoting). The codebase implements the full protocol stack:

| Protocol Layer | FreeRDP File | Specification |
|---|---|---|
| TPKT | `libfreerdp/core/tpkt.c` | RFC 1006 |
| X.224 / TPDU | `libfreerdp/core/tpdu.c` | ITU-T X.224 |
| MCS | `libfreerdp/core/mcs.c` | ITU-T T.125 |
| GCC | `libfreerdp/core/gcc.c` | ITU-T T.124 |
| Negotiation | `libfreerdp/core/nego.c` | MS-RDPBCGR 2.2.1.1/2 |
| NLA/CredSSP | `libfreerdp/core/nla.c` | MS-NLMP, MS-CSSP |
| RDP Core | `libfreerdp/core/rdp.c` | MS-RDPBCGR |
| Connection Sequence | `libfreerdp/core/connection.c` | MS-RDPBCGR 1.3 |
| Licensing | `libfreerdp/core/license.c` | MS-RDPELE |
| Capabilities | `libfreerdp/core/capabilities.c` | MS-RDPBCGR 2.2.7 |
| Input PDUs | `libfreerdp/core/input.c` | MS-RDPBCGR 2.2.8 |
| Graphics/Updates | `libfreerdp/core/update.c` | MS-RDPBCGR 2.2.9 |

---

## Can FreeRDP Work as a MITM Proxy? **YES — It Already Has One Built-in**

FreeRDP ships with a **production-ready RDP proxy** at `server/proxy/`. This proxy:

1. **Accepts** incoming RDP client connections (acts as an RDP server)
2. **Establishes** an outbound RDP client connection to the real target
3. **Relays** all PDUs bidirectionally between client and target
4. **Handles** TLS termination, NLA/CredSSP, certificate presentation, credential injection, channel routing, and graphics forwarding

The binary is `freerdp-proxy` and is configured via an INI file.

---

## Architecture Overview

### The Two-Session Model

The proxy maintains **two completely independent RDP sessions**:

```
┌──────────────┐                    ┌──────────────────────┐                    ┌──────────────────┐
│  MSTSC /     │   Session A        │   FreeRDP MITM       │   Session B        │  Windows RDP     │
│  RDP Client  │◄══════════════════►│   Proxy              │◄══════════════════►│  Server (Target) │
│              │   TLS Tunnel #1    │   :23389             │   TLS Tunnel #2    │  :3389           │
└──────────────┘                    └──────────────────────┘                    └──────────────────┘
                                     │                   │
                                     │  Server-Side      │  Client-Side
                                     │  (pf_server.c)    │  (pf_client.c)
                                     │  freerdp_peer     │  freerdp_connect()
                                     │                   │
                                     └───────────────────┘
                                         proxyData
                                       (pf_context.c)
```

**Key insight:** The proxy terminates the TLS/NLA session from the client and creates an entirely new TLS/NLA session to the target. These are NOT the same TLS tunnel — the proxy has full plaintext access to all RDP PDUs between the two endpoints.

---

## Source Code Map

### Core Proxy Files (`server/proxy/`)

| File | Role | Key Functions |
|---|---|---|
| `pf_server.c` | Accepts client connections, manages server-side RDP peer | `pf_server_post_connect()`, `pf_server_activate()`, `pf_server_logon()`, `pf_server_receive_channel_data_hook()` |
| `pf_client.c` | Connects to target RDP server as a client | `pf_client_start()`, `pf_client_on_activated()`, `pf_client_load_rdpsnd()`, `proxy_server_reactivate()` |
| `pf_context.c` | Shared state between server and client sides | `pf_context_create_client_context()`, `proxy_data_set_client_context()`, `StaticChannelContext_new()` |
| `pf_config.c` | INI configuration file parsing | Target host/port/credentials, channel policies, security settings |
| `pf_update.c` | Graphics/display update relay (target→client) | `pf_client_begin_paint()`, `pf_client_end_paint()`, `pf_server_refresh_rect()` |
| `pf_input.c` | Input event relay (client→target) | `pf_server_keyboard_event()`, `pf_server_mouse_event()`, `pf_server_synchronize_event()` |
| `pf_channel.c` | Virtual channel routing | Static + dynamic channel setup, passthrough/intercept modes |
| `proxy_modules.c` | Plugin system for hooks and filters | `pf_modules_run_hook()`, `pf_modules_run_filter()` |
| `channels/pf_channel_drdynvc.c` | Dynamic virtual channel proxy | GFX, audio, video, multitouch |
| `channels/pf_channel_rdpdr.c` | Device redirection proxy | Drive, printer, serial, smartcard |
| `channels/pf_channel_smartcard.c` | Smart card channel handling | Smart card redirection |
| `cli/freerdp_proxy.c` | CLI entry point | `main()` for the `freerdp-proxy` binary |

### Protocol Stack Files (`libfreerdp/core/`)

| File | Lines | Purpose |
|---|---|---|
| `connection.c` | ~1800 | Full MS-RDPBCGR connection state machine |
| `nego.c` | ~1500 | Protocol security negotiation (RDP/TLS/NLA/CredSSP/RDSTLS/AAD) |
| `nla.c` | ~1700 | Network Level Authentication (NTLM/Kerberos via SSPI/GSSAPI) |
| `mcs.c` | ~3100 | T.125 Multipoint Communication Service |
| `gcc.c` | ~3300 | T.124 Generic Conference Control |
| `rdp.c` | ~3100 | Core RDP state machine, PDU dispatch |
| `peer.c` | ~1200 | Server-side `freerdp_peer` implementation |
| `transport.c` | ~2200 | Multi-layer transport (TPKT, FastPath, Gateway) |
| `tpdu.c` | ~280 | X.224 Transport Protocol Data Units |
| `tpkt.c` | ~170 | RFC 1006 TPKT framing |
| `security.c` | ~1000 | RC4, FIPS, encryption key derivation |
| `license.c` | ~2500 | RDP licensing protocol |
| `activation.c` | ~300 | Demand Active / Confirm Active |
| `capabilities.c` | ~4500 | 40+ capability sets negotiation |
| `info.c` | ~1200 | Client Info PDU (credential transmission) |
| `input.c` | ~400 | Input PDU processing |
| `update.c` | ~3400 | Screen update PDUs and drawing orders |
| `orders.c` | ~4000 | RDP drawing orders |
| `fastpath.c` | ~600 | Fast-Path compression/framing |

---

## Connection Lifecycle — Step by Step

### Phase 1: Client → Proxy (Server-Side Negotiation)

```
MSTSC Client                          MITM Proxy (:23389)
     │                                       │
     │──── TCP Connect to :23389 ───────────►│  proxy accepts via freerdp_listener
     │                                       │
     │──── X.224 Connection Request ────────►│  nego.c: negotiation begins
     │◄─── X.224 Connection Confirm ─────────│  nego.c: agree on NLA/TLS
     │                                       │
     │──── TLS Handshake ──────────────────►│  proxy presents its OWN certificate
     │◄─── TLS Established ─────────────────│  (CertificateFile from config)
     │                                       │
     │──── NLA/CredSSP (NTLM/Kerberos) ───►│  nla.c: proxy acts as CredSSP server
     │◄─── NLA Complete ────────────────────│  pf_server_logon() receives credentials
     │                                       │
     │──── MCS Connect Initial + GCC ──────►│  mcs.c + gcc.c
     │◄─── MCS Connect Response + GCC ──────│
     │──── MCS Erect Domain ───────────────►│
     │──── MCS Attach User ────────────────►│
     │◄─── MCS Attach User Confirm ─────────│
     │──── MCS Channel Join ───────────────►│
     │◄─── MCS Channel Join Confirm ────────│
```

**What the proxy does here:**
- `pf_server.c` uses `freerdp_peer` to accept the incoming connection
- The proxy presents its own self-signed TLS certificate (the client sees the proxy's cert, NOT the target's cert)
- NLA/CredSSP authentication is handled by the proxy — it acts as the authentication server
- The `pf_server_logon()` callback captures the client's `SEC_WINNT_AUTH_IDENTITY` (domain/username/password)

### Phase 2: Proxy → Target (Client-Side Connection)

After `pf_server_post_connect()` completes, the proxy spawns a client thread:

```c
// pf_server.c:339
pdata->client_thread = CreateThread(NULL, 0, pf_client_start, pc, 0, NULL);
```

```
MITM Proxy                             Target Server (:3389)
     │                                       │
     │──── TCP Connect to :3389 ────────────►│
     │──── X.224 Connection Request ────────►│
     │◄─── X.224 Connection Confirm ─────────│
     │                                       │
     │──── TLS Handshake ──────────────────►│  proxy validates target's cert
     │◄─── TLS Established ─────────────────│
     │                                       │
     │──── NLA/CredSSP ────────────────────►│  CREDENTIAL INJECTION HERE
     │◄─── NLA Complete ────────────────────│  Uses TargetUser/TargetPassword
     │                                       │  from proxy config
     │──── MCS + GCC exchange ─────────────►│
     │◄─── ... ─────────────────────────────│
     │◄─── Demand Active ──────────────────│
     │──── Confirm Active ─────────────────►│
```

**What the proxy does here:**
- `pf_client.c` uses `freerdp_connect()` to establish a standard RDP client connection to the target
- Credentials are injected from the proxy config (`TargetUser`, `TargetPassword`, `TargetDomain`)
- This is a completely independent TLS session from Session A

### Phase 3: Bidirectional PDU Relay (Active Session)

Once both sessions are active, the proxy relays PDUs in both directions:

```
Client ──Input PDUs──► pf_input.c ──────► Target
Client ◄──Graphics──── pf_update.c ◄───── Target
Client ──Channels────► pf_channel.c ────► Target
Client ◄──Channels──── pf_channel.c ◄──── Target
```

**Key relay files:**
- `pf_input.c`: Forwards keyboard events, mouse events, synchronize events from client to target
- `pf_update.c`: Forwards bitmap updates, surface commands, begin/end paint from target to client
- `pf_channel.c`: Routes virtual channels (clipboard, audio, drive redirection, etc.) bidirectionally
- `pf_server.c:pf_server_receive_channel_data_hook()`: Intercepts client→target channel data

---

## Configuration: Proxy INI File

The proxy is configured with an INI file. Here's a configuration for your use case:

```ini
[Server]
; Proxy listens here — MSTSC connects to this
Host = 0.0.0.0
Port = 23389

[Target]
; Real Windows RDP server
FixedTarget = true
Host = 192.168.1.100
Port = 3389

; Credential injection — these get sent to the target
User = administrator
Domain = MYDOMAIN
Password = MySecretPassword

; TLS security level for target connection
TlsSecLevel = 0

[Channels]
; Which channels to proxy
GFX = true
DisplayControl = true
Clipboard = true
AudioOutput = true
AudioInput = true
DeviceRedirection = true
RemoteApp = false
VideoRedirection = true
CameraRedirection = false

; Passthrough all channels not explicitly listed
PassthroughIsBlacklist = true

[Input]
Keyboard = true
Mouse = true
Multitouch = true

[Security]
; Server-side (what proxy offers to MSTSC)
ServerNlaSecurity = true
ServerTlsSecurity = true
ServerRdpSecurity = false

; Client-side (what proxy uses towards target)
ClientNlaSecurity = true
ClientTlsSecurity = true
ClientRdpSecurity = false
ClientAllowFallbackToTls = true

[Certificates]
; Proxy's own TLS certificate (presented to MSTSC)
CertificateFile = /path/to/proxy-cert.pem
PrivateKeyFile = /path/to/proxy-key.pem
```

---

## How to Build and Run

### Build FreeRDP with Proxy Support

```bash
cd /path/to/FreeRDP
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DWITH_PROXY=ON \
  -DWITH_SERVER=ON \
  -DWITH_CLIENT=ON
cmake --build build -j$(nproc)
```

### Generate Self-Signed Certificate for the Proxy

```bash
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout proxy-key.pem -out proxy-cert.pem \
  -days 365 -subj "/CN=RDP-Proxy"
```

### Run the Proxy

```bash
./build/server/proxy/freerdp-proxy --config proxy.ini
```

### Connect with MSTSC

On a Windows machine, open `mstsc.exe` and connect to:
```
proxy-host:23389
```

The proxy will transparently forward the connection to the target specified in the INI file.

---

## MITM Interception Points

### 1. Credential Capture (Server-Side)

In `pf_server.c:pf_server_logon()`:

```c
static BOOL pf_server_logon(freerdp_peer* peer,
                            const SEC_WINNT_AUTH_IDENTITY* identity,
                            BOOL automatic)
{
    // identity->User     — client's username
    // identity->Domain   — client's domain
    // identity->Password — client's password (plaintext from CredSSP)
    // You can log, modify, or use these credentials
}
```

### 2. Credential Injection (Client-Side)

In `pf_server.c:pf_server_get_target_info()`:

```c
// Lines 160-177: Config credentials are injected into the client settings
if (config->TargetUser)
    freerdp_settings_set_string(settings, FreeRDP_Username, config->TargetUser);
if (config->TargetDomain)
    freerdp_settings_set_string(settings, FreeRDP_Domain, config->TargetDomain);
if (config->TargetPassword)
    freerdp_settings_set_string(settings, FreeRDP_Password, config->TargetPassword);
```

### 3. Certificate Handling

- **Server-side**: The proxy presents its own certificate (from `CertificateFile` / `PrivateKeyFile` in config) to the connecting client. The client will see this certificate, not the real target's certificate.
- **Client-side**: The proxy validates (or can skip validation of) the target server's certificate when connecting outbound.

### 4. Input Interception (`pf_input.c`)

```c
// Keyboard events — can log/modify keystrokes
static BOOL pf_server_keyboard_event(rdpInput* input, UINT16 flags, UINT8 code) {
    // Forward to target: freerdp_input_send_keyboard_event()
}

// Mouse events — can log/modify mouse activity
static BOOL pf_server_mouse_event(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y) {
    // Forward to target: freerdp_input_send_mouse_event()
}
```

### 5. Graphics Interception (`pf_update.c`)

```c
// Bitmap updates from target → can inspect/modify before forwarding to client
static BOOL pf_client_end_paint(rdpContext* context) {
    // Access bitmap data, modify, then forward to client
}
```

### 6. Channel Data Interception (`pf_channel.c`)

Channel data (clipboard, files, audio) can be intercepted in passthrough or intercept mode. The channel mode is configured per-channel in the INI file via `Passthrough` and `Intercept` lists.

### 7. Plugin/Module Hooks (`proxy_modules.c`)

The proxy has a full plugin system with hooks at every stage:

| Hook/Filter | When It Fires |
|---|---|
| `HOOK_TYPE_SERVER_POST_CONNECT` | After client completes connection |
| `HOOK_TYPE_SERVER_ACTIVATE` | After session activation |
| `FILTER_TYPE_SERVER_PEER_LOGON` | When client credentials arrive |
| `FILTER_TYPE_SERVER_FETCH_TARGET_ADDR` | When determining target address |
| Channel-specific hooks | Per-channel intercept callbacks |

---

## Data Flow Summary

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        FreeRDP MITM Proxy                              │
│                                                                         │
│  ┌─────────────┐     ┌──────────────┐     ┌─────────────────────────┐  │
│  │ pf_server.c │     │  proxyData   │     │    pf_client.c          │  │
│  │             │     │              │     │                         │  │
│  │ Accept conn │────►│ ps (server)  │────►│ Connect to target       │  │
│  │ TLS/NLA     │     │ pc (client)  │     │ TLS/NLA                 │  │
│  │ Present cert│     │ config       │     │ Inject credentials      │  │
│  │ Recv creds  │     │ module       │     │ Forward settings        │  │
│  └──────┬──────┘     └──────┬───────┘     └──────────┬──────────────┘  │
│         │                   │                        │                  │
│         │            ┌──────┴───────┐                │                  │
│         │            │              │                │                  │
│  ┌──────▼──────┐  ┌──▼────────┐  ┌─▼──────────┐  ┌─▼──────────────┐  │
│  │ pf_input.c  │  │pf_update.c│  │pf_channel.c│  │proxy_modules.c │  │
│  │ kbd → fwd   │  │ gfx ← fwd │  │ ch ↔ route │  │ hooks/filters  │  │
│  │ mouse → fwd │  │ bmp ← fwd │  │ drdynvc    │  │ custom plugins │  │
│  └─────────────┘  └───────────┘  └────────────┘  └────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Important Considerations

### NLA Compatibility

When NLA is enabled (which modern Windows requires by default), the proxy must authenticate with the client using CredSSP. This means:

- The proxy needs to run an SSPI/GSSAPI-compatible authentication server
- For NTLM: the proxy can accept any credentials (it's the "server")
- For Kerberos: more complex — the proxy needs to be in the domain or use NTLM fallback
- Setting `ServerNlaSecurity = false` and `ServerTlsSecurity = true` in the proxy config simplifies this (client uses TLS only to the proxy)

### Certificate Trust

The MSTSC client will show a certificate warning because the proxy presents a self-signed certificate. Options:
1. User clicks "accept" on the warning (simplest)
2. Deploy the proxy certificate to the client's trusted store
3. Use `ServerRdpSecurity = true` to avoid TLS entirely (less secure, older protocol)

### Security Context

This proxy architecture is designed for legitimate use cases:
- Security testing and penetration testing (with authorization)
- RDP session recording and auditing
- Connection brokering and load balancing
- Protocol analysis and debugging

---

## Visualization

See the accompanying SVG file: **`rdp-mitm-proxy-architecture.svg`**

The SVG contains four detailed diagrams:
1. **High-Level Network Topology** — Client, Proxy, Target with port mappings
2. **Protocol Stack** — Two independent sessions with all protocol layers
3. **Connection Sequence** — Step-by-step message flow with interception points
4. **Internal Component Map** — All proxy source files and their relationships
