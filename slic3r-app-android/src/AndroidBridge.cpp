#include "AndroidBridge.hpp"

#include <SDL.h>
#include <SDL_system.h>
#include <jni.h>

namespace Slic3r::App::Android {

namespace {

constexpr const char* ACTIVITY_CLASS = "com/prusa3d/slicer/PrusaSlicerActivity";

struct Jni
{
    JNIEnv* env;
    jclass cls;

    Jni()
    {
        env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        jclass local = env->GetObjectClass(activity);
        cls = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        env->DeleteLocalRef(activity);
    }
    ~Jni() { env->DeleteGlobalRef(cls); }

    jstring str(const std::string& s) { return env->NewStringUTF(s.c_str()); }

    std::string to_string(jstring s)
    {
        if (s == nullptr)
            return {};
        const char* chars = env->GetStringUTFChars(s, nullptr);
        std::string result(chars);
        env->ReleaseStringUTFChars(s, chars);
        env->DeleteLocalRef(s);
        return result;
    }

    jmethodID method(const char* name, const char* signature)
    {
        return env->GetStaticMethodID(cls, name, signature);
    }
};

} // namespace

std::vector<std::string> pick_files_to_open(const std::string& title, const std::string& mime_types, bool multiple)
{
    Jni jni;
    jmethodID m = jni.method("pickFilesToOpen", "(Ljava/lang/String;Ljava/lang/String;Z)[Ljava/lang/String;");
    auto array = static_cast<jobjectArray>(
        jni.env->CallStaticObjectMethod(jni.cls, m, jni.str(title), jni.str(mime_types), jboolean(multiple)));
    std::vector<std::string> paths;
    if (array != nullptr) {
        const jsize n = jni.env->GetArrayLength(array);
        for (jsize i = 0; i < n; ++i)
            paths.push_back(jni.to_string(static_cast<jstring>(jni.env->GetObjectArrayElement(array, i))));
        jni.env->DeleteLocalRef(array);
    }
    return paths;
}

std::optional<std::string> pick_file_to_save(const std::string& title, const std::string& suggested_name, const std::string& mime_type)
{
    Jni jni;
    jmethodID m = jni.method("pickFileToSave", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    auto result = static_cast<jstring>(
        jni.env->CallStaticObjectMethod(jni.cls, m, jni.str(title), jni.str(suggested_name), jni.str(mime_type)));
    if (result == nullptr)
        return std::nullopt;
    return jni.to_string(result);
}

std::optional<std::string> input_dialog(const std::string& title, const std::string& text, const std::string& default_value)
{
    Jni jni;
    jmethodID m = jni.method("inputDialog", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    auto result = static_cast<jstring>(
        jni.env->CallStaticObjectMethod(jni.cls, m, jni.str(title), jni.str(text), jni.str(default_value)));
    if (result == nullptr)
        return std::nullopt;
    return jni.to_string(result);
}

int choice_dialog(const std::string& title, const std::vector<std::string>& values)
{
    Jni jni;
    jclass string_class = jni.env->FindClass("java/lang/String");
    jobjectArray array = jni.env->NewObjectArray(jsize(values.size()), string_class, nullptr);
    for (size_t i = 0; i < values.size(); ++i)
        jni.env->SetObjectArrayElement(array, jsize(i), jni.str(values[i]));
    jmethodID m = jni.method("choiceDialog", "(Ljava/lang/String;[Ljava/lang/String;)I");
    const int index = jni.env->CallStaticIntMethod(jni.cls, m, jni.str(title), array);
    jni.env->DeleteLocalRef(array);
    return index;
}

int diff_dialog(const std::string& title, const std::string& message, const std::vector<std::string>& lines,
                bool can_transfer, std::vector<bool>& checked)
{
    Jni jni;
    jclass string_class = jni.env->FindClass("java/lang/String");
    jobjectArray array = jni.env->NewObjectArray(jsize(lines.size()), string_class, nullptr);
    for (size_t i = 0; i < lines.size(); ++i)
        jni.env->SetObjectArrayElement(array, jsize(i), jni.str(lines[i]));
    jbooleanArray jchecked = jni.env->NewBooleanArray(jsize(lines.size()));
    jmethodID m = jni.method("diffDialog", "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Z[Z)I");
    const int result = jni.env->CallStaticIntMethod(jni.cls, m, jni.str(title), jni.str(message), array,
                                                    jboolean(can_transfer), jchecked);
    checked.assign(lines.size(), false);
    if (!lines.empty()) {
        std::vector<jboolean> values(lines.size());
        jni.env->GetBooleanArrayRegion(jchecked, 0, jsize(lines.size()), values.data());
        for (size_t i = 0; i < lines.size(); ++i)
            checked[i] = values[i] != JNI_FALSE;
    }
    jni.env->DeleteLocalRef(jchecked);
    jni.env->DeleteLocalRef(array);
    return result;
}

void open_url(const std::string& url)
{
    SDL_OpenURL(url.c_str());
}

void open_folder(const std::string& path)
{
    Jni jni;
    jmethodID m = jni.method("openFolder", "(Ljava/lang/String;)V");
    jni.env->CallStaticVoidMethod(jni.cls, m, jni.str(path));
}

void set_side_bar_preferences_selected(bool selected)
{
    Jni jni;
    jmethodID m = jni.method("setPreferencesSelected", "(Z)V");
    jni.env->CallStaticVoidMethod(jni.cls, m, jboolean(selected));
}

void set_log_export(bool enabled)
{
    Jni jni;
    jmethodID m = jni.method("setLogExport", "(Z)V");
    jni.env->CallStaticVoidMethod(jni.cls, m, jboolean(enabled));
}

std::string files_dir()
{
    const char* path = SDL_AndroidGetInternalStoragePath();
    return path != nullptr ? path : "";
}

} // namespace Slic3r::App::Android
