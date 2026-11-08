#include "badge_tls.h"
#include "badge_ss.h"
#include "esp_tls.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "mbedtls/ssl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include <fcntl.h>
#include <stdatomic.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_random.h"
#else
static void esp_fill_random(void *buf, size_t len) { memset(buf, 0x11, len); }
#endif
int __real_lwip_send(int, const void *, size_t, int);
int __real_lwip_recv(int, void *, size_t, int);
int __real_lwip_close(int);

static SemaphoreHandle_t s_socket_lock;
static int s_socket=-1;
static atomic_bool s_cancel, s_writing, s_can_abort;
static badge_proxy_t s_proxy;
static _Atomic(TaskHandle_t) s_proxy_owner;
static char s_proxy_target[96];
static int s_proxy_target_port;
static int64_t s_proxy_deadline;
static const char *s_proxy_error;

#ifdef CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
static esp_tls_client_session_t *s_session;
static esp_tls_t *s_session_connection;
static char s_session_host[96], s_session_name[96];
static int s_session_port;
static int64_t s_session_saved;
static esp_err_t (*s_session_bundle)(void *);

static void session_clear(void) {
    esp_tls_free_client_session(s_session);
    s_session=NULL;
    s_session_saved=0;
}

static bool session_matches(const char *host,int length,int port,const esp_tls_cfg_t *cfg) {
    const char *name=cfg->common_name ? cfg->common_name : "";
    return s_session && s_session_port==port &&
        strlen(s_session_host)==(size_t)length && !memcmp(s_session_host,host,(size_t)length) &&
        !strcmp(s_session_name,name) && s_session_bundle==cfg->crt_bundle_attach &&
        esp_timer_get_time()-s_session_saved<600000000;
}

static bool session_eligible(const char *host,int length,const esp_tls_cfg_t *cfg) {
    return host && length>0 && (size_t)length<sizeof(s_session_host) &&
        (!cfg->common_name || strlen(cfg->common_name)<sizeof(s_session_name)) &&
        cfg->crt_bundle_attach && !cfg->cacert_buf && !cfg->use_global_ca_store &&
        !cfg->skip_common_name && !cfg->client_session && !cfg->clientcert_buf &&
        !cfg->clientkey_buf && !cfg->alpn_protos && !cfg->ciphersuites_list &&
        cfg->tls_version==ESP_TLS_VER_ANY && !cfg->is_plain_tcp &&
        !cfg->use_secure_element && !cfg->ds_data;
}
#endif

void badge_tls_set_proxy(const badge_proxy_t *proxy) {
    if (proxy) s_proxy=*proxy; else memset(&s_proxy,0,sizeof(s_proxy));
}
const char *badge_tls_proxy_error(void) { return s_proxy_error; }

static bool proxy_begin(const char *host,int length,int port,int timeout_ms) {
    s_proxy_error=NULL;
    if (!s_proxy.enabled) return true;
    if (!badge_proxy_valid(&s_proxy) || length<1 || (size_t)length>=sizeof(s_proxy_target)) {
        s_proxy_error="代理配置无效";return false;
    }
    memcpy(s_proxy_target,host,(size_t)length);s_proxy_target[length]=0;
    s_proxy_target_port=port;
    s_proxy_deadline=esp_timer_get_time()+(int64_t)timeout_ms*1000;
    atomic_store(&s_proxy_owner,xTaskGetCurrentTaskHandle());
    return true;
}

static int proxy_wait(int fd,bool writing) {
    while (!badge_tls_cancelled() && esp_timer_get_time()<s_proxy_deadline) {
        fd_set set;FD_ZERO(&set);FD_SET(fd,&set);
        struct timeval tick={.tv_usec=50000};
        int n=select(fd+1,writing?NULL:&set,writing?&set:NULL,NULL,&tick);
        if (n>0) return 1;
        if (n<0 && errno!=EINTR) return -1;
    }
    errno=badge_tls_cancelled()?ECANCELED:ETIMEDOUT;return -1;
}

int __real_lwip_connect(int,const struct sockaddr *,socklen_t);
/* Only the gateway worker's current connect goes through Shadowsocks. The
 * server resolves the gateway name; ESP-TLS still owns the socket and
 * verifies the original gateway's certificate end to end. */
