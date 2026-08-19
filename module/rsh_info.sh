#!/system/bin/sh
# rsh_info.sh —— 真实设备信息采集
# 供账号登录与设备信息获取使用。仅采集/展示真实信息，不修改任何设备数据。
# 采集结果为只读文本，写入 /data/adb/rshmod/device_info.log

OUT=/data/adb/rshmod/device_info.log
{
  echo "=== RshMod 设备信息采集 $(date) ==="
  echo "[Build]"
  echo "  manufacturer: $(getprop ro.product.manufacturer)"
  echo "  brand:        $(getprop ro.product.brand)"
  echo "  model:        $(getprop ro.product.model)"
  echo "  device:       $(getprop ro.product.device)"
  echo "  board:        $(getprop ro.product.board)"
  echo "  hardware:     $(getprop ro.hardware)"
  echo "  android ver:  $(getprop ro.build.version.release) / SDK $(getprop ro.build.version.sdk)"
  echo "  abi:          $(getprop ro.product.cpu.abi)"
  echo "[系统]"
  echo "  android_id:   $(settings get secure android_id 2>/dev/null)"
  echo "  serial:       $(getprop ro.serialno)"
  echo "[网络]"
  echo "  wifi ssid:    $(dumpsys wifi 2>/dev/null | grep -m1 'SSID')"
  echo "  bt name:      $(settings get secure bluetooth_name 2>/dev/null)"
  echo "[账户/登录]"
  echo "  已登录账户数:  $(pm list users 2>/dev/null | grep -c UserInfo)"
} > "$OUT" 2>&1

chmod 644 "$OUT"
exit 0