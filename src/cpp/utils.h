#pragma once
#include <optional>

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
};