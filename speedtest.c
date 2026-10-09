#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <getopt.h>
  #define CLOSE_SOCKET closesocket
  #define SLEEP_SEC(s) Sleep((s) * 1000)
  #define USLEEP(us) Sleep((us) / 1000)
  typedef SOCKET socket_t;
  #define IS_INVALID_SOCKET(s) ((s) == INVALID_SOCKET)
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <netdb.h>
  #define CLOSE_SOCKET close
  #define SLEEP_SEC(s) sleep(s)
  #define USLEEP(us) usleep(us)
  typedef int socket_t;
  #define IS_INVALID_SOCKET(s) ((s) < 0)
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>

static const char* my_strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    size_t nlen = strlen(needle);
    if (nlen == 0) return haystack;
    for (; *haystack; haystack++) {
#ifdef _WIN32
        if (_strnicmp(haystack, needle, nlen) == 0) {
            return haystack;
        }
#else
        if (strncasecmp(haystack, needle, nlen) == 0) {
            return haystack;
        }
#endif
    }
    return NULL;
}

#define MAX_SERVERS 64
#define DEFAULT_BLOCK_SIZE 25000000ULL
#define BUF_SIZE (128 * 1024)
#define SOCK_BUF_SIZE (2 * 1024 * 1024)

typedef struct {
    char id[32];
    char sponsor[128];
    char name[128];
    char country[64];
    char host[256];
    int https_functional;
    double ping_ms;
} server_info_t;

static server_info_t g_servers[MAX_SERVERS];
static int g_server_count = 0;

static char g_url[1024] = {0};
static char g_host[256] = {0};
static char g_port[16] = "8080";
static char g_path[512] = "/download?size=25000000";
static int g_is_https = 1;
static uint64_t g_block_size = DEFAULT_BLOCK_SIZE;

static atomic_uint_fast64_t g_total_bytes = 0;
static atomic_uint_fast32_t g_completed_blocks = 0;
static volatile sig_atomic_t g_running = 1;

static SSL_CTX *g_ssl_ctx = NULL;
static char g_upload_buf[BUF_SIZE];

typedef struct {
    int thread_id;
    int timeout;
} thread_arg_t;

static void sigint_handler(int sig) {
    (void)sig;
    g_running = 0;
}

// Cross-platform thread-safe PRNG (Xorshift32)
static inline uint32_t fast_rand(unsigned int *seed) {
    uint32_t x = *seed;
    if (x == 0) x = (uint32_t)(uintptr_t)seed ^ 0x5bf03635;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *seed = x;
    return x;
}

static inline void generate_uuid(char *out, size_t out_len, unsigned int *seed) {
    snprintf(out, out_len, "%04x%04x-%04x-%04x-%04x-%04x%04x%04x",
             fast_rand(seed) & 0xffff, fast_rand(seed) & 0xffff,
             fast_rand(seed) & 0xffff,
             (fast_rand(seed) & 0x0fff) | 0x4000,
             (fast_rand(seed) & 0x3fff) | 0x8000,
             fast_rand(seed) & 0xffff, fast_rand(seed) & 0xffff, fast_rand(seed) & 0xffff);
}

static void parse_url(const char *url) {
    const char *p = url;
    g_is_https = 0;
    strcpy(g_port, "80");

    if (strncmp(p, "https://", 8) == 0) {
        g_is_https = 1;
        strcpy(g_port, "443");
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        g_is_https = 0;
        strcpy(g_port, "80");
        p += 7;
    }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    if (colon && (!slash || colon < slash)) {
        size_t host_len = (size_t)(colon - p);
        if (host_len >= sizeof(g_host)) host_len = sizeof(g_host) - 1;
        memcpy(g_host, p, host_len);
        g_host[host_len] = '\0';

        const char *port_start = colon + 1;
        size_t port_len = slash ? (size_t)(slash - port_start) : strlen(port_start);
        if (port_len >= sizeof(g_port)) port_len = sizeof(g_port) - 1;
        memcpy(g_port, port_start, port_len);
        g_port[port_len] = '\0';
    } else if (slash) {
        size_t host_len = (size_t)(slash - p);
        if (host_len >= sizeof(g_host)) host_len = sizeof(g_host) - 1;
        memcpy(g_host, p, host_len);
        g_host[host_len] = '\0';
    } else {
        strncpy(g_host, p, sizeof(g_host) - 1);
        g_host[sizeof(g_host) - 1] = '\0';
    }

    if (slash) {
        strncpy(g_path, slash, sizeof(g_path) - 1);
        g_path[sizeof(g_path) - 1] = '\0';
    } else {
        strcpy(g_path, "/");
    }

    char *size_pos = strstr(g_path, "size=");
    if (size_pos) {
        uint64_t parsed_size = strtoull(size_pos + 5, NULL, 10);
        if (parsed_size > 0) {
            g_block_size = parsed_size;
        }
    }
}

