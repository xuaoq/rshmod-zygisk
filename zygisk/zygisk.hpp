#pragma once
#include <jni.h>
#include <vector>
#include <string>

namespace zygisk {

class Api {
public:
    virtual void setOption(int opt) = 0;
    virtual void *getSym(const char *sym) = 0;
    virtual void pltHook(void *target, void *detour) = 0;
    virtual void pltUnhook(void *target) = 0;
    virtual void *pltResolve(const char *name, void *def = nullptr) = 0;
    virtual void *hook(void *target, void *detour) = 0;
    virtual void unhook(void *target) = 0;
    virtual void connectCompanion(void *(*entry)(void *)) = 0;
    virtual JavaVM *getJavaVM() = 0;
    virtual void *getNativeBridgeInterface() = 0;
    virtual jclass loadDex(const char *dex_path) = 0;
};

struct AppSpecializeArgs {
    jint uid;
    jint gid;
    jint *managed_flags;
    jint *flags;
    jint *mount_external;
    jstring *process;
    jstring *nice_name;
    jstring *app_data_dir;
    jstring *se_info;
    jstring *se_name;
    jstring *se_prefix;
    jboolean *is_child_zygote;
};

struct ServerSpecializeArgs {
    jint uid;
    jint gid;
    jint *managed_flags;
    jint *flags;
    jint *mount_external;
    jboolean *is_child_zygote;
};

class ModuleBase {
public:
    virtual ~ModuleBase() = default;
    virtual void onLoad(Api *api) {}
    virtual void preAppSpecialize(AppSpecializeArgs *args) {}
    virtual void postAppSpecialize(const AppSpecializeArgs *args) {}
    virtual void preServerSpecialize(ServerSpecializeArgs *args) {}
    virtual void postServerSpecialize(const ServerSpecializeArgs *args) {}
};

} // namespace zygisk

#define REGISTER_ZYGISK_MODULE(MODULE_CLASS) extern "C" __attribute__((visibility("default"))) zygisk::ModuleBase *zygisk_module_entry(zygisk::Api *api) {     return new MODULE_CLASS(api); }
