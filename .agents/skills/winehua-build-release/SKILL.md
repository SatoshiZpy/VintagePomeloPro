---
name: winehua-build-release
description: Build, test, sign, inspect, install, diagnose, and privately publish the WineHua/Old Pomelo Pro HarmonyOS project. Use for local quick checks, persistent Docker app-only builds, full native builds, API 23 HAP or APP packaging, x86_64 emulator and ARM64 tablet variants, Box64/Wine/Wayland/VirGL/guest-gfx changes, signing-profile work, HDC deployment, ARM-only Wine startup failures, submodule updates, isolated private-branch work, or publishing VintagePomeloPro without pushing private code to WineHua origin. Also use after a WSL or Docker Desktop reinstall (or on a fresh machine) to rebuild the compile environment (Docker WSL integration, git credentials and proxy, private repo/submodule restore, Wine Mono staging, and signing-config recovery).
---

# WineHua Build, Test, And Release

Treat this file as the portable source of truth for the WineHua private product build. Derive machine paths and the current version from the checkout; do not copy paths, versions, device IDs, credentials, or artifact hashes from an older machine.

## Default Development Route: Local Checks And App-Only HAP

For ordinary ArkTS/application changes, prefer the configured local route over a CI
run or a full Wine rebuild. Find the worktree with `scripts/local_hap.py` and
`.hvigor/local-build/config.json` using `git worktree list`; the main checkout may
not contain the local cache. Keep the main checkout on `main_proton_steamarm64`
and edit code in a dedicated worktree.

From that worktree:

```bash
node scripts/run_local_tests.cjs              # fast checks, no HAP/device/CI
python3 scripts/local_hap.py doctor           # inspect the configured Linux environment
python3 scripts/local_hap.py build            # actual local app-only HAP compilation
```

The build driver invokes `ci/Makefile.local-app` inside a pinned Linux Docker
image with persistent Linux source/dependency/Hvigor caches. It includes eligible
uncommitted ArkTS changes. The initial CI image includes its own Linux SDK; do not
mount the macOS SDK as a substitute. The validated configuration uses an API 24
toolchain with both minimum and target API 23; check artifact metadata rather than
assuming the installed toolchain API is the package target.

Output is `.hvigor/outputs/local-hap/<run>/entry-default-unsigned.hap` plus
`report.json`. Inspect the report's source snapshot, runtime commit, API/ABI,
SHA-256 and immutable-payload verification. This is an unsigned candidate: use the
existing development signing flow if an installable package is requested, and
report device acceptance separately. If the user owns interactive verification,
leave device operation to them.

Runtime reuse is conditional on the driver's checks. Native code, gitlinks,
runtime resources, SDK/ABI/product inputs or an incompatible baseline require a
corresponding full build and a new verified base. Do not loosen the guard, change
receipts, or describe the reused runtime as newly compiled. Use the full-build
sections below when reuse is rejected or native work is requested; they are not
mandatory steps for every app-only change.

For cache locations, first-time image/base import and measured timings, read
[Local Docker builds](../../../docs/linux-steam/LOCAL_DOCKER_BUILD.zh-CN.md).
For host-only check scope, read
[Local validation](../../../docs/linux-steam/LOCAL_VALIDATION.zh-CN.md).
Reuse existing configuration and volumes. Missing `winehua-dev` alone does not
mean this configured CI-image route is unavailable.

## Non-Negotiable Policy

