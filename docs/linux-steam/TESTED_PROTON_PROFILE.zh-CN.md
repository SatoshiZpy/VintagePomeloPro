# 从主分支重建当前 HMDeck Proton 包

`main_proton_steamarm64` 的默认推送构建使用 **Build HMDeck tested Proton profile**。
也可在 GitHub Actions 选择该工作流、选择此分支、点击 Run workflow。
输出为 API 23、ARM64、HMDeck 1.5.5 的未签名 HAP，以及 `graphics-build-report.json`。
设备安装仍需现有开发签名流程；构建成功不代表新产物已完成设备验收。

## 与当前实测包的关系

本轮产品源码根据 2026-11-09 01:12:07 本地包的实际 source.tar 和原生来源报告整理，
包含 HMDeck 名称与图标、GNU Proton/DXVK、BGRA UAV 兼容、启动依赖修复、FPS/分辨率 HUD、
提交和 Render Pass/绘制组采样。原生源码哈希与该包记录逐项一致。
设备私有的采样开关、Steam/游戏安装目录及存档不属于构建输入。

CI 重编完整 `steamohos_gnu_child`、GNU probe/Vulkan adapter 和 DXVK Legacy ARM64X，
然后重新编译 ArkTS 与资源。其余 Wine/FEX/Mesa 等运行时沿用已验证完整构建的原始字节，
逐个 HAP 成员校验不变。它复现相同源码和运行时组合，不保证 ZIP 时间戳、编译路径、
签名或整个 HAP 的 SHA 完全相同。

## 固定输入和边界

- `docs/graphics/proton-ci-profile.lock.json` 固定基础包、镜像 digest 及原实测包标识。
- `docs/graphics/proton-ci-base-source.json` 保存基础包的原始来源记录。
- 基础包来自完整 CI run `42360978164`、commit `3c634ea9440fb9bd6e283c3952b3f16c7241d3c1`，
  以 `build-inputs-20261109` 预发布资产保存，避免依赖会到期的 Actions artifact。
- GE Proton、llvm-mingw、DXVK gitlink/补丁及 BC 解码头文件分别由现有 lock 和来源收据校验。
- `scripts/ci_proton_graphics.py guard` 要求干净工作区、允许范围内的变更及相同基础原生构建配方。
  新的 Wine/FEX/Mesa、gitlink、ABI/SDK 或其他原生输入变化会拒绝复用；应完整重建并更新基线，
  不应扩大白名单来绕过校验。

CI 实际入口是 `ci/Makefile.proton-ci`。本地日常应用修改仍优先使用
`python3 scripts/local_hap.py build`；原生改动应按对应 Makefile 和来源校验流程处理。
旧 **Build VintagePomeloPro HAP** 工作流保留完整源码构建：在主分支手动运行时勾选
`full_rebuild`，或使用发布 tag。主分支普通推送只运行当前 Proton 包构建，避免重复全量编译。
