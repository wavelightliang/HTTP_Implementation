#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <time.h>
#include <pthread.h>
#include <windows.h>
#include <unistd.h>

#define PORT "8080"
#define BACKLOG 20
#define RECV_BUFFER_SIZE 2048
#define NUM_THREADS 4
#define TASK_QUEUE_SIZE 16
#define CGI_DIR "/cgi-bin/"
#define ROOT_PATH "E:/Project/HTTP"

// --- 类型定义 ---
typedef struct {
    SOCKET queue[TASK_QUEUE_SIZE];
    int head, tail, count;
    pthread_mutex_t mutex;
    pthread_cond_t not_full, not_empty;
} task_queue_t;

// --- 全局变量 ---
task_queue_t task_queue;
pthread_mutex_t log_mutex;

// --- 函数声明 ---
void log_request(const char* method, const char* path, int status_code);
void handle_request(SOCKET client_socket);
void* worker_thread(void* arg);
void init_task_queue(task_queue_t* tq);
void push_task(task_queue_t* tq, SOCKET client_socket);
SOCKET pop_task(task_queue_t* tq);
void execute_cgi(SOCKET client_socket, const char* path, const char* method, const char* query_string);
void serve_404(SOCKET client_socket);
void serve_500(SOCKET client_socket);

// --- Main 函数 ---
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    SetConsoleOutputCP(CP_UTF8);
    init_task_queue(&task_queue);
    pthread_mutex_init(&log_mutex, NULL);
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }

    struct addrinfo hints, *res;
    SOCKET listen_socket = INVALID_SOCKET;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    getaddrinfo(NULL, PORT, &hints, &res);
    for (struct addrinfo* p = res; p != NULL; p = p->ai_next) {
        if ((listen_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == INVALID_SOCKET) continue;
        int yes = 1;
        setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(int));
        if (bind(listen_socket, p->ai_addr, (int)p->ai_addrlen) == SOCKET_ERROR) {
            closesocket(listen_socket);
            listen_socket = INVALID_SOCKET;
            continue;
        }
        break;
    }
    freeaddrinfo(res);
    if (listen_socket == INVALID_SOCKET) return 1;
    if (listen(listen_socket, BACKLOG) == SOCKET_ERROR) return 1;
    
    printf("Server is listening on port %s with %d worker threads...\n", PORT, NUM_THREADS);

    while (1) {
        SOCKET client_socket = accept(listen_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET) continue;
        push_task(&task_queue, client_socket);
    }
    return 0;
}

// --- 工作线程 ---
void* worker_thread(void* arg) {
    (void)arg;
    while (1) {
        SOCKET client_socket = pop_task(&task_queue);
        handle_request(client_socket);
    }
    return NULL;
}

// --- 请求处理 ---
void handle_request(SOCKET client_socket) {
    char recv_buffer[RECV_BUFFER_SIZE];
    int bytes_received = recv(client_socket, recv_buffer, RECV_BUFFER_SIZE - 1, 0);
    
    if (bytes_received <= 0) {
        closesocket(client_socket);
        return;
    }
    recv_buffer[bytes_received] = '\0';

    char method[16] = "UNKNOWN";
    char uri[2048] = "/";
    char* query_string = NULL;

    char* next_token;
    char* line = strtok_s(recv_buffer, "\r\n", &next_token);
    if (line) {
        char* m = strtok_s(line, " ", &next_token);
        if (m) strncpy(method, m, sizeof(method) - 1);

        char* full_uri = strtok_s(NULL, " ", &next_token);
        if (full_uri) {
            strncpy(uri, full_uri, sizeof(uri) - 1);
            query_string = strchr(uri, '?');
            if (query_string) {
                *query_string = '\0';
                query_string++;
            }
        }
    }

    if (strncmp(uri, CGI_DIR, strlen(CGI_DIR)) == 0) {
        execute_cgi(client_socket, uri, method, query_string);
    } else {
        const char* http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nNot a CGI request.";
        send(client_socket, http_response, (int)strlen(http_response), 0);
        log_request(method, uri, 200);
    }

    closesocket(client_socket);
}