1. Build HAP/native payloads through repository Makefile targets inside Linux Docker. Use `scripts/local_hap.py` / `ci/Makefile.local-app` for eligible app-only builds; use the root `Makefile` and the configured native build environment for full builds. Host-only diagnostic tests do not require Docker.
2. On Windows, use WSL only to invoke Docker. Do not compile Wine, Box64, Mesa, VirGL, native libraries, or HAPs directly in WSL.
3. Keep source and build output on a Linux filesystem. Do not perform heavy builds from `/mnt/c`, `/mnt/d`, `/mnt/f`, or another Windows-mounted tree.
4. Use API 23 as the current minimum and default: `TARGET_SDK_VERSION=6.1.0(23)` and `COMPATIBLE_SDK_VERSION=6.1.0(23)`. Never silently lower either value. Raise them only after verifying the SDK and target device.
5. Keep `BUILD_GUEST_GFX=1`. A package without the nested guest Mesa/VirGL receiver is incomplete even if the native compositor libraries exist.
6. Keep `GUEST_ARCH=x86_64` for both device ABIs. Wine and guest graphics are x86_64; ARM64 devices execute them through Box64.
7. Use debug signing for development HAP installation. Use the formal `proRelease` profile only for an explicitly requested release or formal-signature test.
8. Never expose, print, commit, archive, or paste signing passwords, private keys, profiles, tokens, device identifiers, or certificate contents.
9. Never reset, clean, switch, merge, rebase, or commit another worktree or branch as a side effect. Preserve unrelated changes.
10. Never push private changes to the WineHua `origin`. Publish only when explicitly requested, through the explicit VintagePomeloPro URL, without `--force` and without setting an upstream.

## Product Invariants

Read current values from source before every release. Keep these stable unless the user explicitly changes product identity:

| Item | Source of truth | Required value |
| --- | --- | --- |
| Bundle | `AppScope/app.json5` | `com.vintage.pomelopro` |
| Display name | localized `app_name` and `EntryAbility_label` resources | `旧柚Pro` |
| Version | `AppScope/app.json5` | Increment `versionName` and `versionCode` together for a release |
| Target/compatible SDK | Makefile plus ignored root `build-profile.json5` | `6.1.0(23)` unless deliberately raised |
| App library | `AppCatalogService.ets` and `EntryAbility.ets` | `Download/com.vintage.pomelopro/games` |
| Private branch | local worktree | `private/wine-engine-app` |
| Private destination | explicit URL | `https://github.com/yifengling0/VintagePomeloPro`, branch `main` |

Verify that cold start creates the fixed `games` directory without a directory picker. Verify that Wine maps `Z:` to `Download/com.vintage.pomelopro`, so `Z:\games` is the scanned application directory.

## Resolve The Environment

Locate the checkout instead of assuming a username or drive:

```bash
git rev-parse --show-toplevel
git branch --show-current
git status --short --branch
git worktree list
git remote -v
```

Define these conceptual values for the current machine:

| Variable | Meaning |
| --- | --- |
| `REPO` | Private worktree on a Linux filesystem |
| `TOOLS` | Linux HarmonyOS command-line-tools directory mounted read-only |
| `IMAGE` | Existing Docker image, normally `winehua-dev` |
| `ARCH` | `x86_64` for emulator or `arm64-v8a` for a physical tablet |
| `HDC` | Host-accessible API 23+ `hdc` executable |
| `TARGET` | One explicitly selected connected HDC target |

On Windows, express `REPO` and `TOOLS` as WSL paths when passing them to Docker. A normal layout is a repository below `/home/<user>/src` and tools below `/mnt/<drive>/command-line-tools`; these are examples, not constants.

On Linux or macOS, invoke the same Linux Docker build with host-absolute bind paths. Do not fall back to a native host build when Docker or the Linux Harmony tools are unavailable. Report the missing prerequisite instead.

### Current Machine Layout (2026-08, after WSL reinstall)

| Value | Location |
| --- | --- |
| `REPO` | `/home/liufeng/src/VintagePomeloPro` (WSL Ubuntu-22.04, user `liufeng`), branch `private/wine-engine-app` |
| Windows view | `\\wsl.localhost\Ubuntu-22.04\home\liufeng\src\VintagePomeloPro` |
| `TOOLS` | `/mnt/f/command-line-tools` (read-only mount at `/apps/harmony`) |
| `IMAGE` | `winehua-dev` (never rebuild) |
| Proxy | host proxy listens on `0.0.0.0:8080`; from WSL use `http://172.17.80.1:8080` (WSL gateway IP, verify with `ip route`) |
| Signing source | `F:\PomeloWin\signs` and `F:\MyProject\vintage-pomelo\docs\签名` |
| Artifacts | `F:\PomeloWin\artifacts\VintagePomeloPro-<version>-<date>[-rebuild]` |

