# RshMod —— Zygisk 机型伪装模块 + Web 管理界面

将原 **Xposed 机型伪装 APP（com.zennolab.zennodroid v1.8.5）** 改造为
**Zygisk 模块 + Web 管理界面** 的完整工程。

---

## 一、原 APP 逆向分析结论（已确认）

| 项目 | 结论 |
|---|---|
| 包名 | `com.zennolab.zennodroid` |
| 类型 | **Xposed 模块**（`xposedmodule=true`），非 LSP 专属 |
| 版本 | v1.8.5 (build 19) |
| 伪装方式 | **仅 Hook 返回伪造值，不修改设备** ✅ |
| 配置存储 | `XSharedPreferences` → `user_config.xml` |
| 核心 Hook | 23 个：Build, SystemProperties, IMEI, GsfId, AndroidId, AdsId, MediaDrm, WiFi, Bluetooth, Operator, Geo, Package, Settings, WebView, File, Process, Account, PartnerId, Accessibility 等 |
| 账号登录 | `LoginActivity` + `AuthMenuHelper` + `internal/b.smali`(认证/加密核心, 108KB, 混淆 a–k) |
| Host 通信 | `RshModContentProvider`(`im.rsh.mod`) + `ZennoDroidContentProvider` + `ZennoDroidService`(前台服务) |
| Native 库 | `libzennodroid.so`（四 ABI） |
| 作用域 | 由 LSP 管理器勾选（指定 APP）/ 全选（全局） |

### 与原 APP 的行为对齐（用户要求逐条）
1. ✅ **Zygisk 模块（非 LSP）**：本工程是原生 Zygisk 模块，由 Magisk/KernelSU(APatch, SKRoot) 的 Zygisk 直接加载，不依赖 LSPosed/EdXposed。
2. ✅ **单独 APP / 全局**：配置 `{"global":true}` = 全局；`{"global":false,"scopes":[...]}` = 仅指定包。
3. ✅ **只伪装不修改**：native/JNI 层 Hook 覆盖返回路径，不对系统文件、Build 持久属性做任何真实修改。
4. ✅ **保留账号登录 + 设备信息获取**：`rsh_info.sh` 采集真实设备信息用于登录校验；管理端 "设备信息" 标签可查看并用于账号登录流程。

---

## 二、工程结构

```
zygisk_rshmod/
├─ zygisk/                 # Zygisk native 模块（C++）
│  ├─ module.cpp           # 模块入口：进程识别/注入/挂 hook
│  ├─ config.hpp/.cpp      # 配置模型 + 零依赖 JSON 解析
│  ├─ zygisk.hpp           # Zygisk API 抽象
│  ├─ Android.mk           # NDK 构建
│  └─ Application.mk
├─ jni/
│  └─ RshRuntime.java      # 注入目标进程的 Java 运行时（改 Build 字段等）
├─ module/                 # Magisk 模块打包内容
│  ├─ module.prop
│  ├─ customize.sh / service.sh
│  ├─ rshmod_cli.sh        # 配置 CLI（read/set/info/status）
│  ├─ rsh_info.sh          # 设备信息采集（账号登录用）
│  └─ rshmod_config.json   # 默认配置
├─ web/
│  └─ index.html           # Web 管理界面（单页）
├─ manager/                # 管理端 APK（WebView 宿主，可选）
│  ├─ AndroidManifest.xml
│  └─ src/com/rshmod/manager/ManagerActivity.java
├─ build.sh                # 一键打包
└─ README.md
```

---

## 三、Web 管理界面功能

`web/index.html`（通过管理端 WebView 或浏览器加载）提供：

- **设备属性**：Manufacturer / Brand / Model / Device / Board / Product / Hardware / Build ID / Fingerprint / Tags / Type / Display / Baseband / CPU ABI
- **标识符**：Android ID / IMEI / GSF ID / 广告 ID / 序列号 / SIM 序列号
- **网络/定位**：WiFi (SSID/BSSID/MAC) / 蓝牙(名称/地址) / 运营商(MCC/MNC/载波) / 经纬度
- **生效范围**：全局伪装开关 + 指定包名列表
- **设备信息**：真实设备信息展示（供账号登录参考）

JavaScript 桥 `AndroidBridge`：`read()` / `set(json)` / `info()` / `shell(cmd)`。

---

## 四、关键机制实现说明

### 1. 作用域决策（单独 APP / 全局）
`zygisk/module.cpp::preAppSpecialize` 读取配置：
```cpp
if (g_config.enableGlobal) return !pkg.empty();      // 全局
for (auto& s : g_config.scopes)                       // 指定包
    if (pkg == s || proc == s) return true;
```
命中 → 注入 Java 运行时 + 挂 native hook；未命中 → 不注入即零开销。

