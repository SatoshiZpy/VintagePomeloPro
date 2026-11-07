# 本地 Docker 应用层出包

在配置过的修复 worktree 根目录运行：

```sh
python3 scripts/local_hap.py doctor
python3 scripts/local_hap.py build
```

`build` 使用已下载的镜像与原生基包，不推送 Git、不触发 CI，也不连接设备。
默认输出 API 23 / ARM64 的 **未签名 HAP**，路径会打印在终端：
`.hvigor/outputs/local-hap/<时间>/entry-default-unsigned.hap`。
同目录 `report.json` 记录源码提交、工作区是否有修改、源码快照摘要、镜像 ID、
原生运行时提交、HAP SHA-256、打包耗时及原生文件逐字节验证结果。
需要安装时，再使用本机已有开发签名；本入口不会复制签名材料进容器。

## 构建与缓存

- 使用项目 CI 的 `ghcr.io/panedioic/winehua-buildenv:test` Linux x86_64 镜像，
  初始化后固定到本机镜像 ID，避免远端同名标签变化导致环境漂移。
- Apple Silicon 通过 Docker/Colima 的 Rosetta 支持运行该工具链。
- 项目副本位于独立 Docker 数据卷 `/data/src`；不会改写宿主 worktree 的
  `entry/build-profile.json5`、资源或依赖锁。主仓库不参与构建。
- 使用持久容器、Linux 文件系统和保留的 Hvigor/ohpm 缓存。
  相同内容不重写文件，保留时间戳用于增量判断。
- 一次性从成功的全量 CI 构建导入原生基包；每次出包比较全部原生库和 rawfile
  成员，保留原始运行时来源，不把复用的 Wine 宣称为本次重新编译。
- 支持未提交的 ArkTS 编辑、新文件和删除。源码快照反映实际参与构建的内容。
  修改 C++、gitlink、运行时资源、SDK/打包身份等输入时会拒绝复用，要求新全量基包。
- 容器中的 Node 堆限制为 2 GiB，并关闭 Hvigor 并行任务，适应当前 Docker
  2 CPU / 4 GiB 内存配置；不重启或调整其他服务的容器。

本入口面向应用层日常修改。完整 Wine/FEX/Mesa 原生重编译仍需要初始化对应
子模块、工具链及执行全量构建；本地应用层缓存不等于原生编译缓存。
已有的 `node scripts/run_local_tests.cjs` 仍用于更快的代码检查。

## 本机验证（2026-11-07）

- 已导入镜像并固定 ID，Linux SDK 为 API 24 / 6.1.1.125，与现用 CI 一致；
  打包目标和最低 API 均保持 23，输出校验通过。
- 首轮构建（不含镜像下载）：Hvigor 44.3 秒；包含运行时准备、校验和导出共 61.39 秒。
- 无源码变更的增量构建：Hvigor 3.1 秒；包含校验和导出共 11.94 秒。
  有代码变更时，需要重新执行对应编译任务，耗时以实际日志为准。
- 两轮均生成 ARM64 HAP，303 个原生/运行时成员与基包逐字节相同，ZIP32 检查通过。
- 快速检查 21/21 通过；CI 复用保护测试 9/9、本地构建输入保护测试 5/5 通过。
- 未操作设备，未进行本地新包的交互验收。

## 在新的 worktree 或机器初始化

需要 Docker（Apple Silicon 需启用 amd64 执行支持）、Python 3、Git、已登录的
`gh`，以及足够磁盘空间。镜像导入、来源验证和首次依赖安装需要联网；
依赖缓存准备好后，日常构建无需访问 GitHub 或 CI。

```sh
docker pull --platform linux/amd64 ghcr.io/panedioic/winehua-buildenv:test
python3 scripts/local_hap.py init \
  --image ghcr.io/panedioic/winehua-buildenv:test \
  --artifact /absolute/path/full-ci-artifact.zip \
  --metadata /absolute/path/github-artifact-metadata.json
python3 scripts/local_hap.py doctor
python3 scripts/local_hap.py build
```

`--artifact` 必须是 GitHub 保存的完整 artifact ZIP，`--metadata` 是该 artifact
的 GitHub API JSON。初始化会核对在线 metadata、成功构建状态、archive digest、
HAP digest、API/ABI、应用身份和原生来源；拒绝使用由其他 app-only 包继续衍生的链条。

本机配置和基包保存在忽略目录 `.hvigor/local-build/`。容器和卷名由 worktree
路径生成，记录在 `config.json`，不复用其他工程的容器。移除该目录或 Docker
卷会失去对应缓存；正常开发无需清理或重建。

构建中的 HAP 是实际快照的结果；如果构建期间继续修改代码，下一次执行
`build` 才会包含后续修改。此工具不替代设备功能验收。