Before building, verify the existing environment:

```bash
docker image inspect winehua-dev
test -f "$REPO/Makefile"
test -x "$TOOLS/bin/hvigorw"
test -d "$TOOLS/sdk/default/openharmony/native"
git -C "$REPO" submodule status --recursive
```

Do not silently rebuild or replace `winehua-dev`. If the image is absent, inspect the project `Dockerfile` and ask for the expected image/import process.

## Rebuild The Environment (fresh WSL / Docker Desktop)

After a WSL or Docker Desktop reinstall, restore in this order before compiling. Full commands and pitfalls: [REBUILD-ENVIRONMENT.md](references/rebuild-environment.md); signing recovery: [SIGNING-RECOVERY.md](references/signing-recovery.md).

1. **Docker access**: enable WSL integration for the Ubuntu distro (`%APPDATA%\Docker\settings.json` -> `integratedWslDistros`, restart Docker Desktop), then verify `docker` works inside WSL and `docker image inspect winehua-dev` succeeds. Never rebuild the image.
2. **Git credentials**: retrieve the cached GitHub token from the Windows credential manager (`git credential fill` for `github.com`, do not print it), write `~/.git-credentials` (chmod 600), enable `credential.helper store`, and set `http.lowSpeedLimit 1` / `http.lowSpeedTime 999`.
3. **Proxy**: point git at the host proxy for github.com (`http.https://github.com/.proxy`). Private GitHub fetches require both proxy and credentials; without credentials the clone hangs or reports "Repository not found".
4. **Clone**: `git clone https://github.com/yifengling0/VintagePomeloPro.git`, create local branch `private/wine-engine-app` from `main`, and confirm `AppScope/app.json5` bundle/version before building.
5. **Submodules**: the three `git@github.com:winehua/...` URLs fail in `git submodule update` because `insteadOf` URL rewriting is not inherited by the submodule clone process. Override them in local config first:

   ```bash
   git config submodule.thirdparty/wine.url https://github.com/winehua/wine.git
   git config submodule.thirdparty/box64.url https://github.com/winehua/box64.git
   git config submodule.thirdparty/mesa.url https://github.com/winehua/mesa-ohos.git
   git submodule update --init --recursive
   ```

   Repair a wiped submodule working tree with `git submodule update --init --recursive --force`; if one clone keeps failing, clone it manually and check out the pinned SHA.
6. **Wine Mono**: the image has no curl/wget, so the mono download is skipped and `assemble` aborts with `appwiz.cpl packaged but wine-mono MSI missing`. Stage the MSI as root before the build (build dir is root-owned):

   ```bash
   docker cp /mnt/f/PomeloWin/wine-mono-11.1.0-x86.msi vp-build:/data/src/winehua/build/wine-ohos/share/wine/mono/
   ```

7. **Signing config**: restore `build-profile.json5` + `signs/` before `make hap` / `assembleApp` (see signing recovery reference).
8. **Build**: `bash scripts/vpbuild.sh make hap`, then formal APP with `assembleApp -p product=proRelease -p buildMode=release`.

## Protect The Repository

Use the dedicated private worktree. On the established machine it is normally `WineHua-private`; the original WineHua worktree remains on its existing branch.

Before any edit or sync, record:

```bash
git -C "$REPO" status --short --branch
git -C "$REPO" log -1 --oneline
git -C "$REPO" branch -vv
git -C "$REPO" worktree list
git -C "$REPO" remote -v
git -C "$REPO" submodule status --recursive
```

