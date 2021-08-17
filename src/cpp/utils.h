#pragma once
#include <optional>
#include <mutex>
#include <condition_variable>
#include <deque>

#ifdef _WIN32
#include <direct.h>
#define cwd _getcwd
#define cd _chdir
#else
#include "unistd.h"
#define cwd getcwd
#define cd chdir
#endif

#define TRY_METHOD(method) \
    try { \
        method; \
    } catch (std::exception &e) { \
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException(); \
    } catch (...) { \
        Napi::Error::New(info.Env(), "Unexpected error.").ThrowAsJavaScriptException(); \
    }

class NapiUtil {

public:
    static inline bool isUndefined(Napi::Object object, const std::string &property) {
        return object.Get(property).IsUndefined();
    }

    static inline int getInt(Napi::Object object, const std::string &property) {
        auto value = object.Get(property);
        if (value.IsUndefined()) {
            throw std::invalid_argument(property + " should not be undefined");
        }
        return value.As<Napi::Number>();
    }

    static inline std::optional<int> getIntOptional(Napi::Object object, const std::string &property) {
        auto value = object.Get(property);
        return value.IsUndefined() ? std::nullopt : std::optional<int>{value.As<Napi::Number>()};
    }

    static inline std::string getString(Napi::Object object, const std::string &property) {
        auto value = object.Get(property);
        if (value.IsUndefined()) {
            throw std::invalid_argument(property + " should not be undefined");
        }
        return value.As<Napi::String>();
    }

    static inline std::optional<std::string> getStringOptional(Napi::Object object, const std::string &property) {
        auto value = object.Get(property);
        return value.IsUndefined() ? std::nullopt : std::optional<std::string>{value.As<Napi::String>()};
    }

    static inline bool getBoolean(Napi::Object object, const std::string &property) {
        auto value = object.Get(property);
        if (value.IsUndefined()) {
            throw std::invalid_argument(property + " should not be undefined");
        }
        return value.As<Napi::Boolean>();
    }

    static inline std::optional<bool> getBooleanOptional(Napi::Object object, const std::string &property) {
        auto value = object.Get(property);
        return value.IsUndefined() ? std::nullopt : std::optional<bool>{value.As<Napi::Boolean>()};
    }

    static inline std::vector<std::string> getStringArray(const Napi::Array &array) {
        std::vector<std::string> result;
        for (uint32_t i = 0; i < array.Length(); ++i) {
            result.push_back((std::string)array.Get(i).As<Napi::String>());
        }
        return result;
    }
};

template <typename T>
class queue
{
private:
    std::mutex              d_mutex;
    std::condition_variable d_condition;
    std::deque<T>           d_queue;
public:
    void push(T const& value) {
        {
            std::unique_lock<std::mutex> lock(this->d_mutex);
            d_queue.push_front(value);
        }
        this->d_condition.notify_one();
    }
    T pop() {
        std::unique_lock<std::mutex> lock(this->d_mutex);
        this->d_condition.wait(lock, [=]{ return !this->d_queue.empty(); });
        T rc(std::move(this->d_queue.back()));
        this->d_queue.pop_back();
        return rc;
    }
};

static inline std::string getCurrentPath() {
    char path[256] = {};
    cwd(path, 256);
    return std::string(path);
}

static inline void setCurrentPath(const std::string& path) {
    cd(path.c_str());
}
