[Simplified Chinese](networking.zh_CN.md) · **English**

# Gateway connectivity

The Passport connects over Wi-Fi directly to your OpenClaw gateway. The Mac/iPhone
app configures it; neither app forwards conversations. Install the
[project plugin](../tools/openclaw/README.md) on the gateway host first. Examples
use reserved placeholders, not a working shared service.

## Choose an ingress

| Option | Device endpoint | When to use |
| --- | --- | --- |
| Same LAN | `ws://gateway-host:18789` | Trusted local testing only; no TLS |
| Cloudflare Tunnel | `wss://passport.example` | Public TLS, no router port forwarding |
| SakuraFrp or another TCP tunnel | `wss://assigned-host:assigned-port` | Regional routing; TLS still required |
| Private VPN | Private gateway address | Device/router must actually participate in the VPN |

An HTTPS proxy or supported Shadowsocks route is an optional outbound path from
the device, not gateway authentication. Set it per assistant only when needed.
Use direct routing to a reliable regional endpoint when possible. Never disable
certificate checks, expose an unauthenticated gateway, or publish credentials.

## Cloudflare Tunnel

1. Install `cloudflared` on the OpenClaw host and authenticate with your own
   Cloudflare account. Create a named tunnel and a DNS route in a zone you own.
2. Keep OpenClaw bound to loopback. Route the entire host to
   `http://127.0.0.1:18789`, including native gateway WebSocket traffic and
   `/passport/v1`. A hooks-only HTTP route cannot carry Passport sessions.
3. Keep tunnel credentials outside this repository with owner-only permissions.
   Example ingress in your private cloudflared configuration:

```yaml
tunnel: YOUR_TUNNEL_ID
credentials-file: /private/path/to/YOUR_TUNNEL_ID.json
ingress:
  - hostname: passport.example
    service: http://127.0.0.1:18789
  - service: http_status:404
```

4. Run the tunnel using the host's normal service manager. Check the public host
   and native WebSocket upgrade from the actual device network.
5. Preserve OpenClaw's device pairing/token authentication. If you add Cloudflare
   Access, the device must also be configured with its supported service-token
   headers; an interactive browser login page is not a WebSocket authentication
   mechanism. Use narrow policies and independently revocable credentials.

The plugin forwards through loopback to native OpenClaw. Its local hop must be
accepted by the existing trusted-proxy policy; do not broadly trust arbitrary
forwarded headers. Review the gateway's public exposure separately. A tunnel
hides the origin address but does not make a publicly reachable service private.

## SakuraFrp / regional TCP tunnel

1. Install the official launcher/client on the gateway host and sign into your
   own account. Use the current provider console for tunnel and node management.
2. Create a TCP tunnel to a local **TLS reverse proxy**, not directly to an
   unencrypted public OpenClaw socket. The TLS proxy must use a publicly trusted
   certificate whose hostname matches the device endpoint, support WebSockets,
   and forward to `127.0.0.1:18789` including `/passport/v1`.
3. A provider-assigned hostname/port avoids a custom domain only if that route
   actually provides compatible TLS and a valid certificate. A plain TCP tunnel
   does not automatically add TLS; do not assume `wss://` works just by changing
   the URL prefix. If the free tier cannot provide this, bring a domain and TLS
   proxy or use a TLS-terminating ingress instead.
4. Put the resulting `wss://assigned-host:assigned-port` endpoint in the app and
   leave the assistant proxy disabled when routing is direct. Keep tunnel access
   keys and account files local, never in the release or firmware.

Free-tier quotas, TCP port availability and node quality can change. Compare
DNS, TCP/TLS connection, WebSocket handshake, first audio and long-call loss
across networks before blaming an ISP or changing ports. Do not delete working
tunnels or rotate account credentials as a diagnostic default.

## Pairing and troubleshooting

Once an ingress already exists, generate a limited setup code on the gateway
with `openclaw qr --json --no-ascii --limited --url wss://passport.example`.
Import the returned address/code via the app or its QR/deep link. The gateway
manages device authorization and revocation; never paste `gateway.auth.token`
into a public issue or distribute a shared administrator token in firmware.

For failures, separate Wi-Fi, DNS, time sync, TCP/TLS, WebSocket upgrade, native
pairing, plugin protocol and model readiness. Run the plugin doctor and
`passport.status` on the gateway. Redact hostnames, IPs, tokens, setup codes,
Wi-Fi passwords and user transcripts before sharing logs.
