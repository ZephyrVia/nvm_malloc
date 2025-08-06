# C 语言项目模板 (C Project Template)

这是一个功能齐全、开箱即用的 C 语言项目模板，采用业界标准的 **CMake** 构建系统，专注于提供现代化的开发体验，包括跨平台支持、自动化测试和无缝的 IDE 集成。

## ✨ 特性 (Features)

-   🚀 **现代构建系统 (CMake)**：采用业界标准的 CMake，提供无与伦比的跨平台能力。一次编写，即可在 Windows (Visual Studio)、macOS (Xcode) 和 Linux (Makefiles/Ninja) 等多种环境下无缝构建。
-   📂 **清晰且可扩展的目录结构**：预设了 `src`, `include`, `tests`, `lib` 等标准目录，并通过模块化的 `CMakeLists.txt` 管理，让项目从一开始就保持高度整洁和可维护性。
-   🛡️ **真正的源码与构建分离**：所有编译产物都会被严格限制在 `build` 目录中，绝不污染您的源代码树，实现了真正的源码外构建 (Out-of-Source Builds)。
-   ✅ **深度集成的单元测试**：内置 [Unity Test Framework](https://github.com/ThrowTheSwitch/Unity)，并与 CMake 的测试工具 `CTest` 深度集成。只需一条命令 (`ctest`) 即可自动发现并运行所有测试。
-   💻 **无缝的 IDE 支持**：为现代 IDE（如 CLion、VS Code）提供一流的开箱即用体验。项目加载后即可实现智能提示、代码补全和一键调试。

## 🚀 使用流程 (Workflow)

遵循标准的 `配置 -> 编译 -> 测试` 流程，轻松驾驭你的 C 语言项目。

### 1. 使用此模板创建你的新项目

这是开始一个新项目的最佳方式，它会为你创建一个拥有独立 Git 历史的全新仓库。

1.  **点击 "Use this template"**：在本仓库主页，点击绿色的 **"Use this template"** 按钮，然后选择 "Create a new repository"。
2.  **填写新项目信息**：为你即将开始的新项目命名，并完成创建。
3.  **克隆你的新项目到本地**：
    ```bash
    git clone https://github.com/your-username/your-new-project.git
    cd your-new-project
    ```

### 2. 配置与编译

首次使用或修改了 `CMakeLists.txt` 文件后，需要运行配置步骤。

```bash
# 1. 创建并进入构建目录
mkdir build
cd build

# 2. 运行 CMake 进行配置 (我们选择 Debug 模式)
# 这会生成 Makefile 或其他构建系统所需的文件
cmake .. -D CMAKE_BUILD_TYPE=Debug

# 3. 编译项目
# --build 会自动调用正确的编译工具 (make, ninja, MSBuild...)
cmake --build .
```

### 3. 运行测试

编译成功后，在 `build` 目录中运行 `ctest`。

```bash
# (确保你仍然在 build 目录中)
# --output-on-failure 只在测试失败时打印详细信息，非常实用
ctest --output-on-failure
```

### 4. 日常开发

1.  **添加/修改代码**：
    -   在 `src/` 和 `include/` 目录中修改或添加你的 `.c` 和 `.h` 文件。
    -   在 `tests/` 目录中修改或添加你的 `test_*.c` 测试文件。

2.  **重新编译**：
    -   如果只是**修改了文件内容**，只需在 `build` 目录中再次运行编译命令即可：
      ```bash
      # (在 build 目录中)
      cmake --build .
      ```
    -   如果**添加或删除了文件**，你需要重新运行配置命令来更新构建系统，然后再编译：
      ```bash
      # (在 build 目录中)
      cmake .. 
      cmake --build .
      ```

3.  **清理项目**：
    想从头开始？最简单的方法就是删除并重建 `build` 目录。
    ```bash
    # (先返回项目根目录)
    cd ..
    rm -rf build
    ```