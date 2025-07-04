# Makefile for HTTPD Server on Windows with MSYS2 UCRT64

# 编译器与编译选项
# CC: C Compiler
# CFLAGS: Compiler Flags. -Wall(开启所有警告), -Wextra(开启额外警告), -g(生成调试信息), -std=c11(遵循C11标准)
CC = gcc
CFLAGS = -Wall -Wextra -g -std=c11

# 链接器选项
# LDFLAGS: Linker Flags.
# -lws2_32: 链接 Winsock2 库
# -lpthread: 链接 POSIX Threads 库
LDFLAGS = -lws2_32 -lpthread

# 目标可执行文件名
TARGET = httpd.exe

# 源文件列表
SRCS = httpd.c

# 根据源文件列表自动生成的目标文件(.o)列表
OBJS = $(SRCS:.c=.o)

# 默认目标：'make' 或 'make all' 将会执行这里
all: $(TARGET)

# 链接规则：如何从目标文件(.o)生成最终的可执行文件
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# 编译规则：如何从源文件(.c)生成目标文件(.o)
# $< 代表依赖项中的第一个 (即.c文件)
# $@ 代表目标 (即.o文件)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理规则：'make clean' 将会删除所有生成的文件
clean:
	rm -f $(OBJS) $(TARGET)