static socket_t connect_socket(const char *host, const char *port, int timeout_sec) {
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
#ifdef _WIN32
        return INVALID_SOCKET;
#else
        return -1;
#endif
    }

    socket_t fd;
#ifdef _WIN32
    fd = INVALID_SOCKET;
#else
    fd = -1;
#endif

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (IS_INVALID_SOCKET(fd)) continue;

#ifdef _WIN32
        DWORD tv = timeout_sec * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#else
        struct timeval tv;
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#endif

        int sock_buf = SOCK_BUF_SIZE;
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char*)&sock_buf, sizeof(sock_buf));
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char*)&sock_buf, sizeof(sock_buf));

        int flag = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(int));

        if (connect(fd, rp->ai_addr, (int)rp->ai_addrlen) == 0) {
            break;
        }
        CLOSE_SOCKET(fd);
#ifdef _WIN32
        fd = INVALID_SOCKET;
#else
        fd = -1;
#endif
    }

    freeaddrinfo(res);
    return fd;
}

static char* https_get_string(const char *host, const char *port, const char *path) {
    socket_t fd = connect_socket(host, port, 5);
    if (IS_INVALID_SOCKET(fd)) return NULL;

    SSL *ssl = SSL_new(g_ssl_ctx);
    if (!ssl) {
        CLOSE_SOCKET(fd);
        return NULL;
    }
    SSL_set_fd(ssl, (int)fd);
    SSL_set_tlsext_host_name(ssl, host);

    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl);
        CLOSE_SOCKET(fd);
        return NULL;
    }

    char req[1024];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: Mozilla/5.0 (compatible; Speedtest-C/1.0)\r\n"
             "Accept: */*\r\n"
             "Connection: close\r\n\r\n",
             path, host);

    SSL_write(ssl, req, (int)strlen(req));

    size_t cap = 64 * 1024;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        SSL_free(ssl);
        CLOSE_SOCKET(fd);
        return NULL;
    }

    while (1) {
        if (len + 4096 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) break;
            buf = nb;
        }
        int n = SSL_read(ssl, buf + len, 4096);
        if (n <= 0) break;
        len += n;
    }
    buf[len] = '\0';

    SSL_shutdown(ssl);
    SSL_free(ssl);
    CLOSE_SOCKET(fd);

    char *body = strstr(buf, "\r\n\r\n");
    if (body) {
        char *result = strdup(body + 4);
        free(buf);
        return result;
    }

    free(buf);
    return NULL;
}

static void extract_json_val(const char *json, const char *key, char *out, size_t out_max) {
    out[0] = '\0';
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);
    char *p = strstr(json, search_key);
    if (!p) return;
    p += strlen(search_key);
    while (*p == ' ' || *p == '\t') p++;

    if (*p == '\"') {
        p++;
        size_t idx = 0;
        while (*p && *p != '\"' && idx < out_max - 1) {
            if (*p == '\\' && *(p+1) == '/') {
                out[idx++] = '/';
                p += 2;
            } else if (*p == '\\' && *(p+1)) {
                p++;
                out[idx++] = *p++;
            } else {
                out[idx++] = *p++;
            }
        }
        out[idx] = '\0';
    } else {
        size_t idx = 0;
        while (*p && *p != ',' && *p != '}' && *p != ']' && idx < out_max - 1) {
            out[idx++] = *p++;
        }
        out[idx] = '\0';
    }
}

