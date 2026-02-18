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
make -j$(nproc) freerdp-proxy
```

**Output binary:** `build/server/proxy/cli/freerdp-proxy`

To verify the build:

```bash
./build/server/proxy/cli/freerdp-proxy
# Expected: prints version info and usage
```

---

## 3. Runtime Configuration Files

All runtime files are placed under one directory. This guide uses `/tmp/freerdp-proxy-test/`
as the example path (change it to any permanent location you prefer).

```bash
mkdir -p /tmp/freerdp-proxy-test
```

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
| `proxy-key.pem` | Private key for the certificate (keep secret) |

### 3.2 Credential Mapping File (`credentials.json`)

The proxy maps an **mstshash identifier** (sent by the RDP client in the
`LoadBalanceInfo` field) to the real target server and credentials.

```bash
cat > /tmp/freerdp-proxy-test/credentials.json << 'EOF'
{
  "credentials": {
    "hash1": {
      "target_host": "192.168.1.33",
      "target_port": 3389,
      "username": "guest123",
      "password": "novell@123",
      "domain": ""
    },
    "adminuser": {
      "target_host": "10.0.0.50",
      "target_port": 3389,
      "username": "administrator",
      "password": "P@ssw0rd!",
      "domain": "CORP"
    }
  }
}
EOF
```

**Field reference:**

| Field | Description |
|-------|-------------|
| `"hash1"` | The mstshash key the client sends in `LoadBalanceInfo`. Can be any string. |
| `target_host` | IP or hostname of the real Windows RDP server |
| `target_port` | RDP port on the real server (almost always `3389`) |
| `username` | Windows username on the real server |
| `password` | Windows password for that account |
| `domain` | Windows domain name, or `""` for local accounts |

### 3.3 Proxy Configuration File (`proxy.ini`)

```bash
cat > /tmp/freerdp-proxy-test/proxy.ini << 'EOF'
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
EOF
```

**Key security settings explained:**

| Setting | Value | Meaning |
|---------|-------|---------|
| `ServerNlaSecurity = false` | | Proxy does **not** require NLA from the connecting client — the client sends only the mstshash token, not real credentials |
| `ServerTlsSecurity = true` | | Connection is TLS-encrypted (client uses `/cert:ignore` to accept the self-signed cert) |
| `ClientNlaSecurity = true` | | Proxy authenticates to the target Windows server using NLA/CredSSP with the real credentials from `credentials.json` |

---

## 4. Start the Proxy

```bash
./build/server/proxy/cli/freerdp-proxy /tmp/freerdp-proxy-test/proxy.ini
```

Expected startup output:

```
[INFO][com.freerdp.proxy.credentials] Loading credential mapping from: /tmp/freerdp-proxy-test/credentials.json
[INFO][com.freerdp.proxy.credentials] Loaded credentials for mstshash: hash1 (user: guest123)
[INFO][com.freerdp.proxy.credentials] Loaded credentials for mstshash: adminuser (user: administrator)
[INFO][com.freerdp.proxy.credentials] Loaded 2 credential mappings
[INFO][com.freerdp.proxy.config]      Listening on [0.0.0.0]:13389
```

The proxy is now listening on port **13389**.

---

## 5. Test the Proxy

### Understanding the two binaries

| Binary | Role |
|--------|------|
| `freerdp-proxy` | **The proxy** — listens on 13389, injects credentials, forwards to target |
| `xfreerdp` | **Test client** — an RDP client used to *connect to* the proxy for testing |

These are entirely separate programs. `xfreerdp` is the system-installed RDP client;
it is not built from this stripped repository.

### 5.1 Quick connectivity test (auth-only)

Connect to the proxy without starting a graphical session:

```bash
xfreerdp \
  /v:localhost:13389 \
  /load-balance-info:"Cookie: mstshash=hash1" \
  /cert:ignore \
  /u:hash1 \
  /p:dummy \
  +auth-only
```

Expected xfreerdp output:

```
[INFO] Authentication only, exit status 0
```

> **Note on `/u:` and `/p:`:** The proxy completely **ignores** these values.
> It uses the mstshash value (`hash1`) to look up the real credentials from
> `credentials.json`. You can put anything in `/u:` and `/p:`.

> **Note on `+auth-only`:** This flag tests only the **client → proxy** TLS
> handshake, not the full proxy → target authentication. Since `ServerNlaSecurity = false`,
> the proxy never challenges the client for credentials — TLS completion alone counts as
> "auth success." To fully validate that the target credentials in `credentials.json`
> are correct, use a full connection (§ 5.2).

### 5.2 Full desktop session test

```bash
xfreerdp \
  /v:localhost:13389 \
  /load-balance-info:"Cookie: mstshash=hash1" \
  /cert:ignore \
  /u:hash1 \
  /p:dummy \
  /size:1024x768
```

If the credentials in `credentials.json` are correct and the target server is reachable,
a Windows desktop session will open in the xfreerdp window.

### 5.3 What to look for in the proxy log

```
Nego cookie: Cookie: mstshash=hash1              ← client's token received
Extracted mstshash: hash1                         ← token parsed
Using mapped credentials for mstshash: hash1      ← mapping found in JSON
  (user: guest123)
Connecting to target: 192.168.1.33:3389           ← real target from mapping
remote target is 192.168.1.33:3389
connecting using client info: Username: guest123   ← real credentials injected
connecting using security settings: nla=1          ← NLA to real Windows server
client activated                                   ← NLA succeeded, session up
client logon info: Username: guest123, Domain: …  ← Windows login confirmed
```

### 5.4 Windows `.rdp` file (for mstsc on Windows)

To test from a Windows machine, create a `.rdp` file:

```
full address:s:PROXY_IP:13389
server port:i:13389
username:s:hash1
prompt for credentials on client:i:0
use redirection server name:i:1
LoadBalanceInfo:s:Cookie: mstshash=hash1
```

Replace `PROXY_IP` with the proxy machine's IP address and open the file with Remote
Desktop Connection (`mstsc`). The real credentials are never shown to the user.

---

## 6. Troubleshooting

### "No negotiation cookie available"

The client is not sending the mstshash token. Ensure the `.rdp` file contains
`LoadBalanceInfo:s:Cookie: mstshash=<key>` or the xfreerdp command includes
`/load-balance-info:"Cookie: mstshash=<key>"`.

### "No mapping found for mstshash: xyz"

The key `xyz` is not in `credentials.json`. Keys are **case-sensitive**.

### `HYBRID_REQUIRED_BY_SERVER` / NLA failure against target

The target Windows server requires NLA. Ensure `ClientNlaSecurity = true` in `proxy.ini`.

### `krb5_parse_name: Configuration file does not specify default realm`

Benign Kerberos warning — the proxy falls back to NTLM, which works for local Windows
accounts. Safe to ignore.

### Port already in use

```bash
kill $(lsof -ti:13389) 2>/dev/null
```

### Verify proxy is listening

```bash
ss -tlnp | grep 13389
# Expected: LISTEN 0 10 0.0.0.0:13389
```

---

## 7. File Summary

```
/tmp/freerdp-proxy-test/
├── proxy.ini          # Main proxy configuration (INI format)
├── credentials.json   # mstshash → {host, port, username, password, domain} map
├── proxy-cert.pem     # Self-signed TLS certificate (presented to clients)
└── proxy-key.pem      # TLS private key (keep secret)
```
