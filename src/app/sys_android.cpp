// Android side of sys.h: the app's folders, links and the update check (the files part: sys_posix.cpp).
#include "sys.h"
#include <SDL.h>
#include <jni.h>

std::wstring exe_dir() { return data_dir(); }

std::wstring data_dir() {
    static std::wstring dir;
    if (dir.empty()) {
        const char* p = SDL_AndroidGetInternalStoragePath();
        dir = from_utf8(p ? p : ".");
    }
    return dir;
}

// files/roms (where the ROM picker copies to) and, for adb or a file manager, the app's external files folder
// (Android/data/com.darhkfox.mickeydonald/files) and its "roms" subfolder.
std::vector<std::wstring> rom_dirs() {
    std::vector<std::wstring> dirs = {path_join(data_dir(), L"roms")};
    if (const char* ext = SDL_AndroidGetExternalStoragePath()) {
        dirs.push_back(from_utf8(ext));
        dirs.push_back(path_join(from_utf8(ext), L"roms"));
    }
    return dirs;
}

std::wstring export_dir() {
    const char* p = SDL_AndroidGetExternalStoragePath();
    return p ? from_utf8(p) : data_dir();
}

void open_url(const char* url) { SDL_OpenURL(url); }

// Through MDActivity.httpGet (Java's HttpURLConnection), on the calling thread (SDL attaches it to the JVM).
bool http_get(const char* url, std::string& out) {
    out.clear();
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!env || !act) return false;
    jclass cls = env->GetObjectClass(act);
    if (jmethodID m = env->GetStaticMethodID(cls, "httpGet", "(Ljava/lang/String;)Ljava/lang/String;")) {
        jstring u = env->NewStringUTF(url);
        jstring r = (jstring)env->CallStaticObjectMethod(cls, m, u);
        if (r && !env->ExceptionCheck()) {
            if (const char* c = env->GetStringUTFChars(r, nullptr)) {
                out = c;
                env->ReleaseStringUTFChars(r, c);
            }
        }
        if (r) env->DeleteLocalRef(r);
        env->DeleteLocalRef(u);
    }
    env->ExceptionClear();
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(act);
    return !out.empty();
}