static double measure_latency(const char *host_with_port) {
    char host[256];
    char port[16] = "8080";
    const char *colon = strchr(host_with_port, ':');
    if (colon) {
        size_t hlen = colon - host_with_port;
        if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
        memcpy(host, host_with_port, hlen);
        host[hlen] = '\0';
        strncpy(port, colon + 1, sizeof(port) - 1);
    } else {
        strncpy(host, host_with_port, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    socket_t fd = connect_socket(host, port, 2);
    if (IS_INVALID_SOCKET(fd)) return 9999.0;

    SSL *ssl = SSL_new(g_ssl_ctx);
    if (!ssl) {
        CLOSE_SOCKET(fd);
        return 9999.0;
    }
    SSL_set_fd(ssl, (int)fd);
    SSL_set_tlsext_host_name(ssl, host);

    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl);
        CLOSE_SOCKET(fd);
        return 9999.0;
    }

    char req[512];
    snprintf(req, sizeof(req),
             "GET /hello HTTP/1.1\r\n"
             "Host: %s:%s\r\n"
             "User-Agent: Speedtest-C/3.0\r\n"
             "Connection: close\r\n\r\n",
             host, port);
    SSL_write(ssl, req, (int)strlen(req));

    char buf[256];
    SSL_read(ssl, buf, sizeof(buf));

    clock_gettime(CLOCK_MONOTONIC, &ts_end);

    SSL_shutdown(ssl);
    SSL_free(ssl);
    CLOSE_SOCKET(fd);

    double ms = (ts_end.tv_sec - ts_start.tv_sec) * 1000.0 +
                (ts_end.tv_nsec - ts_start.tv_nsec) / 1e6;
    return ms;
}

typedef struct {
    int index;
} ping_task_t;

static void* ping_worker(void *arg) {
    ping_task_t *task = (ping_task_t*)arg;
    int idx = task->index;
    g_servers[idx].ping_ms = measure_latency(g_servers[idx].host);
    return NULL;
}

static int fetch_servers(int max_limit) {
    printf("🔍 Fetching Speedtest.net server list...\n");
    char path[128];
    snprintf(path, sizeof(path), "/api/js/servers?engine=js&limit=%d", max_limit);

    char *json = https_get_string("www.speedtest.net", "443", path);
    if (!json) {
        fprintf(stderr, "⚠️  Failed to retrieve server list, please check your network.\n");
        return 0;
    }

    g_server_count = 0;
    char *obj_start = strchr(json, '{');
    while (obj_start && g_server_count < MAX_SERVERS) {
        char *obj_end = strchr(obj_start, '}');
        if (!obj_end) break;

        size_t obj_len = (size_t)(obj_end - obj_start + 1);
        char *obj_str = malloc(obj_len + 1);
        if (obj_str) {
            memcpy(obj_str, obj_start, obj_len);
            obj_str[obj_len] = '\0';

            extract_json_val(obj_str, "id", g_servers[g_server_count].id, sizeof(g_servers[0].id));
            extract_json_val(obj_str, "sponsor", g_servers[g_server_count].sponsor, sizeof(g_servers[0].sponsor));
            extract_json_val(obj_str, "name", g_servers[g_server_count].name, sizeof(g_servers[0].name));
            extract_json_val(obj_str, "country", g_servers[g_server_count].country, sizeof(g_servers[0].country));
            extract_json_val(obj_str, "host", g_servers[g_server_count].host, sizeof(g_servers[0].host));
            char https_func[16];
            extract_json_val(obj_str, "https_functional", https_func, sizeof(https_func));
            g_servers[g_server_count].https_functional = atoi(https_func);
            g_servers[g_server_count].ping_ms = 9999.0;

            if (strlen(g_servers[g_server_count].id) > 0 && strlen(g_servers[g_server_count].host) > 0) {
                g_server_count++;
            }
            free(obj_str);
        }
        obj_start = strchr(obj_end + 1, '{');
    }

    free(json);
    return g_server_count;
}

static void ping_all_servers() {
    printf("⚡ Measuring latency to %d candidate servers...\n", g_server_count);
    pthread_t threads[MAX_SERVERS];
    ping_task_t tasks[MAX_SERVERS];

    for (int i = 0; i < g_server_count; i++) {
        tasks[i].index = i;
        pthread_create(&threads[i], NULL, ping_worker, &tasks[i]);
    }

    for (int i = 0; i < g_server_count; i++) {
        pthread_join(threads[i], NULL);
    }

    for (int i = 0; i < g_server_count - 1; i++) {
        for (int j = 0; j < g_server_count - 1 - i; j++) {
            if (g_servers[j].ping_ms > g_servers[j+1].ping_ms) {
                server_info_t tmp = g_servers[j];
                g_servers[j] = g_servers[j+1];
                g_servers[j+1] = tmp;
            }
        }
    }
}

