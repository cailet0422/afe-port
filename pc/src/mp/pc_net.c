/* pc_net.c - see pc_net.h */
#include "pc_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define close_sock closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define close_sock close
#endif

struct PcNetSocket {
    sock_t fd;
    uint16_t port;
};

int pc_net_init(void) {
#ifdef _WIN32
    static int done = 0;

    if (!done) {
        WSADATA wsa;

        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return 0;
        }
        done = 1;
    }
#endif
    return 1;
}

static void to_sockaddr(const PcNetAddr* a, struct sockaddr_in* sa) {
    memset(sa, 0, sizeof(*sa));
    sa->sin_family = AF_INET;
    sa->sin_addr.s_addr = htonl(a->ip);
    sa->sin_port = htons(a->port);
}

static void set_nonblocking(sock_t fd) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(fd, FIONBIO, &on);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

static PcNetSocket* open_socket(uint16_t port, int shared) {
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    PcNetSocket* s;
    sock_t fd;
    int on = 1;

    if (!pc_net_init()) {
        return NULL;
    }
    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == BAD_SOCK) {
        return NULL;
    }
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof(on));
    if (shared) {
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
#ifdef SO_REUSEPORT
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const char*)&on, sizeof(on));
#endif
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
        close_sock(fd);
        return NULL;
    }
    set_nonblocking(fd);
#ifdef _WIN32
    {
        /* Windows reports an ICMP "port unreachable" for an earlier
         * packet (a DHT node gone, a guest's old port) as an error of the
         * next recvfrom: not an error of this socket, so not at all */
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
        BOOL report = FALSE;
        DWORD bytes = 0;

        WSAIoctl(fd, SIO_UDP_CONNRESET, &report, sizeof(report), NULL, 0, &bytes, NULL, NULL);
    }
#endif
    {
        int size = 1 << 20;
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char*)&size, sizeof(size));
    }
    s = (PcNetSocket*)calloc(1, sizeof(*s));
    if (s == NULL) {
        close_sock(fd);
        return NULL;
    }
    s->fd = fd;
    getsockname(fd, (struct sockaddr*)&sa, &len);
    s->port = ntohs(sa.sin_port);
    return s;
}

PcNetSocket* pc_net_open(uint16_t port) {
    return open_socket(port, 0);
}

PcNetSocket* pc_net_open_shared(uint16_t port) {
    return open_socket(port, 1);
}

void pc_net_close(PcNetSocket* s) {
    if (s != NULL) {
        close_sock(s->fd);
        free(s);
    }
}

uint16_t pc_net_port(PcNetSocket* s) {
    return s != NULL ? s->port : 0;
}

/* tests: drop this share of packets (percent) */
int pc_net_test_drop_percent = 0;

int pc_net_send(PcNetSocket* s, const PcNetAddr* to, const void* data, size_t len) {
    struct sockaddr_in sa;

    if (s == NULL || to->ip == 0 || to->port == 0) {
        return 0;
    }
    if (pc_net_test_drop_percent > 0 && rand() % 100 < pc_net_test_drop_percent) {
        return 1;
    }
    to_sockaddr(to, &sa);
    return sendto(s->fd, (const char*)data, (int)len, 0, (struct sockaddr*)&sa, sizeof(sa)) == (int)len;
}

int pc_net_recv(PcNetSocket* s, PcNetAddr* from, void* buf, size_t cap) {
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    int n;

    if (s == NULL) {
        return -1;
    }
    for (int tries = 0;; tries++) {
        len = sizeof(sa);
        n = (int)recvfrom(s->fd, (char*)buf, (int)cap, 0, (struct sockaddr*)&sa, &len);
#ifdef _WIN32
        /* a port that refused an earlier packet (ICMP): not this socket's
         * error, and the packets after it are still to be read */
        if (n < 0 && WSAGetLastError() == WSAECONNRESET && tries < 64) continue;
#else
        if (n < 0 && (errno == ECONNREFUSED || errno == EINTR) && tries < 64) continue;
#endif
        break;
    }
    if (n < 0) {
#ifdef _WIN32
        int err = WSAGetLastError();

        return err == WSAEWOULDBLOCK || err == WSAECONNRESET ? 0 : -1;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED || errno == EINTR ? 0 : -1;
#endif
    }
    from->ip = ntohl(sa.sin_addr.s_addr);
    from->port = ntohs(sa.sin_port);
    return n;
}

