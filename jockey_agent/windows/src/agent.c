#include "jockey_vm.h"
#include "jky_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_BYTECODE (16 * 1024 * 1024)

static int read_exact(SOCKET fd, void *buf, size_t n) {
    uint8_t *p = buf;
    size_t got = 0;
    while (got < n) {
        int r = recv(fd, (char *)(p + got), (int)(n - got), 0);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

static int write_exact(SOCKET fd, const void *buf, size_t n) {
    const uint8_t *p = buf;
    size_t sent = 0;
    while (sent < n) {
        int r = send(fd, (const char *)(p + sent), (int)(n - sent), 0);
        if (r <= 0) return -1;
        sent += (size_t)r;
    }
    return 0;
}

static void handle_client(SOCKET fd) {
    uint32_t sz_be = 0;
    if (read_exact(fd, &sz_be, 4) != 0) return;
    uint32_t sz = ntohl(sz_be);

    if (sz == 0 || sz > MAX_BYTECODE) {
        uint32_t zero = 0;
        write_exact(fd, &zero, 4);
        write_exact(fd, &zero, 4);
        return;
    }

    uint8_t *bytecode = malloc(sz);
    if (!bytecode) return;
    if (read_exact(fd, bytecode, sz) != 0) { free(bytecode); return; }

    FILE *ms = tmpfile();
    if (!ms) { free(bytecode); return; }

    VM vm;
    vm_init(&vm);
    vm_set_output(&vm, ms);

    int rc = 0;
    if (vm_load(&vm, bytecode, sz) != 0) {
        fprintf(ms, "agent: bad bytecode\n");
        rc = 1;
    } else {
        rc = vm_execute(&vm);
        if (rc != 0) rc = 2;
    }
    vm_free(&vm);
    free(bytecode);

    fflush(ms);
    long outlen = ftell(ms);
    if (outlen < 0) outlen = 0;
    char *outbuf = malloc((size_t)outlen + 1);
    rewind(ms);
    size_t got = outbuf ? fread(outbuf, 1, (size_t)outlen, ms) : 0;
    if (outbuf) outbuf[got] = 0;
    fclose(ms);

    uint32_t rc_be  = htonl((uint32_t)rc);
    uint32_t len_be = htonl((uint32_t)got);
    write_exact(fd, &rc_be, 4);
    write_exact(fd, &len_be, 4);
    if (got > 0) write_exact(fd, outbuf, got);
    free(outbuf);
}

int agent_main(int argc, char **argv) {
    int port = 9090;
    WSADATA wsa;
    SOCKET srv;
    struct sockaddr_in sa;
    int opt = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--serve") == 0 && i + 1 < argc && argv[i+1][0] != '-')
            port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = atoi(argv[++i]);
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) {
        fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons((u_short)port);
    sa.sin_addr.s_addr = INADDR_ANY;

    if (bind(srv, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
        closesocket(srv); WSACleanup();
        return 1;
    }
    if (listen(srv, 8) != 0) {
        fprintf(stderr, "listen failed: %d\n", WSAGetLastError());
        closesocket(srv); WSACleanup();
        return 1;
    }

    printf("jockey-agent listening on 0.0.0.0:%d\n", port);
    fflush(stdout);

    for (;;) {
        SOCKET cfd = accept(srv, NULL, NULL);
        if (cfd == INVALID_SOCKET) continue;
        handle_client(cfd);
        closesocket(cfd);
    }

    closesocket(srv);
    WSACleanup();
    return 0;
}