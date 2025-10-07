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

template <typename T, typename E>
class Result {

    struct OkTag {};
    struct ErrTag {};

    union {
        T value;
        E error;
    };
    bool is_ok;
    
    explicit Result(OkTag, T&& val) : value(std::move(val)), is_ok(true) {}
    explicit Result(ErrTag, E&& err) : error(std::move(err)), is_ok(false) {}

    // 日志和终止钩子
    static std::function<void(const std::string&)> log_hook;
    static std::function<void()> terminate_hook;
    static std::mutex hook_mutex;

    // 内部日志方法
    void logError(const std::string& message) const {
        std::lock_guard<std::mutex> lock(hook_mutex);
        if (log_hook) {
            log_hook(message);
        } else {
            // 默认行为：输出到 std::cerr
            std::cerr << message << std::endl;
        }
    }

    // 终止程序
    void terminateProgram() const {
        std::lock_guard<std::mutex> lock(hook_mutex);
        if (terminate_hook) {
            terminate_hook();
        } else {
            std::terminate();
        }
    }

    // 错误信息格式化
    std::string formatError(const std::string& context) const {
        std::string message;
        if (!context.empty()) {
            message = context + ": ";
        }
        
        if (!is_ok) {
            // 错误到字符串转换
            message += errorToString(error);
        } else {
            message += "Attempted to unwrapErr an Ok value";
        }
        return message;
    }

    // 错误到字符串的转换
    template<typename U>
    static std::string errorToString(const U& err) {
        // 通用实现：使用流操作符
        std::ostringstream oss;
        oss << err;
        return oss.str();
    }

    // 针对常见类型的特化
    static std::string errorToString(const std::string& err) {
        return err;
    }

    static std::string errorToString(const char* err) {
        return err ? err : "nullptr error";
    }

public:
    // 工厂方法
    static Result Ok(T val) {
        return Result(OkTag{}, std::move(val));
    }
    
    static Result Err(E err) {
        return Result(ErrTag{}, std::move(err));
    }

    // 钩子管理接口
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

    ~Result() {
        if (is_ok) value.~T();
        else error.~E();
    }

    // 禁用拷贝
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    // 移动语义
    Result(Result&& other) noexcept : is_ok(other.is_ok) {
        if (is_ok) new (&value) T(std::move(other.value));
        else new (&error) E(std::move(other.error));
    }

    Result& operator=(Result&& other) noexcept {
        if (this != &other) {
            this->~Result();
            is_ok = other.is_ok;
            if (is_ok) new (&value) T(std::move(other.value));
            else new (&error) E(std::move(other.error));
        }
        return *this;
    }

    // 状态检查
    bool isOk() const { return is_ok; }
    bool isErr() const { return !is_ok; }

    // 改进的解包方法 - 使用钩子系统

    // 基础解包 - 终止程序但记录日志
    T unwrap(const std::string& context = "") {
        if (is_ok) return std::move(value);
        
        auto message = formatError(context);
        logError("FATAL: Attempted to unwrap an Err value - " + message);
        terminateProgram();
        
        return T{}; // 永不执行
    }

    E unwrapErr(const std::string& context = "") {
        if (!is_ok) return std::move(error);
        
        auto message = formatError(context);
        logError("FATAL: Attempted to unwrapErr an Ok value - " + message);
        terminateProgram();
        
        return E{};
    }

    // 安全解包 - 记录日志但继续执行
    T unwrapOrLog(const std::string& context = "", T default_val = T{}) {
        if (is_ok) return std::move(value);
        
        auto message = formatError(context);
        logError("RECOVERABLE: " + message);
        return default_val;
    }

    // 检查后再解包（最安全的方式）
    T unwrapChecked() {
        if (!is_ok) {
            logError("Warning: Attempted to unwrapChecked an Err value");
            return T{};
        }
        return std::move(value);
    }

    // 期望解包 - 类似 Rust 的 expect
    T expect(const std::string& expectation) {
        if (is_ok) return std::move(value);
        
        auto message = "Expectation failed: " + expectation + ". " + formatError("");
        logError("FATAL: " + message);
        terminateProgram();
        
        return T{};
    }

    // 安全解包方法
    T unwrapOr(T default_val) {
        return is_ok ? std::move(value) : default_val;
    }

    template<typename F>
    T unwrapOrElse(F fallback) {
        return is_ok ? std::move(value) : fallback();
    }

    // 模式匹配
    template<typename U, typename V>
    void match(U ok, V err) {
        if (is_ok) ok(value);
        else err(error);
    }

    // 链式操作 - map转换
    template<typename U>
    auto map(U mapper) -> Result<decltype(mapper(std::declval<T>())), E> {
        typedef decltype(mapper(std::declval<T>())) ReturnType;
        if (is_ok) {
            return Result<ReturnType, E>::Ok(mapper(std::move(value)));
        }
        return Result<ReturnType, E>::Err(std::move(error));
    }

    // 链式操作 - map_error错误转换
    template<typename F>
    auto mapError(F mapper) -> Result<T, decltype(mapper(std::declval<E>()))> {
        typedef decltype(mapper(std::declval<E>())) ErrorType;
        if (!is_ok) {
            return Result<T, ErrorType>::Err(mapper(std::move(error)));
        }
        return Result<T, ErrorType>::Ok(std::move(value));
    }

    // 组合操作 - and_then (Monadic bind)
    template<typename F>
    auto andThen(F f) -> decltype(f(std::declval<T>())) {
        typedef decltype(f(std::declval<T>())) NextResult;
        if (is_ok) {
            return f(std::move(value));
        } else {
            return NextResult::Err(std::move(error));
        }
    }

    // 组合操作 - or_else错误恢复
    template<typename F>
    auto orElse(F f) -> decltype(f(std::declval<E>())) {
        typedef decltype(f(std::declval<E>())) NextResult;
        if (!is_ok) {
            return f(std::move(error));
        } else {
            return NextResult::Ok(std::move(value));
        }
    }
};

// 静态成员定义
template<typename T, typename E>
std::function<void(const std::string&)> Result<T, E>::log_hook = nullptr;

template<typename T, typename E>
std::function<void()> Result<T, E>::terminate_hook = nullptr;

template<typename T, typename E>
std::mutex Result<T, E>::hook_mutex;

