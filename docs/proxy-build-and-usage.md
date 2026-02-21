# freerdp-proxy: Build, Setup, and Usage Guide

Complete guide for building the stripped FreeRDP proxy, creating its runtime
configuration files, starting it, and testing it with an RDP client.

---

## 1. Prerequisites

### Build dependencies (Ubuntu / Debian)

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake git \
  libssl-dev \
  libjansson-dev \
  libkrb5-dev \
  libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
  libfuse3-dev \
  libicu-dev \
  libpkcs11-helper1-dev
```

> **Note:** X11 libraries (`libx11-dev`, etc.) are NOT required after stripping the
> X11 client from this build.

### Test-client dependency (optional, for testing only)

```bash
# System xfreerdp is used only as a test client — it is NOT built from this repo.
sudo apt-get install -y freerdp2-x11
```

---

## 2. Build the Proxy

```bash
cd /path/to/FreeRDP

# Create (or reuse) the build directory
mkdir -p build && cd build

# Configure — proxy-only, no client GUIs, no shadow server
cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWITH_SERVER=ON \
  -DWITH_PROXY=ON \
  -DWITH_CLIENT_COMMON=ON \
  -DWITH_CLIENT=OFF \
  -DWITH_X11=OFF \
  -DWITH_SHADOW=OFF \
  -DWITH_SAMPLE=OFF \
  -DWITH_RDTK=OFF \
  -DWITH_WAYLAND=OFF \
  -DCHANNEL_PRINTER=OFF \
  -DCHANNEL_URBDRC=OFF \
  -DCHANNEL_TSMF=OFF \
  -DCHANNEL_RDP2TCP=OFF \
  -DCHANNEL_RDPEAR=OFF

# Build only the proxy target
cd ..
make -C build -j$(nproc) freerdp-proxy
```

**Output binary:** `build/server/proxy/cli/freerdp-proxy`

To verify the build:

```bash
./build/server/proxy/cli/freerdp-proxy --version
# Expected: prints "FreeRDP version: 3.x.x  Git commit: xxxxxxx"
```

> **Important — shared libraries:** The proxy links against shared libraries built
> in the local `build/` tree. These are **not** installed system-wide. You must set
> `LD_LIBRARY_PATH` before running the binary, or use the provided `start-proxy.sh`
> wrapper (see § 4) which sets this automatically.

---

## 3. Runtime Configuration Files

### 3.0 Example files in `tmp-files/`

The repository ships example configuration files under `tmp-files/`:

```
tmp-files/
├── proxy.ini          # Example proxy configuration
└── credentials.json   # Example mstshash credential mapping
```

Copy them to your working directory and edit to suit your environment:

```bash
mkdir -p /tmp/freerdp-proxy-test
cp tmp-files/proxy.ini /tmp/freerdp-proxy-test/
cp tmp-files/credentials.json /tmp/freerdp-proxy-test/
```

The TLS certificate and key are **not** included (private keys must not be committed
to source control). Generate them as described in § 3.1.

### 3.1 TLS Certificate and Key

The proxy presents a TLS certificate to connecting clients. Generate a self-signed
certificate:

```bash
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout /tmp/freerdp-proxy-test/proxy-key.pem \
  -out    /tmp/freerdp-proxy-test/proxy-cert.pem \
  -days 365 \
  -subj "/CN=freerdp-proxy"
```

This creates two files:

| File | Purpose |
|------|---------|
| `proxy-cert.pem` | TLS certificate presented to connecting RDP clients |
| `proxy-key.pem` | Private key for the certificate (keep secret, never commit) |

### 3.2 Credential Mapping File (`credentials.json`)

The proxy maps an **mstshash identifier** (sent by the RDP client in the
`LoadBalanceInfo` field) to the real target server and credentials.

Edit `/tmp/freerdp-proxy-test/credentials.json`:

```json
{
  "credentials": {
    "myhash1": {
      "target_host": "192.168.1.100",
      "target_port": 3389,
      "username": "your_windows_user",
      "password": "your_windows_password",
      "domain": ""
    },
    "myhash2": {
      "target_host": "10.0.0.50",
      "target_port": 3389,
      "username": "administrator",
      "password": "AdminPassword123",
      "domain": "CORP"
    }
  }
}
```

**Field reference:**

| Field | Description |
|-------|-------------|
| `"myhash1"` | The mstshash key the client sends in `LoadBalanceInfo`. Can be any string. |
| `target_host` | IP or hostname of the real Windows RDP server |
| `target_port` | RDP port on the real server (almost always `3389`) |
| `username` | Windows username on the real server |
| `password` | Windows password for that account |
| `domain` | Windows domain name, or `""` for local accounts |

> **mstshash trimming:** The proxy automatically strips trailing whitespace and CRLF
> characters from the extracted mstshash value. This handles clients that include
> trailing `\r\n` in the `LoadBalanceInfo` field.

### 3.3 Proxy Configuration File (`proxy.ini`)

Edit `/tmp/freerdp-proxy-test/proxy.ini`:

```ini
[Server]
Host = 0.0.0.0
Port = 13389