int pc_net_join_multicast(PcNetSocket* s, uint32_t group) {
    struct ip_mreq mreq;

    if (s == NULL) {
        return 0;
    }
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = htonl(group);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    return setsockopt(s->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq)) == 0;
}

int pc_net_resolve(const char* host, uint16_t port, PcNetAddr* out) {
    struct addrinfo hints, *res = NULL;
    int ok = 0;

    if (!pc_net_init()) {
        return 0;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) == 0 && res != NULL) {
        const struct sockaddr_in* sa = (const struct sockaddr_in*)res->ai_addr;

        out->ip = ntohl(sa->sin_addr.s_addr);
        out->port = port;
        ok = out->ip != 0;
    }
    if (res != NULL) {
        freeaddrinfo(res);
    }
    return ok;
}

uint32_t pc_net_local_ip(void) {
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    uint32_t ip = 0;
    sock_t fd;

    if (!pc_net_init()) {
        return 0;
    }
    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == BAD_SOCK) {
        return 0;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(0x08080808); /* any routable address: nothing is sent */
    sa.sin_port = htons(53);
    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) == 0 && getsockname(fd, (struct sockaddr*)&sa, &len) == 0) {
        ip = ntohl(sa.sin_addr.s_addr);
    }
    close_sock(fd);
    return ip;
}

int pc_net_broadcast_addrs(uint32_t* out, int max) {
    int n = 0;

    if (max <= 0) {
        return 0;
    }
#ifdef _WIN32
    {
        ULONG size = 16384;
        IP_ADAPTER_ADDRESSES* list = (IP_ADAPTER_ADDRESSES*)malloc(size);

        if (list != NULL && GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                                               GAA_FLAG_SKIP_DNS_SERVER,
                                                 NULL, list, &size) == NO_ERROR) {
            for (IP_ADAPTER_ADDRESSES* a = list; a != NULL && n < max - 1; a = a->Next) {
                if (a->OperStatus != IfOperStatusUp) continue;
                for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u != NULL && n < max - 1; u = u->Next) {
                    struct sockaddr_in* sa = (struct sockaddr_in*)u->Address.lpSockaddr;
                    uint32_t ip = ntohl(sa->sin_addr.s_addr);
                    int prefix = u->OnLinkPrefixLength;
                    uint32_t mask = prefix <= 0 ? 0 : prefix >= 32 ? 0xFFFFFFFFu : ~((1u << (32 - prefix)) - 1);

                    if ((ip >> 24) == 127 || mask == 0xFFFFFFFFu) continue;
                    out[n++] = ip | ~mask;
                }
            }
        }
        free(list);
    }
#elif defined(__ANDROID__) && __ANDROID_API__ < 24
    /* no getifaddrs before Android 7: the general broadcast only */
#else
    {
        struct ifaddrs* list = NULL;

        if (getifaddrs(&list) == 0) {
            for (struct ifaddrs* a = list; a != NULL && n < max - 1; a = a->ifa_next) {
                if (a->ifa_addr == NULL || a->ifa_addr->sa_family != AF_INET || a->ifa_netmask == NULL) continue;
                if ((a->ifa_flags & IFF_UP) == 0 || (a->ifa_flags & IFF_LOOPBACK)) continue;
                uint32_t ip = ntohl(((struct sockaddr_in*)a->ifa_addr)->sin_addr.s_addr);
                uint32_t mask = ntohl(((struct sockaddr_in*)a->ifa_netmask)->sin_addr.s_addr);
                if (mask == 0xFFFFFFFFu) continue;
                out[n++] = ip | ~mask;
            }
            freeifaddrs(list);
        }
    }
