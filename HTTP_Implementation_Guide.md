# 指南：在Windows上通过增量开发构建多线程CGI HTTP服务器

您好！作为您的专属计算机科学与编程教授，我们将采用一种更严谨、更符合工程实践的**增量式、测试驱动**的方法来完成`HTTP_PRD.md`中的任务。本指南将作为我们的开发蓝图，确保每一步都坚实可靠。

---

## 1. 技术栈与框架规划 (Technology Stack & Framework Plan)

在编写第一行代码前，我们首先明确整个项目的技术选型与顶层设计。

### 1.1 技术栈 (Tech Stack)
-   **编程语言**: C (遵循 C11 标准)
-   **编译器**: GCC (通过 MSYS2 UCRT64 环境提供)
-   **构建系统**: `make` (使用 `Makefile` 管理编译流程)
-   **核心系统API**: 
    -   **网络**: Winsock2 API (在Windows环境下，通过包含 `<winsock2.h>` 并链接 `ws2_32.lib` 使用)
    -   **并发**: POSIX Threads (pthreads)。MinGW-w64环境原生提供了`<pthread.h>`，这是实现跨平台线程代码的关键。
    -   **进程通信**: POSIX Process API (`fork`, `pipe`, `exec`系列, `waitpid`)。同样由MSYS2环境提供，是实现CGI的核心。

### 1.2 整体架构 (Architecture)
我们将构建一个基于 **生产者-消费者模型** 的并发服务器：
-   **主线程 (生产者)**: 唯一的职责是监听端口，接收新的客户端连接 (`accept`)。一旦接收到新连接（一个`client_socket`），它便将这个“任务”放入一个全局的、线程安全的**任务队列**中。
-   **工作线程池 (消费者)**: 我们将预先创建一组（`MAX_THREADS=4`）固定的工作线程。这些线程循环地从任务队列中取出“任务”（`client_socket`），然后负责处理该连接的所有后续工作（解析请求、执行CGI、发送响应、关闭连接）。
-   **任务队列 (Task Queue)**: 一个有界缓冲区（Bounded Buffer），用于在主线程和工作线程之间解耦。它必须是**线程安全**的，我们将使用**互斥锁 (Mutex)** 和 **条件变量 (Condition Variables)** 来保护对队列的并发访问。

