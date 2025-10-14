#pragma once
#include <utility>
#include <string>
#include <iostream>
#include <sstream>
#include <functional>
#include <stdexcept>
#include <mutex>
#include <type_traits>

// ---------------------------
// Base (general T) Result<T,E>
// ---------------------------
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

    // hooks
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
        std::ostringstream oss; oss << err; return oss.str();
    }
    static std::string errorToString(const std::string& err) { return err; }
    static std::string errorToString(const char* err) { return err ? err : "nullptr error"; }

    void destroy() {
        if (is_ok) storage.value.~T();
        else storage.error.~E();
    }

public:
    // factory
    static Result Ok(T val) { return Result(OkTag{}, std::move(val)); }
    static Result Err(E err) { return Result(ErrTag{}, std::move(err)); }

    static void setLogHook(std::function<void(const std::string&)> hook) {
        std::lock_guard<std::mutex> lock(hook_mutex); log_hook = std::move(hook);
    }
    static void setTerminateHook(std::function<void()> hook) {
        std::lock_guard<std::mutex> lock(hook_mutex); terminate_hook = std::move(hook);
    }
    static void clearHooks() {
        std::lock_guard<std::mutex> lock(hook_mutex); log_hook = nullptr; terminate_hook = nullptr;
    }

    ~Result() { destroy(); }

    // non-copyable
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    // movable
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

    // unwrap (fatal on Err)
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

    // unwrapOrLog: fallback default (forwarding)
    template<typename U = T,
             typename = typename std::enable_if<std::is_constructible<T, U&&>::value>::type>
    T unwrapOrLog(const std::string& context = "", U&& default_val = U{}) {
        if (is_ok) return std::move(storage.value);
        auto message = formatError(context);
        logError("RECOVERABLE: " + message);
        return T(std::forward<U>(default_val));
    }

    // unwrapChecked: returns default-constructed T on Err (document requirement)
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

    // unwrapOr with forwarding to support move-only types
    template<typename U,
             typename = typename std::enable_if<std::is_constructible<T, U&&>::value>::type>
    T unwrapOr(U&& default_val) {
        if (is_ok) return std::move(storage.value);
        return T(std::forward<U>(default_val));
    }

    // unwrapOrElse (deferred factory)
    template<typename F>
    T unwrapOrElse(F fallback) {
        return is_ok ? std::move(storage.value) : fallback();
    }

    // match (observe)
    template<typename U, typename V>
    void match(U ok, V err) {
        if (is_ok) ok(storage.value);
        else err(storage.error);
    }

    // map: mapper(T&&) -> U
    template<typename Mapper,
             typename ReturnType = decltype(std::declval<Mapper>()(std::declval<T&&>()))>
    auto map(Mapper mapper) -> Result<ReturnType, E> {
        if (is_ok) {
            return Result<ReturnType, E>::Ok(mapper(std::move(storage.value)));
        }
        return Result<ReturnType, E>::Err(std::move(storage.error));
    }

    // mapError
    template<typename F,
             typename ErrorType = decltype(std::declval<F>()(std::declval<E&&>()))>
    auto mapError(F mapper) -> Result<T, ErrorType> {
        if (!is_ok) {
            return Result<T, ErrorType>::Err(mapper(std::move(storage.error)));
        }
        return Result<T, ErrorType>::Ok(std::move(storage.value));
    }

    // andThen (monadic bind)
    template<typename F>
    auto andThen(F f) -> decltype(f(std::declval<T&&>())) {
        typedef decltype(f(std::declval<T&&>())) NextResult;
        if (is_ok) return f(std::move(storage.value));
        else return NextResult::Err(std::move(storage.error));
    }

    // orElse
    template<typename F>
    auto orElse(F f) -> decltype(f(std::declval<E&&>())) {
        typedef decltype(f(std::declval<E&&>())) NextResult;
        if (!is_ok) return f(std::move(storage.error));
        else return NextResult::Ok(std::move(storage.value));
    }
};

// static members
template<typename T, typename E>
std::function<void(const std::string&)> Result<T, E>::log_hook = nullptr;
template<typename T, typename E>
std::function<void()> Result<T, E>::terminate_hook = nullptr;
template<typename T, typename E>
std::mutex Result<T, E>::hook_mutex;