### 2. 只伪装（不修改）
- **native 层**：Hook `__system_property_get`，命中配置键返回伪造值，其余走原函数（仅进程内内存态，不影响系统）。
- **Java 层**：`RshRuntime` 反射改写 `Build` 静态字段（内存态，进程内有效）。

### 3. 配置热更新
配置存于 `/data/adb/rshmod/rshmod_config.json`，Zygisk 每次 fork 前读取，
Web 界面保存后**下次目标应用启动即生效**，免重启系统。

### 4. Web 界面 → CLI → 配置
管理端 JS `set(json)` → base64 → `echo | base64 -d > rshmod_config.json`
管理端 JS `read()`  → `cat rshmod_config.json`

---

## 五、编译与安装

### 前置
- 安装了 Zygisk 的 **Magisk**（或 KernelSU/APatch 的 Zygisk Next）
- 本机构建机：Android NDK + Android SDK（build-tools 含 d8）

### 编译模块
```bash
export NDK=/path/to/android-ndk-r26
export ANDROID_JAR=/path/to/android-sdk/platforms/android-34/android.jar
./build.sh all        # 生成 out/RshMod-zygisk-v1.8.5.zip
```
> 无 NDK 时，可用已编好的 .so 手动放入 `out/module/zygisk/<abi>/` 再 `./build.sh module`。

### 🔧 补编 native `.so`（在 x86_64 / arm64 Mac / x86 机器上）
本机构建机 `zygisk/zygisk` 目录为 C++，需 **NDK（官方 linux-x86_64 / darwin 工具链）** 产出 ARM .so：
```bash
export NDK=/path/to/android-ndk-r26
./build.sh native            # 产物: out/module/zygisk/{arm64-v8a,armeabi-v7a,x86,x86_64}/librshmod.so
./build.sh module            # 重新打包，.so 自动并入 zip（含 dex）
```
> 若只编译了 **arm64-v8a**（绝大多数真机），其余 ABI 可留空，Magisk 按机型选择加载一个即可。
> 无 NDK 也可用 **Termux(arm64) + `pkg install ndk-multilib`** 在真机上编译后导入。

### 已验证的构建注意点
- `zygisk/Android.mk` 已移除 `-fno-exceptions`（module.cpp/config.hpp 的 try/catch 依赖异常）；若自定义项目请勿再加回。
- Java 运行时 `jni/RshRuntime.java` 用 `-cp android.jar` + `d8` 编为 `rshmod_rt.dex`，已由 `build.sh` 自动完成。
- 全部核心逻辑已用 NDK 真实 `jni.h` + `-Wall` 在工具链上编译验证通过。

### ☁️ 方式 A：GitHub Actions 云端编译（推荐，免费免本机 NDK）
工程内已内置 `.github/workflows/build.yml`，把仓库推到 GitHub 后由云端 **x86_64 runner** 自动编译出含全部 `.so` 的模块：

```bash
# 1. 创建 GitHub 仓库并推送（首次需登录）
git init && git add -A && git commit -m "rshmod zygisk"
git remote add origin https://github.com/<你的用户名>/rshmod-zygisk.git
git push -u origin main

# 2. 在 GitHub 仓库页 Actions → Build RshMod → Run workflow（手动触发）
# 3. 构建完成后，最下方 Artifacts → 下载 RshMod-zygisk（含 4 ABI .so 的完整模块 zip）
```
> 免费额度足够；构建产物 retention-days 默认 1 天，请及时下载。
> workflow 已在 `ubuntu-latest` 安装与工程完全一致的 `NDK 26.3.11579264`，已用真实 `-fexceptions` flag 验证 module.cpp 可编译。

### 📱 方式 B：真机 Termux 编译（无电脑）
```bash
# 1. 安装已改版 proot 环境（含 NDK 的工具）：
pkg update && pkg install termux-tools
# 真机用 termux 的 aarch64 NDK（ndk-multilib 或直接装官方 NDK for arm64）
```


### 安装
```bash
# 1. 把 zip 刷入 Magisk（通过 Magisk App 或 ADB）
adb push out/RshMod-zygisk-v1.8.5.zip /data/local/tmp/
adb shell su -c 'magisk --install-module /data/local/tmp/RshMod-zygisk-v1.8.5.zip'
adb reboot

# 2. 验证模块激活
adb shell su -c 'lsmod'   # 或 magisk --listmodules
adb shell su -c 'sh /data/adb/rshmod/rshmod_cli.sh status'
```

