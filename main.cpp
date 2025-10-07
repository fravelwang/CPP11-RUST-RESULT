#include "cpp_rust_result.hpp"

// 使用示例
auto divide(double a, double b) -> Result<double, std::string> {
    if (b == 0.0) return Result<double, std::string>::Err("Division by zero");
    return Result<double, std::string>::Ok(a / b);
}

auto readFile(const std::string& path) -> Result<std::string, std::string> {
    std::ifstream file(path);
    if (!file) return Result<std::string, std::string>::Err("File not found");
    
    std::string content;
    std::string line;
    while (std::getline(file, line)) {
        content += line + "\n";
    }
    return Result<std::string, std::string>::Ok(content);
}

auto parseInput(const std::string& input) -> Result<std::string, std::string> {
    if (input.empty()) return Result<std::string, std::string>::Err("Empty input");
    return Result<std::string, std::string>::Ok("Processed: " + input);
}

// 自定义钩子示例
void setupCustomHooks() {
    // 设置日志钩子 - 输出到文件（修复版本）
    Result<double, std::string>::setLogHook([](const std::string& message) {
        static std::ofstream logfile("result_errors.log", std::ios::app);
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        
        // 使用 put_time 精确控制时间格式
        logfile << "[" << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") 
                << "] " << message << std::endl;
    });

    // 设置终止钩子 - 优雅终止
    Result<double, std::string>::setTerminateHook([]() {
        std::cout << "Application terminating due to Result error" << std::endl;
        // 可以在这里执行清理操作
        std::exit(1);
    });
}

// 复杂操作示例
auto processFile(const std::string& filename) -> Result<double, std::string> {
    return readFile(filename)
        .andThen(static_cast<Result<std::string, std::string>(*)(const std::string&)>(parseInput))
        .map([](const std::string& processed) -> double {
            return static_cast<double>(processed.length());
        })
        .andThen([](double value) -> Result<double, std::string> {
            if (value > 100.0) {
                return Result<double, std::string>::Err("Value too large");
            }
            return Result<double, std::string>::Ok(value * 2.0);
        })
        .orElse([](const std::string& error) -> Result<double, std::string> {
            std::cout << "Error recovered: " << error << std::endl;
            return Result<double, std::string>::Ok(0.0);
        });
}

int main() {
    // 设置自定义钩子
    setupCustomHooks();

    // 基础使用示例
    std::cout << "=== Basic Usage ===" << std::endl;
    auto divResult = divide(10.0, 2.0);
    divResult.match(
        [](double v) { std::cout << "Result: " << v << "\n"; },
        [](const std::string& e) { std::cerr << "Error: " << e << "\n"; }
    );

    // 测试各种解包方法
    std::cout << "\n=== Unwrap Methods ===" << std::endl;
    
    // 安全解包 - 不会终止
    auto safeResult = divide(10.0, 0.0).unwrapOrLog("safe division", 0.0);
    std::cout << "Safe result: " << safeResult << std::endl;
    
    // 检查解包
    auto checkedResult = divide(10.0, 0.0).unwrapChecked();
    std::cout << "Checked result: " << checkedResult << std::endl;
    
    // 正常解包
    auto normalResult = divide(20.0, 4.0).unwrap("normal division");
    std::cout << "Normal result: " << normalResult << std::endl;

    // 期望解包
    auto expectedResult = divide(30.0, 5.0).expect("division should work");
    std::cout << "Expected result: " << expectedResult << std::endl;

    // 链式操作示例
    std::cout << "\n=== Chained Operations ===" << std::endl;
    auto fileResult = readFile("main.cpp")
        .map([](const std::string& content) {
            return "Content length: " + std::to_string(content.length());
        })
        .mapError([](const std::string& error) {
            return "File error: " + error;
        });

    fileResult.match(
        [](const std::string& content) { 
            std::cout << "File: " << content << std::endl; 
        },
        [](const std::string& error) { 
            std::cout << "Error: " << error << std::endl; 
        }
    );

    // 复杂管道示例
    std::cout << "\n=== Complex Pipeline ===" << std::endl;
    auto processed = processFile("main.cpp");
    processed.match(
        [](double result) { 
            std::cout << "Final result: " << result << std::endl; 
        },
        [](const std::string& error) { 
            std::cerr << "Pipeline error: " << error << std::endl; 
        }
    );

    // 清理钩子
    Result<double, std::string>::clearHooks();

    return 0;
}