#endif
    out[n++] = 0xFFFFFFFFu;
    return n;
}

int pc_net_addr_equal(const PcNetAddr* a, const PcNetAddr* b) {
    return a->ip == b->ip && a->port == b->port;
}

void pc_net_addr_str(const PcNetAddr* a, char* buf, size_t n) {
    snprintf(buf, n, "%u.%u.%u.%u:%u", (a->ip >> 24) & 255, (a->ip >> 16) & 255, (a->ip >> 8) & 255, a->ip & 255,
             a->port);
}

int pc_net_addr_parse(const char* s, uint16_t default_port, PcNetAddr* out) {
    unsigned a, b, c, d, port = default_port;
    char tail;
    int n = sscanf(s, "%u.%u.%u.%u:%u%c", &a, &b, &c, &d, &port, &tail);

    if (n != 4 && n != 5) {
        return 0;
    }
    if (a > 255 || b > 255 || c > 255 || d > 255 || port == 0 || port > 65535) {
        return 0;
    }
    out->ip = (a << 24) | (b << 16) | (c << 8) | d;
    out->port = (uint16_t)port;
    return 1;
}

int pc_net_ip_private(uint32_t ip) {
    return (ip >> 24) == 10 || (ip >> 20) == (0xAC10 >> 4) || (ip >> 16) == 0xC0A8 || (ip >> 22) == (0x6440 >> 6) ||
           (ip >> 24) == 127 || (ip >> 16) == 0xA9FE;
}

uint32_t pc_net_ms(void) {
#ifdef _WIN32
    return (uint32_t)GetTickCount();
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
#endif
}

void pc_net_sleep(uint32_t ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

/* --- TCP --- */

static int wait_fd(sock_t fd, int for_write, int timeout_ms) {
    fd_set set;
    struct timeval tv;

    FD_ZERO(&set);
    FD_SET(fd, &set);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return select((int)fd + 1, for_write ? NULL : &set, for_write ? &set : NULL, NULL, &tv) > 0;
}

int pc_net_tcp_exchange(const PcNetAddr* to, const char* request, size_t req_len, char* buf, size_t cap,
                        int timeout_ms) {
    struct sockaddr_in sa;
    sock_t fd;
    size_t got = 0;
    size_t sent = 0;

    if (!pc_net_init() || cap == 0) {
        return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == BAD_SOCK) {
        return -1;
    }
    set_nonblocking(fd);
    to_sockaddr(to, &sa);
    connect(fd, (struct sockaddr*)&sa, sizeof(sa));
    if (!wait_fd(fd, 1, timeout_ms)) {
        close_sock(fd);
        return -1;
    }
    {
        int err = 0;
        socklen_t len = sizeof(err);

        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&err, &len) != 0 || err != 0) {
            close_sock(fd);
            return -1;
        }
    }
    while (sent < req_len) {
        int n = (int)send(fd, request + sent, (int)(req_len - sent), 0);

        if (n <= 0) {
#ifdef _WIN32
            int again = n < 0 && WSAGetLastError() == WSAEWOULDBLOCK;
#else
            int again = n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
#endif
            if (!again || !wait_fd(fd, 1, timeout_ms)) {
                close_sock(fd);
                return -1;
            }
            continue;
        }
        sent += (size_t)n;
    }
    for (;;) {
        int n;

        if (got + 1 >= cap || !wait_fd(fd, 0, timeout_ms)) {
            break;
        }
        n = (int)recv(fd, buf + got, (int)(cap - 1 - got), 0);
        if (n <= 0) {
            break;
        }
        got += (size_t)n;
    }
    close_sock(fd);
    buf[got] = '\0';
    return (int)got;
}
