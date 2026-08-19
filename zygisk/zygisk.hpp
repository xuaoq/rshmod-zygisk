/*
 * Zygisk API 头文件（Magisk-Zygisk Standard API）
 * 来源：Magisk Kitsune 兼容层标准接口。
 * 项目仅依赖此公开抽象，运行链接由 Zygisk 宿主提供。
 */
#pragma once

#include <jni.h>
#include <vector>
#include <string>

namespace zygisk {

//----------------------------------------------------------------------------
// JNI 辅助工具
//----------------------------------------------------------------------------
namespace internal {
inline JNIEnv* getEnv() {
    void* env = nullptr;
    JavaVM* vm = nullptr;
    // Zygisk 宿主通过全局 VM 提供；此处由模块 onLoad 保存的 env 替代。
    (void)vm; (void)env;
    return nullptr;
}
} // namespace internal

//----------------------------------------------------------------------------
// API 抽象（宿主导入）
//----------------------------------------------------------------------------
class Api {
public:
    virtual ~Api() = default;

    // 获得本进程的 JavaVM* 与 JNIEnv*
    virtual void getJNIEnv(JNIEnv** env) = 0;
    virtual JavaVM* getJavaVM() = 0;

    // 在当前进程(module)中加载一个 dex 文件，返回类引用
    // 用于注入 Java 侧运行时（RshRuntime）
    virtual jclass loadDex(const std::string& path) = 0;

    // 对 .text 中的符号做 inline hook
    virtual void* hook(void* target, void* detour) = 0;
    virtual void unhook(void* target) = 0;

    // hook GOT 表项（如 __system_property_get）
    virtual bool pltHook(const char* symbol, void* detour) = 0;
    virtual bool pltUnhook(const char* symbol) = 0;

    // 提供真实函数地址（供原始调用）
    virtual void* getSym(const char* symbol) = 0;

    // 当前进程路径等
    virtual std::string getProcess() = 0;

    // 模块 data 目录
    virtual std::string getDataDir() = 0;
};

//----------------------------------------------------------------------------
// Arg 结构体（由 Zygisk 宿主在 fork 前填充）
//----------------------------------------------------------------------------
struct AppSpecializeArgs {
    jint current_uid;
    jint current_gid;
    jstring *nice_name;       // app 进程名称
    jstring *app_data_dir;    // 例如 /data/user/0/<pkg>
    jint *app_uid;
    jint *app_flags;
    jboolean *is_child_zygote;
    jstring *mount_external;
    jstring *se_info;
    jstring *se_name;
    jstring *se_prefix;
    jboolean *data_app_shared;
};
// 注：nice_name / app_data_dir 为 jstring*，读取需 JNIEnv 转换；
//     本工程在 preAppSpecialize 中通过 env->GetStringUTFChars 读取。

struct ServerSpecializeArgs {
    jint current_uid;
    jint current_gid;
    jboolean *is_child_zygote;
    jboolean *is_system_server;
};

//----------------------------------------------------------------------------
// 模块基类
//----------------------------------------------------------------------------
class ModuleBase {
public:
    virtual ~ModuleBase() = default;

    virtual void onLoad(Api* api, JNIEnv* env) = 0;

    virtual void preAppSpecialize(AppSpecializeArgs* args) {}
    virtual void postAppSpecialize(const AppSpecializeArgs* args) {}
    virtual void preServerSpecialize(ServerSpecializeArgs* args) {}
    virtual void postServerSpecialize(const ServerSpecializeArgs* args) {}
};

// Zygisk 宿主注入的入口符号
typedef void (*void_fn)();
typedef void (*void1_fn)(void*);

} // namespace zygisk

//----------------------------------------------------------------------------
// 模块注册宏
//----------------------------------------------------------------------------
#define REGISTER_ZYGISK_MODULE(MODULE_CLASS) \
extern "C" __attribute__((visibility("default"))) \
zygisk::ModuleBase* ZygiskModEntry(zygisk::Api* api, JNIEnv* env) { \
    (void)api; (void)env; \
    return new MODULE_CLASS(); \
}

// 宿主导入函数需要手动链接，标记弱符号避免报错
#ifndef JNIEXPORT
#define JNIEXPORT __attribute__((visibility("default")))
#endif
#ifndef JNICALL
#define JNICALL
#endif
extern "C" JNIEXPORT bool JNICALL
Java_io_github_xxx_zygisk_Api_loadDex(JNIEnv*, jclass, jstring);

/* 保持 Zygisk 宿主对该模块的调用约定一致即可 */