int __wrap_lwip_connect(int fd,const struct sockaddr *address,socklen_t size) {
    if (!s_proxy.enabled || atomic_load(&s_proxy_owner)!=xTaskGetCurrentTaskHandle())
        return __real_lwip_connect(fd,address,size);
    int64_t started=esp_timer_get_time();
    badge_tls_track_socket(fd);
    int n=__real_lwip_connect(fd,address,size);
    if (n<0 && errno!=EINPROGRESS) goto failure;
    if (n<0 && proxy_wait(fd,true)<0) goto failure;
    int error=0;socklen_t error_size=sizeof(error);
    if (getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&error_size)<0 || error) {
        if (error) errno=error;
        goto failure;
    }
    uint8_t salt[32], hello[320];
    size_t hello_len=0;
    esp_fill_random(salt,sizeof(salt));
    if (badge_ss_hello(salt,badge_proxy_method_name(s_proxy.method),s_proxy.secret,s_proxy_target,s_proxy_target_port,hello,sizeof(hello),&hello_len)!=0) {
        s_proxy_error="代理配置无效";errno=EINVAL;goto failure;
    }
    for (size_t sent=0;sent<hello_len;) {
        if (proxy_wait(fd,true)<0) { badge_ss_close();goto failure; }
        n=(int)__real_lwip_send(fd,hello+sent,hello_len-sent,0);
        if (n>0) sent+=(size_t)n;
        else if (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)) continue;
        else { badge_ss_close();goto failure; }
    }
    badge_ss_bind(fd);
    ESP_LOGI("badge_proxy","shadowsocks ready elapsed=%lldms",(long long)((esp_timer_get_time()-started)/1000));
    return 0;
failure:
    if (!s_proxy_error) s_proxy_error="连不上代理，请检查服务器和端口";
    ESP_LOGW("badge_proxy","shadowsocks failed errno=%d cancelled=%d",errno,badge_tls_cancelled());
    badge_tls_forget_socket();
    return -1;
}


/* Wait until the socket accepts more bytes, for at most `ms`. */
static bool writable(int fd, int ms) {
    int64_t end=esp_timer_get_time()+(int64_t)ms*1000;
    do {
        fd_set set;FD_ZERO(&set);FD_SET(fd,&set);
        struct timeval tick={.tv_usec=ms ? 50000 : 0};
        int n=select(fd+1,NULL,&set,NULL,&tick);
        if (n>0) return true;
        if (n<0 && errno!=EINTR) return false;
    } while (!badge_tls_cancelled() && esp_timer_get_time()<end);
    return false;
}

/* Every sealed chunk advances the cipher, so a chunk is either written
 * completely or the connection fails; returning EAGAIN after sealing would
 * let the caller resend bytes that are already counted. Before anything is
 * sealed, EAGAIN passes through untouched so non-blocking callers retry. */
int __wrap_lwip_send(int fd, const void *data, size_t len, int flags) {
    if (!badge_ss_owns(fd)) return __real_lwip_send(fd, data, len, flags);
    const uint8_t *bytes=data;
    size_t off=0;
    /* Bound the whole call: a stalled peer must fail, not hang the upload. */
    int64_t deadline=esp_timer_get_time()+20000000LL;
    while (off<len) {
        /* Wait for room instead of reporting EAGAIN: ESP-TLS treats a
         * would-block write mid-record as a failed connection. */
        int left=(int)((deadline-esp_timer_get_time())/1000);
        if (left<=0 || !writable(fd,left)) {
            if (off) return (int)off;
            errno=EIO; return -1;
        }
        size_t n=len-off; if (n>1024) n=1024;
        uint8_t sealed[1024]; size_t sealed_len=0;
        if (badge_ss_seal(bytes+off, n, sealed, sizeof(sealed), &sealed_len)!=0) { errno=EIO; return -1; }
        for (size_t sent=0; sent<sealed_len; ) {
            int wrote=__real_lwip_send(fd, sealed+sent, sealed_len-sent, flags);
            if (wrote>0) { sent+=(size_t)wrote; continue; }
            int left=(int)((deadline-esp_timer_get_time())/1000);
            int failure = errno;
            if (wrote<0 && (failure==EINTR || failure==EAGAIN || failure==EWOULDBLOCK || failure==ENOMEM || failure==ENOBUFS) &&
                left>0 && !badge_tls_cancelled()) {
                vTaskDelay(pdMS_TO_TICKS(2));
                if (writable(fd,left)) continue;
            }
            ESP_LOGW("badge_tls", "proxy_write_failed errno=%d remaining=%u", failure, (unsigned)(sealed_len-sent));
            errno=EIO; return -1;
        }
        off+=n;
    }
    return (int)len;
}

