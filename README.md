# ToughHA: A Procedural Two-Player LAN Cybersecurity Training Game

**Tough Hacker Attack (THA)**

---

## Abstract

ToughHA is a two-player, local-area-network (LAN) cybersecurity training game implemented as a single C++ executable. One participant acts as an attacker (*hacker*); the other acts as a system administrator (*defender*). At match start, the server procedurally generates a synthetic service environment: a C++ service script with embedded logical flaws, a database containing a crown secret, firewall overlay rules, activity logs, cryptographic tokens, and a population of background clients that inject noise into telemetry. The hacker interacts through a Linux-like console and must discover and chain service vulnerabilities via crafted network packets. The defender monitors client activity, inspects server artifacts, applies compensating controls, and attempts to identify and ban the attacker before the match timer expires. Victory conditions are asymmetric: the hacker wins by extracting the crown secret; the defender wins by surviving until timeout, correctly banning the hacker, or indirectly forcing a service collapse through over-aggressive policy. This document presents the system architecture, formal game rules, protocol semantics, defense mechanisms, and a representative match walkthrough.

**Keywords:** cybersecurity education, procedural content generation, red-team/blue-team simulation, LAN game, exploit chaining, service hardening

---

## 1. Introduction

Hands-on cybersecurity training often relies on static capture-the-flag (CTF) puzzles or isolated virtual machines. While effective for skill building, such setups rarely capture the adversarial dynamics of live service defense: incomplete visibility, noisy background traffic, policy trade-offs, and time pressure. ToughHA addresses this gap by embedding a complete attack–defense scenario inside a lightweight, reproducible game loop.

Each match instantiates a unique *world* from a random seed. Vulnerability parameters, tokens, routes, and debug triggers change between sessions, preventing memorization while preserving a stable multi-stage exploit chain. Both roles operate through text consoles connected to a central TCP game server, making the system suitable for classroom LAN deployments, pair exercises, and automated regression via a built-in simulation mode.

The remainder of this paper is organized as follows. Section 2 describes system architecture. Section 3 formalizes world generation. Sections 4–6 define game rules, player interfaces, and the service API. Section 7 covers defense policy. Section 8 discusses background clients and stochastic events. Section 9 provides deployment instructions. Section 10 presents a case study. Appendices list command references and quick-start playbooks.

---

## 2. System Architecture

### 2.1. Components

ToughHA ships as one executable (`ToughHA.exe`) with four runtime modes:

| Mode | Role | Description |
|------|------|-------------|
| `server` | Match host | Generates the world, serves TCP connections, simulates bots, enforces rules |
| `defender` | Blue team | Administrative console with filesystem and telemetry access |
| `hacker` | Red team | Client console for packet injection and local artifact inspection |
| `sim` | Test harness | Automated end-to-end validation over loopback TCP |

Communication uses TCP. The server multiplexes multiple clients; the defender connects as an administrator, while the hacker connects as an ordinary client among procedurally spawned background clients.

### 2.2. Runtime Layout

During a match, the server materializes artifacts under `runtime/server/`:

```text
runtime/server/service/generated_service.cpp
runtime/server/db/main.db
runtime/server/config/firewall.rules
runtime/server/logs/activity.log
```

The hacker receives response files in `runtime/hacker_tmp/`. The defender may buffer live notifications in `runtime/defender_tmp/events.log`. These paths are exposed through in-game virtual paths (`/srv/...` for the defender, `tmp/...` for the hacker).

### 2.3. Packet Model

All hacker-side interactions reduce to *packets* with three components:

```text
mode + endpoint + payload
```

Example:

```text
custom /api/profile user_id=-1&debug=1
```

| Field | Meaning |
|-------|---------|
| `mode` | `standard` (benign client behavior) or `custom` (attacker-crafted) |
| `endpoint` | Service function path (e.g., `/api/profile`) |
| `payload` | `key=value` pairs separated by `&` |

The defender observes resulting activity in logs and client telemetry but receives no explicit label marking which client is the hacker.

---

## 3. Procedural World Generation

At server startup, the `World` generator produces:

