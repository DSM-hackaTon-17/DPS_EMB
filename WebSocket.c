#include <stdio.h>
#include<string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <winsock.h>

int connect_server(int sockfd, const int* ip, int port);
int receive_response(int sockfd, char* buffer, int size);
int send_message(int sockfd, const char* message);


int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    connect_server(sockfd, , 0); //***
    const char* handshake = ; //****
    printf("�������� ���� ��û: \n%s\n", handshake);
    send_message(sockfd, handshake);

    char buffer[128];
    receive_response(sockfd, buffer, sizeof(buffer));
    printf("���� ����: \n%s\n", buffer);

    if (strstr(buffer, "101 Switching Protocols")) {
        printf("������ �ڵ����ũ ����\n");
    }
    else {
        printf("������ �ڵ����ũ ����\n");
    }
    closesocket(sockfd);
    WSACleanup();
}

int connect_server(int sockfd, const int* ip, int port) {
    struct sockaddr_in serv;
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    serv.sin_addr.s_addr = inet_addr(ip);

    return connect(sockfd, (struct sockaddr*)&serv, sizeof(serv));
}

int receive_response(int sockfd, char* buffer, int size) {
    int len = receive_response(sockfd, buffer, size - 1, 0);
    if (len > 0) {
        buffer[len] = '\0';
    }
    return len;
}

int send_message(int sockfd, const char* message) {
    return send(sockfd, message, strlen(message), 0);
}