/* CFB keeps ciphertext and plaintext the same length, so read no more from
 * the socket than the caller asked for. Anything beyond that stays in the
 * socket, where select() still sees it; a private stash would hide the next
 * TLS record from the transport's readiness check and stall the session. */
int __wrap_lwip_recv(int fd, void *data, size_t len, int flags) {
    if (!badge_ss_owns(fd) || !len) return __real_lwip_recv(fd, data, len, flags);
    int nonblock=0; int fl=fcntl(fd,F_GETFL,0); if (fl>=0) nonblock=fl & O_NONBLOCK;
    for (;;) {
        uint8_t incoming[512];
        size_t want=len<sizeof(incoming) ? len : sizeof(incoming);
        int n=__real_lwip_recv(fd, incoming, want, flags);
        if (n<=0) return n;
        size_t off=0, total=0;
        while (off<(size_t)n) {
            size_t used=0, got=0;
            if (badge_ss_consume(incoming+off, (size_t)n-off, &used, (uint8_t *)data+total, len-total, &got)<0 || !used) {
                errno=EIO; return -1;
            }
            off+=used; total+=got;
        }
        if (total) return (int)total;
        /* Only the server's IV arrived so far. */
        if (nonblock) { errno=EAGAIN; return -1; }
    }
}

/* mbedTLS uses read()/write(). lwIP routes those to recvfrom/send inside
 * its own object file, past the recv/send hooks, so wrap them as well. */
ssize_t __real_lwip_read(int, void *, size_t);
ssize_t __wrap_lwip_read(int fd, void *data, size_t len) {
    if (!badge_ss_owns(fd)) return __real_lwip_read(fd, data, len);
    return __wrap_lwip_recv(fd, data, len, 0);
}
ssize_t __real_lwip_write(int, const void *, size_t);
ssize_t __wrap_lwip_write(int fd, const void *data, size_t len) {
    if (!badge_ss_owns(fd)) return __real_lwip_write(fd, data, len);
    return __wrap_lwip_send(fd, data, len, 0);
}

int __wrap_lwip_close(int fd) {
    if (badge_ss_owns(fd)) badge_ss_close();
    return __real_lwip_close(fd);
}

static const char *connection_phase(esp_tls_conn_state_t state) {
    switch (state) {
        case ESP_TLS_INIT: return "dns_tcp";
        case ESP_TLS_CONNECTING: return "tcp";
        case ESP_TLS_HANDSHAKE: return "tls";
        case ESP_TLS_DONE: return "ready";
        case ESP_TLS_FAIL: return "failed";
        default: return "unknown";
    }
}

static bool log_peer(int fd) {
    struct sockaddr_storage peer;
    socklen_t size=sizeof(peer);
    char address[INET6_ADDRSTRLEN];
    if (fd<0 || getpeername(fd,(struct sockaddr *)&peer,&size)<0) return false;
    const void *ip=peer.ss_family==AF_INET ? (const void *)&((struct sockaddr_in *)&peer)->sin_addr
                   : peer.ss_family==AF_INET6 ? (const void *)&((struct sockaddr_in6 *)&peer)->sin6_addr : NULL;
    if (!ip || !inet_ntop(peer.ss_family,ip,address,sizeof(address))) return false;
    ESP_LOGI("badge_tls","peer=%s",address);
    return true;
}