Require the current branch to be `private/wine-engine-app` for private development. Do not move `master`, `VintagePomeloMaster`, `dev`, `release/pomelo-pro`, or another branch pointer.

The repository's worktrees share Git remote configuration. Do not add, delete, rename, or rewrite remotes just to publish the private branch. Keep the existing WineHua `origin` unchanged.

Treat every submodule as an independent repository:

```bash
git -C "$REPO" submodule foreach --recursive 'git status --short'
git -C "$REPO" diff --submodule=log
```

Do not commit a gitlink when its submodule is dirty. Keep all 13 current `.gitmodules` URLs unchanged unless a separately approved feature truly requires a fork. Do not vendor submodule source into the main repository.

## Selective Upstream Sync

Fetch WineHua upstream without checking out or moving its local branches:

```bash
git -C "$REPO" fetch origin
git -C "$REPO" log --oneline --decorate HEAD..origin/master
git -C "$REPO" show --stat --submodule=log <candidate-sha>
```

Cherry-pick only reviewed commits related to Wine/Box64 compatibility, Wayland/window management, graphics, audio, input, HarmonyOS API adaptation, build, or runtime fixes. Inspect file scope, gitlink movement, and private-architecture conflicts before each pick.

Do not merge all of `origin/master`. Do not import unrelated branding, release configuration, experimental UI, or VintagePomelo Git history. Record each source SHA, reason, excluded content, conflict resolution, and validation result in the private repository's sync record.

## Understand The Architecture Split

Do not equate the HAP ABI with the Wine guest ABI:

| Build | Harmony native layer | Wine/PE runtime | Translation | Guest graphics |
| --- | --- | --- | --- | --- |
| x86_64 emulator | x86_64 | x86_64 | No Box64 | x86_64 Mesa/VirGL receiver |
| ARM64 tablet | arm64-v8a | x86_64 | ARM64 Box64 | x86_64 Mesa/VirGL receiver through Box64 |

For ARM64, require `box64.so` and ARM64 compositor/NAPI libraries in the HAP, while keeping Wine binaries and `bin/guest_gfx` x86_64 inside `wine-data.zip`. Building guest graphics as ARM64 for the ARM HAP is incorrect.

The Makefile pipeline is:

```text
deps -> wine -> box64 (ARM only) -> native -> assemble -> hap
```

Use the smallest target that proves a local change, then finish with `hap` for an installable candidate:

| Changed area | Minimum focused target | Required candidate target |
| --- | --- | --- |
| ArkTS, catalog, settings (eligible inputs) | `node scripts/run_local_tests.cjs`, then local HAP compile | `python3 scripts/local_hap.py build`; sign when installation is requested |
| Resources or other inputs rejected by reuse checks | focused checks plus full HAP compile | `hap` |
| NAPI, compositor, broker, audio, input native code | `native`, then `assemble` | `hap` for each affected ABI |
| Wine submodule or Wine build script | `wine`, then `assemble` | `hap` for each affected ABI |
| Box64 | `box64`, then ARM `assemble` | ARM64 `hap` |
| Mesa, libdrm, VirGL, guest-gfx scripts | `deps`, `native`, `assemble` | ARM64 and x86_64 `hap` when shared behavior changes |
| Signing, product, SDK, ABI filters | `assemble` if payload changed | Debug HAP and/or formal APP as requested |

## Full Native Docker Build

Use this section for full/native builds; eligible app-only changes use the local
route above and do not need a separate host-mounted Linux SDK.

Prefer the repository's persistent-container helper so every build reuses one
container instead of creating a new writable layer per `docker run`:

```bash
cd "$REPO"
bash scripts/vpbuild.sh make hap
```

`vpbuild.sh` creates the `vp-build` container once (repo + read-only tools
bind mounts, API 23 env, `BUILD_GUEST_GFX=1`, `BUILD_WINE_MONO=1`), restarts it
if WSL/Docker Desktop stopped it, and runs every build via `docker exec`. It
rebuilds the container automatically if the repo worktree path changes. To
force a clean container: `docker rm -f vp-build`. When running builds manually
with `docker run`, still use `--rm` and a stable container name, and never leave
stopped build containers behind.

