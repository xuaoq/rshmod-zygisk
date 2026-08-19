#!/system/bin/sh
SKIPMOUNT=false
PROPFILE=false
POSTFSDATA=false
LATESTARTSERVICE=true

ui_print "*******************************"
ui_print " RshMod Zygisk 机型伪装模块"
ui_print " v1.8.5"
ui_print "*******************************"

# Zygisk 必须启用
if [ "$ZYGISK" != "true" ] && [ -z "$Z" ]; then
  ui_print " [!] 警告：未检测到 Zygisk，模块将无法注入"
fi

# 复制 Web 管理资源
mkdir -p /data/adb/rshmod/web
cp -rf "$MODPATH/web/"* /data/adb/rshmod/web/ 2>/dev/null

ui_print " 安装完成，请在 Web 界面配置伪装参数"
