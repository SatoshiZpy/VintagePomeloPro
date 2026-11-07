# GNU Proton x64 D3D11 图形修复（2026-11-08）

## 已定位的问题与设备证据

Dark Souls Remastered 的主程序是 AMD64 PE。旧 GNU 启动路径统一选择
`C:\dxvk26`，该目录中的 d3d11/dxgi 是 i386，不能作为这个 64 位进程的图形 DLL。
在本次 ARM64 PC 上，Venus 实际报告 Vulkan 1.2.309；DXVK 2.6 系列要求的
Vulkan 1.3 不满足，不能只替换成官方 x64 2.6 DLL。

项目已有 Legacy DXVK 的 ARM64X 产物可以用于该 AMD64 Wine/FEX 进程。
它在此设备请求了未提供的 geometryStreams，vkCreateDevice 返回 -8。
项目已有的 `WINEHUA_DXVK_RELAXED_FEATURES=1` 按实际支持情况选择可选功能。
这不等于设备支持 geometryStreams，也不证明使用它的所有游戏都兼容。

旧 GNU Venus 的内存路径即使 Present 成功，样例仍只显示清屏背景。
同一 AMD64 D3D11 样例换成包内 native-game-support 的 Venus，并启用
`VN_OHOS_NATIVE_MEMORY=1` 后，现场截图明确显示彩色旋转立方体。
2026-11-07 23:47:06 的有界运行记录：12 秒、700 帧、0 次角度回退，
初始化/Present 均返回 S_OK，进程正常退出，截图窗口显示 59 FPS。

这是早期临时对照配置的设备证据。修复候选包安装后，2026-11-08 00:23 和 00:24
通过正式 `graphics-x64` 路径再次运行：12 秒分别 691/692 帧，初始化与 Present 为 S_OK，
正常退出。使用 `C:\dxvk64` 模块；第二次样例只检查产品传入的环境变量，未自行设置兼容开关。
第一轮现场截图确认彩色立方体；临时注册表兼容开关已恢复原状态。
未取得有效的接收端 hilog 计数；没有声称黑魂已经通过。
截图、完整日志和临时变更台账保存于本机忽略的证据目录，不提交设备证据。

## 产品路径

- 读取启动 EXE 的 PE 头确定 AMD64/i386；拒绝缺失、截断、非法或符号链接的 EXE。
- GNU D3D11 的 AMD64 进程选择 `graphics-x64` 图形支持目录及 `C:\dxvk64`。
  其余原有路径保留；跨位数启动器及跨位数子游戏仍需单独验收。
- 从既有 Wine 运行时中复用 Legacy DXVK 的 d3d11、dxgi、libc++、libunwind。
- 从锁定 GE-Proton11-11-aarch64 压缩包补齐 aarch64/x86_64 两套
  winevulkan、vulkan-1、d3dcompiler_47 PE 模块。以 Wine 的 ARM64EC 路径加载。
- 只为此图形路径启用内存协议及可选功能兼容开关，传递至 Wine 子进程。
  不写持久化注册表设置。
- 独立目录不归旧 GNU 基础组件解压覆盖；启动前验证文件大小、SHA-256 及
  Wine Unix Vulkan 后端版本。文件不匹配时先修复，版本不匹配时拒绝混用。
- 前缀 DLL 如已有不同内容，启动会明确失败，不覆盖不明来源 DLL。

上游 DLL 沿用 GE/Wine 原许可证，来源及压缩包 SHA-512 见
`gnu-proton-probe.lock.json`，对应许可证随原 GNU 组件交付。
新版 Venus 是项目已交付的 native-game-support 原始二进制，未声称重编译 Mesa。

## 本地验证与构建

普通业务改动仍使用原来的 `scripts/local_hap.py`。此次涉及原生代码，
应用层复用检查必须拒绝，不能放宽其校验。此次专用构建的范围是：
GNU 适配库、Wine WSI、`steamohos_gnu_child` 完整六个源文件；其余运行时保留原来源。

在已配置 Linux Docker 容器的 Linux 文件系统中，准备同一锁定 GE 压缩包、
源码，以及从已验证基础 HAP 的 GNU runtime 提取的链接库：
`libc.so.6`、`libstdc++.so.6`、`libgcc_s.so.1`、`libwayland-client.so.0`、
`ld-linux-aarch64.so.1`。编译器需 GNU AArch64 GCC/G++，头文件需 Vulkan/Wayland；
OHOS 部分使用 Linux SDK。运行：

```sh
make -f ci/Makefile.proton-graphics proton-x64-graphics \
  OUT=/data/proton-graphics-build GNU_LINK=/data/proton-graphics-link \
  ARCHIVE=/data/GE-Proton11-11-aarch64.tar.gz
```

产物中记录源码、头文件、编译器、链接库及二进制哈希。构建器会核对新库所有
强引用动态符号与指定 GNU 运行库，缺失则失败。

将生成的 rawfile/linux-proton/graphics-x64 同步回开发 worktree，
将 `libsteamohos_gnu_child.so` 与 `native-child-build.json` 放入
`.hvigor/outputs/proton-x64-native/`。宿主 worktree 中运行：

```sh
node scripts/run_local_tests.cjs
python3 scripts/local_proton_graphics_hap.py build
```