- A C++ service script (`generated_service.cpp`) encoding conditional branches that implement the exploit chain.
- A SQLite-style database file (`main.db`) storing the crown secret.
- Firewall/service overlay rules (`firewall.rules`).
- An append-only activity log.
- Randomized cryptographic artifacts: stage-1 key, stage-2 session token, vault password, ROT cipher shift, debug flag names/values, image magic format string, backup route identifier, and vulnerable `user_id` threshold.
- A configurable population of background clients (`--bots N`).

Because generation is seed-driven (`--seed N`), matches are reproducible for debugging and grading while remaining unpredictable in casual play.

Representative constants visible to the defender in the generated service include:

```text
kDebugFlagName, kDebugFlagValue, kBadUserId
kImageMagic, kBackupRoute, kStage1Key
kStage2Token, kVaultPassword, kCipherShift
```

---

## 4. Game Rules and Victory Conditions

### 4.1. Match Parameters

```powershell
ToughHA.exe server [port] [--seed N] [--duration seconds] [--bots N]
```

| Parameter | Default | Effect |
|-----------|---------|--------|
| `port` | `7777` | TCP listen port |
| `--seed` | random | Deterministic world generation |
| `--duration` | unlimited | Defender survival timer (seconds) |
| `--bots` | `12` | Background client count |

### 4.2. Hacker Victory

The hacker wins by completing the exploit chain and invoking:

```text
/core/export password=<final-password>
```

The final password is obtained by decrypting the vault blob returned from `/vault/read`.

### 4.3. Defender Victory

The defender wins if any of the following occur:

1. **Timeout:** the match timer reaches zero before the hacker exports the crown secret.
2. **Correct ban:** the defender bans the hacker by client ID (`ban <id>`) or hardware ID (`banhw <HWID>`).
3. **Indirect failure (hacker):** the hacker disconnects or is banned (if correctly identified).

### 4.4. Defender Defeat (Service Collapse)

Defensive actions carry a *reputation cost*. Each enabled rule increases service latency and adds complaints. False bans of background clients add substantial complaints. Random instability events may also raise complaint scores. If complaints reach **100**, the service collapses and the defender loses (*reputation failure*).

This models a fundamental blue-team constraint: security controls must be balanced against availability and operator error.

---

## 5. Player Roles and Interfaces

### 5.1. Hacker Console

Prompt:

```text
hacker@toughha:~$
```

The hacker sends packets, inspects downloaded artifacts, decrypts vault data, and optionally runs batch scripts. Custom packets increase suspicion scores visible to the defender.

Core command categories:

| Category | Commands |
|----------|----------|
| Packet injection | `send standard`, `send custom`, `custom`, `std`, `burst` |
| Local files | `ls tmp`, `cat tmp/<file>`, `meta tmp/<file>`, `inbox` |
| Cryptography | `rot <text> <shift>` |
| Automation | `batch <file>` |
| Notifications | `events [n]`, `events clear`, `watch on\|off` |
| Session | `help`, `quit` |

### 5.2. Defender Console

Prompt:

```text
defender@toughha:/srv$
```

The defender reads server files, monitors clients, tailors firewall rules, rotates vault cipher, and bans suspects.

Core command categories:

| Category | Commands |
|----------|----------|
| Situation awareness | `status`, `clients`, `logs [n]`, `inspect <id>`, `events [n]` |
| Enforcement | `ban <id>`, `banhw <HWID>` |
| Policy | `rules`, `rule add <rule>`, `rule del <rule>`, `cipher <0-25>` |
| Filesystem | `ls /srv/...`, `cat /srv/...`, `tail service` |
| Automation | `batch <file>`, `watch on\|off` |
| Session | `help`, `quit` |

### 5.3. Information Asymmetry

The UI deliberately omits labels such as `BOT`, `HACK`, or `fake/tcp`. All clients appear in neutral telemetry tables. The defender must infer the hacker through behavioral signals: endpoint sequences, custom-packet ratio, error rates, suspicion scores, and exploit-chain progress (`PROG` column). Background clients may mimic partial attacker behavior, making premature bans risky.

---

## 6. Service API and Exploit Chain

The generated service exposes a public HTTP-like API. The canonical multi-stage attack proceeds as follows.

### 6.1. Stage 0 — Reconnaissance

```powershell
send standard /api/help
send standard /api/ping client=hacker
send standard /api/profile user_id=5
```