static void list_servers() {
    printf("\n%-8s %-26s %-16s %-14s %-10s\n", "ID", "Sponsor", "City", "Country", "Latency");
    printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < g_server_count; i++) {
        if (g_servers[i].ping_ms < 9000.0) {
            printf("%-8s %-26.26s %-16.16s %-14.14s %6.2f ms\n",
                   g_servers[i].id, g_servers[i].sponsor, g_servers[i].name,
                   g_servers[i].country, g_servers[i].ping_ms);
        } else {
            printf("%-8s %-26.26s %-16.16s %-14.14s %8s\n",
                   g_servers[i].id, g_servers[i].sponsor, g_servers[i].name,
                   g_servers[i].country, "Timeout");
        }
    }
    printf("--------------------------------------------------------------------------------\n");
}

static void* download_worker_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t*)arg;
    unsigned int seed = (unsigned int)(time(NULL) ^ (uintptr_t)pthread_self());
    char *recv_buf = malloc(BUF_SIZE);
    if (!recv_buf) return NULL;

    uint64_t local_bytes = 0;
    uint32_t local_completed = 0;

    while (g_running) {
        socket_t fd = connect_socket(g_host, g_port, targ->timeout);
        if (IS_INVALID_SOCKET(fd)) {
            USLEEP(100000);
            continue;
        }

        SSL *ssl = NULL;
        if (g_is_https) {
            ssl = SSL_new(g_ssl_ctx);
            if (!ssl) {
                CLOSE_SOCKET(fd);
                USLEEP(100000);
                continue;
            }
            SSL_set_fd(ssl, (int)fd);
            SSL_set_tlsext_host_name(ssl, g_host);
            if (SSL_connect(ssl) <= 0) {
                SSL_free(ssl);
                CLOSE_SOCKET(fd);
                USLEEP(100000);
                continue;
            }
        }

        while (g_running) {
            char nocache[64], guid[64];
            generate_uuid(nocache, sizeof(nocache), &seed);
            generate_uuid(guid, sizeof(guid), &seed);

            char req[2048];
            char sep = (strchr(g_path, '?') != NULL) ? '&' : '?';
            int req_len = snprintf(req, sizeof(req),
                     "GET %s%cnocache=%s&guid=%s HTTP/1.1\r\n"
                     "Host: %s:%s\r\n"
                     "User-Agent: Speedtest-C/3.0\r\n"
                     "Accept: */*\r\n"
                     "Connection: keep-alive\r\n\r\n",
                     g_path, sep, nocache, guid, g_host, g_port);

            int write_ret = g_is_https ? SSL_write(ssl, req, req_len) : (int)send(fd, req, req_len, 0);
            if (write_ret <= 0) break;

            int header_done = 0;
            int64_t content_length = -1;
            int64_t body_read = 0;
            char header_buf[4096];
            int header_len = 0;

            while (g_running && !header_done) {
                int n = g_is_https ? SSL_read(ssl, recv_buf, BUF_SIZE) : (int)recv(fd, recv_buf, BUF_SIZE, 0);
                if (n <= 0) break;

                int search_start = header_len > 3 ? header_len - 3 : 0;
                int copy_len = n;
                if (header_len + copy_len > (int)sizeof(header_buf) - 1) {
                    copy_len = (int)sizeof(header_buf) - 1 - header_len;
                }
                memcpy(header_buf + header_len, recv_buf, copy_len);
                header_len += copy_len;
                header_buf[header_len] = '\0';

                char *hdr_end = strstr(header_buf + search_start, "\r\n\r\n");
                if (hdr_end) {
                    header_done = 1;
                    const char *cl_pos = my_strcasestr(header_buf, "Content-Length:");
                    if (cl_pos) {
                        content_length = strtoll(cl_pos + 15, NULL, 10);
                    }
                    
                    size_t hdr_bytes_in_read = (size_t)(hdr_end + 4 - header_buf) - (header_len - copy_len);
                    size_t body_in_first_read = (size_t)n - hdr_bytes_in_read;
                    if (body_in_first_read > 0) {
                        local_bytes += body_in_first_read;
                        body_read += body_in_first_read;
                    }
                }
            }

            if (!header_done) break;

            if (content_length > 0) {
                while (g_running && body_read < content_length) {
                    size_t to_read = BUF_SIZE;
                    if ((int64_t)to_read > (content_length - body_read)) {
                        to_read = (size_t)(content_length - body_read);
                    }

                    int n = g_is_https ? SSL_read(ssl, recv_buf, (int)to_read) : (int)recv(fd, recv_buf, (int)to_read, 0);
                    if (n <= 0) break;

                    local_bytes += n;
                    body_read += n;

                    if (local_bytes >= (1024 * 1024)) {
                        atomic_fetch_add_explicit(&g_total_bytes, local_bytes, memory_order_relaxed);
                        local_bytes = 0;
                    }
                }

                if (body_read >= content_length && g_running) {
                    local_completed++;
                    if (local_completed >= 1) {
                        atomic_fetch_add_explicit(&g_completed_blocks, local_completed, memory_order_relaxed);
                        local_completed = 0;
                    }
                } else {
                    break;
                }
            } else {
                while (g_running) {
                    int n = g_is_https ? SSL_read(ssl, recv_buf, BUF_SIZE) : (int)recv(fd, recv_buf, BUF_SIZE, 0);
                    if (n <= 0) break;
                    local_bytes += n;
                }
                break;
            }

            if (local_bytes > 0) {
                atomic_fetch_add_explicit(&g_total_bytes, local_bytes, memory_order_relaxed);
                local_bytes = 0;
            }
        }

        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
        CLOSE_SOCKET(fd);
    }

    if (local_bytes > 0) {
        atomic_fetch_add_explicit(&g_total_bytes, local_bytes, memory_order_relaxed);
    }
    if (local_completed > 0) {
        atomic_fetch_add_explicit(&g_completed_blocks, local_completed, memory_order_relaxed);
    }

    free(recv_buf);
    return NULL;
}