### Windows Host With WSL

Use PowerShell only to launch WSL and Docker. Replace the example paths after discovery:

```powershell
$Distro = 'Ubuntu-22.04'
$RepoWsl = '/home/<user>/src/WineHua-private'
$ToolsWsl = '/mnt/<drive>/command-line-tools'
$Arch = 'arm64-v8a' # use x86_64 for the emulator

wsl.exe -d $Distro -- docker run --rm -i `
  --mount "type=bind,src=$RepoWsl,dst=/data/src/winehua" `
  --mount "type=bind,src=$ToolsWsl,dst=/apps/harmony,readonly" `
  -w /data/src/winehua `
  -e "NATIVE_ARCH=$Arch" `
  -e GUEST_ARCH=x86_64 `
  -e BUILD_GUEST_GFX=1 `
  -e 'TARGET_SDK_VERSION=6.1.0(23)' `
  -e 'COMPATIBLE_SDK_VERSION=6.1.0(23)' `
  -e TOOL_HOME=/apps/harmony `
  -e OHOS_SDK=/apps/harmony/sdk/default/openharmony `
  winehua-dev make "NATIVE_ARCH=$Arch" GUEST_ARCH=x86_64 BUILD_GUEST_GFX=1 hap
```

### Linux-Compatible Docker Host

```bash
REPO=/absolute/path/to/WineHua-private
TOOLS=/absolute/path/to/linux-command-line-tools
ARCH=arm64-v8a # use x86_64 for the emulator

docker run --rm -i \
  --mount "type=bind,src=$REPO,dst=/data/src/winehua" \
  --mount "type=bind,src=$TOOLS,dst=/apps/harmony,readonly" \
  -w /data/src/winehua \
  -e NATIVE_ARCH="$ARCH" \
  -e GUEST_ARCH=x86_64 \
  -e BUILD_GUEST_GFX=1 \
  -e 'TARGET_SDK_VERSION=6.1.0(23)' \
  -e 'COMPATIBLE_SDK_VERSION=6.1.0(23)' \
  -e TOOL_HOME=/apps/harmony \
  -e OHOS_SDK=/apps/harmony/sdk/default/openharmony \
  winehua-dev make NATIVE_ARCH="$ARCH" GUEST_ARCH=x86_64 BUILD_GUEST_GFX=1 hap
```

Do not start a second overlapping Docker/Hvigor build. If a build appears stuck, inspect the running Docker process, last log lines, output timestamps, and `build/.stamps` before retrying.

## Tests Before Packaging

Run the project catalog/model tests inside the same container/tool mount:

```bash
docker run --rm -i \
  --mount "type=bind,src=$REPO,dst=/data/src/winehua" \
  --mount "type=bind,src=$TOOLS,dst=/apps/harmony,readonly" \
  -w /data/src/winehua \
  winehua-dev /apps/harmony/tool/node/bin/node scripts/run_catalog_unit_tests.cjs
```

Treat successful HAP compilation as the ArkTS/ArkUI type and resource gate. Do not claim lint or unit-test coverage for commands that the repository does not define.

For overlay changes on an API 23+ target, run the repository automation from Windows PowerShell with an explicit HAP, HDC, and target:

```powershell
& '<repo>\scripts\verify_overlay_debug.ps1' `
  -HapPath '<debug-signed-hap>' `
  -Target '<connected-target>' `
  -HdcPath '<command-line-tools>\sdk\default\openharmony\toolchains\hdc.exe'
```

Keep generated screenshots, layouts, and logs below ignored `.hvigor/outputs`. Do not commit device evidence.

## Debug HAP

Use `make ... hap` for a development-installable package. Its authoritative output is:

```text
entry/build/default/outputs/default/entry-default-signed.hap
```