`/api/help` lists public endpoints. `/api/ping` verifies connectivity. Normal profile requests establish baseline behavior.

### 6.2. Stage 1 — Debug Profile Leak

The service may expose a hidden debug branch triggered by a specific negative `user_id` and a seed-dependent debug flag (e.g., `debug=1`, `trace=true`, `diag=dump`).

```powershell
custom /api/profile user_id=-1&debug=1
```

On success, the server returns a dump file hinting at the image endpoint and required parameters. The defender can read exact trigger values in `generated_service.cpp`.

**Defensive countermeasure:** `type_guard` blocks dangerous profile/debug combinations.

### 6.3. Stage 2 — Image Metadata Leak

```powershell
custom /cdn/image asset=avatar&format=raw_frame&width=0
```

A specially crafted `format` and `width=0` may return a `.thaimg` file whose metadata embeds the stage-1 key:

```text
EXIF-Comment=stage1=K1-...
EXIF-Auth=/auth/check
EXIF-Bug=token prefix accepts overflow suffix
```

Inspect via:

```powershell
ls tmp
meta tmp/avatar_raw_....thaimg
```

**Defensive countermeasure:** `media_sanitizer` strips exploitable metadata from image responses.

### 6.4. Stage 3 — Authentication Prefix Bypass

The `/auth/check` endpoint may compare only a prefix of the token. The attacker appends an overflow suffix:

```powershell
custom /auth/check token=K1-ABCDEF::overflow
```

(Use the first eight characters of the stage-1 key plus a suffix such as `::overflow`.)

Success yields:

```text
session=S2-...
route=backup_...
next=/vault/read
```

**Defensive countermeasure:** `auth_fullmatch` requires exact token matching.

### 6.5. Stage 4 — Vault Read and Decryption

```powershell
custom /vault/read session=S2-...&route=backup_...
```

Response example:

```text
cipher=rot9
data=AXXC-...
hint=decrypt with negative shift
```

Decrypt with the in-console ROT utility:

```powershell
rot AXXC-... -9
```

 yielding `ROOT-...`.

The defender may rotate cipher mid-match:

```powershell
cipher 9
```

This regenerates the service script and invalidates previously captured blobs at the cost of added complaints.

**Defensive countermeasure:** `backup_acl` denies vault reads via the backup route.

### 6.6. Stage 5 — Crown Export (Terminal Objective)

```powershell
custom /core/export password=ROOT-...
```

Correct password returns crown data and terminates the match:

```text
flag=TOUGHHA{ROOT-...}
status=hacker_win
```

### 6.7. Endpoint Reference

| Endpoint | Parameters | Purpose |
|----------|------------|---------|
| `/api/help` | — | Public API catalog |
| `/api/ping` | `client=<name>` | Liveness check |
| `/api/profile` | `user_id=<int>` | User profile; debug branch |
| `/cdn/image` | `asset`, `format`, `width` | Media delivery; metadata side channel |
| `/auth/check` | `token=<string>` | Token validation; prefix flaw |
| `/vault/read` | `session`, `route` | Encrypted secret blob |
| `/core/export` | `password=<plain>` | Final exfiltration gate |

Error responses (`ERR ...`, `DENY ...`) are informative: they reveal type constraints, missing parameters, and active defensive rules.

---

## 7. Defense Mechanisms

The defender enables overlay rules at runtime:

```powershell
rule add type_guard
rule add media_sanitizer
rule add auth_fullmatch
rule add backup_acl
```

| Rule | Effect |
|------|--------|
| `type_guard` | Blocks dangerous profile requests with debug flags or invalid `user_id` |
| `media_sanitizer` | Removes exploitable metadata from image responses |
| `auth_fullmatch` | Closes prefix-comparison flaw on `/auth/check` |
| `backup_acl` | Blocks vault access via backup route |

Each active rule increases latency and contributes to the complaint score. Disabling a rule (`rule del <rule>`) restores performance but reopens the corresponding attack surface.

The `status` command exposes critical state:

```text
time_left=542s
complaints=12/100
rules_on=2
cipher_shift=9
visible_clients=13
background_traffic=enabled
game_over=no
```

Recommended defensive strategy:

1. Read `generated_service.cpp` immediately to learn seed-specific parameters.
2. Monitor `clients` and `logs`; correlate endpoint sequences with rising `PROG`.
3. Apply *targeted* rules aligned to observed attacker progress—not blanket hardening.
4. Ban only after `inspect`; false bans accelerate complaint accumulation.
5. Rotate `cipher` if the attacker likely holds an encrypted blob.

---

## 8. Background Clients and Stochastic Events

### 8.1. Background Clients

The server spawns `--bots N` synthetic clients that:

- Issue routine standard requests;
- Occasionally emit errors;
- Sometimes send custom packets and bursts;
- Possess unique IDs, IPs, and hardware IDs;
- May appear superficially suspicious.

Banning a background client counts as a *false ban*, sharply increasing complaints. The interface never reveals client type; failed bans are indistinguishable from successful ones except through complaint growth.

Live bot notifications are buffered by default in `runtime/defender_tmp/events.log` and viewed with `events 20`. Legacy inline streaming remains available via `watch on`.

### 8.2. Random Events

During a match, the server may inject:

| Event | Effect |
|-------|--------|
| Network jitter | Temporary latency increase |
| Routing storm | Latency spike; complaints +4 |
| Cache rollback | Partial attacker progress loss |
| Instability | Additional complaint pressure |

These events reduce linear predictability and simulate operational noise.

---

## 9. Deployment

### 9.1. Standard LAN Session

On the host machine:

```powershell
ToughHA.exe server 7777 --duration 600 --bots 12
```

Defender workstation:

```powershell
ToughHA.exe defender 192.168.1.10 7777
```

Hacker workstation:

```powershell
ToughHA.exe hacker 192.168.1.10 7777
```

Replace `192.168.1.10` with the server host's LAN address. Ensure the host firewall permits inbound TCP on the chosen port.

### 9.2. Split Host: Server + Defender on One PC

```powershell
# Machine A — window 1
ToughHA.exe server 7777

# Machine A — window 2
ToughHA.exe defender 127.0.0.1 7777

# Machine B
ToughHA.exe hacker <Machine-A-LAN-IP> 7777
```

The defender uses loopback; the hacker connects over LAN.

### 9.3. Local Training (Single Machine)

```powershell
ToughHA.exe server 7777 --duration 600 --bots 8
ToughHA.exe defender 127.0.0.1 7777
ToughHA.exe hacker 127.0.0.1 7777
```

### 9.4. Automated Validation

```powershell
ToughHA.exe sim
```

The simulation mode starts a local server, connects defender and hacker over TCP, and verifies file I/O, rules, cipher rotation, background traffic, burst packets, temporary files, the full exploit chain, and match termination.

---

## 10. Case Study: Representative Hacker Victory

The following walkthrough illustrates a successful attacker path. All secrets are seed-dependent; values differ in live play.

**Setup:**

```powershell
ToughHA.exe server 7777 --duration 600 --bots 12
ToughHA.exe defender 127.0.0.1 7777
ToughHA.exe hacker 127.0.0.1 7777
```

**Defender reconnaissance:** `status`, `clients`, `logs 10`, then `cat /srv/service/generated_service.cpp`. Defender enables `rule add media_sanitizer` cautiously.

**Hacker reconnaissance:**

```powershell
send standard /api/help
send standard /api/profile user_id=5
custom /api/profile user_id=-1&debug=1
ls tmp
cat tmp/profile_dump_....txt
```

**Metadata extraction:**

```powershell
custom /cdn/image asset=avatar&format=raw_frame&width=0
meta tmp/avatar_raw_....thaimg
```

**Defender observation:** elevated `CUS`, `SUS`, and `PROG` on one client; `logs 30` shows `image metadata leak`. Premature `ban <id>` risks false-ban complaints.

**Auth bypass:**

```powershell
custom /auth/check token=K1-ABCDEF::overflow
cat tmp/auth_session_....tmp
```

**Vault exfiltration and decryption:**

```powershell
custom /vault/read session=S2-...&route=backup_...
rot AXXC-... -9
```

**Terminal action:**

```powershell
custom /core/export password=ROOT-...
```

**Counterfactual defender responses** that would have stopped this chain:

- `rule add media_sanitizer` before stage 2;
- `rule add auth_fullmatch` before stage 3;
- `rule add backup_acl` before stage 4;
- Correct `ban <id>` after `inspect` confirmed exploit-chain progress;
- `cipher <n>` after vault blob capture.

---

## 11. Conclusion

ToughHA combines procedural vulnerability generation, asymmetric information, policy trade-offs, and noisy background traffic into a self-contained LAN training game. Its single-binary deployment, seed-based reproducibility, and built-in simulation mode make it suitable for instructional labs, competitive pair play, and regression testing. Future extensions may include additional vulnerability classes, team modes, and formal scoring metrics for classroom assessment.

---

## Appendix A — Hacker Command Reference

```powershell
help
send standard <endpoint> [k=v ...]
send custom <endpoint> [k=v ...]
custom <endpoint> [k=v&x=y ...]
std <endpoint> [k=v ...]
burst <count> <standard|custom> <endpoint> [payload]
ls tmp
cat tmp/<file>
meta tmp/<file>
inbox
events [n]
events clear
watch on|off
rot <text> <shift>
batch <file>
quit
```

**Payload syntax:** space-separated `key=value` pairs in `send standard`, or `&`-joined pairs in `custom`. Example equivalence:

```powershell
send standard /cdn/image asset=logo format=jpg width=128
# internally: asset=logo&format=jpg&width=128
```

**Batch files** support `sleep <ms>` delays and `#` / `rem` comments.

**Quick cheat sheet:**

```powershell
send standard /api/help
send standard /api/ping client=test
send standard /api/profile user_id=5
custom /api/profile user_id=-1&debug=1
custom /cdn/image asset=avatar&format=raw_frame&width=0
ls tmp
meta tmp/<file>
cat tmp/<file>
custom /auth/check token=<stage1-prefix>::overflow
custom /vault/read session=<session>&route=<route>
rot <encrypted-data> -<cipher-number>
custom /core/export password=<decrypted-password>
```

---

## Appendix B — Defender Command Reference

```powershell
help
status
clients
logs [n]
inspect <id>
ban <id>
banhw <HWID>
rules
rule add type_guard|media_sanitizer|auth_fullmatch|backup_acl
rule del <rule>
cipher <0-25>
ls /srv[/...]
cat /srv/<path>
tail service
events [n]
watch on|off
batch <file>
quit
```

**Client telemetry columns:**

| Column | Meaning |
|--------|---------|
| `ID` | Actor identifier |
| `NET` | Neutral network token |
| `HWID` | Neutral hardware token |
| `REQ` | Total requests |
| `ERR` | Error count |
| `CUS` | Custom-packet count |
| `SUS` | Suspicion score |
| `PROG` | Exploit-chain progress |
| `LAST` | Seconds since last activity |

**Quick cheat sheet:**

```powershell
status
cat /srv/service/generated_service.cpp
clients
logs 30
inspect <id>
rule add media_sanitizer
rule add auth_fullmatch
cipher 11
ban <id>
```

---

## Appendix C — Hacker Tutorial Path

1. Verify connectivity: `send standard /api/ping client=hacker`
2. Enumerate API: `send standard /api/help`
3. Baseline profile: `send standard /api/profile user_id=5`
4. Fuzz profile debug branch with varied `user_id` and flag names
5. Read dumps: `ls tmp`, `cat tmp/<file>`
6. Follow hints to `/cdn/image`; extract `stage1=` via `meta`
7. Exploit `/auth/check` prefix flaw
8. Read vault: `/vault/read` with session and route
9. Decrypt: `rot <data> -<cipher>`
10. Export: `custom /core/export password=<plain>`

---

## Appendix D — Defender Tutorial Path

1. Assess match state: `status`
2. Static analysis: `cat /srv/service/generated_service.cpp`
3. Monitor: `clients`, `logs 20`
4. Investigate high-`PROG` clients: `inspect <id>`
5. Ban confirmed attacker: `ban <id>`
6. Otherwise, apply stage-aligned rules and consider `cipher` rotation
7. Balance security against complaints—avoid indiscriminate rule activation and false bans

---

## License

MIT License. Copyright (c) 2026 empty?

See [LICENSE](LICENSE) for full terms.
