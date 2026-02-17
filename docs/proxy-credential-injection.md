# FreeRDP Proxy: mstshash Credential Injection

Complete reference for the mstshash-based credential injection enhancement to the
FreeRDP proxy server. This single document consolidates all architecture, implementation,
configuration, and operational details.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Architecture](#2-architecture)
3. [Protocol-Level Details](#3-protocol-level-details)
4. [Implementation](#4-implementation)
   - 4.1 [New Public API: `freerdp_nego_get_cookie()`](#41-new-public-api-freerdp_nego_get_cookie)
   - 4.2 [Credential Mapping Module (`pf_credentials`)](#42-credential-mapping-module-pf_credentials)
   - 4.3 [Server-Side Integration (`pf_server`)](#43-server-side-integration-pf_server)
   - 4.4 [Configuration Integration (`pf_config`)](#44-configuration-integration-pf_config)
5. [Configuration Reference](#5-configuration-reference)
6. [Build & Run](#6-build--run)
7. [Validated Test Flow](#7-validated-test-flow)
8. [File Change Summary](#8-file-change-summary)
9. [The Cookie vs Routing-Token Bug](#9-the-cookie-vs-routing-token-bug)
10. [Troubleshooting](#10-troubleshooting)

---

## 1. Overview

The FreeRDP proxy (`freerdp-proxy`) can act as a transparent RDP man-in-the-middle,
accepting client connections and forwarding them to a real RDP target server. This
enhancement adds **credential injection** based on the `mstshash` value carried in
the client's `LoadBalanceInfo` field.

**What it does:**

- A client `.rdp` file contains `LoadBalanceInfo:s:Cookie: mstshash=hash1`
- The client connects to the proxy (e.g., port 13389) using `hash1` as its identifier
- The proxy extracts `hash1`, looks it up in a JSON credential mapping file
- The mapping provides the **real target host, port, username, password, and domain**
- The proxy connects to the real RDP server using the injected credentials
- The client never knows or needs the real credentials

**Key properties:**

- Single `freerdp-proxy` binary, no additional processes
- No NLA/authentication required between client and proxy (TLS only)
- NLA-capable for the proxy-to-target connection (supports Windows 10/11)
- Backward compatible with the legacy `Cookie: msts=host:port` format

---

## 2. Architecture

### The Two-Session Model

The proxy maintains two independent RDP sessions bridged internally:

```
                        Session A (Server-Side)               Session B (Client-Side)
┌────────────┐      TLS (no NLA)       ┌─────────────────┐       TLS + NLA        ┌──────────────┐
│            │◄════════════════════════►│                 │◄═══════════════════════►│              │
│ RDP Client │    Port 13389           │  freerdp-proxy  │    Port 3389            │ Windows RDP  │
│ (mstsc /   │                         │                 │                         │ Server       │
│  xfreerdp) │  Cookie: mstshash=hash1 │  pServerContext │  Username: guest123     │ 192.168.1.33 │
│            │                         │  pClientContext │  Password: novell@123   │              │
└────────────┘                         └─────────────────┘                         └──────────────┘

 Client only knows             Proxy extracts hash1,              Target receives
 "hash1" identifier            looks up real credentials,         real credentials
                                injects them into Session B        via NLA/CredSSP
```

### Source Code Layout

```
server/proxy/
├── cli/freerdp_proxy.c       # CLI entry point
├── pf_server.c               # Server-side: accepts clients, extracts mstshash
├── pf_client.c               # Client-side: connects to target
├── pf_credentials.c          # NEW: JSON credential mapping loader
├── pf_credentials.h          # NEW: credential mapping API
├── pf_config.c               # Config loader (INI + credential mapping)
├── pf_channel.c              # Channel routing
├── pf_update.c               # Graphics/update forwarding
└── CMakeLists.txt            # Build system

include/freerdp/
├── freerdp.h                 # Public API (added freerdp_nego_get_cookie)
└── server/proxy/
    └── proxy_config.h        # proxyConfig struct (added credentialMap fields)

libfreerdp/core/
├── nego.c                    # Negotiation layer (added nego_get_cookie)
├── nego.h                    # Internal nego API
└── freerdp.c                 # Public API impl (added freerdp_nego_get_cookie)
```

---

## 3. Protocol-Level Details

### How mstshash Travels Through the RDP Protocol

When a client connects with `LoadBalanceInfo:s:Cookie: mstshash=hash1`, the
flow through the RDP protocol stack is:

```
1. TCP connect to proxy:13389
2. X.224 Connection Request PDU (CR)
   ├── Contains: "Cookie: mstshash=hash1\r\n"
   └── Parsed by: nego_read_request_token_or_cookie() in nego.c
3. Negotiation layer stores it:
   ├── "Cookie: mstshash=..." → nego_set_cookie()     ← stored as COOKIE
   ├── "Cookie: msts=..."     → nego_set_routing_token() ← stored as ROUTING TOKEN
   └── "tsv:..." / "mth://..."→ nego_set_routing_token()
4. X.224 Connection Confirm PDU (CC) sent back
5. TLS handshake (proxy presents self-signed cert)
6. RDP connection sequence continues
7. PostConnect callback fires → pf_server_post_connect()
   ├── pf_server_extract_mstshash() calls freerdp_nego_get_cookie()
   ├── Retrieves "Cookie: mstshash=hash1"
   ├── Parses hash value: "hash1"
   ├── Looks up in credential map → CredentialEntry found
   └── Injects credentials + target into client-side settings
8. Proxy spawns client thread → connects to 192.168.1.33:3389 with NLA
```

### Critical Distinction: Cookie vs Routing Token

The RDP specification (MS-RDPBCGR 2.2.1.1) defines the X.224 CR PDU as containing
either a **routing token** or a **cookie**, which are mutually exclusive:

| Format | Type | Stored via | Retrieved via |
|--------|------|------------|---------------|
| `Cookie: mstshash=value` | Cookie | `nego_set_cookie()` | `freerdp_nego_get_cookie()` |
| `Cookie: msts=host:port` | Routing Token | `nego_set_routing_token()` | `freerdp_nego_get_routing_token()` |
| `tsv://MS Terminal Services Plugin...` | Routing Token | `nego_set_routing_token()` | `freerdp_nego_get_routing_token()` |

The original proxy code only used `freerdp_nego_get_routing_token()`, which returned
`NULL` for mstshash cookies. This was the root cause of the connection failure.
The fix adds `freerdp_nego_get_cookie()` as a new public API.

---

## 4. Implementation

### 4.1 New Public API: `freerdp_nego_get_cookie()`

Added to expose the negotiation cookie (which stores mstshash values) through
the public FreeRDP API.

**Internal layer** (`libfreerdp/core/nego.h` / `nego.c`):

```c
/* nego.h - added declaration */
FREERDP_LOCAL const char* nego_get_cookie(const rdpNego* nego);

/* nego.c - added implementation */
const char* nego_get_cookie(const rdpNego* nego)
{
    if (!nego)
        return NULL;
    return nego->cookie;
}
```

**Public layer** (`include/freerdp/freerdp.h` / `libfreerdp/core/freerdp.c`):

```c
/* freerdp.h - added declaration */
FREERDP_API const char* freerdp_nego_get_cookie(const rdpContext* context);

/* freerdp.c - added implementation */
const char* freerdp_nego_get_cookie(const rdpContext* context)
{
    if (!context || !context->rdp)
        return NULL;
    return nego_get_cookie(context->rdp->nego);
}
```

### 4.2 Credential Mapping Module (`pf_credentials`)

New files: `server/proxy/pf_credentials.h` and `server/proxy/pf_credentials.c`.

**Data structure:**

```c
typedef struct {
    char* username;
    char* password;
    char* domain;
    char* target_host;   /* Target RDP server address */
    UINT16 target_port;  /* Target RDP server port (default 3389) */
} CredentialEntry;
```

**API:**

```c
/* Load credential mapping from a JSON file into a HashTable */
wHashTable* pf_credentials_load_mapping(const char* filepath);

/* Lookup credentials by mstshash key */
CredentialEntry* pf_credentials_lookup(wHashTable* map, const char* mstshash);

/* Free the entire mapping and all entries */
void pf_credentials_free_mapping(wHashTable* map);

/* Free a single credential entry */
void pf_credentials_free_entry(CredentialEntry* entry);
```

The JSON parser is custom (no external dependencies), using brace-counting
to extract credential blocks and simple string matching for key-value pairs.

### 4.3 Server-Side Integration (`pf_server`)

The core logic lives in `pf_server_get_target_info()`, which is called from
`pf_server_post_connect()` after a client connects:

```c
/* Extract mstshash from the negotiation cookie */
static char* pf_server_extract_mstshash(rdpContext* context)
{
    const char* cookie = freerdp_nego_get_cookie(context);
    /* Parse "Cookie: mstshash=<value>" → return "<value>" */
}

/* Target resolution with credential injection */
static BOOL pf_server_get_target_info(rdpContext* context,
                                      rdpSettings* settings,
                                      const proxyConfig* config)
{
    /* For LOAD_BALANCE_INFO method: */
    char* mstshash = pf_server_extract_mstshash(context);

    if (mstshash && config->credentialMap) {
        CredentialEntry* cred = pf_credentials_lookup(map, mstshash);
        if (cred) {
            /* Inject: ServerHostname, ServerPort, Username, Password, Domain */
            return TRUE;
        }
    }
    /* Fallback: legacy Cookie: msts=host:port format */
    return pf_server_parse_target_from_routing_token(...);
}
```

### 4.4 Configuration Integration (`pf_config`)

Added `[Credentials]` section support in the INI parser:

```c
/* pf_config.c */
static BOOL pf_config_load_credentials(wIniFile* ini, proxyConfig* config)
{
    const char* mapping_file = pf_config_get_str(ini, "Credentials", "MappingFile", FALSE);
    if (mapping_file) {
        config->CredentialMappingFile = _strdup(mapping_file);
        config->credentialMap = pf_credentials_load_mapping(mapping_file);
    }
    return TRUE;
}
```

Also fixed `pf_config_clone()` to properly deep-copy `TargetUser`, `TargetDomain`,
`TargetPassword`, `CredentialMappingFile`, and re-load `credentialMap` for the
cloned config (preventing use-after-free when the original config is freed).

---

## 5. Configuration Reference

### proxy.ini

```ini
[Server]
Host = 0.0.0.0
Port = 13389

[Target]
FixedTarget = false
Port = 3389
TlsSecLevel = 1

[Security]
# Proxy accepts TLS connections from clients (no NLA required)
ServerTlsSecurity = true
ServerNlaSecurity = false
ServerRdpSecurity = false

# Proxy connects to target with NLA (required by Windows 10/11)
ClientTlsSecurity = true
ClientNlaSecurity = true
ClientRdpSecurity = false

[Certificates]
CertificateFile = /path/to/proxy-cert.pem
PrivateKeyFile = /path/to/proxy-key.pem

[Credentials]
MappingFile = /path/to/credentials.json
```

### credentials.json

```json
{
  "credentials": {
    "hash1": {
      "target_host": "192.168.1.33",
      "target_port": 3389,
      "username": "guest123",
      "password": "novell@123",
      "domain": ""
    },
    "hash2": {
      "target_host": "10.0.0.50",
      "target_port": 3389,
      "username": "admin",
      "password": "P@ssw0rd",
      "domain": "CORP"
    }
  }
}
```

Each top-level key under `"credentials"` is an mstshash identifier. The corresponding
object provides the target server and credentials that will be injected.

### Client .rdp file (Windows)

```
full address:s:PROXY_IP:13389
server port:i:13389
username:s:hash1
prompt for credentials on client:i:0
use redirection server name:i:1
LoadBalanceInfo:s:Cookie: mstshash=hash1
```

### Client command (Linux / xfreerdp)

```bash
xfreerdp /v:PROXY_IP:13389 \
  /load-balance-info:"Cookie: mstshash=hash1" \
  /cert:ignore \
  /u:hash1 /p:dummy
```

The `/u:` and `/p:` values are irrelevant — the proxy ignores them and injects
the real credentials from the mapping file.

---

## 6. Build & Run

### Prerequisites (Ubuntu/Debian)

```bash
sudo apt-get install -y build-essential cmake git libssl-dev \
  libx11-dev libxext-dev libxinerama-dev libxcursor-dev \
  libxdamage-dev libxv-dev libxkbfile-dev libasound2-dev \
  libxml2-dev libxrandr-dev
```

### Build

```bash
cd /path/to/FreeRDP
mkdir -p build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWITH_SERVER=ON \
  -DWITH_PROXY=ON \
  -DCHANNEL_PRINTER=OFF \
  -DCHANNEL_URBDRC=OFF
make -j$(nproc) freerdp-proxy
```

Binary output: `build/server/proxy/cli/freerdp-proxy`

### Generate TLS Certificates

```bash
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout proxy-key.pem -out proxy-cert.pem \
  -days 365 -subj "/CN=proxy.local"
```

### Run the Proxy

```bash
./freerdp-proxy /path/to/proxy.ini
```

Expected startup log:

```
[INFO] Loading credential mapping from: /path/to/credentials.json
[INFO] Loaded credentials for mstshash: hash1 (user: guest123)
[INFO] Loaded 1 credential mappings
[INFO] Listening on [0.0.0.0]:13389
```

---

## 7. Validated Test Flow

Tested against a real Windows 10 VM at `192.168.1.33` with user `guest123` / `novell@123`.

### Direct Connection (baseline)

```bash
xfreerdp /v:192.168.1.33 /u:guest123 /p:'novell@123' /cert:ignore +auth-only
# Output: Authentication only, exit status 0  ← SUCCESS
```

### Through the Proxy

```bash
# Terminal 1: Start proxy
./freerdp-proxy /tmp/freerdp-proxy-test/proxy.ini

# Terminal 2: Connect through proxy
xfreerdp /v:localhost:13389 \
  /load-balance-info:"Cookie: mstshash=hash1" \
  /cert:ignore /u:hash1 /p:dummy /size:800x600
```

### Proxy Log Output (successful flow)

```
Nego cookie: Cookie: mstshash=hash1              ← Cookie retrieved from client
Extracted mstshash: hash1                         ← Hash value parsed
Using mapped credentials for mstshash: hash1      ← Mapping found
  (user: guest123)
Connecting to target: 192.168.1.33:3389           ← Target from mapping
remote target is 192.168.1.33:3389                ← PostConnect confirmed
connecting using client info: Username: guest123   ← Credentials injected
connecting using security settings: nla=1          ← NLA enabled for target
client activated                                   ← Session B established
client logon info: Username: guest123,             ← Logged into Windows
  Domain: DESKTOP-F0SA6BD
```

---

## 8. File Change Summary

### New Files

| File | Lines | Purpose |
|------|-------|---------|
| `server/proxy/pf_credentials.h` | 78 | Credential mapping API declarations |
| `server/proxy/pf_credentials.c` | 399 | JSON parser, hash table, lookup functions |

### Modified Files

| File | Change | Purpose |
|------|--------|---------|
| `include/freerdp/freerdp.h` | +1 | `freerdp_nego_get_cookie()` declaration |
| `libfreerdp/core/freerdp.c` | +8 | `freerdp_nego_get_cookie()` implementation |
| `libfreerdp/core/nego.h` | +1 | `nego_get_cookie()` declaration |
| `libfreerdp/core/nego.c` | +7 | `nego_get_cookie()` implementation |
| `include/freerdp/server/proxy/proxy_config.h` | +4 | `CredentialMappingFile` and `credentialMap` fields |
| `server/proxy/pf_config.c` | +54 | `[Credentials]` INI parsing, `pf_config_clone` fixes |
| `server/proxy/pf_server.c` | +99 | `pf_server_extract_mstshash()`, credential injection in `pf_server_get_target_info()` |
| `server/proxy/CMakeLists.txt` | +2 | Added `pf_credentials.c/h` to build |

---

## 9. The Cookie vs Routing-Token Bug

This was the root cause of the original `freerdp_nego_get_routing_token()` returning
`NULL` and the proxy failing with `PostConnect for peer failed`.

### The Problem

In `libfreerdp/core/nego.c`, function `nego_read_request_token_or_cookie()` (line 880):

```c
if (memcmp(ptr, "Cookie: mstshash=", 17) != 0) {
    // Not mstshash — check for msts=, tsv:, mth://
    isToken = TRUE;   // ← these are stored as routing tokens
} else {
    // IS mstshash
    // isToken remains FALSE  ← stored as a cookie
}

// Later:
if (isToken)
    result = nego_set_routing_token(nego, str, len);  // msts=, tsv:, mth://
else
    result = nego_set_cookie(nego, str);               // mstshash=
```

So `Cookie: mstshash=` is stored via `nego_set_cookie()` and is **only** retrievable
via `nego->cookie`, not via `nego_get_routing_token()`.

### The Fix

Added a new public API `freerdp_nego_get_cookie()` that retrieves `nego->cookie`,
and updated `pf_server_extract_mstshash()` to use it instead of
`freerdp_nego_get_routing_token()`.

### Additional Bugs Fixed

- **`pf_config_clone()` use-after-free:** `TargetUser`, `TargetDomain`, `TargetPassword`
  were shallow-copied (pointer copy) but freed in `pf_server_config_free()`. Fixed
  by adding proper deep-copy calls.
- **`pf_config_clone()` double-free:** `credentialMap` pointer was shared between
  original and clone. Fixed by re-loading the credential mapping for the clone.
- **`pf_credentials.c` API misuse:** Direct access to `wHashTable->valueFree` replaced
  with proper `HashTable_ValueObject()` API.

---

## 10. Troubleshooting

### "No negotiation cookie available"

The client is not sending `LoadBalanceInfo`. Ensure the `.rdp` file contains:
```
LoadBalanceInfo:s:Cookie: mstshash=hash1
```
Or use the xfreerdp flag:
```
/load-balance-info:"Cookie: mstshash=hash1"
```

### "No mapping found for mstshash: xyz"

The hash value `xyz` does not exist in `credentials.json`. Check:
- The JSON file path in `proxy.ini` under `[Credentials] MappingFile`
- The hash key matches exactly (case-sensitive)
- The JSON is valid (check for trailing commas, missing quotes)

### "HYBRID_REQUIRED_BY_SERVER" / NLA errors

The target Windows server requires NLA. Ensure `proxy.ini` has:
```ini
ClientNlaSecurity = true
```

### Kerberos errors (krb5_parse_name)

Benign warning — Kerberos fails because there's no domain realm configured.
The proxy falls back to NTLM authentication, which works for local Windows accounts.

### "address already in use"

Another process is using port 13389. Kill it:
```bash
pkill -9 freerdp-proxy
```

### Connection works but client shows blank/disconnects

The first test may be killed by timeout. Try a longer connection:
```bash
xfreerdp /v:localhost:13389 /load-balance-info:"Cookie: mstshash=hash1" \
  /cert:ignore /u:hash1 /p:dummy /size:1024x768
```