esp_err_t badge_tls_control_prepare(void) {
    if (!s_socket_lock) s_socket_lock=xSemaphoreCreateMutex();
    return s_socket_lock ? ESP_OK : ESP_ERR_NO_MEM;
}
void badge_tls_control_begin(void) {
    atomic_store(&s_cancel,false);atomic_store(&s_writing,false);atomic_store(&s_can_abort,false);
    atomic_store(&s_proxy_owner,NULL);s_proxy_error=NULL;
}
bool badge_tls_cancelled(void) { return atomic_load(&s_cancel); }
bool badge_tls_cancel_can_write(void) { return atomic_load(&s_can_abort); }
void badge_tls_set_writing(bool writing) { atomic_store(&s_writing,writing); }
void badge_tls_track_socket(int fd) {
    if (!s_socket_lock || fd<0) return;
    xSemaphoreTake(s_socket_lock,portMAX_DELAY);s_socket=fd;xSemaphoreGive(s_socket_lock);
}
void badge_tls_forget_socket(void) {
    if (!s_socket_lock) return;
    xSemaphoreTake(s_socket_lock,portMAX_DELAY);s_socket=-1;xSemaphoreGive(s_socket_lock);
}
void badge_tls_request_cancel(bool allow_remote_abort) {
    atomic_store(&s_cancel,true);
    bool cooperative=allow_remote_abort && !atomic_load(&s_writing);
    atomic_store(&s_can_abort,cooperative);
    if (!s_socket_lock) return;
    xSemaphoreTake(s_socket_lock,portMAX_DELAY);
    if (s_socket>=0) {
        if (!cooperative) (void)shutdown(s_socket,SHUT_RDWR);
        else {
            struct timeval timeout={.tv_usec=250000};
            (void)setsockopt(s_socket,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
            timeout.tv_usec=100000;
            (void)setsockopt(s_socket,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        }
    }
    xSemaphoreGive(s_socket_lock);
}

/* Drive the SDK's supported nonblocking TLS API so a cancellation never has
 * to wait for the complete handshake timeout. The transport still owns TLS. */
int __wrap_esp_tls_conn_new_sync(const char *host,int hostlen,int port,
                               const esp_tls_cfg_t *cfg,esp_tls_t *tls) {
    if (!cfg || !tls || badge_tls_cancelled()) return -1;
    esp_tls_cfg_t asynchronous=*cfg;asynchronous.non_block=true;
    int64_t start=esp_timer_get_time();
    int timeout=cfg->timeout_ms>0 ? cfg->timeout_ms : 20000;
    if (!proxy_begin(host,hostlen,port,timeout)) return -1;
#ifdef CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
    bool cacheable=session_eligible(host,hostlen,cfg);
    bool ticket=cacheable && session_matches(host,hostlen,port,cfg);
    s_session_connection=NULL;
    if (!ticket) session_clear();
    if (cacheable) asynchronous.client_session=ticket ? s_session : NULL;
    ESP_LOGI("badge_tls","session_offer=%d",ticket);
#endif
    const char *connect_host=host;
    int connect_length=hostlen,connect_port=port;
    if (s_proxy.enabled) {
        /* Connect to the Shadowsocks server; it resolves the gateway name.
         * Preserve SNI and certificate verification for the gateway. */
        connect_host=s_proxy.host;connect_length=(int)strlen(s_proxy.host);connect_port=s_proxy.port;
        asynchronous.common_name=cfg->common_name?cfg->common_name:s_proxy_target;
    }
    esp_tls_conn_state_t phase=ESP_TLS_INIT;
    int previous_phase=-1;
    bool peer_logged=false;
    while (!badge_tls_cancelled() && esp_timer_get_time()-start<(int64_t)timeout*1000) {
        int result=esp_tls_conn_new_async(connect_host,connect_length,connect_port,&asynchronous,tls);
        int fd=-1;
        if (esp_tls_get_conn_sockfd(tls,&fd)==ESP_OK && fd>=0) badge_tls_track_socket(fd);
        if (!peer_logged) peer_logged=log_peer(fd);
        if (esp_tls_get_conn_state(tls,&phase)==ESP_OK && (int)phase!=previous_phase) {
            ESP_LOGI("badge_tls","phase=%s elapsed=%lldms",connection_phase(phase),
                     (long long)((esp_timer_get_time()-start)/1000));
            previous_phase=(int)phase;
        }
#ifdef CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
        if (phase==ESP_TLS_HANDSHAKE || phase==ESP_TLS_DONE) {
            asynchronous.client_session=NULL;
            if (ticket) session_clear();
        }
#endif
        if (badge_tls_cancelled() || result<0) break;
        if (result==1) {
            int flags=fcntl(fd,F_GETFL,0);
            if (flags<0 || fcntl(fd,F_SETFL,flags & ~O_NONBLOCK)<0) break;
            mbedtls_ssl_context *ssl=esp_tls_get_ssl_context(tls);
            if (ssl) ESP_LOGI("badge_tls","version=%s cipher=%s elapsed=%lldms",mbedtls_ssl_get_version(ssl),
                       mbedtls_ssl_get_ciphersuite(ssl),(long long)((esp_timer_get_time()-start)/1000));
#ifdef CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
            if (cacheable) {
                memcpy(s_session_host,host,(size_t)hostlen);s_session_host[hostlen]=0;
                snprintf(s_session_name,sizeof(s_session_name),"%s",cfg->common_name ? cfg->common_name : "");
                s_session_port=port;s_session_bundle=cfg->crt_bundle_attach;
                s_session_connection=tls;
            }
#endif
            atomic_store(&s_proxy_owner,NULL);return 1;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGW("badge_tls","connect_stopped phase=%s elapsed=%lldms cancelled=%d",
             connection_phase(phase),(long long)((esp_timer_get_time()-start)/1000),badge_tls_cancelled());
#ifdef CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
    if (ticket) session_clear();
#endif
    /* The SDK closes this socket when we return failure. Unpublish first so
     * cancellation cannot touch a descriptor already reused by another task. */
    atomic_store(&s_proxy_owner,NULL);badge_tls_forget_socket();return -1;
}

int __real_esp_tls_conn_destroy(esp_tls_t *tls);
int __wrap_esp_tls_conn_destroy(esp_tls_t *tls) {
#ifdef CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
    if (tls && tls==s_session_connection) {
        s_session_connection=NULL;
        session_clear();
        esp_tls_client_session_t *saved=badge_tls_cancelled() ? NULL : esp_tls_get_client_session(tls);
        if (saved) {
            s_session=saved;s_session_saved=esp_timer_get_time();
            ESP_LOGI("badge_tls","session_cached=1");
        }
    }
#endif
    return __real_esp_tls_conn_destroy(tls);
}

esp_err_t __real_esp_tls_plain_tcp_connect(const char *,int,int,const esp_tls_cfg_t *,esp_tls_error_handle_t,int *);
esp_err_t __wrap_esp_tls_plain_tcp_connect(const char *host,int hostlen,int port,const esp_tls_cfg_t *cfg,
                                         esp_tls_error_handle_t error,int *socket_out) {
    if (!cfg || !socket_out || badge_tls_cancelled()) return ESP_ERR_INVALID_STATE;
    esp_tls_cfg_t asynchronous=*cfg;asynchronous.non_block=true;
    int fd=-1;
    int timeout=cfg->timeout_ms>0 ? cfg->timeout_ms : 20000;
    if (!proxy_begin(host,hostlen,port,timeout)) return ESP_FAIL;
    esp_err_t result=__real_esp_tls_plain_tcp_connect(s_proxy.enabled?s_proxy.host:host,
        s_proxy.enabled?(int)strlen(s_proxy.host):hostlen,s_proxy.enabled?s_proxy.port:port,&asynchronous,error,&fd);
    atomic_store(&s_proxy_owner,NULL);
    if (result!=ESP_OK) return result;
    if (fd<0) return ESP_FAIL;
    badge_tls_track_socket(fd);
    int64_t end=esp_timer_get_time()+(int64_t)(cfg->timeout_ms>0 ? cfg->timeout_ms : 20000)*1000;
    while (!badge_tls_cancelled() && esp_timer_get_time()<end) {
        fd_set writable,errors;FD_ZERO(&writable);FD_ZERO(&errors);FD_SET(fd,&writable);FD_SET(fd,&errors);
        struct timeval tick={.tv_usec=50000};
        int ready=select(fd+1,NULL,&writable,&errors,&tick);
        if (badge_tls_cancelled() || ready<0) break;
        if (ready>0) {
            int failure=0;socklen_t size=sizeof(failure);
            if (getsockopt(fd,SOL_SOCKET,SO_ERROR,&failure,&size)<0 || failure) break;
            int flags=fcntl(fd,F_GETFL,0);
            if (flags<0 || fcntl(fd,F_SETFL,flags & ~O_NONBLOCK)<0) break;
            *socket_out=fd;return ESP_OK;
        }
    }
    badge_tls_forget_socket();close(fd);*socket_out=-1;return ESP_FAIL;
}
