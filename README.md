# Result<T, E> 类型说明文档 - C++11 实现

## 第一部分：应用场景

### 错误处理范式演进
在传统的C++编程中，错误处理主要依赖以下方式：
- **异常机制**：通过`try/catch`块处理，但存在性能开销和代码结构侵入性强的问题
- **错误码返回**：函数返回特定错误码，调用方需要手动检查，容易遗漏错误处理
- **输出参数**：通过引用或指针传递错误状态，使函数签名复杂化

`Result<T, E>`类型提供了**函数式错误处理**的解决方案，特别适用于以下场景：

### 典型应用场景

#### 1. 数学运算验证
```cpp
// 传统方式
double result;
if (!safe_divide(10.0, 0.0, result)) {
    std::cerr << "Division by zero" << std::endl;
}

// Result方式
auto result = divide(10.0, 0.0);
result.match(
    [](double v) { /* 处理成功 */ },
    [](const std::string& e) { /* 处理错误 */ }
);
```

#### 2. I/O 操作
```cpp
// 文件读取、网络请求、数据库操作等可能失败的操作
auto content = readFile("config.json");
if (content.isErr()) {
    // 统一的错误处理路径
    return;
}
```

#### 3. 数据处理流水线
```cpp
// 链式操作，避免深层嵌套的条件判断
auto processed = readFile("data.txt")
    .andThen(parseInput)
    .map(validateData)
    .map(transformData);
```

#### 4. 系统编程
- 资源管理（内存分配、文件句柄）
- 外部服务调用
- 配置解析和验证

## 第二部分：接口调用详解

### 核心接口方法

#### 1. 工厂方法构造
```cpp
// 成功结果
auto success = Result<double, std::string>::Ok(42.0);

// 错误结果  
auto failure = Result<double, std::string>::Err("Invalid input");
```

#### 2. 状态检查
```cpp
Result<double, std::string> result = divide(10, 2);

if (result.isOk()) {
    // 处理成功情况
}

if (result.isErr()) {
    // 处理错误情况
}
```

#### 3. 模式匹配（推荐用法）
```cpp
result.match(
    // Ok分支 - 处理成功值
    [](double value) {
        std::cout << "计算结果: " << value << std::endl;
    },
    // Err分支 - 处理错误
    [](const std::string& error) {
        std::cerr << "计算失败: " << error << std::endl;
    }
);
```

#### 4. 链式转换操作
```cpp
// map: 对成功值进行转换，保持错误状态不变
auto stringResult = divide(10, 2)
    .map([](double d) { return std::to_string(d); });

// 如果divide失败，stringResult保持相同的错误
// 如果成功，将double转换为string
```

#### 5. 组合操作 - andThen
```cpp
// andThen: 链式操作，当前结果为Ok时执行函数
auto result = readFile("data.txt")
    .andThen(parseInput)    // parseInput必须返回Result类型
    .andThen(validateData); // 只有前一步成功才会执行
```

#### 6. 错误恢复操作 - orElse
```cpp
// orElse: 当前结果为Err时执行恢复函数
auto result = readFile("config.json")
    .orElse([](const std::string& error) -> Result<std::string, std::string> {
        // 从错误中恢复，返回默认配置
        return Result<std::string, std::string>::Ok("default config");
    });
```

#### 7. 错误转换 - mapError
```cpp
// mapError: 转换错误类型
auto result = divide(10, 0)
    .mapError([](const std::string& error) {
        return "Math Error: " + error;
    });
```

#### 8. 安全解包
```cpp
auto result = divide(10, 0);

// 提供默认值，避免程序崩溃
double value = result.unwrapOr(0.0);  // 返回0.0而不是崩溃

// 延迟计算的默认值
double value = result.unwrapOrElse([]() { return calculateDefault(); });

// 注意：没有提供类似Rust的unwrap()，强制考虑错误情况
```

### 完整使用示例
```cpp
// 构建数据处理流水线
auto processData(const std::string& filename) -> Result<std::string, std::string> {
    return readFile(filename)
        .andThen(parseInput)
        .map([](const std::string& processed) {
            return "Final: " + processed;
        });
}

int main() {
    processData("input.txt").match(
        [](const std::string& data) {
            std::cout << "处理成功: " << data << std::endl;
        },
        [](const std::string& error) {
            std::cerr << "处理失败: " << error << std::endl;
        }
    );
}
```

## 第三部分：钩子系统详解

### 设计理念

钩子系统允许用户完全控制 `Result` 的日志输出和终止行为，实现了**关注点分离**：
- `Result` 类型专注于错误处理逻辑
- 用户控制日志输出和终止行为

