#include <stdio.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>
#include <stdlib.h>
#include "../include/proxy.h"

#pragma comment(lib, "Ws2_32.lib")

HANDLE log_mutex;
ServerConfig cfg;

void make_timestamp(char *out) {
    time_t now = time(0);
    struct tm *ti = localtime(&now);
    strftime(out, 64, "%Y-%m-%d %H:%M:%S", ti);
}

void write_log_entry(char *client, char *target, int code) {
    WaitForSingleObject(log_mutex, INFINITE);
    FILE *fp = fopen(cfg.log_path, "a");
    if (fp != NULL) {
        char ts[64];
        make_timestamp(ts);
        fprintf(fp, "[%s] Client=%s | Host=%s | Code=%d\n", ts, client, target, code);
        fclose(fp);
    }
    ReleaseMutex(log_mutex);
}

int decode_http_request(char *buffer, ParsedRequest *req) {
    char temp[BUFFER_SIZE];
    memcpy(temp, buffer, strlen(buffer) + 1);

    char uri[2048], proto[16];
    if (sscanf(temp, "%s %s %s", req->method, uri, proto) != 3) return -1;

    char *host_ptr = uri;
    if (!strncmp(uri, "http://", 7)) host_ptr += 7;

    char *slash = strchr(host_ptr, '/');
    if (slash != NULL) *slash = 0;

    char *colon = strchr(host_ptr, ':');
    if (colon) {
        *colon = 0;
        req->port = atoi(colon + 1);
    } else {
        req->port = (!strcmp(req->method, "CONNECT")) ? 443 : 80;
    }

    strncpy(req->host, host_ptr, sizeof(req->host) - 1);
    req->host[sizeof(req->host) - 1] = 0;
    return 0;
}

void strip_whitespace(char *s) {
    int i = strlen(s) - 1;
    while (i >= 0 && isspace(s[i])) s[i--] = 0;
    char *p = s;
    while (*p && isspace(*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

void read_server_config(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        cfg.port = 8888;
        strcpy(cfg.log_path, "logs/proxy.log");
        strcpy(cfg.blocked_file, "../config/blocked.txt");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || strlen(line) < 3) continue;

        char *k = strtok(line, "=");
        char *v = strtok(NULL, "\n");
        if (k && v) {
            strip_whitespace(k);
            strip_whitespace(v);

            if (strcmp(k, "PORT") == 0) cfg.port = atoi(v);
            else if (strcmp(k, "LOG_PATH") == 0) strcpy(cfg.log_path, v);
            else if (strcmp(k, "BLOCKED_LIST") == 0) strcpy(cfg.blocked_file, v);
        }
    }
    fclose(fp);
}

int host_blocked(char *host) {
    FILE *fp = fopen(cfg.blocked_file, "r");
    if (!fp) return 0;

    char entry[256];
    while (fgets(entry, sizeof(entry), fp)) {
        entry[strcspn(entry, "\r\n")] = 0;
        if (entry[0] && strstr(host, entry)) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

SOCKET open_connection(char *host, int port) {
    struct addrinfo hints, *res, *p;
    SOCKET sock = INVALID_SOCKET;
    char pbuf[8];

    sprintf(pbuf, "%d", port);
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, pbuf, &hints, &res) != 0) return INVALID_SOCKET;

    for (p = res; p != NULL; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == INVALID_SOCKET) continue;
        if (connect(sock, p->ai_addr, (int)p->ai_addrlen) == 0) break;
        closesocket(sock);
        sock = INVALID_SOCKET;
    }

    freeaddrinfo(res);
    return sock;
}

void tunnel_https(SOCKET client, SOCKET server) {
    char buf[BUFFER_SIZE];
    fd_set fds;

    send(client, "HTTP/1.1 200 Connection Established\r\n\r\n", 39, 0);

    for (;;) {
        FD_ZERO(&fds);
        FD_SET(client, &fds);
        FD_SET(server, &fds);

        if (select(0, &fds, NULL, NULL, NULL) <= 0) break;

        if (FD_ISSET(client, &fds)) {
            int n = recv(client, buf, BUFFER_SIZE, 0);
            if (n <= 0) break;
            send(server, buf, n, 0);
        }

        if (FD_ISSET(server, &fds)) {
            int n = recv(server, buf, BUFFER_SIZE, 0);
            if (n <= 0) break;
            send(client, buf, n, 0);
        }
    }
}

DWORD WINAPI client_handler(LPVOID arg) {
    SOCKET client = (SOCKET)arg;

    struct sockaddr_in addr;
    int len = sizeof(addr);
    getpeername(client, (struct sockaddr *)&addr, &len);
    char *ip = inet_ntoa(addr.sin_addr);

    char buffer[BUFFER_SIZE];
    ParsedRequest req;

    int n = recv(client, buffer, BUFFER_SIZE, 0);
    if (n <= 0) {
        closesocket(client);
        return 0;
    }

    if (decode_http_request(buffer, &req) != 0) {
        closesocket(client);
        return 0;
    }

    if (host_blocked(req.host)) {
        write_log_entry(ip, req.host, 403);
        const char *msg =
            "HTTP/1.1 403 Forbidden\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 13\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Access Denied";
        send(client, msg, (int)strlen(msg), 0);
        closesocket(client);
        return 0;
    }

    write_log_entry(ip, req.host, 200);

    SOCKET upstream = open_connection(req.host, req.port);
    if (upstream == INVALID_SOCKET) {
        write_log_entry(ip, req.host, 502);
        closesocket(client);
        return 0;
    }

    if (strcmp(req.method, "CONNECT") == 0) {
        tunnel_https(client, upstream);
    } else {
        send(upstream, buffer, n, 0);
        while (1) {
            int r = recv(upstream, buffer, BUFFER_SIZE, 0);
            if (r <= 0) break;
            send(client, buffer, r, 0);
        }
    }

    closesocket(upstream);
    closesocket(client);
    return 0;
}

int main() {
    read_server_config("../config/server.conf");

    WSADATA wsa;
    SOCKET listener, client;
    struct sockaddr_in srv;
    int slen = sizeof(srv);

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    CreateDirectory("logs", NULL);
    log_mutex = CreateMutex(NULL, FALSE, NULL);

    listener = socket(AF_INET, SOCK_STREAM, 0);
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port = htons(cfg.port);

    if (bind(listener, (struct sockaddr *)&srv, sizeof(srv)) == SOCKET_ERROR) {
        printf("Bind failed on port %d\n", cfg.port);
        return 1;
    }

    listen(listener, 10);

    printf("Proxy listening on %d\n", cfg.port);
    printf("Log path: %s\n", cfg.log_path);
    printf("Blocked file: %s\n", cfg.blocked_file);

    while (1) {
        client = accept(listener, (struct sockaddr *)&srv, &slen);
        if (client != INVALID_SOCKET) {
            HANDLE t = CreateThread(NULL, 0, client_handler, (LPVOID)client, 0, NULL);
            if (t) CloseHandle(t);
        }
    }

    CloseHandle(log_mutex);
    closesocket(listener);
    WSACleanup();
    return 0;
}