The HAP uses the local debug signing configuration and `sign.py`. Do not rename a stale older artifact and report it as the new build. Check the output timestamp, version metadata, ABI, and hash after every run.

Use ARM64 only for a physical ARM tablet and x86_64 only for the x86 emulator. Do not install an x86 package on the ARM target or use an emulator success as proof of ARM startup.

## Formal APP

Build the payload first with the ARM64 Docker `assemble` target and `BUILD_GUEST_GFX=1`. Then invoke the formal Hvigor product in the same Docker environment:

```bash
docker run --rm -i \
  --mount "type=bind,src=$REPO,dst=/data/src/winehua" \
  --mount "type=bind,src=$TOOLS,dst=/apps/harmony,readonly" \
  -w /data/src/winehua \
  -e NATIVE_ARCH=arm64-v8a \
  -e GUEST_ARCH=x86_64 \
  -e BUILD_GUEST_GFX=1 \
  -e 'TARGET_SDK_VERSION=6.1.0(23)' \
  -e 'COMPATIBLE_SDK_VERSION=6.1.0(23)' \
  -e TOOL_HOME=/apps/harmony \
  -e OHOS_SDK=/apps/harmony/sdk/default/openharmony \
  winehua-dev bash /apps/harmony/bin/hvigorw assembleApp \
    -m project -p product=proRelease -p buildMode=release
```

Expected formal outputs are:

```text
build/outputs/proRelease/winehua-proRelease-signed.app
entry/build/proRelease/outputs/default/entry-default-signed.hap
```

The release profile file name must be ASCII: use `signs/pomelo-pro-release.p7b`. A Chinese file name (e.g. `旧柚Pro-正式Release.p7b`) is mangled to `??????` by the Java signing tool inside the container and fails with `11012002 File not exist`.

Keep the formal signing configuration local and ignored:

```text
build-profile.json5
signs/hyperview.cer
signs/hyperview.p12
signs/pomelo-pro-release.p7b
```

Use `proRelease` bound to the `release` signing profile and `SHA256withECDSA`. Never route a formal APP through the debug `sign.py` flow. Never copy signing material into a commit, Docker image, build log, evidence bundle, or skill.

## Artifact Integrity Gate

Reject the candidate unless all applicable checks pass:

1. Confirm the output exists, is non-empty, and is newer than the build start.
2. Confirm bundle `com.vintage.pomelopro`, display name `旧柚Pro`, intended version, API 23, and exactly the intended ABI.
3. Confirm the outer APP contains the signed entry HAP.
4. Confirm an ARM HAP contains at least:

   ```text
   libs/arm64-v8a/box64.so
   libs/arm64-v8a/libentry.so
   libs/arm64-v8a/libwine_child.so
   libs/arm64-v8a/libwinehua_vtest_server.so
   libs/arm64-v8a/libvirglrenderer.so.1
   libs/arm64-v8a/libvirgl_child.so
   resources/rawfile/wine-data.zip
   ```

5. Confirm the nested `wine-data.zip` contains at least:

   ```text
   bin/guest_gfx/winehua-guest-gfx.env
   bin/guest_gfx/lib/libEGL.so
   bin/guest_gfx/lib/dri/virtio_gpu_dri.so
   bin/x86_64-windows/winehua_graphics_smoke.exe
   bin/x86_64-windows/winehua_audio_smoke.exe
   ```

6. Confirm the x86 HAP has only `libs/x86_64` native payload and does not require Box64.
7. Verify APP/HAP signatures with the SDK `hap-sign-tool.jar`; do not infer signature success from Hvigor's exit code alone.
8. Record absolute path, size, SHA-256, version, bundle, API, ABI, signing profile type, and guest-gfx result.

Use archive listing tools structurally. A typical Docker/Unix inspection uses `unzip -Z1`; Windows can use `tar -tf`. Extract nested archives only into a new temporary directory and remove only that exact directory after inspection.

For formal verification, use the current SDK tool rather than a copied jar:

```bash
java -jar "$TOOLS/sdk/default/openharmony/toolchains/lib/hap-sign-tool.jar" verify-app \
  -inFile <signed-app-or-hap> -outFile <verification-output>
```

Check the installed tool's help if its CLI differs. Never include certificate fingerprints or profile contents in the final report.

## Install And Start

Discover targets first:

```text
hdc list targets -v
```

Select the single intended `USB Connected` ARM target for tablet testing or the explicit `127.0.0.1:<port>` target for emulator testing. Never silently substitute the emulator for an ARM diagnosis.

Verify boot and API before installation:

```text
hdc -t <target> shell param get bootevent.boot.completed
hdc -t <target> shell param get const.ohos.apiversion
```

Require boot completion and API 23 or higher. Install the validated debug HAP with replacement semantics to preserve application data:

```text
hdc -t <target> install -r <entry-default-signed.hap>
hdc -t <target> shell aa start -b com.vintage.pomelopro -a EntryAbility -m entry
```

Do not uninstall by default. Uninstall only for an explicitly requested clean-install test or an unavoidable signature mismatch, and state that application data may be removed.

`hdc install -r` fails with `install sign info inconsistent (9568332)` when the previously installed build used a different debug certificate; uninstall and clean-install in that case (application data/game library is removed). Key pass markers in `hilog`: `AppCatalog: Download games directory ready: /storage/Users/currentUser/Download/com.vintage.pomelopro/games`; Wine child lines `dlopen box64.so ... calling box64_hmos_main`; guest env `WINEHUA_GUEST_GFX_READY=1`, `WINEHUA_VIRGL_READY=1`, `WINEHUA_VULKAN_PRESENT=1`. Screenshots via `snapshot_display -f /data/local/tmp/x.jpeg` (suffix must be `.jpeg`).

After launch, verify the foreground ability and exercise the real startup path. A complete smoke includes:

1. Cold-start initialization blocker while `wine-data.zip` is extracted or refreshed.
2. Creation of `Download/com.vintage.pomelopro/games` without authorization UI.
3. Wine desktop startup and reopening after the desktop window is closed.
4. Built-in File Explorer opening `Z:` and showing `games`.
5. Built-in command prompt, graphics smoke, and audio smoke.
6. Download application scan and launch.
7. Virtual input visibility, mapping, pointer routing, and forced key/button release.
8. Graphics and audio behavior on the physical ARM tablet; emulator behavior is a separate result.

## Diagnose ARM-Only Wine Startup Failure

Start with package provenance. Confirm that the installed HAP is the newly built ARM64/API 23 artifact and that its hash matches the validated candidate. Then compare x86 and ARM architecture inputs; do not patch ArkTS error text before finding the native failure.

Inspect the runtime chain together:

```text
WineEngineService.ets
wine_env.h / wine_env.cpp
wine_launch.h / wine_launch.cpp
wine_exe.cpp
wine_child.cpp
broker.h / broker.cpp
graphics_broker.cpp
plugin_manager.cpp
```

Check these ARM-specific causes in order:

1. `box64.so` is present, loadable, and selected only on ARM64.
2. The broker/appspawn environment reaches the child through the current serialized `__env__` channel.
3. Native library search paths resolve ARM64 `libentry`, `libwine_child`, VirGL, Wayland, audio, and C++ runtime libraries.
4. Wine runtime extraction completed, required executables exist, and the runtime version marker matches.
5. `HOME` and `Z:` resolve to `Download/com.vintage.pomelopro`; `games` exists.
6. `winehua-guest-gfx.env` is loaded and its library/driver paths point into the extracted x86_64 guest bundle.
7. VirGL/vtest starts before the graphics Wine process and exposes the expected socket/environment.
8. The child PID, session ID, Wayland toplevel, and Ability startup remain associated.

Capture only bounded logs from the physical target:

```text
hdc -t <target> shell hilog -z 4000 -v wrap
```

