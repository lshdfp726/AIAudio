#include "lsh_socket.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include "esp_log.h"
#include <errno.h>   // 包含 errno 定义

static const char* TAG = "lsh_socket";

static int clientfd;

int createSocket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_fd);
        return -1;
    }
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    if(listen(server_fd, 3) < 0) {
        perror("listen failed");
        close(server_fd);
        return -1;
    }
    printf("等待客户端连接（端口：%d）...\n", port);
    // 接受客户端连接
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("accept failed");
        close(server_fd);
        return -1;
    }
    printf("客户端已连接：%s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL failed");
        return -1;
    }

    if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL failed");
        return -1;
    }

    clientfd = client_fd;
    return client_fd;
}

int sendMessage(void *dataptr, size_t size, int flags) {
    if (clientfd <= 0) {
        ESP_LOGE(TAG, "sendMessage fd is: %d",clientfd);
        return -1;
    }

    if (dataptr == NULL || size <= 0) {
        ESP_LOGE(TAG, "sendMessage dataptr or size is error");
        return -1;
    }
    int ret = send(clientfd, dataptr, size, flags);
    if (ret < 0) {
        ESP_LOGE(TAG, "send失败 | 错误码: %d | 原因: %s", errno, strerror(errno));
    }
    return ret;
}