### 钩子接口

#### 1. 日志钩子
```cpp
static void setLogHook(std::function<void(const std::string&)> hook);
```
设置自定义日志处理函数，当 `Result` 需要记录错误时调用。

**使用示例：**
```cpp
// 集成到 spdlog
Result<T, E>::setLogHook([](const std::string& message) {
    spdlog::error("Result Error: {}", message);
});

// 集成到 glog
Result<T, E>::setLogHook([](const std::string& message) {
    LOG(ERROR) << "Result: " << message;
});

// 集成到自定义日志系统
Result<T, E>::setLogHook([](const std::string& message) {
    MyLogger::logError("RESULT_SYSTEM", message);
});
```

#### 2. 终止钩子
```cpp
static void setTerminateHook(std::function<void()> hook);
```
设置自定义终止行为，当 `Result` 遇到不可恢复错误时调用。

**使用示例：**
```cpp
// 优雅终止
Result<T, E>::setTerminateHook([]() {
    std::cout << "执行清理操作..." << std::endl;
    cleanupResources();
    std::exit(1);
});

// 嵌入式系统重启
Result<T, E>::setTerminateHook([]() {
    system_reboot();
});
```

#### 3. 钩子管理
```cpp
static void clearHooks();
```
清除所有钩子，恢复默认行为。

### 解包方法与钩子的关系

#### 严格解包 - 触发终止钩子
```cpp
// unwrap - 失败时记录日志并终止
T unwrap(const std::string& context = "");

// expect - 失败时记录期望信息并终止  
T expect(const std::string& expectation);

// unwrapErr - 错误解包失败时终止
E unwrapErr(const std::string& context = "");
```

#### 安全解包 - 仅触发日志钩子
```cpp
// unwrapOrLog - 记录错误但继续执行
T unwrapOrLog(const std::string& context = "", T default_val = T{});

// unwrapChecked - 记录警告并返回默认值
T unwrapChecked();
```

### 多环境配置示例

#### 嵌入式环境
```cpp
void setupEmbeddedHooks() {
    Result<int, const char*>::setLogHook([](const std::string& message) {
        uart_send("[ERROR] ");
        uart_send(message.c_str());
        uart_send("\n");
        led_blink(ERROR_PATTERN);
    });
    
    Result<int, const char*>::setTerminateHook([]() {
        system_reboot();
    });
}
```

#### 生产服务器环境
```cpp
void setupProductionHooks() {
    Result<std::string, std::string>::setLogHook([](const std::string& message) {
        // 结构化日志
        logger->error(R"({
            "source": "result_system",
            "message": "{}",
            "timestamp": "{}"
        })", message, getCurrentTime());
        
        // 触发告警
        alertSystem.notify("Result failure");
    });
    
    Result<std::string, std::string>::setTerminateHook([]() {
        logger->critical("Application terminating due to Result error");
        metrics::increment("fatal_errors");
        std::exit(1);
    });
}
```

#### 开发测试环境
```cpp
void setupDevelopmentHooks() {
    Result<T, E>::setLogHook([](const std::string& message) {
        std::cout << "🐛 [Result] " << message << std::endl;
    });
    
    Result<T, E>::setTerminateHook([]() {
        std::cout << "❌ 测试中检测到致命错误" << std::endl;
        throw TestException("Result would terminate");
    });
}
```

#### 静默测试环境
```cpp
void setupSilentHooks() {
    // 测试中忽略所有日志和终止
    Result<T, E>::setLogHook([](const std::string&) {});
    Result<T, E>::setTerminateHook([]() {});
}
```

### 线程安全性

钩子系统使用互斥锁保护，确保在多线程环境中的安全使用：

```cpp
static std::mutex hook_mutex;

static void setLogHook(std::function<void(const std::string&)> hook) {
    std::lock_guard<std::mutex> lock(hook_mutex);
    log_hook = std::move(hook);
}
```

### 默认行为

当没有设置自定义钩子时，系统使用合理的默认行为：

```cpp
void logError(const std::string& message) const {
    std::lock_guard<std::mutex> lock(hook_mutex);
    if (log_hook) {
        log_hook(message);  // 用户自定义日志
    } else {
        std::cerr << message << std::endl;  // 默认：输出到标准错误
    }
}

void terminateProgram() const {
    std::lock_guard<std::mutex> lock(hook_mutex);
    if (terminate_hook) {
        terminate_hook();  // 用户自定义终止
    } else {
        std::terminate();  // 默认：终止程序
    }
}
```

## 第四部分：实现原理

### 语言特性运用

#### 1. 模板元编程
```cpp
template <typename T, typename E>
class Result {
    // 支持任意类型的成功值和错误类型
    // 编译时多态，零运行时开销
};
```

