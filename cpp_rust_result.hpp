#pragma once
#include <utility>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <mutex>
#include <iomanip>
#include <type_traits>

template <typename T, typename E>
class Result {

    struct OkTag {};
    struct ErrTag {};

    union Storage {
        T value;
        E error;
        Storage() {}
        ~Storage() {}
    } storage;

    bool is_ok;

    explicit Result(OkTag, T&& val) : is_ok(true) { new (&storage.value) T(std::move(val)); }
    explicit Result(ErrTag, E&& err) : is_ok(false) { new (&storage.error) E(std::move(err)); }

    // 日志和终止钩子
    static std::function<void(const std::string&)> log_hook;
    static std::function<void()> terminate_hook;
    static std::mutex hook_mutex;

    void logError(const std::string& message) const {
        std::lock_guard<std::mutex> lock(hook_mutex);
        if (log_hook) log_hook(message);
        else std::cerr << message << std::endl;
    }

    void terminateProgram() const {
        std::lock_guard<std::mutex> lock(hook_mutex);
        if (terminate_hook) terminate_hook();
        else std::terminate();
    }

    std::string formatError(const std::string& context) const {
        std::string message;
        if (!context.empty()) message = context + ": ";
        if (!is_ok) message += errorToString(storage.error);
        else message += "Attempted to unwrapErr an Ok value";
        return message;
    }

    template<typename U>
    static std::string errorToString(const U& err) {
        std::ostringstream oss;
        oss << err;
        return oss.str();
    }
    static std::string errorToString(const std::string& err) { return err; }
    static std::string errorToString(const char* err) { return err ? err : "nullptr error"; }

    // helpers for destructor
    void destroy() {
        if (is_ok) storage.value.~T();
        else storage.error.~E();
    }

public:
    // 工厂方法
    static Result Ok(T val) { return Result(OkTag{}, std::move(val)); }
    static Result Err(E err) { return Result(ErrTag{}, std::move(err)); }

    // 钩子管理
    static void setLogHook(std::function<void(const std::string&)> hook) {
        std::lock_guard<std::mutex> lock(hook_mutex);
        log_hook = std::move(hook);
    }
    static void setTerminateHook(std::function<void()> hook) {
        std::lock_guard<std::mutex> lock(hook_mutex);
        terminate_hook = std::move(hook);
    }
    static void clearHooks() {
        std::lock_guard<std::mutex> lock(hook_mutex);
        log_hook = nullptr;
        terminate_hook = nullptr;
    }

    ~Result() { destroy(); }

    // 禁用拷贝
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    // 移动语义
    Result(Result&& other) noexcept : is_ok(other.is_ok) {
        if (is_ok) new (&storage.value) T(std::move(other.storage.value));
        else new (&storage.error) E(std::move(other.storage.error));
    }
    Result& operator=(Result&& other) noexcept {
        if (this != &other) {
            destroy();
            is_ok = other.is_ok;
            if (is_ok) new (&storage.value) T(std::move(other.storage.value));
            else new (&storage.error) E(std::move(other.storage.error));
        }
        return *this;
    }

    bool isOk() const { return is_ok; }
    bool isErr() const { return !is_ok; }

    // unwrap / expect / unwrapErr (消费 value 或 error，move)
    T unwrap(const std::string& context = "") {
        if (is_ok) return std::move(storage.value);
        auto message = formatError(context);
        logError("FATAL: Attempted to unwrap an Err value - " + message);
        terminateProgram();
        return T{};
    }

    E unwrapErr(const std::string& context = "") {
        if (!is_ok) return std::move(storage.error);
        auto message = formatError(context);
        logError("FATAL: Attempted to unwrapErr an Ok value - " + message);
        terminateProgram();
        return E{};
    }

    // unwrapChecked - 返回默认构造的 T 如果不是 Ok（注意文档说明需要 T 默认构造）
    T unwrapChecked() {
        if (!is_ok) {
            logError("Warning: Attempted to unwrapChecked an Err value");
            return T{};
        }
        return std::move(storage.value);
    }

    T expect(const std::string& expectation) {
        if (is_ok) return std::move(storage.value);
        auto message = "Expectation failed: " + expectation + ". " + formatError("");
        logError("FATAL: " + message);
        terminateProgram();
        return T{};
    }

    // unwrapOr: 通用转发实现（仅在 U 可构造出 T 时启用）
    template<typename U,
             typename = typename std::enable_if<std::is_constructible<T, U&&>::value>::type>
    T unwrapOr(U&& default_val) {
        if (is_ok) return std::move(storage.value);
        return T(std::forward<U>(default_val));
    }

    // unwrapOrLog: 同样用转发并记录可恢复错误
    template<typename U = T,
             typename = typename std::enable_if<std::is_constructible<T, U&&>::value>::type>
    T unwrapOrLog(const std::string& context = "", U&& default_val = U{}) {
        if (is_ok) return std::move(storage.value);
        auto message = formatError(context);
        logError("RECOVERABLE: " + message);
        return T(std::forward<U>(default_val));
    }

    // 延迟计算默认值（对 move-only 类型安全）
    template<typename F>
    T unwrapOrElse(F fallback) {
        return is_ok ? std::move(storage.value) : fallback();
    }

    // map / mapError: 使用 T&& 在 decltype 推导以支持接收右值引用的可调用对象
    template<typename Mapper>
    auto map(Mapper mapper) -> Result<decltype(mapper(std::declval<T&&>())), E> {
        typedef decltype(mapper(std::declval<T&&>())) ReturnType;
        if (is_ok) {
            return Result<ReturnType, E>::Ok(mapper(std::move(storage.value)));
        }
        return Result<ReturnType, E>::Err(std::move(storage.error));
    }

    template<typename F>
    auto mapError(F mapper) -> Result<T, decltype(mapper(std::declval<E&&>()))> {
        typedef decltype(mapper(std::declval<E&&>())) ErrorType;
        if (!is_ok) {
            return Result<T, ErrorType>::Err(mapper(std::move(storage.error)));
        }
        return Result<T, ErrorType>::Ok(std::move(storage.value));
    }

    // andThen / orElse (monadic bind)
    template<typename F>
    auto andThen(F f) -> decltype(f(std::declval<T&&>())) {
        typedef decltype(f(std::declval<T&&>())) NextResult;
        if (is_ok) {
            return f(std::move(storage.value));
        } else {
            return NextResult::Err(std::move(storage.error));
        }
    }

    template<typename F>
    auto orElse(F f) -> decltype(f(std::declval<E&&>())) {
        typedef decltype(f(std::declval<E&&>())) NextResult;
        if (!is_ok) {
            return f(std::move(storage.error));
        } else {
            return NextResult::Ok(std::move(storage.value));
        }
    }

    // match: 观察语义（不会移动），传递 const T& / const E&
    template<typename OkFn, typename ErrFn>
    void match(OkFn ok, ErrFn err) const {
        if (is_ok) ok(storage.value);
        else err(storage.error);
    }

    // matchConsume: 消费语义，传递 T&& / E&&（用于需要移动 value 的回调）
    template<typename OkFn, typename ErrFn>
    void matchConsume(OkFn ok, ErrFn err) {
        if (is_ok) ok(std::move(storage.value));
        else err(std::move(storage.error));
    }

    // 模仿 Rust 风格的链式 API：or_else 等已提供
};

// 静态成员定义
template<typename T, typename E>
std::function<void(const std::string&)> Result<T, E>::log_hook = nullptr;

template<typename T, typename E>
std::function<void()> Result<T, E>::terminate_hook = nullptr;

template<typename T, typename E>
std::mutex Result<T, E>::hook_mutex;