// --- CGI 执行 ---
void execute_cgi(SOCKET client_socket, const char* path, const char* method, const char* query_string) {
    char script_path[MAX_PATH];
    char command_line[MAX_PATH + 128];
    sprintf(script_path, "%s%s", ROOT_PATH, path);

    // ======================= 终极核心修正 =======================
    // 将路径中的所有正斜杠 '/' 替换为反斜杠 '\'，以确保Windows API能正确解析
    for (int i = 0; script_path[i] != '\0'; i++) {
        if (script_path[i] == '/') {
            script_path[i] = '\\';
        }
    }
    // ==========================================================

    if (_access(script_path, 0) != 0) {
        serve_404(client_socket);
        log_request(method, path, 404);
        return;
    }

    SetEnvironmentVariable("REQUEST_METHOD", method);
    SetEnvironmentVariable("QUERY_STRING", query_string ? query_string : "");
    SetEnvironmentVariable("SCRIPT_NAME", path);

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hChildStd_OUT_Rd = NULL;
    HANDLE hChildStd_OUT_Wr = NULL;

    if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &sa, 0)) {
        serve_500(client_socket);
        log_request(method, path, 500);
        return;
    }
    if (!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) {
        serve_500(client_socket);
        log_request(method, path, 500);
        return;
    }

    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdOutput = hChildStd_OUT_Wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    // 请确保此路径是您电脑上 bash.exe 的正确路径
    const char* bash_path = "C:\\msys64\\usr\\bin\\bash.exe";
    sprintf(command_line, "\"%s\" \"%s\"", bash_path, script_path);

    BOOL bSuccess = CreateProcessA(
        NULL, command_line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi
    );

    if (!bSuccess) {
        serve_500(client_socket);
        log_request(method, path, 500);
        CloseHandle(hChildStd_OUT_Rd);
        CloseHandle(hChildStd_OUT_Wr);
        return;
    }

    CloseHandle(hChildStd_OUT_Wr);

    DWORD dwRead;
    CHAR chBuf[4096];
    while (ReadFile(hChildStd_OUT_Rd, chBuf, sizeof(chBuf), &dwRead, NULL) && dwRead != 0) {
        send(client_socket, chBuf, dwRead, 0);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(hChildStd_OUT_Rd);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    log_request(method, path, 200);
}

// --- 任务队列实现 ---
void init_task_queue(task_queue_t* tq) {
    tq->head = 0; tq->tail = 0; tq->count = 0;
    pthread_mutex_init(&tq->mutex, NULL);
    pthread_cond_init(&tq->not_full, NULL);
    pthread_cond_init(&tq->not_empty, NULL);
}

void push_task(task_queue_t* tq, SOCKET client_socket) {
    pthread_mutex_lock(&tq->mutex);
    while (tq->count == TASK_QUEUE_SIZE) pthread_cond_wait(&tq->not_full, &tq->mutex);
    tq->queue[tq->tail] = client_socket;
    tq->tail = (tq->tail + 1) % TASK_QUEUE_SIZE;
    tq->count++;
    pthread_cond_signal(&tq->not_empty);
    pthread_mutex_unlock(&tq->mutex);
}

SOCKET pop_task(task_queue_t* tq) {
    pthread_mutex_lock(&tq->mutex);
    while (tq->count == 0) pthread_cond_wait(&tq->not_empty, &tq->mutex);
    SOCKET client_socket = tq->queue[tq->head];
    tq->head = (tq->head + 1) % TASK_QUEUE_SIZE;
    tq->count--;
    pthread_cond_signal(&tq->not_full);
    pthread_mutex_unlock(&tq->mutex);
    return client_socket;
}

// --- 日志与错误响应函数 ---
void log_request(const char* method, const char* path, int status_code) {
    pthread_mutex_lock(&log_mutex);
    time_t now = time(NULL);
    struct tm t;
    char time_buf[20];
    localtime_s(&t, &now);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &t);
    printf("[%s] [Thread %llu] [%s] [%s] [%d]\n", 
           time_buf, (unsigned long long)pthread_self(), method, path, status_code);
    fflush(stdout);
    pthread_mutex_unlock(&log_mutex);
}

void serve_404(SOCKET client_socket) {
    const char* response = "HTTP/1.1 404 Not Found\r\n"
                           "Content-Type: text/html\r\n"
                           "\r\n"
                           "<html><body><h1>404 Not Found</h1></body></html>";
    send(client_socket, response, (int)strlen(response), 0);
}

void serve_500(SOCKET client_socket) {
    const char* response = "HTTP/1.1 500 Internal Server Error\r\n"
                           "Content-Type: text/html\r\n"
                           "\r\n"
                           "<html><body><h1>500 Internal Server Error</h1></body></html>";
    send(client_socket, response, (int)strlen(response), 0);
}