static void* upload_worker_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t*)arg;
    unsigned int seed = (unsigned int)(time(NULL) ^ (uintptr_t)pthread_self());
    char resp_buf[1024];

    uint64_t local_bytes = 0;
    uint32_t local_completed = 0;

    while (g_running) {
        socket_t fd = connect_socket(g_host, g_port, targ->timeout);
        if (IS_INVALID_SOCKET(fd)) {
            USLEEP(100000);
            continue;
        }

        SSL *ssl = NULL;
        if (g_is_https) {
            ssl = SSL_new(g_ssl_ctx);
            if (!ssl) {
                CLOSE_SOCKET(fd);
                USLEEP(100000);
                continue;
            }
            SSL_set_fd(ssl, (int)fd);
            SSL_set_tlsext_host_name(ssl, g_host);
            if (SSL_connect(ssl) <= 0) {
                SSL_free(ssl);
                CLOSE_SOCKET(fd);
                USLEEP(100000);
                continue;
            }
        }

        while (g_running) {
            char nocache[64], guid[64];
            generate_uuid(nocache, sizeof(nocache), &seed);
            generate_uuid(guid, sizeof(guid), &seed);

            char req_hdr[2048];
            int hdr_len = snprintf(req_hdr, sizeof(req_hdr),
                     "POST /upload?nocache=%s&guid=%s HTTP/1.1\r\n"
                     "Host: %s:%s\r\n"
                     "User-Agent: Speedtest-C/3.0\r\n"
                     "Content-Type: application/octet-stream\r\n"
                     "Content-Length: %lu\r\n"
                     "Connection: keep-alive\r\n\r\n",
                     nocache, guid, g_host, g_port, (unsigned long)g_block_size);

            int write_ret = g_is_https ? SSL_write(ssl, req_hdr, hdr_len) : (int)send(fd, req_hdr, hdr_len, 0);
            if (write_ret <= 0) break;

            uint64_t body_sent = 0;
            while (g_running && body_sent < g_block_size) {
                size_t to_send = BUF_SIZE;
                if (to_send > (g_block_size - body_sent)) {
                    to_send = (size_t)(g_block_size - body_sent);
                }

                int n = g_is_https ? SSL_write(ssl, g_upload_buf, (int)to_send) : (int)send(fd, g_upload_buf, (int)to_send, 0);
                if (n <= 0) break;

                body_sent += n;
                local_bytes += n;

                if (local_bytes >= (1024 * 1024)) {
                    atomic_fetch_add_explicit(&g_total_bytes, local_bytes, memory_order_relaxed);
                    local_bytes = 0;
                }
            }

            if (body_sent >= g_block_size && g_running) {
                int resp_n = g_is_https ? SSL_read(ssl, resp_buf, sizeof(resp_buf) - 1) : (int)recv(fd, resp_buf, sizeof(resp_buf) - 1, 0);
                if (resp_n > 0) {
                    local_completed++;
                    if (local_completed >= 1) {
                        atomic_fetch_add_explicit(&g_completed_blocks, local_completed, memory_order_relaxed);
                        local_completed = 0;
                    }
                } else {
                    break;
                }
            } else {
                break;
            }

            if (local_bytes > 0) {
                atomic_fetch_add_explicit(&g_total_bytes, local_bytes, memory_order_relaxed);
                local_bytes = 0;
            }
        }

        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
        CLOSE_SOCKET(fd);
    }

    if (local_bytes > 0) {
        atomic_fetch_add_explicit(&g_total_bytes, local_bytes, memory_order_relaxed);
    }
    if (local_completed > 0) {
        atomic_fetch_add_explicit(&g_completed_blocks, local_completed, memory_order_relaxed);
    }

    return NULL;
}

