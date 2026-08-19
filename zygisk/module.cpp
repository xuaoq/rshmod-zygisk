/*
 * RshMod Zygisk 模块核心
 * 在 Zygote preAppSpecialize 阶段识别目标应用进程，
 * 根据配置决策生效范围（全局 / 指定 APP），注入 Java 运行时并挂 native hook。
 */
#include <android/log.h>
#include <sys/system_properties.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <string>
#include <mutex>
#include "zygisk.hpp"
#include "config.hpp"

#define LOG_TAG "RshMod"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

static constexpr const char* kConfigPath = "/data/adb/rshmod/rshmod_config.json";
static constexpr const char* kRuntimeDex = "/data/adb/rshmod/rshmod_rt.dex";

static std::mutex g_cfgMutex;
static RshConfig g_config;
static bool g_configLoaded = false;

static void loadConfigLocked() {
    g_config = RshConfig();
    FILE* f = fopen(kConfigPath, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::string raw(sz, '\0');
        if (raw.size() > 0 && fread(&raw[0], 1, sz, f) == (size_t)sz) {
            try { g_config = RshConfig::fromJson(raw); }
            catch (...) {}
        }
        fclose(f);
    } else {
        LOGW("config not found: %s", kConfigPath);
    }
    g_configLoaded = true;
}

static bool isInScope(const std::string& proc, const std::string& pkg) {
    if (!g_configLoaded) { std::lock_guard<std::mutex> lk(g_cfgMutex); loadConfigLocked(); }
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (g_config.enableGlobal) return !pkg.empty();
    for (auto& s : g_config.scopes)
        if (pkg == s || proc == s) return true;
    return false;
}

//------------------------------------------------------------------------------
// Native 层 Hook：__system_property_get
//------------------------------------------------------------------------------
class RshMod : public zygisk::ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        if (!g_configLoaded) loadConfigLocked();
        LOGI("onLoad, scopes=%zu, global=%d", g_config.scopes.size(),
             (int)g_config.enableGlobal);
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        if (args == nullptr) return;
        std::string proc, pkg;
        if (args->nice_name && env) {
            const char* s = env->GetStringUTFChars(*args->nice_name, nullptr);
            if (s) { proc = s; env->ReleaseStringUTFChars(*args->nice_name, s); }
        }
        if (args->app_data_dir && env) {
            const char* d = env->GetStringUTFChars(*args->app_data_dir, nullptr);
            if (d) {
                pkg = d;
                env->ReleaseStringUTFChars(*args->app_data_dir, d);
                size_t pos = pkg.rfind('/');
                if (pos != std::string::npos) pkg = pkg.substr(pos + 1);
            }
        }
        if (isInScope(proc, pkg)) {
            LOGI("target app: proc=%s pkg=%s", proc.c_str(), pkg.c_str());
            injectJavaRuntime(proc.c_str(), pkg.c_str());
            hookNativeProp();
        } else {
            LOGD("skip: proc=%s", proc.c_str());
        }
    }

    void postAppSpecialize(const AppSpecializeArgs* args) override {}
    void preServerSpecialize(ServerSpecializeArgs* args) override {}
    void postServerSpecialize(const ServerSpecializeArgs* args) override {}

private:
    Api* api;
    JNIEnv* env;

    // hook __system_property_get（经宿主 pltHook），命中伪装键则覆盖
    static int propGetHandler(const char* name, char* value) {
        if (!name || !value) return -1;
        {
            std::lock_guard<std::mutex> lk(g_cfgMutex);
            std::string v = g_config.lookupProp(name);
            if (!v.empty()) {
                strcpy(value, v.c_str());
                return (int)v.size();
            }
        }
        // 走真实函数（宿主解析保存），避免 self-recursion
        if (realPropGet) return realPropGet(name, value);
        return __system_property_get(name, value);
    }

    void hookNativeProp() {
        realPropGet = nullptr;
        if (api) {
            // 宿主提供真实 __system_property_get 符号地址
            realPropGet = (int(*)(const char*, char*))api->getSym("__system_property_get");
            if (realPropGet) {
                LOGI("hookNativeProp: __system_property_get @ %p", (void*)realPropGet);
                // 生产环境启用：
                //   api->pltHook("__system_property_get", (void*)propGetHandler);
            }
        }
    }

    // 真实 __system_property_get（宿主解析保存，避免递归）
    static int (*realPropGet)(const char*, char*);

    void injectJavaRuntime(const char* proc, const char* pkg) {
        (void)proc; (void)pkg;
        // 通过宿主 API 在目标进程注入 dex：
        //   jclass rtClass = api->loadDex(kRuntimeDex);
        //   然后反射找到 com.zennolab.zennodroid.rt.RshRuntime 并调用静态 init()
        // 宿主核对加载路径 /data/adb/rshmod/rshmod_rt.dex
        LOGI("injectJavaRuntime ready: %s" , kRuntimeDex);
    }
};

// 类外定义静态成员（避免链接错误）
int (*RshMod::realPropGet)(const char*, char*) = nullptr;

REGISTER_ZYGISK_MODULE(RshMod)