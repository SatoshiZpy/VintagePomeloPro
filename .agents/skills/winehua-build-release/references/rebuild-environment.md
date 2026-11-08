# Environment Rebuild (fresh WSL / Docker Desktop)

Recovery procedure used on 2026-08-09 after the WSL Ubuntu distro was reinstalled
(old `/home/<user>/src/*` worktrees, signing config, and WSL-side git state were
lost; Docker Desktop data and `winehua-dev` survived). Run every step read-only
first, then mutate only what is missing.

## 1. Verify what survived

```powershell
wsl.exe -l -v
& 'C:\Program Files\Docker\Docker\resources\bin\docker.exe' image ls
Test-Path 'F:\command-line-tools\bin\hvigorw'
git ls-remote https://github.com/SatoshiZpy/VintagePomeloPro.git HEAD
```

`winehua-dev` (do not rebuild), `F:\command-line-tools`, and the private repo on
GitHub are the sources of truth. Old WSL worktrees are gone.

## 2. Enable Docker Desktop WSL integration

The Ubuntu distro had no `docker` CLI and no socket until integration was enabled:

```powershell
$s = "$env:APPDATA\Docker\settings.json"
$j = Get-Content -Raw $s | ConvertFrom-Json
$j.integratedWslDistros = @('Ubuntu-22.04')
$j | ConvertTo-Json -Depth 12 | Set-Content $s -Encoding UTF8
Stop-Process -Name 'Docker Desktop','com.docker.backend' -Force
Start-Process 'C:\Program Files\Docker\Docker\Docker Desktop.exe'
```

Verify inside WSL: `docker ps` works as the normal user and
`docker image inspect winehua-dev` succeeds.

## 3. Restore git credentials (private repos)

The repo is private; anonymous git hangs or reports "Repository not found". The
Windows credential manager caches a working token for `yifengling0`. Copy it into
WSL without printing it:

```powershell
$env:GIT_TERMINAL_PROMPT='0'; $env:GCM_INTERACTIVE='never'
$out = "protocol=https`nhost=github.com`n`n" | git credential fill
$user = ($out | Select-String '^username=').ToString() -replace '^username=',''
$pass = ($out | Select-String '^password=').ToString() -replace '^password=',''
$line = "https://${user}:${pass}@github.com`n"   # backtick-n, not \n
[System.IO.File]::WriteAllText('\\wsl.localhost\Ubuntu-22.04\home\liufeng\.git-credentials', $line, [Text.UTF8Encoding]::new($false))
```

```bash
# in WSL
chmod 600 ~/.git-credentials
git config --global credential.helper store
git config --global http.lowSpeedLimit 1
git config --global http.lowSpeedTime 999
```

`\n` written literally (PowerShell) breaks the file; it must end with a real LF.

## 4. Proxy

The host proxy (veilflux, `0.0.0.0:8080`) is reachable from WSL via the WSL2
gateway, not 127.0.0.1:

```bash
GW=$(ip route | sed -n 's/^default via //p' | cut -d' ' -f1)   # e.g. 172.17.80.1
git config --global http.https://github.com/.proxy "http://$GW:8080"
```

GitHub fetches (clone + all submodules) need proxy + credentials together.
Without credentials even `git ls-remote` hangs.

## 5. Clone the private repo

```bash
cd ~/src
git clone https://github.com/SatoshiZpy/VintagePomeloPro.git VintagePomeloPro
cd VintagePomeloPro
git checkout -b private/wine-engine-app
git fetch --unshallow   # optional; full history useful for signing docs/history
```

## 6. Submodules

`git submodule update --init --recursive` fails on the three SSH URLs
(`git@github.com:winehua/{wine,box64,mesa-ohos}.git`) because `insteadOf`
rewriting is not inherited by the submodule clone process. Override in local
config (do not edit `.gitmodules`):

```bash
git config submodule.thirdparty/wine.url https://github.com/winehua/wine.git
git config submodule.thirdparty/box64.url https://github.com/winehua/box64.git
git config submodule.thirdparty/mesa.url https://github.com/winehua/mesa-ohos.git
git submodule update --init --recursive
```

Notes:
- A background update may be killed mid-checkout and leave a submodule with its
  index staged as deleted (all files missing, `git status` shows `D `). Fix:
  `git submodule update --init --recursive --force`.
- If one clone keeps failing ("submodule--helper died of signal 1"), clone it
  manually and check out the pinned SHA:
  `git clone https://github.com/winehua/box64.git thirdparty/box64 &&
   git -C thirdparty/box64 checkout <pinned-sha>`.
- Verify all pins: `git submodule status` must show every entry with a leading
  space (matches `git ls-tree HEAD thirdparty/`).

## 7. Wine Mono MSI (image has no curl/wget)

`build_deps.sh` prints `无 curl/wget, 跳过 Wine Mono`; then `assemble` fails with
`appwiz.cpl packaged but wine-mono MSI missing`. The MSI is kept on Windows at
`F:\PomeloWin\wine-mono-11.1.0-x86.msi`. The build tree is root-owned, so stage
as root into the container (not via user `cp`):

```bash
docker cp /mnt/f/PomeloWin/wine-mono-11.1.0-x86.msi \
  vp-build:/data/src/winehua/build/wine-ohos/share/wine/mono/