[Target]
FixedTarget = false
Port = 3389
TlsSecLevel = 1

[Security]
# Client → Proxy: TLS only, no NLA (client does NOT need to supply real credentials)
ServerTlsSecurity = true
ServerNlaSecurity = false
ServerRdpSecurity = false

# Proxy → Target: NLA required (needed by Windows 10/11 servers)
ClientTlsSecurity = true
ClientNlaSecurity = true
ClientRdpSecurity = false

[Certificates]
CertificateFile = /tmp/freerdp-proxy-test/proxy-cert.pem
PrivateKeyFile  = /tmp/freerdp-proxy-test/proxy-key.pem

[Credentials]
MappingFile = /tmp/freerdp-proxy-test/credentials.json
```

**Key security settings explained:**

| Setting | Meaning |
|---------|---------|
| `ServerNlaSecurity = false` | Proxy does **not** require NLA from the connecting client. The client only needs the mstshash token, not real credentials. |
| `ServerTlsSecurity = true` | Connection is TLS-encrypted. Client uses `/cert:ignore` to accept the self-signed cert. |
| `ClientNlaSecurity = true` | Proxy authenticates to the target Windows server using NLA/CredSSP with real credentials from `credentials.json`. |

---

## 4. Start the Proxy

### 4.1 Using `start-proxy.sh` (recommended)

The repository includes a wrapper script that sets `LD_LIBRARY_PATH` automatically
and validates the config file before starting:

```bash
# With default config (/tmp/freerdp-proxy-test/proxy.ini)
./start-proxy.sh

# With a custom config file
./start-proxy.sh /path/to/your/proxy.ini

# In the background, logging to a file
./start-proxy.sh /tmp/freerdp-proxy-test/proxy.ini > /tmp/proxy.log 2>&1 &
```

### 4.2 Starting manually

If you prefer to run the binary directly, set `LD_LIBRARY_PATH` first:

```bash
export LD_LIBRARY_PATH=\
build/libfreerdp:\
build/winpr/libwinpr:\
build/libfreerdp/core:\
build/server/proxy

./build/server/proxy/cli/freerdp-proxy /tmp/freerdp-proxy-test/proxy.ini
```

> **Why `LD_LIBRARY_PATH`?** The proxy was built against shared libraries in the
> local `build/` tree. Without this variable, the OS cannot find them and the binary
> will either fail to start or crash immediately.

### 4.3 Expected startup output

```
[INFO][com.freerdp.proxy.credentials] Loading credential mapping from: /tmp/freerdp-proxy-test/credentials.json
[INFO][com.freerdp.proxy.credentials] Loaded credentials for mstshash: myhash1 (user: your_windows_user)
[INFO][com.freerdp.proxy.credentials] Loaded credentials for mstshash: myhash2 (user: administrator)
[INFO][com.freerdp.proxy.credentials] Loaded 2 credential mappings
[INFO][com.freerdp.proxy.server]      Proxy listening on [0.0.0.0]:13389
```

The proxy is now listening on port **13389**.

### 4.4 Verify the proxy is listening

```bash
ss -tlnp | grep 13389
# Expected: LISTEN  0  10  0.0.0.0:13389
```

### 4.5 Stop the proxy

```bash
pkill -f freerdp-proxy
# or, if you know the PID:
kill <PID>
```

---

## 5. Test the Proxy

### Understanding the two binaries

| Binary | Role |
|--------|------|
| `freerdp-proxy` | **The proxy** — listens on 13389, injects credentials, forwards to target |
| `xfreerdp` | **Test client** — a system-installed RDP client used to *connect to* the proxy |

These are entirely separate programs. `xfreerdp` is the system-installed RDP client;
it is not built from this stripped repository.

### 5.1 Quick connectivity test

```bash
xfreerdp \
  /v:localhost:13389 \
  /load-balance-info:"Cookie: mstshash=myhash1" \
  /cert:ignore \
  /u:anything \
  /p:anything \
  /size:800x600
