#!/system/bin/sh
# RshMod Zygisk 模块安装脚本（Zygisk 模块使用 service.sh 保持配置目录）
MODDIR=${0%/*}

# 确保配置目录存在
mkdir -p /data/adb/rshmod
chmod 755 /data/adb/rshmod

# 若尚无配置文件，写入默认模板
if [ ! -f /data/adb/rshmod/rshmod_config.json ]; then
  cp "$MODDIR/rshmod_config.json" /data/adb/rshmod/rshmod_config.json 2>/dev/null || true
fi
chmod 644 /data/adb/rshmod/rshmod_config.json

# 设备信息采集脚本（账号登录 + 设备信息获取）
[ -f "$MODDIR/rsh_info.sh" ] && sh "$MODDIR/rsh_info.sh" >/dev/null 2>&1

exit 0