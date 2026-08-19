package com.rshmod.manager;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Bundle;
import android.util.Base64;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;

/**
 * RshMod 管理端 —— WebView 承载 Web 管理界面，经 root 与模块 CLI 交互。
 * 纯原生 Activity，无第三方依赖，便于快速构建。
 */
public class ManagerActivity extends Activity {

    private WebView webView;

    private static final String CLI   = "/data/adb/rshmod/rshmod_cli.sh";
    private static final String CONFIG = "/data/adb/rshmod/rshmod_config.json";
    private static final String WEB   = "/data/adb/rshmod/web/index.html";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setTitle("RshMod 管理");

        webView = new WebView(this);
        setContentView(webView);

        WebSettings s = webView.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(true);
        s.setAllowFileAccessFromFileURLs(true);
        s.setAllowUniversalAccessFromFileURLs(true);

        webView.setWebViewClient(new WebViewClient());
        webView.setWebChromeClient(new WebChromeClient());
        webView.addJavascriptInterface(new Bridge(), "AndroidBridge");
        webView.loadUrl("file://" + WEB);
    }

    /** 具备 root（su 存在）则静默继续，否则提示 */
    private void ensureRoot() {
        String[] p = {"/system/bin/su","/system/xbin/su","/sbin/su","/system/app/Superuser.apk"};
        for (String x : p) if (new java.io.File(x).exists()) return;
        new AlertDialog.Builder(this)
            .setTitle("需要 Root / Magisk")
            .setMessage("管理端需 Root 或 Shizuku 权限以读写模块配置。")
            .setPositiveButton("知道了", null)
            .show();
    }

    private class Bridge {
        private String runRoot(String cmd) {
            try {
                Process proc = Runtime.getRuntime().exec(new String[]{"su","-c",cmd});
                BufferedReader br = new BufferedReader(new InputStreamReader(proc.getInputStream()));
                StringBuilder out = new StringBuilder();
                String line;
                while ((line = br.readLine()) != null) out.append(line).append('\n');
                proc.waitFor();
                return out.toString();
            } catch (Exception e) {
                return "ERROR: " + e.getMessage();
            }
        }

        @JavascriptInterface
        public String read() {
            String body = runRoot("cat " + CONFIG + " 2>/dev/null");
            return body.trim().isEmpty() ? "{}" : body;
        }

        @JavascriptInterface
        public String set(String json) {
            String b64 = Base64.encodeToString(
                    json.getBytes(StandardCharsets.UTF_8), Base64.NO_WRAP);
            String cmd = "echo " + b64 + " | base64 -d > " + CONFIG +
                    " && chmod 644 " + CONFIG + " && echo OK";
            String r = runRoot(cmd);
            return r.contains("OK") ? "{\"ok\":true}" : "{\"ok\":false}";
        }

        @JavascriptInterface
        public String info() {
            String r = runRoot("sh " + CLI + " info 2>/dev/null");
            if (r == null) r = "无法获取设备信息";
            return r.replace("\n","\\n");
        }

        @JavascriptInterface
        public String shell(String cmd) { return runRoot(cmd); }
    }

    @Override
    public void onBackPressed() {
        if (webView.canGoBack()) webView.goBack();
        else super.onBackPressed();
    }
}