### 安装管理端（可选）
```bash
./build.sh manager
adb install -r out/RshModManager.apk
# 或在浏览器打开 file:///data/adb/rshmod/web/index.html（需 root 文件访问）
```

---

## 六、CLI 使用

```bash
adb shell su -c 'sh /data/adb/rshmod/rshmod_cli.sh read'     # 读配置
adb shell su -c 'sh /data/adb/rshmod/rshmod_cli.sh set "<json>"'  # 写配置
adb shell su -c 'sh /data/adb/rshmod/rshmod_cli.sh info'     # 设备信息
adb shell su -c 'sh /data/adb/rshmod/rshmod_cli.sh status'   # 状态
```

---

## 七、配置示例

```json
{
  "global": true,
  "scopes": ["com.tencent.mm"],
  "props": {
    "ro.product.manufacturer": "Google",
    "ro.product.model": "Pixel 7 Pro",
    "ro.product.brand": "google",
    "ro.product.device": "cheetah",
    "ro.product.board": "taro",
    "ro.product.name": "cheetah",
    "ro.hardware": "taro",
    "ro.build.id": "TP1A.220624.014",
    "ro.build.fingerprint": "google/cheetah/cheetah:13/TP1A.220624.014/A1:user/release-keys",
    "ro.build.tags": "release-keys",
    "ro.build.type": "user",
    "ro.build.display.id": "TP1A.220624.014",
    "ro.baseband": "g5123b-130269_221201",
    "ro.product.cpu.abi": "arm64-v8a"
  },
  "ids": {
    "android_id": "5a3c1e2f9b8d4a67",
    "imei": "356938035643809",
    "gsf_id": "9b8d4a67-5a3c-1e2f-9b8d-4a675a3c1e2f",
    "advertising_id": "00000000-0000-0000-0000-000000000000",
    "serial": "RF8M21ABCDEF"
  },
  "wifi": {"ssid": "Pixel_7_Pro_5G", "bssid": "02:00:00:00:00:00", "mac": "02:1a:2b:3c:4d:5e"},
  "bluetooth": {"name": "Pixel 7 Pro", "address": "AB:CD:EF:12:34:56"},
  "operator": {"carrier": "T-Mobile", "mcc": "310", "mnc": "260"},
  "geo": {"lat": 37.421999, "lon": -122.084057},
  "switch": {
    "build_prop": true, "android_id": true, "imei": true,
    "gsf": true, "ads": true, "webview": true,
    "wifi": true, "bluetooth": true, "drm": true, "operator": true, "geo": true
  }
}
```

---

## 八、风险与兼容性

- **兼容性**：需要 Zygisk（Magisk 24+ 内置 / KernelSU/APatch 的 Zygisk Next）。Android 8~14 均可。
- **性能**：未命中作用域的应用零注入、零开销；命中应用仅进程内内存 hook。
- **安全性**：native 层 `__system_property_get` Hook 仅进程内生效，不污染系统属性池。
- **已知边界**：
  - JobScheduler/跨进程(如 system_server upcall)读取仍可能透出真实值 —— 与原生 Xposed 一致。
  - 某些 APP 用 `MediaDrm`（DRM ID）强绑定设备，需配置 `drm` 段。
  - WebView UA 伪装需 target 进程内 `RshRuntime` 生效。

---

## 九、与原 Xposed 模块的映射对照

| 原 Xposed Hook | 本工程处理 |
|---|---|
| `BuildHook` / `SystemPropertiesHook` | native `__system_property_get` + Java `Build` 字段反射 |
| `AndroidIdHook` | `ids.android_id` → Settings.Secure 返回伪装 |
| `ImeiHook` / `GsfIdHook` / `PartnerIdHook` | `ids.*` → Telephony/GSF 路径返回伪装 |
| `AdsIdHook` / `MediaDrmHook` | `ids.advertising_id` / `drm.*` |
| `WiFiHook`/`WiFiManagerHook`/`WiFiStateHook` | `wifi.*` |
| `BluetoothHook` | `bluetooth.*` |
| `OperatorHook` | `operator.*`（MCC/MNC/载波）|
| `GeoHook` | `geo.lat/lon` |
| `AccountHook` / 登录 | `rsh_info.sh` 采集 + 管理端设备信息 |
| 作用域（LSP 勾选） | `global` / `scopes` 配置 |

---

## 十、TODO / 进一步开发点
- 补充 `MediaDrmHook`、`AccountHook` 的 Java 侧细节（需在 `RshRuntime` 中扩展对 `MediaDrm.getPropertyByteArray` 的反射 Hook）
- 管理端接入 Shizuku 以支持非 root 静默写入
- 增加"已知机型一键模板"，减少手动填写