Filter on the host for `WineEngine`, `Launch`, `broker`, `wine_child`, `box64`, `guest_gfx`, `virgl`, `EGL`, `dlopen`, `failed`, and `error`. Preserve a finite, ignored evidence run under `.hvigor/outputs`; redact target IDs, tokens, signing data, and unrelated application logs.

Do not treat a generic ArkTS timeout as the root cause. Correlate it with the earliest native or extraction error and fix that layer. Rebuild the exact ARM target through Docker, reinstall, cold start, and repeat the real smoke.

## Incremental Rebuild Discipline

The Makefile uses architecture-specific stamps and sentinel files. Let it decide incremental work first. When a change is incorrectly considered up to date:

1. Identify the exact target and its stamp below `build/.stamps`.
2. Confirm the relevant source or script is newer and the expected sentinel/output is missing or stale.
3. Remove only that exact stamp, then rerun the focused target.
4. Use `make clean` only when the user requests a full rebuild or narrower invalidation cannot be made reliable.

Docker-created files may be root-owned. Do not broadly change ownership or delete the entire build tree merely to bypass one stale stamp.

After every build, compare `git status` with the pre-build snapshot. Packaging may update ABI filters or generated configuration. Do not revert pre-existing user changes. Do not commit build outputs, logs, `.hvigor`, device evidence, `.ohos`, ignored `build-profile.json5`, or signing materials.

## Commit And Private Publish

For product changes, validate the affected scope with focused tests, the applicable Docker HAP build and artifact checks; perform device regression when it is part of the authorized task. Documentation/agent-instruction changes need no HAP rebuild. Check-only and unsigned-build requests do not require installation; preserve any user-owned device verification handoff. Split commits by coherent milestones. Do not include unrelated worktree changes or dirty submodule state.

Before first or subsequent private publication:

1. Confirm `gh` is authenticated to the intended account and VintagePomeloPro is private and writable.
2. Use `git ls-remote` to inspect remote `main`. On first publish, require it to be empty or explicitly known. Later, require it to equal the last published SHA before pushing.
3. Check tracked paths for `.ohos`, `signs`, P12, P7B, PEM, KEY, certificate, token, password, and build/evidence artifacts.
4. Run a full-history credential scan such as `gitleaks git --redact`. If no suitable scanner is installed, stop publication rather than claiming the history is safe.
5. Confirm the local branch is `private/wine-engine-app`, its worktree is clean, submodules are clean, and the candidate commit is the tested commit.

Publish with the explicit URL and no upstream:

```bash
git -C "$REPO" push --porcelain \
  https://github.com/yifengling0/VintagePomeloPro.git \
  private/wine-engine-app:refs/heads/main
```

Never use `--force`, `--force-with-lease`, `-u`, or plain `git push` from this worktree. If remote `main` has an unexpected commit, stop and resolve the divergence explicitly.

When a local HTTP proxy is available, scope it to the one command instead of changing global Git configuration:

```bash
git -c http.proxy=http://127.0.0.1:8080 -C "$REPO" fetch origin
git -c http.proxy=http://127.0.0.1:8080 -C "$REPO" push --porcelain <explicit-private-url> private/wine-engine-app:refs/heads/main
```

Do not assume `127.0.0.1` points to the same proxy from every VM, container, WSL distribution, or remote host. Probe it before use and omit it when unavailable.

## Completion Report

Report only verified facts:

- branch and commit tested;
- Docker image identity and architecture;
- API, bundle, version, and signing type;
- unit/build/device checks actually run and their result;
- absolute artifact path, size, and SHA-256;
- guest-gfx and required native payload result;
- selected device class without exposing its unique identifier;
- whether code was committed or pushed, and the exact private destination if pushed;
- any untested hardware path, unavailable external peripheral, or remaining risk.

Distinguish quick checks, verified unsigned HAP compilation, signed installable packaging, and device acceptance. Report only the requested stage actually completed; an unsigned local build does not establish signing or device success.