#### 2. Union 与手动内存管理
```cpp
union {
    T value;    // 成功时存储T类型值
    E error;    // 失败时存储E类型错误
};
bool is_ok;     // 判别式，标识当前状态

// 手动调用析构函数，避免union的局限性
~Result() {
    if (is_ok) value.~T();
    else error.~E();
}
```

#### 3. 移动语义与资源管理
```cpp
// 禁用拷贝构造，防止重复释放
Result(const Result&) = delete;
Result& operator=(const Result&) = delete;

// 移动构造，支持高效资源转移
Result(Result&& other) noexcept : is_ok(other.is_ok) {
    if (is_ok) new (&value) T(std::move(other.value));
    else new (&error) E(std::move(other.error));
}
```

#### 4. 类型推导与decltype
```cpp
template<typename F>
auto andThen(F f) -> decltype(f(std::declval<T>())) {
    // C++11的decltype用于推导返回类型
    // std::declval在编译时创建类型实例
}
```

### 设计模式与编程范式

#### 1. 函数式编程思想
- **Monad模式**：`Result`类型实现了类似Haskell/Either或Rust/Result的Monad
- **不可变状态**：操作产生新对象而非修改现有状态
- **组合性**：通过`map`、`andThen`等方法组合操作序列

#### 2. 工厂模式
```cpp
struct OkTag {};
struct ErrTag {};

// 私有构造 + 静态工厂方法，强制正确初始化
static Result Ok(T val) {
    return Result(OkTag{}, std::move(val));
}
```

#### 3. 策略模式（通过钩子系统）
```cpp
// 用户通过钩子提供不同的日志和终止策略
Result<T, E>::setLogHook(my_log_strategy);
Result<T, E>::setTerminateHook(my_terminate_strategy);
```

#### 4. RAII与异常安全
```cpp
// 构造函数获取资源，析构函数释放资源
// 即使在异常情况下也能正确清理
```

### Monad 模式实现详解

#### Monad 三定律
```cpp
// 1. 左单位元：Result::Ok(x).andThen(f) ≡ f(x)
auto left_identity = Result<int, string>::Ok(5)
    .andThen([](int x) { return Result<int, string>::Ok(x * 2); });
// 等价于：[](int x) { return Result<int, string>::Ok(x * 2); }(5)

// 2. 右单位元：m.andThen(Result::Ok) ≡ m  
auto right_identity = Result<int, string>::Ok(5)
    .andThen([](int x) { return Result<int, string>::Ok(x); });
// 等价于：Result<int, string>::Ok(5)

// 3. 结合律：m.andThen(f).andThen(g) ≡ m.andThen([&](x){ return f(x).andThen(g); })
auto associative_law = Result<int, string>::Ok(5)
    .andThen(f).andThen(g);
// 等价于：
auto associative_law = Result<int, string>::Ok(5)
    .andThen([](int x) { return f(x).andThen(g); });
```

### 类型系统设计哲学

#### 1. 显式错误处理
```cpp
// 强制调用方处理错误，无法忽略
auto result = dangerousOperation();
// 编译器强制要求检查isOk()或使用match
```

#### 2. 无异常设计
- 适用于禁用异常的环境（嵌入式系统、高性能计算）
- 可预测的性能特征
- 明确的错误路径控制流

#### 3. 类型安全
```cpp
// 编译时检查类型正确性
auto result = Result<int, string>::Ok(42);
// result.map([](string s) { ... });  // 编译错误：类型不匹配
```

#### 4. 零开销抽象
- 内联函数调用
- 无虚函数开销
- 栈上分配，无动态内存分配

### C++11 特定实现细节

#### 1. 类型推导策略
```cpp
// 使用decltype和std::declval进行编译时类型推导
template<typename F>
auto map(F mapper) -> Result<decltype(mapper(std::declval<T>())), E> {
    typedef decltype(mapper(std::declval<T>())) ReturnType;
    // ...
}
```

#### 2. Lambda 表达式处理
```cpp
// C++11 lambda需要显式指定返回类型
.andThen([](double value) -> Result<double, std::string> {
    // 必须显式指定返回类型
    return Result<double, std::string>::Ok(value * 2.0);
})
```

#### 3. 函数指针转换
```cpp
// 在某些情况下需要显式转换函数指针
.andThen(static_cast<Result<std::string, std::string>(*)(const std::string&)>(parseInput))
```

### 性能优化特性

#### 1. 零开销抽象
- 所有操作均为内联函数
- 无虚函数调用开销
- 编译时类型解析