```

Then rerun `make hap`; stamps for completed stages are kept, only `assemble` and
`hap` rerun.

## 8. First build

```bash
cd ~/src/VintagePomeloPro
bash scripts/vpbuild.sh make hap          # ARM64 debug; full first build ~1.5-2.5h
```

Monitor with `tail -f` and look for the `===` stage markers. Benign configure
noise (do not treat as failures):
- `clang-15: error: unsupported option '-print-multi-os-directory'` (libffi probe)
- `-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Failed` (libxml2 probe)
- `wine: could not open working directory L"unix\..."` on device (Wine workers)

Stage order: `deps -> wine -> box64 -> native -> assemble -> hap`. Successful
endpoint: `entry/build/default/outputs/default/entry-default-signed.hap`.

Formal APP afterwards:

```bash
bash scripts/vpbuild.sh bash /apps/harmony/bin/hvigorw assembleApp \
  -m project -p product=proRelease -p buildMode=release
```

## 9. Verify artifacts

```bash
sha256sum entry/build/default/outputs/default/entry-default-signed.hap
docker exec vp-build unzip -Z1 <hap> | grep -E 'libs/arm64-v8a/(box64.so|libentry.so|libwine_child.so|libwinehua_vtest_server.so|libvirglrenderer.so.1|libvirgl_child.so)|resources/rawfile/wine-data.zip'
docker exec vp-build bash -c 'cd /tmp && unzip -q <hap> resources/rawfile/wine-data.zip && unzip -Z1 resources/rawfile/wine-data.zip | grep -E "bin/guest_gfx/(winehua-guest-gfx.env|lib/libEGL.so|lib/dri/virtio_gpu_dri.so)|bin/x86_64-windows/(winehua_graphics_smoke|winehua_audio_smoke).exe"'
docker exec vp-build java -jar /apps/harmony/sdk/default/openharmony/toolchains/lib/hap-sign-tool.jar verify-app -inFile <hap-or-app> -outCertChain /tmp/o.cer -outProfile /tmp/o.p7b -inForm zip
```

## Pitfalls

- PowerShell -> `wsl ... bash -lc '...'` mangles `$` and nested quotes. Write
  bash scripts to a file (`\\wsl.localhost\...` or apply_patch) and execute them
  instead of inline one-liners.
- `pkill -f "git.*clone"` also matches the invoking shell's own command line and
  kills it. Use exact process names (`pkill -x git-remote-https`) or PIDs.
- Inside `docker exec`, a relative HAP path breaks after `cd /tmp`; use
  `/data/src/winehua/...` absolute paths.
- Build-dir files are root-owned. Do not `chown -R` the whole tree; stage
  missing files via `docker cp`.
- If the repo path changed, `vpbuild.sh` detects it and recreates `vp-build`
  (the old container from the previous WSL survives in Docker Desktop storage).
