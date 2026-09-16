#include "jockey_vm.h"
#include "jky_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>

#define MAX_BYTECODE (16 * 1024 * 1024)

static int read_exact(int fd, void *buf, size_t n) {
    uint8_t *p = buf;
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, p + got, n - got, 0);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

static int write_exact(int fd, const void *buf, size_t n) {
    const uint8_t *p = buf;
    size_t sent = 0;
    while (sent < n) {
        ssize_t r = send(fd, p + sent, n - sent, 0);
        if (r <= 0) return -1;
        sent += (size_t)r;
    }
    return 0;
}

static void handle_client(int fd) {
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
    if (read_exact(fd, bytecode, sz) != 0) {
        free(bytecode);
        return;
    }

    char  *outbuf = NULL;
    size_t outlen = 0;
    FILE  *ms = open_memstream(&outbuf, &outlen);
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

    fclose(ms);

    uint32_t rc_be  = htonl((uint32_t)rc);
    uint32_t len_be = htonl((uint32_t)outlen);
    write_exact(fd, &rc_be, 4);
    write_exact(fd, &len_be, 4);
    if (outlen > 0) write_exact(fd, outbuf, outlen);

    free(outbuf);
}

int main(int argc, char **argv) {
    int port = 9090;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--serve") == 0 && i + 1 < argc) port = atoi(argv[++i]);
    }

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons((uint16_t)port);
    sa.sin_addr.s_addr = INADDR_ANY;

    if (bind(srv, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        perror("bind");
        return 1;
    }
    if (listen(srv, 8) != 0) { perror("listen"); return 1; }

    printf("jockey-agent listening on 0.0.0.0:%d\n", port);
    fflush(stdout);

    for (;;) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int cfd = accept(srv, (struct sockaddr *)&ca, &cl);
        if (cfd < 0) continue;
        handle_client(cfd);
        close(cfd);
    }
    return 0;
}