static double run_benchmark(int is_upload, int threads, int duration, int timeout, uint64_t *out_bytes, uint32_t *out_completed) {
    g_running = 1;
    atomic_store_explicit(&g_total_bytes, 0, memory_order_relaxed);
    atomic_store_explicit(&g_completed_blocks, 0, memory_order_relaxed);

    double block_size_mb = (double)g_block_size / (1024.0 * 1024.0);
    const char *action_str = is_upload ? "Upload" : "Download";
    const char *icon_str = is_upload ? "📤" : "📥";

    printf("------------------------------------------------------------\n");
    printf("%s Running %s Speed Test...\n", icon_str, action_str);
    printf("⚙️  Threads: %d | Block: %.2f MB | Duration: %d seconds\n", threads, block_size_mb, duration);
    printf("------------------------------------------------------------\n");

    pthread_t *thread_handles = malloc(sizeof(pthread_t) * threads);
    thread_arg_t *thread_args = malloc(sizeof(thread_arg_t) * threads);

    for (int i = 0; i < threads; i++) {
        thread_args[i].thread_id = i;
        thread_args[i].timeout = timeout;
        if (is_upload) {
            pthread_create(&thread_handles[i], NULL, upload_worker_thread, &thread_args[i]);
        } else {
            pthread_create(&thread_handles[i], NULL, download_worker_thread, &thread_args[i]);
        }
    }

    struct timespec start_ts, now_ts, last_ts;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);
    last_ts = start_ts;
    uint64_t last_bytes = 0;

    for (int sec = 1; sec <= duration && g_running; sec++) {
        SLEEP_SEC(1);
        clock_gettime(CLOCK_MONOTONIC, &now_ts);

        double elapsed = (now_ts.tv_sec - start_ts.tv_sec) + 
                         (now_ts.tv_nsec - start_ts.tv_nsec) / 1e9;
        double delta_time = (now_ts.tv_sec - last_ts.tv_sec) + 
                           (now_ts.tv_nsec - last_ts.tv_nsec) / 1e9;

        uint64_t current_bytes = atomic_load_explicit(&g_total_bytes, memory_order_relaxed);
        uint32_t completed_count = atomic_load_explicit(&g_completed_blocks, memory_order_relaxed);

        uint64_t delta_bytes = current_bytes - last_bytes;
        double speed_mbps = (delta_time > 0) ? (delta_bytes * 8.0) / (delta_time * 1024.0 * 1024.0) : 0.0;
        double speed_mbs = (delta_time > 0) ? delta_bytes / (delta_time * 1024.0 * 1024.0) : 0.0;
        double blocks_float = (double)current_bytes / (double)g_block_size;

        printf("\r⏱️  %s Progress: %02ds/%ds | Current: %6.2f Mbps (%5.2f MB/s) | 📦 Blocks: %5.1f (Full: %u)",
               action_str, (int)elapsed, duration, speed_mbps, speed_mbs, blocks_float, completed_count);
        fflush(stdout);

        last_ts = now_ts;
        last_bytes = current_bytes;
    }

    g_running = 0;

    for (int i = 0; i < threads; i++) {
        pthread_join(thread_handles[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &now_ts);
    double total_time = (now_ts.tv_sec - start_ts.tv_sec) + 
                        (now_ts.tv_nsec - start_ts.tv_nsec) / 1e9;
    uint64_t final_bytes = atomic_load_explicit(&g_total_bytes, memory_order_relaxed);
    uint32_t final_completed = atomic_load_explicit(&g_completed_blocks, memory_order_relaxed);

    if (out_bytes) *out_bytes = final_bytes;
    if (out_completed) *out_completed = final_completed;

    double avg_mbps = (total_time > 0) ? (final_bytes * 8.0) / (total_time * 1024.0 * 1024.0) : 0.0;
    double avg_mbs = (total_time > 0) ? final_bytes / (total_time * 1024.0 * 1024.0) : 0.0;
    double total_mb = final_bytes / (1024.0 * 1024.0);
    double total_blocks = (double)final_bytes / (double)g_block_size;

    printf("\n  ↳ %s Finished: Avg %6.2f Mbps (%5.2f MB/s) | Transferred: %.2f MB (%.2f blocks)\n\n",
           action_str, avg_mbps, avg_mbs, total_mb, total_blocks);

    free(thread_handles);
    free(thread_args);

    return avg_mbps;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -U            Enable upload test (Default: download only. Performs download then upload)\n");
    printf("  --only-upload Upload test only (Skip download test)\n");
    printf("  -L            List nearby Speedtest servers and their latencies\n");
    printf("  -s <ID>       Specify a Speedtest server ID manually\n");
    printf("  -u <URL>      Specify a custom download URL directly\n");
    printf("  -d <sec>      Test duration in seconds (Default: 10)\n");
    printf("  -t <threads>  Number of concurrent streams/threads (Default: 4)\n");
    printf("  -T <sec>      Socket connection timeout in seconds (Default: 10)\n");
    printf("  -b <bytes>    Block size in bytes for stats (Default: 25000000)\n");
    printf("  -h            Show this help message\n\n");
    printf("Examples:\n");
    printf("  %s                   # Default: auto-select best server & test download\n", prog);
    printf("  %s -U                # Full test: download + upload\n", prog);
    printf("  %s -L                # List available nearby servers & latencies\n", prog);
    printf("  %s -s 65463 -U       # Test HKBN server (ID: 65463) with upload & download\n", prog);
    printf("  %s -d 15 -t 4        # 4 streams for 15 seconds\n", prog);
}

int main(int argc, char *argv[]) {
    int duration = 10;
    int threads = 4;
    int timeout = 10;
    int list_mode = 0;
    int test_upload = 0;
    int only_upload = 0;
    char target_id[32] = {0};

    memset(g_upload_buf, 'x', sizeof(g_upload_buf));

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--only-upload") == 0) {
            only_upload = 1;
            test_upload = 1;
        }
    }

    int opt;
    while ((opt = getopt(argc, argv, "ULs:u:d:t:T:b:h")) != -1) {
        switch (opt) {
            case 'U':
                test_upload = 1;
                break;
            case 'L':
                list_mode = 1;
                break;
            case 's':
                strncpy(target_id, optarg, sizeof(target_id) - 1);
                break;
            case 'u':
                strncpy(g_url, optarg, sizeof(g_url) - 1);
                break;
            case 'd':
                duration = atoi(optarg);
                if (duration <= 0) duration = 10;
                break;
            case 't':
                threads = atoi(optarg);
                if (threads <= 0) threads = 4;
                break;
            case 'T':
                timeout = atoi(optarg);
                if (timeout <= 0) timeout = 10;
                break;
            case 'b':
                g_block_size = strtoull(optarg, NULL, 10);
                if (g_block_size == 0) g_block_size = DEFAULT_BLOCK_SIZE;
                break;
            case 'h':
            default:
                print_usage(argv[0]);
#ifdef _WIN32
                WSACleanup();
#endif
                return 0;
        }
    }

    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    const SSL_METHOD *method = TLS_client_method();
    g_ssl_ctx = SSL_CTX_new(method);
    if (!g_ssl_ctx) {
        fprintf(stderr, "SSL_CTX initialization failed\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    
    SSL_CTX_set_verify(g_ssl_ctx, SSL_VERIFY_NONE, NULL);
    SSL_CTX_set_mode(g_ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_AUTO_RETRY);

    server_info_t *selected_server = NULL;

    if (strlen(g_url) == 0) {
        int cnt = fetch_servers(20);
        if (cnt == 0) {
            printf("⚠️  Using default HKBN endpoint for speedtest\n");
            strcpy(g_url, "https://speedtest21.hkbn.net.prod.hosts.ooklaserver.net:8080/download?size=25000000");
        } else {
            if (list_mode) {
                ping_all_servers();
                list_servers();
                SSL_CTX_free(g_ssl_ctx);
#ifdef _WIN32
                WSACleanup();
#endif
                return 0;
            }

            int selected_idx = -1;
            if (strlen(target_id) > 0) {
                for (int i = 0; i < g_server_count; i++) {
                    if (strcmp(g_servers[i].id, target_id) == 0) {
                        selected_idx = i;
                        break;
                    }
                }
                if (selected_idx < 0) {
                    fprintf(stderr, "❌ Server ID %s not found! Use -L to view available servers.\n", target_id);
                    SSL_CTX_free(g_ssl_ctx);
#ifdef _WIN32
                    WSACleanup();
#endif
                    return 1;
                }
                printf("📶 Measuring latency to specified server...\n");
                g_servers[selected_idx].ping_ms = measure_latency(g_servers[selected_idx].host);
            } else {
                ping_all_servers();
                for (int i = 0; i < g_server_count; i++) {
                    if (g_servers[i].ping_ms < 9000.0) {
                        selected_idx = i;
                        break;
                    }
                }
                if (selected_idx < 0) selected_idx = 0;
            }

            selected_server = &g_servers[selected_idx];
            snprintf(g_url, sizeof(g_url), "https://%s/download?size=25000000", selected_server->host);
        }
    }

    parse_url(g_url);

    printf("============================================================\n");
    printf("🚀 bb-speedtest (High Performance Native C Edition)\n");
    if (selected_server) {
        printf("🎯 Target Server: [%s] %s (%s, %s)\n", selected_server->id, selected_server->sponsor, selected_server->name, selected_server->country);
        printf("📶 Server Latency: %.2f ms\n", selected_server->ping_ms);
    } else {
        printf("📌 Target URL: %s\n", g_url);
    }
    printf("🛠️  Test Mode: %s\n", only_upload ? "Upload Only" : (test_upload ? "Full Duplex (Download + Upload)" : "Default (Download Only)"));
    printf("============================================================\n");

    uint64_t dl_bytes = 0, ul_bytes = 0;
    uint32_t dl_comp = 0, ul_comp = 0;
    double dl_speed = 0.0, ul_speed = 0.0;

    if (!only_upload) {
        dl_speed = run_benchmark(0, threads, duration, timeout, &dl_bytes, &dl_comp);
    }

    if (test_upload) {
        ul_speed = run_benchmark(1, threads, duration, timeout, &ul_bytes, &ul_comp);
    }

    printf("============================================================\n");
    printf("📊 Benchmark Summary:\n");
    if (selected_server) {
        printf("  • Server: [%s] %s | Latency: %.2f ms\n", selected_server->id, selected_server->sponsor, selected_server->ping_ms);
    }
    if (!only_upload) {
        printf("  • 📥 Download: %8.2f Mbps (%6.2f MB/s) | Transferred: %7.2f MB (%u full blocks)\n",
               dl_speed, dl_speed / 8.0, (double)dl_bytes / (1024.0 * 1024.0), dl_comp);
    }
    if (test_upload) {
        printf("  • 📤 Upload:   %8.2f Mbps (%6.2f MB/s) | Transferred: %7.2f MB (%u full blocks)\n",
               ul_speed, ul_speed / 8.0, (double)ul_bytes / (1024.0 * 1024.0), ul_comp);
    }
    printf("============================================================\n");

    SSL_CTX_free(g_ssl_ctx);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