```

> **Note on `/u:` and `/p:`:** The proxy completely **ignores** these values.
> It uses the mstshash key (`myhash1`) to look up the real credentials from
> `credentials.json`. You can put anything in `/u:` and `/p:`.

If credentials are correct and the target is reachable, a Windows desktop session
will open in the xfreerdp window.

### 5.2 What to look for in the proxy log

A successful connection produces this sequence in the proxy log:

```
Nego cookie: Cookie: mstshash=myhash1            ← client's token received
Extracted mstshash: myhash1                       ← token parsed (CRLF stripped)
Using mapped credentials for mstshash: myhash1    ← mapping found in JSON
  (user: your_windows_user)
remote target is 192.168.1.100:3389               ← target from mapping
connecting using client info: Username: your_windows_user  ← real credentials injected
connecting using security settings: nla=1          ← NLA to real Windows server
```

### 5.3 Windows `.rdp` file (for mstsc on Windows)

To test from a Windows machine, create a `.rdp` file:

```
full address:s:PROXY_IP:13389
server port:i:13389
username:s:myhash1
prompt for credentials on client:i:0
use redirection server name:i:1
LoadBalanceInfo:s:Cookie: mstshash=myhash1
```

Replace `PROXY_IP` with the proxy machine's IP address and open the file with Remote
Desktop Connection (`mstsc`). The real credentials are never shown to the user.

---

## 6. Security Notes

### Client ↔ Proxy (Session A)
- Security: **TLS only** (`PROTOCOL_SSL = 0x01`)
- The proxy rejects NLA from the client (`ServerNlaSecurity = false`)
- Confirmed in the X.224 Negotiation Response: `selectedProtocol = 0x00000001`
- The client's `/u:` and `/p:` values travel inside the TLS tunnel but are ignored

### Proxy ↔ Target Windows (Session B)
- Security: **NLA/CredSSP** (`ClientNlaSecurity = true`)
- Real credentials from `credentials.json` are injected here
- Kerberos is attempted first; falls back to NTLM for local accounts (benign)

```
xfreerdp ──[TLS]──► proxy:13389 ──[NLA]──► Windows:3389
            No NLA                NLA with real credentials
```

---

## 7. Troubleshooting

### Proxy just prints "Usage:" and exits

You forgot to pass the config file path:

```bash
# Wrong — no config file
./build/server/proxy/cli/freerdp-proxy

# Correct
./start-proxy.sh
# or
./build/server/proxy/cli/freerdp-proxy /tmp/freerdp-proxy-test/proxy.ini
```

### "library not found" / crash on startup

`LD_LIBRARY_PATH` is not set. Use `./start-proxy.sh` which sets it automatically,
or set it manually (see § 4.2).

### "No negotiation cookie available"

The client is not sending the mstshash token. Ensure:
- `.rdp` file contains `LoadBalanceInfo:s:Cookie: mstshash=<key>`
- xfreerdp command includes `/load-balance-info:"Cookie: mstshash=<key>"`

### "No mapping found for mstshash: xyz"

The key `xyz` is not in `credentials.json`. Keys are **case-sensitive**. Also check
that the JSON file path in `proxy.ini` under `[Credentials] MappingFile` is correct.

### `HYBRID_REQUIRED_BY_SERVER` / NLA failure against target

The target Windows server requires NLA. Ensure `ClientNlaSecurity = true` in `proxy.ini`.

### `krb5_parse_name: Configuration file does not specify default realm`

Benign Kerberos warning — the proxy falls back to NTLM, which works for local Windows
accounts. Safe to ignore.

### Port already in use

```bash
pkill -f freerdp-proxy
# or
kill $(ss -tlnp | awk '/13389/{match($0,/pid=([0-9]+)/,a); print a[1]}')
```

---

## 8. Repository File Layout

```
FreeRDP/
├── start-proxy.sh              # Wrapper: sets LD_LIBRARY_PATH and starts proxy
├── tmp-files/
│   ├── proxy.ini               # Example proxy configuration (copy and edit)
│   ├── credentials.json        # Example credential mapping (copy and edit)
│   └── .gitignore              # Excludes TLS cert/key from version control
├── docs/
│   ├── proxy-build-and-usage.md        # This file
│   ├── proxy-credential-injection.md   # Architecture & implementation reference
│   ├── proxy-mstshash-implementation.md
│   ├── rdp-mitm-proxy-architecture.md
│   └── stripped-codebase.md
└── build/                      # (generated — not in git)
    └── server/proxy/cli/freerdp-proxy  # The proxy binary

Runtime files (generated — not in git):
/tmp/freerdp-proxy-test/
├── proxy.ini          # Your working copy of the config
├── credentials.json   # Your working copy of the credential map
├── proxy-cert.pem     # Generated by openssl (§ 3.1)
└── proxy-key.pem      # Generated by openssl (§ 3.1)
```
