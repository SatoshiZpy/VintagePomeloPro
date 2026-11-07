# 本地验证入口

日常修改先在本机运行，不需要推送、联网、Docker 或连接设备：

```sh
node scripts/run_local_tests.cjs
```

默认执行 21 组检查：所选服务的 SDK 类型与 ArkTS 规则、Wine 启动/升级/CPU
模块、窗口任务、Steam 下载/窗口/Deck/诊断、GNU Proton 安装与辅助进程、游戏
启动交接、Mono、CI 包装规则，以及模型、应用目录、应用会话回归。
服务测试运行生产源码，平台调用由测试替身提供；这些测试不能代替真机验收。

可选入口：

```sh
# 只检查本机 Node、Python 与 SDK 的可用性和版本
node scripts/run_local_tests.cjs --doctor
# 更小的 Wine 启动与窗口检查集合
node scripts/run_local_tests.cjs --scope startup
# 为本次修改选择类型检查目标（--files 放在最后）
node scripts/run_local_tests.cjs --files entry/src/main/ets/service/WineEngineService.ets
# 使用指定 SDK；也支持 OHOS_SDK、TOOL_HOME、PYTHON 环境变量
node scripts/run_local_tests.cjs --sdk /apps/harmony/sdk/default/openharmony
```

脚本自动寻找 DevEco/macOS 或 Linux 工具目录。报告及逐项日志位于忽略目录
`.hvigor/outputs/local-check/<run>/`。失败返回非零退出码；某项失败后仍执行其余
独立检查。完整告警保留在日志里，终端显示分组结果、耗时和报告路径。

## 当前本机实测（2026-11-07）

- Node、Python、DevEco SDK/ArkTS 检查器、Hvigor 和 ohpm 均已安装；后两者没有
  配置到普通 shell PATH，但本地检查直接使用已发现的工具路径。
- 默认检查 21/21 通过，单轮耗时 19.27 秒。4 个服务文件的 SDK 检查 0 个错误、
  74 条告警（含异常处理提示）；未修改业务代码来消除已有告警。
- 反向验证：故意加入类型不匹配和显式 `any`，检查器会返回失败。
- 已安装 SDK 为 API 24 / 6.1.1.125；项目目标为 API 23。脚本明确报告此差异，
  不能将本地通过宣称为 API 23 兼容性或整个 ArkUI 应用编译通过。
- 已补齐 CI Linux 镜像、SDK、持久容器及应用层构建缓存。本地出包首轮 61.39 秒，
  无源码变化的增量出包 11.94 秒；均通过 API 23 / ARM64 和运行时逐字节校验。
  入口见 [本地 Docker 出包](LOCAL_DOCKER_BUILD.zh-CN.md)。完整原生重编译的子模块和
  缓存尚未初始化，应用层出包复用已验证的全量原生基包。

## 验证边界

本入口不生成 HAP、不启动原生编译、不下载依赖、不签名、不安装应用、不触发
CI、不操作设备。SDK 检查只针对选定源码，未运行 Hvigor 的完整 ArkUI 转换与
资源打包。涉及 NAPI/C++、Wine/FEX/Mesa、API 兼容性或打包的问题，仍需对应的
原生/完整构建及设备验证。

推荐顺序：本地检查 → 需要安装时构建 HAP → 人工/设备验证。应用层 CI 已支持
复用受校验的原生运行时；本地也已接入同样的复用策略。使用
`python3 scripts/local_hap.py build` 在 Linux 容器中生成未签名 HAP，再按现有
开发签名流程处理。日常修改不需要推送或触发 CI。
