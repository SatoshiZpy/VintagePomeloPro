# 项目 AI 工作指引

## 工作区与构建入口

下述脚本、文档与 skill 路径均相对于选定的开发 worktree；主仓库可能仍是旧版本。

- 主仓库保持 `main_proton_steamarm64` 分支；代码修改在独立 worktree 中完成。
  先用 `git worktree list` 查找已有工作区，优先使用具备
  `scripts/local_hap.py` 和 `.hvigor/local-build/config.json` 的已配置 worktree。
  不要因为当前主仓库没有缓存，就断言本机无法出包或重建环境。
- 涉及构建、签名、安装或 Wine 启动诊断时，读取所选 worktree 中的
  [winehua-build-release skill](.agents/skills/winehua-build-release/SKILL.md)。
- 日常 ArkTS/业务修改先运行 `node scripts/run_local_tests.cjs`；只查启动问题可加
  `--scope startup`。这是快速检查，不是完整 HAP 编译或设备验收。
- 需要 HAP 时，优先运行 `python3 scripts/local_hap.py build`；检查环境用
  `python3 scripts/local_hap.py doctor`。不要为了每次应用层修改默认推送或等待 CI。
  命令在所选 worktree 根目录运行。
- 该入口使用固定 Linux Docker 镜像、持久卷与已校验原生基包，支持未提交的
  ArkTS 改动，输出 `.hvigor/outputs/local-hap/<时间>/entry-default-unsigned.hap`
  及 `report.json`。未签名 HAP 需要现有开发签名流程后才能安装。
- C++/Wine/FEX/Mesa、gitlink、运行时资源、SDK/ABI/应用身份等变化若被复用校验拒绝，
  应走相应完整构建并更新基包，不得放宽校验来强行复用。保留原生运行时来源记录。
- 详细初始化、缓存与实测数据见 [本地 Docker 出包说明](docs/linux-steam/LOCAL_DOCKER_BUILD.zh-CN.md)。
  缺少配置时按该说明初始化；不要删除已有卷、重建镜像或清空缓存来进行常规增量编译。
- 出包成功与设备功能通过分别报告；如果用户接手设备验证，不再自动操作设备。

<!-- HOMEGRAPH_START -->
## HomeGraph

如果仓库根目录存在 `.homegraph/`，理解或定位代码前先使用 `homegraph_explore`
或 `homegraph explore`，再按需使用文本搜索。不存在时跳过，不自行索引。
<!-- HOMEGRAPH_END -->

<!-- CODEGRAPH_START -->
## CodeGraph

如果仓库根目录存在 `.codegraph/`，理解或定位代码前先使用 `codegraph_explore`
或 `codegraph explore`，再按需使用文本搜索。不存在时跳过，不自行索引。
<!-- CODEGRAPH_END -->
