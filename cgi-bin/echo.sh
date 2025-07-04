#!/bin/bash

# CGI脚本必须首先输出一个HTTP响应头，然后是一个空行
echo "Content-Type: text/html; charset=utf-8"
echo ""

# --- HTML Body ---
echo "<html>"
echo "<head><title>CGI Echo</title></head>"
echo "<body>"
echo "<h1>CGI Echo Script</h1>"
echo "<hr>"

echo "<h2>Environment Variables:</h2>"
echo "<ul>"
echo "<li><b>Request Method:</b> $REQUEST_METHOD</li>"
echo "<li><b>Query String:</b> $QUERY_STRING</li>"
echo "<li><b>Script Name:</b> $SCRIPT_NAME</li>"
echo "</ul>"

echo "<hr>"
echo "<h2>Standard Input (for POST):</h2>"
echo "<pre>"
# 读取并转义标准输入，防止HTML注入
if [ "$REQUEST_METHOD" = "POST" ]; then
    cat | sed 's/&/\&/g; s/</\</g; s/>/\>/g'
fi
echo "</pre>"

echo "</body>"
echo "</html>"