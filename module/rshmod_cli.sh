#!/system/bin/sh
# rshmod_cli.sh —— RshMod 配置命令行工具
# 由 Web 管理端 / ADB / root shell 调用。用法：
#   rshmod_cli.sh read              读当前配置
#   rshmod_cli.sh set <json>        写入完整配置
#   rshmod_cli.sh apply <pkg>       手动将伪装应用到指定包（重启后由 Zygisk 生效）
#   rshmod_cli.sh info              查看真实设备信息
#   rshmod_cli.sh status            查看模块状态
CMD="$1"
CFG=/data/adb/rshmod/rshmod_config.json

case "$CMD" in
  read)
    cat "$CFG" 2>/dev/null || echo '{}'
    ;;
  set)
    # 直接写入 JSON（需 root，参数作为 stdin 或整体传入）
    mkdir -p /data/adb/rshmod
    if [ -t 0 ]; then
      echo "$2" > "$CFG"
    else
      cat > "$CFG"
    fi
    chmod 644 "$CFG"
    echo "config written: $CFG"
    ;;
  apply)
    # 触发配置热重载：Zygisk native 每次 fork 前重新读取，无需手动 apply
    echo "config will be picked up on next app launch"
    ;;
  info)
    sh /data/adb/rshmod/rsh_info.sh 2>/dev/null || sh "$(dirname $0)/rsh_info.sh" 2>/dev/null
    cat /data/adb/rshmod/device_info.log 2>/dev/null
    ;;
  status)
    if [ -f /data/adb/rshmod/rshmod_config.json ]; then
      echo "config: present ($(wc -c < /data/adb/rshmod/rshmod_config.json) bytes)"
    else
      echo "config: missing"
    fi
    $0 info
    ;;
  *)
    echo "usage: $0 {read|set|apply|info|status}"
    ;;
esac
exit 0