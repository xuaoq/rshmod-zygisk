/*
 * RshRuntime.java —— 注入目标 APP 进程的 Java 侧运行时
 *
 * 由 Zygisk native 在 postAppSpecialize 时通过 JNI 注入 dex 并反射调用；
 * 不依赖 Xposed 框架。精确复刻原 Xposed 模块的核心伪装逻辑：
 *   - BuildHook：反射改写 Build / Build.VERSION 全部静态字段 + getRadioVersion/getSerial
 *   - SystemPropertiesHook：SystemProperties.get 的 Java 层映射 + DirtyHook(隐藏root) + WebView UA + http.agent
 *
 * 这些 Java 层逻辑与 native 层 __system_property_get Hook 互补：
 *   native 覆盖 /system 进程、无 Java 运行时路径；
 *   Java 层覆盖 app 进程内 Android framework 的静态缓存(Build 字段)与高级 API。
 */
package com.zennolab.zennodroid.rt;

import android.os.Build;
import android.webkit.WebView;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.lang.reflect.Field;

public final class RshRuntime {

    private static final String CONFIG_PATH = "/data/adb/rshmod/rshmod_config.json";
    private static JSONObject sCfg = new JSONObject();
    private static JSONObject sProp = new JSONObject();
    private static JSONObject sIds = new JSONObject();

    private RshRuntime() {}

    /** Zygisk native 注入后的入口 */
    @SuppressWarnings("unused")
    public static void init() {
        loadConfig();
        applyBuildFields();
    }

    //==========================================================================
    // 配置
    //==========================================================================
    private static void loadConfig() {
        try {
            File f = new File(CONFIG_PATH);
            if (!f.exists()) return;
            FileInputStream in = new FileInputStream(f);
            byte[] buf = new byte[(int) f.length()];
            int off = 0, r;
            while ((r = in.read(buf, off, buf.length - off)) > 0) off += r;
            in.close();
            sCfg = new JSONObject(new String(buf, "UTF-8"));
            if (sCfg.has("props")) sProp = sCfg.getJSONObject("props");
            if (sCfg.has("ids"))  sIds  = sCfg.getJSONObject("ids");
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private static boolean enabled(String sw) {
        JSONObject sws = sCfg.optJSONObject("switch");
        return sws != null && sws.optBoolean(sw, false);
    }

    private static String prop(String key) { return sProp.optString(key, ""); }
    private static String id(String key)  { return sIds.optString(key, ""); }

    public static String getProp(String key) { return prop(key); }
    public static String getId(String key)  { return id(key); }

    //==========================================================================
    // Build / Build.VERSION 静态字段 —— 精确复刻 BuildHook
    //==========================================================================
    private static void applyBuildFields() {
        if (!enabled("build_prop") || sProp.length() == 0) return;

        // Build 静态字段：PHP_xxx -> ro.* 属性
        String[][] bm = {
            {"ro.product.model",        "MODEL"},
            {"ro.product.board",        "BOARD"},
            {"ro.product.brand",        "BRAND"},
            {"ro.product.manufacturer", "MANUFACTURER"},
            {"ro.hardware",             "HARDWARE"},
            {"ro.product.device",       "DEVICE"},
            {"ro.product.name",         "PRODUCT"},
            {"ro.build.display.id",     "DISPLAY"},
            {"ro.build.id",             "ID"},
            {"ro.build.tags",           "TAGS"},
            {"ro.build.user",           "USER"},
            {"ro.build.type",           "TYPE"},
            {"ro.build.host",           "HOST"},
            {"ro.serialno",             "SERIAL"},
            {"ro.baseband",             "RADIO"},
            {"ro.build.fingerprint",    "FINGERPRINT"},
        };
        try {
            for (String[] m : bm) {
                String key = m[0], f = m[1];
                if (!sProp.has(key) || prop(key).isEmpty()) continue;
                setStaticField(Build.class, f, prop(key));
            }
            // TIME (build date utc -> ms)
            if (sProp.has("ro.build.date.utc")) {
                try { setStaticField(Build.class, "TIME", Long.parseLong(prop("ro.build.date.utc")) * 1000L); }
                catch (Exception ignore) {}
            }
            // Build.VERSION
            setBuildVersion("RELEASE",           "ro.build.version.release");
            setBuildVersion("INCREMENTAL",       "ro.build.version.incremental");
            setBuildVersion("SECURITY_PATCH",    "ro.build.version.security_patch");
            setBuildVersion("SDK_INT",           "ro.build.version.sdk");
            setBuildVersion("CODENAME",          "ro.build.version.codename");
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private static void setBuildVersion(String field, String propKey) {
        if (!sProp.has(propKey)) return;
        try {
            if ("SDK_INT".equals(field)) {
                setStaticField(Build.VERSION.class, field, Integer.parseInt(prop(propKey)));
            } else {
                setStaticField(Build.VERSION.class, field, prop(propKey));
            }
        } catch (Exception ignore) {}
    }

    private static void setStaticField(Class<?> cls, String field, Object value) {
        try {
            Field f = cls.getDeclaredField(field);
            f.setAccessible(true);
            f.set(null, value);
        } catch (Exception ignore) {}
    }

    //==========================================================================
    // SystemProperties.get —— Java 层映射（脏处理 + 隐藏 root）
    //   native 层已 hook __system_property_get；此处兜底 SystemProperties.get，
    //   处理 qemu / adb.secure / debuggable 等反检测键。
    //==========================================================================
    @SuppressWarnings("unused")
    public static String systemPropertiesGet(String key, String def) {
        if (key == null) return def;
        // DirtyHook 反检测
        switch (key) {
            case "qemu": return "";
            case "ro.adb.secure": return "1";
            case "ro.debuggable": return "0";
            case "ro.secureboot.lockstate": return "locked";
            case "dalvik.vm.isa.arm64.variant": return "default";
            case "ro.bootmode": return "charger";
            case "ro.build.selinux": return "1";
            case "ro.boot.flash.locked": return "1";
            default: break;
        }
        // 配置属性映射
        String v = prop(key);
        if (!v.isEmpty()) return v;
        return def;
    }

    //==========================================================================
    // WebView UA / http.agent —— 复刻 SystemPropertiesHook
    //==========================================================================
    @SuppressWarnings("unused")
    public static void applyWebViewUA(WebView webView) {
        if (webView == null || !enabled("build_prop") || !enabled("webview")) return;
        String ua = webView.getSettings().getUserAgentString();
        String chrome = "Chrome/121.0.6167.101";
        if (ua != null) {
            for (String part : ua.split(" ")) {
                if (part.startsWith("Chrome")) { chrome = part; break; }
            }
        }
        String nua = "Mozilla/5.0 (Linux; Android " + prop("ro.build.version.release")
                + "; " + prop("ro.product.model") + " Build/" + prop("ro.build.id")
                + "; wv) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 "
                + chrome + " Mobile Safari/537.36";
        try { webView.getSettings().setUserAgentString(nua); } catch (Exception ignore) {}
    }

    @SuppressWarnings("unused")
    public static String httpAgent() {
        return "Dalvik/2.1.0 (Linux; U; Android " + prop("ro.build.version.release")
                + "; " + prop("ro.product.model") + " Build/" + prop("ro.build.id") + ")";
    }
}