![Framework Diagram](https://mermaid.ink/svg/eyJjb2RlIjoiZ3JhcGggVERcbiAgICBzdWJncmFwaCBcIk1haW4gVGhyZWFkIChQcm9kdWNlcpilcbiAgICAgICAgQVtMaXN0ZW4gJiBBY2NlcHRdIC0tPnxOZXcgQ29ubmVjdGlvbnwgQihQdXNoIFRhc2spXG4gICAgZW5kXG5cbiAgICBzdWJncmFwaCBcIlRocmVhZC1TYWZlIFRhc2sgUXVldWVcIlxuICAgICAgICBDW1F1ZXVlXSA9PSBDXG4gICAgZW5kXG5cbiAgICBzdWJncmFwaCBcIldvcmtlciBUaHJlYWQgUG9vbCAoQ29uc3VtZXJzKVwiXG4gICAgICAgIFQxW1dvcmtlciAxXSAtLT58UG9wIFRhc2t8IENcbiAgICAgICAgVDJbV29ya2VyIDJdIC0tPnxQb3AgVGFza3wgQ1xuICAgICAgICBUM1tXb3JrZXIgM10gLS0-fFBvcCBUYXNrfCBDXG4gICAgICAgIFQ0W1dvcmtlciA0XSAtLT58UG9wIFRhc2t8IENcbiAgICAgICAgVDQgLS0-fEhhbmRsZSBSZXF1ZXN0fCBEW0NsaWVudF1cbiAgICBlbmRcblxuICAgIEIgLS0-IENcbiIsIm1lcm1haWQiOnsidGhlbWUiOiJkZWZhdWx0In0sInVwZGF0ZUVkaXRvciI6ZmFsc2UsImF1dG9TeW5jIjp0cnVlLCJ1cGRhdGVEaWFncmFtIjpmYWxzZX0)

---

## 2. 增量开发与测试驱动流程 (Incremental Development & Test-Driven Workflow)

我们将项目分解为以下几个可独立验证的阶段。每个阶段都会修改我们的 `httpd.c` 和 `Makefile`。

### **阶段 0: 项目初始化与“Hello, World”编译**

-   **目标**: 搭建项目骨架，确保编译环境和 `Makefile` 工作正常。
-   **实现思路**: 
    1.  创建 `E:/Project/HTTP/` 目录结构 (`cgi-bin`, `httpd.c`, `Makefile`)。
    2.  在 `httpd.c` 中编写一个最简单的 `main` 函数，仅打印一句 "Hello, Server!"。
    3.  编写一个基础的 `Makefile`，能够将 `httpd.c` 编译成 `httpd.exe`。
-   **测试验证**:
    -   在MSYS2 UCRT64终端中，进入项目目录，运行 `make`。
    -   **预期结果**: 成功生成 `httpd.exe`，无编译错误。
    -   运行 `./httpd.exe`。
    -   **预期结果**: 终端打印出 "Hello, Server!"。

### **阶段 1: 基础TCP服务器**

-   **目标**: 实现一个能接受TCP连接，然后立即关闭它的服务器。这是网络编程的“Hello, World”。
-   **实现思路**:
    1.  在 `main` 函数中，初始化Winsock。
    2.  依次调用 `socket()`, `bind()`, `listen()` 来设置服务器监听。
    3.  进入一个无限循环，调用 `accept()` 阻塞等待客户端连接。
    4.  一旦 `accept()` 返回一个新的 `client_socket`，立即打印一条消息，然后调用 `closesocket()` 关闭它。
-   **测试验证**:
    -   编译并运行 `./httpd.exe`。
    -   打开另一个MSYS2终端，使用 `telnet` 或 `nc` (netcat) 连接服务器：`telnet localhost 8080`。
    -   **预期结果**: `telnet` 命令会立刻返回或显示连接已关闭。服务器终端会打印出接收到新连接的日志。

### **阶段 2: 单线程HTTP响应服务器**

-   **目标**: 对收到的任何请求，都回复一个固定的HTTP响应。
-   **实现思路**:
    1.  在 `accept()` 之后，`closesocket()` 之前，增加 `recv()` 来读取客户端发来的数据（暂不解析）。
    2.  使用 `send()` 发送一个硬编码的、完整的HTTP响应字符串，例如：`"HTTP/1.1 200 OK\r\nContent-Length: 18\r\n\r\nUnder construction"`。
-   **测试验证**:
    -   编译并运行 `./httpd.exe`。
    -   打开浏览器，访问 `http://localhost:8080`。
    -   **预期结果**: 浏览器页面显示 "Under construction"。
    -   使用 `curl -v http://localhost:8080`。
    -   **预期结果**: `curl` 的输出中能看到 `> GET / HTTP/1.1` 和 `< HTTP/1.1 200 OK` 等信息。

### **阶段 3: 请求解析与日志记录**

-   **目标**: 解析HTTP请求行，提取方法和路径，并实现符合规范的日志记录。
-   **实现思路**:
    1.  在 `recv()` 之后，对接收到的缓冲区数据进行解析，使用 `sscanf` 或 `strtok` 提取出请求方法和路径。
    2.  实现 `log_request(method, path, status_code)` 函数。该函数必须使用**互斥锁 (mutex)** 保证多线程环境下的打印是原子操作，并调用 `fflush(stdout)`。
    3.  在发送响应后，调用 `log_request`。
-   **测试验证**:
    -   编译并运行 `./httpd.exe`。
    -   浏览器访问 `http://localhost:8080/test/path`。
    -   **预期结果**: 服务器终端打印出格式正确的日志：`[YYYY-MM-DD HH:MM:SS] [GET] [/test/path] [200]`。

### **阶段 4: 线程池与并发处理**

-   **目标**: 将服务器改造为生产者-消费者模型，实现并发处理请求。
-   **实现思路**:
    1.  定义任务队列及其同步原语（互斥锁、条件变量）。
    2.  实现 `push_task` 和 `pop_task` 线程安全函数。
    3.  创建 `handle_request` 函数，将阶段2、3中的请求处理逻辑（`recv`, 解析, `send`, `log`, `close`）移入其中。
    4.  创建 `worker_thread` 函数，其内部是一个无限循环，调用 `pop_task` 获取任务，然后调用 `handle_request`。
    5.  修改 `main` 函数：在 `listen` 后，创建并启动4个 `worker_thread`。`main` 的主循环简化为只调用 `accept` 并将得到的 `client_socket` 推入任务队列。
-   **测试验证**:
    -   编译并运行 `./httpd.exe`。
    -   打开多个（例如5个）终端，几乎同时运行 `curl http://localhost:8080/path/N` (N从1到5)。
    -   **预期结果**: 所有 `curl` 命令都能很快得到响应。服务器终端的日志可能是交错打印的，但每一行日志本身是完整的。

### **阶段 5: CGI核心逻辑实现**

-   **目标**: 实现对 `/cgi-bin/` 路径的请求，能够正确执行CGI脚本并返回其输出。
-   **实现思路**:
    1.  创建 `cgi-bin/echo.sh` 测试脚本。
    2.  在 `handle_request` 中，增加路径判断逻辑：如果路径以 `/cgi-bin/` 开头，则调用 `execute_cgi` 函数，否则走原有逻辑。
    3.  实现 `execute_cgi(client_socket, path, method, query_string)` 函数。
    4.  在 `execute_cgi` 中，使用 `pipe()` 创建管道，`fork()` 创建子进程。
    5.  **子进程**: 使用 `setenv` 设置环境变量，`dup2` 重定向 `stdout` 到管道写端，`execl` 执行脚本。
    6.  **父进程**: 关闭管道写端，循环 `read` 管道读端，将读到的数据 `send` 给 `client_socket`，最后 `waitpid` 回收子进程。
-   **测试验证**:
    -   编译并运行 `./httpd.exe`。
    -   浏览器访问 `http://localhost:8080/cgi-bin/echo.sh?user=test`。
    -   **预期结果**: 浏览器显示 `echo.sh` 脚本的输出，其中应包含 `Request Method: GET` 和 `Query String: user=test`。

### **阶段 6: 完善CGI错误处理**

-   **目标**: 为CGI流程增加健壮的错误处理，能正确返回404和500状态码。
-   **实现思路**:
    1.  在 `execute_cgi` 中，`fork` 之前，使用 `access(script_path, X_OK)` 检查脚本是否存在且可执行。如果失败，直接发送404响应并返回。
    2.  在父进程中，`waitpid` 之后，检查子进程的退出状态。如果 `WEXITSTATUS` 非0，说明脚本执行出错，记录500状态码到日志。
    3.  (高级) 如果CGI脚本在输出任何内容前就失败了，我们可以尝试发送一个500错误页面。如果已经发送了部分内容，就只能中断连接了。在日志中正确记录状态是本阶段的核心要求。
-   **测试验证**:
    -   编译并运行 `./httpd.exe`。
    -   访问 `http://localhost:8080/cgi-bin/non_existent_script.sh`。
    -   **预期结果**: 浏览器收到404 Not Found响应。
    -   创建一个会失败的脚本 `fail.sh` (内容为 `exit 1`)，访问 `http://localhost:8080/cgi-bin/fail.sh`。
    -   **预期结果**: 服务器日志记录状态码500。

---

我们现在有了一份清晰的、循序渐进的作战地图。我将严格按照这个流程，在您的指导下，逐一完成每个阶段的代码编写与验证.