#### 2. 移动语义优化
```cpp
// 避免不必要的拷贝
return f(std::move(value));  // 转移所有权
```

#### 3. 编译时多态
```cpp
// 模板实例化在编译期完成
// 无运行时类型检查开销
```

### 与标准库的对比

#### 1. 与`std::optional`的关系
```cpp
// Result<T, E> 可视为增强版的 std::optional<T>
// 不仅知道"有无值"，还知道"为什么没有"
```

#### 2. 与异常处理的对比
```cpp
// 传统异常
try {
    auto value = riskyOperation();
    process(value);
} catch (const std::exception& e) {
    handleError(e);
}

// Result方式
auto result = riskyOperation();
result.match(
    [](auto value) { process(value); },
    [](auto error) { handleError(error); }
);
```

## 第五部分：最佳实践

### 错误类型选择

```cpp
// 推荐：轻量级错误类型
using Error = const char*;  // 嵌入式友好，零分配
using Error = std::string;  // 桌面/服务器，灵活

// 避免：复杂错误类型可能带来不必要的开销
```

### 解包策略选择

#### 生产环境
```cpp
// 优先使用安全解包
auto value = riskyOperation().unwrapOrLog("operation", default_value);
auto value = riskyOperation().unwrapOr(default_value);
```

#### 开发和测试
```cpp
// 使用严格解包快速发现问题
auto config = loadConfig().expect("配置必须有效");
auto value = criticalOperation().unwrap();
```

#### 用户代码
```cpp
// 使用模式匹配显式处理所有情况
operation().match(
    [](auto& value) { /* 处理成功 */ },
    [](auto& error) { /* 处理错误 */ }
);
```

### 钩子配置策略

#### 应用初始化
```cpp
void initializeApplication() {
    #ifdef PRODUCTION
        setupProductionHooks();
    #elif defined(DEVELOPMENT) 
        setupDevelopmentHooks();
    #elif defined(EMBEDDED)
        setupEmbeddedHooks();
    #else
        setupTestHooks();
    #endif
}
```

#### 动态配置
```cpp
// 运行时根据配置调整
void configureLogging(LogLevel level) {
    Result<T, E>::setLogHook([level](const std::string& message) {
        if (shouldLog(level)) {
            getLogger().log(level, message);
        }
    });
}
```

### 组合操作模式

#### 数据处理管道
```cpp
auto processDataPipeline(const std::string& input) -> Result<Data, std::string> {
    return parseInput(input)
        .andThen(validateData)      // 验证可能失败
        .map(transformData)         // 纯转换
        .andThen(persistData)       // 持久化可能失败
        .orElse(handleDataError)    // 错误恢复
        .mapError(enrichError);     // 错误信息增强
}
```

#### 资源管理
```cpp
auto withResourceCleanup() -> Result<Data, std::string> {
    auto resource = acquireResource();
    
    return processResource(resource)
        .map([resource](auto result) {
            // 确保资源清理
            cleanupResource(resource);
            return result;
        })
        .orElse([resource](auto error) -> Result<Data, std::string> {
            // 错误时也要清理资源
            cleanupResource(resource);
            return Result<Data, std::string>::Err(error);
        });
}
```

## 第六部分：向后兼容性

### C++11 标准兼容
- 仅使用 C++11 核心特性
- 无第三方依赖
- 支持 GCC 4.8+、Clang 3.3+、MSVC 2013+

### 渐进式增强路径
```cpp
// 未来可添加 C++17/20 优化
#if __cplusplus >= 201703L
// 使用 if constexpr 优化模板实例化
#endif
```

### 与未来标准兼容
```cpp
// 为 std::expected (C++23) 预留迁移路径
template<typename T, typename E>
using expected = Result<T, E>;  // 类型别名便于迁移
```

## 总结

`Result<T, E>` 提供了一个现代化、类型安全且高效的错误处理解决方案：

- ✅ **编译时安全**：强制错误处理，无法忽略
- ✅ **零运行时开销**：模板和内联优化
- ✅ **高度可配置**：完整的钩子系统
- ✅ **组合性强**：函数式操作链
- ✅ **多环境支持**：嵌入式到服务器
- ✅ **向后兼容**：纯 C++11 实现

通过钩子系统，用户可以无缝集成到任何现有的日志和错误处理基础设施中，真正实现了"关注点分离"的设计原则。无论是简单的控制台应用还是复杂的企业级系统，`Result<T, E>` 都能提供一致且可靠的错误处理体验。

---
*此实现灵感来源于 Rust 标准库中的 Result<T, E> 类型，参考了其将成功值与错误值封装于一个类型中，并通过组合子进行链式操作的思想。*