/*
 * RshMod Zygisk 模块核心 (standard Zygisk API)
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

static int (*realPropGet)(const char*, char*) = nullptr;
static int propGetHandler(const char* name, char* value) {
    if (!name || !value) return -1;
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        std::string v = g_config.lookupProp(name);
        if (!v.empty()) {
            if (value) strcpy(value, v.c_str());
            return (int)v.size();
        }
    }
    if (realPropGet) return realPropGet(name, value);
    return __system_property_get(name, value);
}

class RshMod : public zygisk::ModuleBase {
public:
    explicit RshMod(Api* api) : api(api) {
        LOGI("RshMod constructed");
    }

    void onLoad(Api* api) override {
        this->api = api;
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        if (!g_configLoaded) loadConfigLocked();
        LOGI("onLoad, scopes=%zu, global=%d", g_config.scopes.size(), (int)g_config.enableGlobal);
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        if (args == nullptr) return;
        std::string proc, pkg;
        if (args->nice_name) {
            JNIEnv* env = getEnv();
            if (env) {
                const char* s = env->GetStringUTFChars(*args->nice_name, nullptr);
                if (s) { proc = s; env->ReleaseStringUTFChars(*args->nice_name, s); }
            }
        }
        if (args->app_data_dir) {
            JNIEnv* env = getEnv();
            if (env) {
                const char* d = env->GetStringUTFChars(*args->app_data_dir, nullptr);
                if (d) {
                    pkg = d;
                    env->ReleaseStringUTFChars(*args->app_data_dir, d);
                    size_t pos = pkg.rfind('/');
                    if (pos != std::string::npos) pkg = pkg.substr(pos + 1);
                }
            }
        }
        if (isInScope(proc, pkg)) {
            LOGI("target app: proc=%s pkg=%s", proc.c_str(), pkg.c_str());
            hookNativeProp();
            if (api) {
                jclass cls = api->loadDex(kRuntimeDex);
                if (cls) LOGI("rshmod_rt.dex loaded");
            }
        } else {
            LOGD("skip: proc=%s", proc.c_str());
        }
    }

    void postAppSpecialize(const AppSpecializeArgs* args) override {}
    void preServerSpecialize(ServerSpecializeArgs* args) override {}
    void postServerSpecialize(const ServerSpecializeArgs* args) override {}

private:
    Api* api = nullptr;

    JNIEnv* getEnv() {
        if (api == nullptr) return nullptr;
        JavaVM* vm = api->getJavaVM();
        if (vm == nullptr) return nullptr;
        JNIEnv* env = nullptr;
        vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (env == nullptr) vm->AttachCurrentThread(&env, nullptr);
        return env;
    }

    void hookNativeProp() {
        if (api) {
            realPropGet = (int(*)(const char*, char*))api->getSym("__system_property_get");
            if (realPropGet) {
                LOGI("hookNativeProp: __system_property_get @ %p", (void*)realPropGet);
            }
        }
    }
};

REGISTER_ZYGISK_MODULE(RshMod)