// -----------------------------------
// Specialization Result<void, E>
// -----------------------------------
template <typename E>
class Result<void, E> {
    struct OkTag {};
    struct ErrTag {};

    E error;          // only store error
    bool is_ok;

    explicit Result(OkTag) : error(), is_ok(true) {}
    explicit Result(ErrTag, E&& err) : error(std::move(err)), is_ok(false) {}

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
        if (!is_ok) message += errorToString(error);
        else message += "Attempted to unwrapErr an Ok value";
        return message;
    }

    template<typename U>
    static std::string errorToString(const U& err) {
        std::ostringstream oss; oss << err; return oss.str();
    }
    static std::string errorToString(const std::string& err) { return err; }
    static std::string errorToString(const char* err) { return err ? err : "nullptr error"; }

public:
    static Result Ok() { return Result(OkTag{}); }
    static Result Err(E err) { return Result(ErrTag{}, std::move(err)); }

    static void setLogHook(std::function<void(const std::string&)> hook) {
        std::lock_guard<std::mutex> lock(hook_mutex); log_hook = std::move(hook);
    }
    static void setTerminateHook(std::function<void()> hook) {
        std::lock_guard<std::mutex> lock(hook_mutex); terminate_hook = std::move(hook);
    }
    static void clearHooks() {
        std::lock_guard<std::mutex> lock(hook_mutex); log_hook = nullptr; terminate_hook = nullptr;
    }

    Result() : error(), is_ok(true) {}
    ~Result() = default;

    // non-copyable
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    // movable
    Result(Result&& o) noexcept : error(std::move(o.error)), is_ok(o.is_ok) {}
    Result& operator=(Result&& o) noexcept { if (this!=&o){ error = std::move(o.error); is_ok = o.is_ok; } return *this; }

    bool isOk() const { return is_ok; }
    bool isErr() const { return !is_ok; }

    // unwrap: void on success, fatal on err
    void unwrap(const std::string& context = "") {
        if (is_ok) return;
        auto message = formatError(context);
        logError("FATAL: Attempted to unwrap an Err value - " + message);
        terminateProgram();
    }

    E unwrapErr(const std::string& context = "") {
        if (!is_ok) return std::move(error);
        auto message = formatError(context);
        logError("FATAL: Attempted to unwrapErr an Ok value - " + message);
        terminateProgram();
        return E{};
    }

    // unwrapOrLog: no-op for void; but we still log if error
    void unwrapOrLog(const std::string& context = "") {
        if (is_ok) return;
        auto message = formatError(context);
        logError("RECOVERABLE: " + message);
    }

    // unwrapOrElse: accepts fallback callable invoked on Err (executes then returns void)
    template<typename F>
    void unwrapOrElse(F fallback) {
        if (is_ok) return;
        fallback();
    }

    // expect: fatal with message if Err
    void expect(const std::string& expectation) {
        if (is_ok) return;
        auto message = "Expectation failed: " + expectation + ". " + formatError("");
        logError("FATAL: " + message);
        terminateProgram();
    }

    // map when T=void: mapper() -> U; returns Result<U,E>
    template<typename Mapper,
             typename ReturnType = decltype(std::declval<Mapper>()())>
    auto map(Mapper mapper) -> Result<ReturnType, E> {
        if (is_ok) {
            return Result<ReturnType, E>::Ok(mapper());
        }
        return Result<ReturnType, E>::Err(std::move(error));
    }

    // andThen when T=void: f() -> Result<U,E>
    template<typename F>
    auto andThen(F f) -> decltype(f()) {
        typedef decltype(f()) NextResult;
        if (is_ok) return f();
        else return NextResult::Err(std::move(error));
    }

    // orElse
    template<typename F>
    auto orElse(F f) -> decltype(f(std::declval<E&&>())) {
        typedef decltype(f(std::declval<E&&>())) NextResult;
        if (!is_ok) return f(std::move(error));
        else return NextResult::Ok();
    }
};

// static members for void specialization
template<typename E>
std::function<void(const std::string&)> Result<void, E>::log_hook = nullptr;
template<typename E>
std::function<void()> Result<void, E>::terminate_hook = nullptr;
template<typename E>
std::mutex Result<void, E>::hook_mutex;