此入口在独立 Linux 源目录通过 Makefile/Hvigor 编译 ArkTS 和资源，
核对原生构建回执与当前源码、链接库与基础包，并逐项检查：
只有一个原生库被完整重编译替换，仅增加清单记录的图形支持文件。
其余运行时文件保持字节一致。它不改动原有应用层出包配置、不更新基础包，
也不下载依赖、不签名、不安装、不重启 Steam。输出为未签名 ARM64/API 23 候选 HAP。

## x64 样例与设备验收

`ci/Makefile.proton-demo` 在 Linux/Mingw 环境构建
`smoke/proton_x64_d3d11_cube.c`，复用原有立方体工作负载。
先把该源文件及 `winehua_d3d_switch_cube.c` 放进诊断构建目录，再通过 Makefile 构建。
`native-memory-proxy.so` 是定位问题时的对照工具，不属于正式组件。

候选包提供 `dxvk11-x64` 诊断阶段；需要先把构建的 AMD64 EXE 部署到 GNU 前缀
`drive_c/proton-x64-d3d11.exe`。阶段会验证真实 PE 架构并明确使用 `--d3d11`。
验收同时检查 PE 架构、模块路径、实际立方体画面、连续帧、窗口显示及正常退出；
只有 Present/提交日志不算出图成功。再测试黑魂，并单独报告游戏结果。

用户接手设备期间不操作设备。此前临时对照实验留下的注册表兼容开关已有备份；
设备交还后按诊断变更台账恢复，再验证正式环境变量传递，避免临时设置掩盖问题。

## 首次候选包设备回归：wineserver 退出 159

首次安装后，Steam 正常，但黑魂在进入 D3D11 前退出。日志确认新图形适配库
已加载，wineserver 收到 `SIGSYS syscall=449`。旧构建生成器把可选调用的
ENOSYS 回退插在 openat2 分支前；当前公共汇编中更早的 FEX 分支已接管 449/282，
而 GNU Wine 不处于 NativeFex 模式，最终错误地转发给内核。

修复仅修改 GNU Proton 生成的 AArch64 调用入口：374、282、449 在任何 FEX
分发前返回 ENOSYS，使用 Wine 本身的回退。公共 Steam/FEX 汇编源不改。
`host_tests/proton_syscall_dispatch_test.py` 用真实交叉编译汇编及 qemu-aarch64
执行：先确认旧顺序可复现失败，再确认修复版本的三个探测、普通 futex、
七个参数的传递及既有文件分发行为。样例不再自行设置兼容环境变量，
而是检查正式启动链是否传入，避免诊断程序掩盖环境传递缺陷。


## 黑魂私有游戏库权限失败与异常递归（2026-11-08）

Steam 的实际安装目录位于应用 `files/.wine/drive_c/SteamLinuxARM64/library`。
原 `proton_private_read_fallback` 只识别 Download 库，导致这次 EXE 所有观测到的
固定私有映射均为 `fallback=0`。增强日志捕获同一游戏线程的完整顺序：

1. `mprotect(0x141e5f000, 4096, 7)` 返回 `EPERM`，该页仍为 EXE 的 `r--p` 文件映射。
2. 游戏指令 `0x140108b27` 触发 `c0000005`；`info[0]=1`（写），
   `info[1]=0x141e5f000`，与刚失败的页完全相同。
3. 后续两个异常地址分别重复 532/531 次，异常分发/展开递归，最终栈溢出。

修复使选定游戏目录的应用私有库逻辑/物理路径也进入 Wine 原有匿名分配+pread 回退。
Download 与私有库各自成对识别，不跨库接受同名游戏；保留 MAP_SHARED 语义，
拒绝邻近目录、其他应用和路径穿越。未伪造 mprotect 成功，未修改系统执行权限。
主机回归测试覆盖上述边界；设备修复前后对照结果另行记录，不能仅凭测试通过声称黑魂可用。


实机对照（00:41，候选构建 20261108-003830，保留数据安装）：

- 全部 9 个图形组件内容与新清单一致。
- 黑魂 EXE 观测到 `fallback=1`；本轮日志 `STEAMOHOS_MPROTECT` 和 `stack overflow` 均为 0，
  原写入页 `0x141e5f000` 的访问异常未复现。
- D3D11 日志从 0 字节变为有效初始化记录，明确 `Using feature level D3D_FEATURE_LEVEL_11_0`。
- 随后发生新的 `alloc_free_area_in_range` 失败及 `create_view` 的 `host_page_mask` 页对齐断言。
  因而只确认原权限/递归栈溢出链已解除，黑魂仍未通过出图验收。
- 本次新增的 pe-map-debug、memory-map-debug、resource-debug 已撤销；详细证据保留本机。


## x64 Vulkan 低地址别名与页对齐断言（诊断中）

设备实测宿主页为 4096。黑魂在映射两个 64 MiB Vulkan 块后，低地址别名
`0x2b7b0000`/`0x2f8b0000` 的释放重映射返回 EINVAL；稍后 Wine 在同一地址
再次分配 4 KiB 失败并触发 create_view 的 host_page_mask 断言。
申请地址本身已页对齐，不能据此修改页大小或禁用断言。

原初始化对所有 native Vulkan 启动无条件启用 MAP32。AMD64/ARM64EC 无需此别名；
新增地址策略只为非 x64 路径保留 MAP32，x64 同时清除 exec 继承的旧值。
`proton_vulkan_addressing_test.c` 验证 i386、x64、重复初始化及继承状态。
新头文件内容嵌入生成的 GNU shim，参与原生源码哈希校验。设备效果待实测记录。
