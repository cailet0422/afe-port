/* pc_upnp.c - asks the home router to forward the room's UDP port (UPnP IGD)
 *
 * SSDP finds the router (multicast M-SEARCH), its description gives the
 * WANIPConnection (or WANPPPConnection) control URL, and a SOAP
 * AddPortMapping forwards the port to this machine. Blocking: run it on a
 * thread. Routers without UPnP simply do not answer; the room then relies on
 * hole punching (pc_dht.h). No game code. */
#include "pc_upnp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_control[256];  /* path of the control URL */
static PcNetAddr s_router;   /* host:port of the router's HTTP server */
static char s_service[96];   /* urn:schemas-upnp-org:service:WANIPConnection:1 */

static const char* find_ci(const char* hay, const char* needle) {
    size_t n = strlen(needle);

    for (; *hay; hay++) {
        size_t i;

        for (i = 0; i < n; i++) {
            char a = hay[i], b = needle[i];

            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (i == n) return hay;
    }
    return NULL;
}

/* "http://192.168.0.1:5000/rootDesc.xml" into the address and the path */
static int parse_url(const char* url, PcNetAddr* addr, char* path, size_t n) {
    const char* p = url;
    char host[64];
    size_t i = 0;

    if (strncmp(p, "http://", 7) != 0) return 0;
    p += 7;
    while (*p && *p != '/' && i < sizeof(host) - 1) host[i++] = *p++;
    host[i] = '\0';
    if (!pc_net_addr_parse(host, 80, addr)) return 0;
    snprintf(path, n, "%s", *p ? p : "/");
    return 1;
}

static int http(const PcNetAddr* to, const char* request, char* reply, size_t cap) {
    return pc_net_tcp_exchange(to, request, strlen(request), reply, cap, 2500);
}

static int discover(void) {
    static const char* const targets[] = {
        "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
        "urn:schemas-upnp-org:service:WANIPConnection:1",
        "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
    };
    PcNetSocket* s = pc_net_open(0);
    PcNetAddr group = { 0xEFFFFFFAu, 1900 }; /* 239.255.255.250 */
    char location[256] = "";
    char buf[2048];

    if (s == NULL) return 0;
    for (int t = 0; t < 3 && location[0] == '\0'; t++) {
        char req[256];
        int n = snprintf(req, sizeof(req),
                         "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: "
                         "%s\r\n\r\n",
                         targets[t]);

        pc_net_send(s, &group, req, (size_t)n);
        uint32_t start = pc_net_ms();
        while (pc_net_ms() - start < 2200 && location[0] == '\0') {
            PcNetAddr from;
            int got = pc_net_recv(s, &from, buf, sizeof(buf) - 1);

            if (got <= 0) {
                pc_net_sleep(20);
                continue;
            }
            buf[got] = '\0';
            const char* loc = find_ci(buf, "\nLOCATION:");
            if (loc != NULL) {
                loc += 10;
                while (*loc == ' ') loc++;
                size_t i = 0;
                while (loc[i] && loc[i] != '\r' && loc[i] != '\n' && i < sizeof(location) - 1) {
                    location[i] = loc[i];
                    i++;
                }
                location[i] = '\0';
            }
        }
    }
    pc_net_close(s);
    if (location[0] == '\0') return 0;

    /* the description: the WAN connection service's control URL */
    char path[256], req[512];
    char* desc = (char*)malloc(65536);
    int ok = 0;

    if (desc == NULL || !parse_url(location, &s_router, path, sizeof(path))) {
        free(desc);
        return 0;
    }
    {
        char host[32];

        pc_net_addr_str(&s_router, host, sizeof(host));
        snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    }
    if (http(&s_router, req, desc, 65536) > 0) {
        static const char* const services[] = {
            "urn:schemas-upnp-org:service:WANIPConnection:2",
            "urn:schemas-upnp-org:service:WANIPConnection:1",
            "urn:schemas-upnp-org:service:WANPPPConnection:1",
        };
        for (int i = 0; i < 3 && !ok; i++) {
            const char* svc = strstr(desc, services[i]);
            const char* ctl = svc ? find_ci(svc, "<controlURL>") : NULL;

            if (ctl == NULL) continue;
            ctl += 12;
            const char* end = strchr(ctl, '<');
            if (end == NULL) continue;
            size_t len = (size_t)(end - ctl);
            if (len >= sizeof(s_control)) continue;
            if (strncmp(ctl, "http://", 7) == 0) {
                char tmp[256];

                memcpy(tmp, ctl, len);
                tmp[len] = '\0';
                ok = parse_url(tmp, &s_router, s_control, sizeof(s_control));
            } else {
                if (ctl[0] == '/') {
                    memcpy(s_control, ctl, len);
                    s_control[len] = '\0';
                } else {
                    snprintf(s_control, sizeof(s_control), "/%.*s", (int)len, ctl);
                }
                ok = 1;
            }
            snprintf(s_service, sizeof(s_service), "%s", services[i]);
        }
    }
    free(desc);
    return ok;
}

static int soap(const char* action, const char* args, char* reply, size_t cap) {
    char body[1024], req[2048], host[32];
    int blen = snprintf(body, sizeof(body),
                        "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:%s xmlns:u=\"%s\">%s</"
                        "u:%s></s:Body></s:Envelope>\r\n",
                        action, s_service, args, action);

    pc_net_addr_str(&s_router, host, sizeof(host));
    snprintf(req, sizeof(req),
             "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: "
             "%d\r\nSOAPAction: \"%s#%s\"\r\nConnection: close\r\n\r\n%s",
             s_control, host, blen, s_service, action, body);
    if (http(&s_router, req, reply, cap) <= 0) return 0;
    return strstr(reply, " 200 ") != NULL;
}

int pc_upnp_map(uint16_t port, uint32_t* external_ip) {
    char args[512], reply[4096], ip[24];
    uint32_t local = pc_net_local_ip();

    if (local == 0 || !discover()) return 0;
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u", local >> 24, (local >> 16) & 255, (local >> 8) & 255, local & 255);
    snprintf(args, sizeof(args),
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>UDP</NewProtocol>"
             "<NewInternalPort>%u</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
             "<NewPortMappingDescription>Dobutsu no Mori e+ room</NewPortMappingDescription>"
             "<NewLeaseDuration>7200</NewLeaseDuration>",
             port, port, ip);
    if (!soap("AddPortMapping", args, reply, sizeof(reply))) {
        /* some routers take no lease time */
        char* lease = strstr(args, "<NewLeaseDuration>7200");
        if (lease != NULL) memcpy(lease + 18, "0   ", 4);
        if (!soap("AddPortMapping", args, reply, sizeof(reply))) return 0;
    }
    if (external_ip != NULL) {
        *external_ip = 0;
        if (soap("GetExternalIPAddress", "", reply, sizeof(reply))) {
            const char* p = strstr(reply, "<NewExternalIPAddress>");
            PcNetAddr a;

            if (p != NULL && pc_net_addr_parse(p + 22, 1, &a)) *external_ip = a.ip;
            else if (p != NULL) {
                char tmp[24];
                size_t i = 0;

                p += 22;
                while (p[i] && p[i] != '<' && i < sizeof(tmp) - 1) {
                    tmp[i] = p[i];
                    i++;
                }
                tmp[i] = '\0';
                if (pc_net_addr_parse(tmp, 1, &a)) *external_ip = a.ip;
            }
        }
    }
    return 1;
}

void pc_upnp_unmap(uint16_t port) {
    char args[256], reply[2048];

    if (s_control[0] == '\0') return;
    snprintf(args, sizeof(args),
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>UDP</NewProtocol>", port);
    soap("DeletePortMapping", args, reply, sizeof(reply));
}
