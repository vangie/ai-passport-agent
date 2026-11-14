/* Exercise cancellation against real blocking socket operations, with only
 * the SDK handshake and RTOS scheduling replaced by host implementations. */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "passport_tls.h"
#include "passport_ss.h"
#include "esp_tls.h"
#include "freertos/semphr.h"
#include "mbedtls/ssl.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "freertos/task.h"
#include "mbedtls/aes.h"
#include "mbedtls/md5.h"
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)pthread_self(); }
int __wrap_lwip_connect(int,const struct sockaddr *,socklen_t);
int __wrap_lwip_recv(int,void *,size_t,int);
int __real_lwip_connect(int fd,const struct sockaddr *p,socklen_t size) { return connect(fd,p,size); }
static int send_fixture_fd=-1,send_fixture_calls,send_fixture_fault;
int __real_lwip_send(int fd,const void *b,size_t n,int flags) {
    if (fd==send_fixture_fd) {
        send_fixture_calls++;
        if (send_fixture_calls>=2 && send_fixture_calls<=5) {
            static const int errors[]={ENOMEM,ENOBUFS,EAGAIN,EINTR};
            errno=send_fixture_fault==1?ECONNRESET:errors[send_fixture_calls-2];
            if (send_fixture_fault==2) passport_tls_request_cancel(false);
            return -1;
        }
        if (n>71) n=71;
    }
    return (int)send(fd,b,n,flags);
}
int __real_lwip_recv(int fd,void *b,size_t n,int flags) { return (int)recv(fd,b,n,flags); }
int __real_lwip_close(int fd) { return close(fd); }
ssize_t __real_lwip_read(int fd,void *b,size_t n) { return read(fd,b,n); }
ssize_t __wrap_lwip_read(int,void *,size_t);
ssize_t __real_lwip_write(int fd,const void *b,size_t n) { return write(fd,b,n); }
ssize_t __wrap_lwip_write(int,const void *,size_t);
int __wrap_lwip_send(int,const void *,size_t,int);
int __wrap_esp_tls_conn_new_sync(const char *,int,int,const esp_tls_cfg_t *,esp_tls_t *);
esp_err_t __wrap_esp_tls_plain_tcp_connect(const char *,int,int,const esp_tls_cfg_t *,esp_tls_error_handle_t,int *);
static int sockets[2];static bool ready,full;static int outcome;static atomic_int calls;
static bool proxy_fixture;
static int proxy_fd=-1,proxy_listener,proxy_status;
static atomic_bool proxy_request_received;
static bool session_fixture,expect_ticket,fail_session;
static int sessions_allocated,sessions_freed,sessions_destroyed;
static int64_t clock_offset;
int __wrap_esp_tls_conn_destroy(esp_tls_t *);
SemaphoreHandle_t xSemaphoreCreateMutex(void) { pthread_mutex_t *m=malloc(sizeof(*m));assert(m);pthread_mutex_init(m,NULL);return m; }
int xSemaphoreTake(SemaphoreHandle_t m,unsigned ticks) { (void)ticks;return pthread_mutex_lock(m)==0; }
int xSemaphoreGive(SemaphoreHandle_t m) { return pthread_mutex_unlock(m)==0; }
void vTaskDelay(unsigned ticks) { struct timespec d={ticks/1000,(ticks%1000)*1000000L};nanosleep(&d,NULL); }
int64_t esp_timer_get_time(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000LL+t.tv_nsec/1000+clock_offset; }
int esp_tls_conn_new_async(const char *h,int n,int port,const esp_tls_cfg_t *cfg,esp_tls_t *tls) {
    if (session_fixture) {
        assert(cfg->non_block);
        assert((cfg->client_session!=NULL)==(tls->step==0 && expect_ticket));
        /* The offered session stays cached while the handshake runs. */
        if (tls->step>0) assert(sessions_allocated-sessions_freed==(expect_ticket ? 1 : 0));
        if (fail_session) return -1;
        tls->fd=sockets[0];
        return ++tls->step>=2 ? 1 : 0;
    }
    if (proxy_fixture) {
        assert(!strcmp(h,"127.0.0.1") && n==9);
        assert(cfg->common_name && !strcmp(cfg->common_name,"gateway.example"));
        struct sockaddr_in target={.sin_family=AF_INET,.sin_port=htons((uint16_t)port)};
        target.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        tls->fd=proxy_fd=socket(AF_INET,SOCK_STREAM,0);assert(proxy_fd>=0);
        assert(fcntl(proxy_fd,F_SETFL,O_NONBLOCK)==0);
        return __wrap_lwip_connect(proxy_fd,(struct sockaddr *)&target,sizeof(target))==0 ? 1 : -1;
    }
    (void)h;(void)n;(void)port;assert(cfg->non_block);tls->fd=sockets[0];atomic_fetch_add(&calls,1);return ready ? 1 : 0;
}
esp_err_t esp_tls_get_conn_sockfd(esp_tls_t *tls,int *fd) { *fd=tls->fd;return ESP_OK; }
esp_err_t esp_tls_get_conn_state(esp_tls_t *tls,esp_tls_conn_state_t *state) { *state=session_fixture ? (tls->step>=2 ? ESP_TLS_DONE : ESP_TLS_HANDSHAKE) : (ready ? ESP_TLS_DONE : ESP_TLS_HANDSHAKE);return ESP_OK; }
void *esp_tls_get_ssl_context(esp_tls_t *tls) { static mbedtls_ssl_context ssl;(void)tls;return &ssl; }
const char *mbedtls_ssl_get_version(const mbedtls_ssl_context *s) { (void)s;return "TLS-test"; }
const char *mbedtls_ssl_get_ciphersuite(const mbedtls_ssl_context *s) { (void)s;return "fixture"; }
esp_tls_client_session_t *esp_tls_get_client_session(esp_tls_t *tls) {
    assert(tls && tls->step==2);
    esp_tls_client_session_t *session=malloc(sizeof(*session));assert(session);
    session->identity=++sessions_allocated;return session;
}
void esp_tls_free_client_session(esp_tls_client_session_t *session) {
    if (session) { sessions_freed++;free(session); }
}
int __real_esp_tls_conn_destroy(esp_tls_t *tls) { assert(tls);sessions_destroyed++;return 0; }
esp_err_t __real_esp_tls_plain_tcp_connect(const char *h,int n,int port,const esp_tls_cfg_t *cfg,esp_tls_error_handle_t e,int *fd) {
    (void)h;(void)n;(void)port;(void)e;assert(cfg->non_block);*fd=sockets[0];atomic_fetch_add(&calls,1);
    if (full) { char bytes[4096]={0};while(write(*fd,bytes,sizeof(bytes))>0){} assert(errno==EAGAIN || errno==EWOULDBLOCK); }
    return ESP_OK;
}
static void pair(void) { assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);fcntl(sockets[0],F_SETFL,O_NONBLOCK);atomic_store(&calls,0);passport_tls_control_begin(); }
static void *handshake(void *unused) {
    (void)unused;esp_tls_t tls={.fd=-1};esp_tls_cfg_t cfg={.timeout_ms=20000};
    outcome=__wrap_esp_tls_conn_new_sync("fixture",7,443,&cfg,&tls);return NULL;
}
static void *plain(void *unused) {
    (void)unused;esp_tls_cfg_t cfg={.timeout_ms=20000};int fd=-1;
    outcome=__wrap_esp_tls_plain_tcp_connect("fixture",7,80,&cfg,NULL,&fd);return NULL;
}
/* A minimal Shadowsocks aes-256-cfb server: it decrypts the target address
 * the passport sends and answers with one encrypted tunnel byte. */
static void ss_key(const char *password,uint8_t key[32]) {
    uint8_t block[16],mix[16+64];size_t n=strlen(password);
    assert(mbedtls_md5((const uint8_t *)password,n,block)==0);memcpy(key,block,16);
    memcpy(mix,block,16);memcpy(mix+16,password,n);
    assert(mbedtls_md5(mix,16+n,block)==0);memcpy(key+16,block,16);
}
static void *proxy_server(void *unused) {
    (void)unused;
    int fd=accept(proxy_listener,NULL,NULL);assert(fd>=0);
    uint8_t iv[16],head[2];size_t used=0;
    while (used<16) { ssize_t r=read(fd,iv+used,16-used);assert(r>0);used+=(size_t)r; }
    uint8_t key[32];ss_key("pw",key);
    mbedtls_aes_context aes;mbedtls_aes_init(&aes);assert(mbedtls_aes_setkey_enc(&aes,key,256)==0);
    uint8_t div[16];memcpy(div,iv,16);size_t doff=0;
    uint8_t raw[2];assert(read(fd,raw,2)==2);
    assert(mbedtls_aes_crypt_cfb128(&aes,MBEDTLS_AES_DECRYPT,2,&doff,div,raw,head)==0);
    assert(head[0]==3 && head[1]==15);
    uint8_t rest[17],plain[17];used=0;
    while (used<sizeof(rest)) { ssize_t r=read(fd,rest+used,sizeof(rest)-used);assert(r>0);used+=(size_t)r; }
    assert(mbedtls_aes_crypt_cfb128(&aes,MBEDTLS_AES_DECRYPT,sizeof(rest),&doff,div,rest,plain)==0);
    assert(!memcmp(plain,"gateway.example\x01\xbb",17));
    atomic_store(&proxy_request_received,true);
    if (proxy_status==0) { char c;while (read(fd,&c,1)>0) {} }
    else {
        uint8_t reply_iv[16]={9},eiv[16],sealed[2];const uint8_t xy[2]={'x','y'};size_t eoff=0;
        memcpy(eiv,reply_iv,16);
        assert(mbedtls_aes_crypt_cfb128(&aes,MBEDTLS_AES_ENCRYPT,2,&eoff,eiv,xy,sealed)==0);
        /* IV and both bytes in one segment, as a real server sends them. */
        uint8_t reply[18];memcpy(reply,reply_iv,16);memcpy(reply+16,sealed,2);
        assert(write(fd,reply,sizeof(reply))==(ssize_t)sizeof(reply));
    }
    mbedtls_aes_free(&aes);close(fd);return NULL;
}
static void *proxy_handshake(void *unused) {
    (void)unused;esp_tls_t tls={.fd=-1};esp_tls_cfg_t cfg={.timeout_ms=2000};
    outcome=__wrap_esp_tls_conn_new_sync("gateway.example",15,443,&cfg,&tls);return NULL;
}
static void check_proxy(int status) {
    proxy_listener=socket(AF_INET,SOCK_STREAM,0);assert(proxy_listener>=0);
    struct sockaddr_in address={.sin_family=AF_INET};address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(proxy_listener,(struct sockaddr *)&address,sizeof(address))==0 && listen(proxy_listener,1)==0);
    socklen_t size=sizeof(address);assert(getsockname(proxy_listener,(struct sockaddr *)&address,&size)==0);
    passport_proxy_t config={.enabled=true,.port=ntohs(address.sin_port)};strcpy(config.host,"127.0.0.1");strcpy(config.secret,"pw");
    passport_tls_set_proxy(&config);passport_tls_control_begin();proxy_fixture=true;proxy_status=status;
    atomic_store(&proxy_request_received,false);
    pthread_t server,client;assert(!pthread_create(&server,NULL,proxy_server,NULL));
    assert(!pthread_create(&client,NULL,proxy_handshake,NULL));
    if (status==0) {
        while (!atomic_load(&proxy_request_received)) vTaskDelay(1);
        /* Another task's socket must bypass the gateway's scoped hook. */
        int other=socket(AF_INET,SOCK_STREAM,0);assert(other>=0);
        assert(__wrap_lwip_connect(other,(struct sockaddr *)&address,sizeof(address))==0);
        int accepted=accept(proxy_listener,NULL,NULL);assert(accepted>=0);
        assert(write(other,"direct",6)==6);char direct[6];assert(read(accepted,direct,6)==6);
        assert(!memcmp(direct,"direct",6));close(other);close(accepted);
        passport_tls_request_cancel(false);
    }
    pthread_join(client,NULL);
    if (status==0) { shutdown(proxy_fd,SHUT_RDWR); }
    pthread_join(server,NULL);
    if (status==200) {
        /* The server's own IV and byte arrive through the wrapped receive. */
        int flags=fcntl(proxy_fd,F_GETFL,0);fcntl(proxy_fd,F_SETFL,flags & ~O_NONBLOCK);
        assert(outcome==1);
        /* mbedTLS reads with read(); that path must decrypt too. */
        char c=0;assert(__wrap_lwip_read(proxy_fd,&c,1)==1 && c=='x');
        /* The unread byte must still wake select(), or TLS stalls. */
        fd_set readable;FD_ZERO(&readable);FD_SET(proxy_fd,&readable);
        struct timeval wait={.tv_usec=200000};
        assert(select(proxy_fd+1,&readable,NULL,NULL,&wait)==1);
        assert(__wrap_lwip_read(proxy_fd,&c,1)==1 && c=='y');
    } else assert(outcome<0 || status==0);
    passport_ss_close();passport_tls_forget_socket();close(proxy_fd);close(proxy_listener);
    proxy_fixture=false;passport_tls_set_proxy(NULL);
}
static void check_proxy_write(int fault) {
    pair();send_fixture_fd=sockets[0];send_fixture_calls=0;send_fixture_fault=fault;
    uint8_t salt[32]={1},hello[128],payload[1200],received[1200],expected[1200];
    size_t length=0;memset(payload,0x93,sizeof(payload));
    assert(!passport_ss_hello(salt,"aes-256-cfb","pw","gateway.example",443,hello,sizeof(hello),&length));
    passport_ss_bind(sockets[0]);
    int64_t start=esp_timer_get_time();
    int result=__wrap_lwip_send(sockets[0],payload,sizeof(payload),0);
    if (!fault) {
        assert(result==sizeof(payload) && send_fixture_calls>5);
        size_t used=0;
        while (used<sizeof(received)) { ssize_t count=read(sockets[1],received+used,sizeof(received)-used);assert(count>0);used+=(size_t)count; }
        assert(!passport_ss_hello(salt,"aes-256-cfb","pw","gateway.example",443,hello,sizeof(hello),&length));
        assert(!passport_ss_seal(payload,sizeof(payload),expected,sizeof(expected),&length));
        assert(length==sizeof(payload) && !memcmp(received,expected,sizeof(expected)));
    } else {
        assert(result<0 && errno==EIO && send_fixture_calls==2);
        assert(esp_timer_get_time()-start<300000);
    }
    send_fixture_fd=-1;passport_ss_close();passport_tls_forget_socket();close(sockets[0]);close(sockets[1]);
}
static esp_err_t bundle(void *unused) { (void)unused;return ESP_OK; }
static esp_err_t other_bundle(void *unused) { (void)unused;return ESP_OK; }
static void check_session(const char *host,int port,esp_tls_cfg_t cfg,bool ticket,bool cancel) {
    pair();session_fixture=true;expect_ticket=ticket;
    esp_tls_t tls={.fd=-1};
    int result=__wrap_esp_tls_conn_new_sync(host,(int)strlen(host),port,&cfg,&tls);
    assert(result==(fail_session ? -1 : 1));
    if (cancel) passport_tls_request_cancel(false);
    passport_tls_forget_socket();
    assert(__wrap_esp_tls_conn_destroy(&tls)==0);
    close(sockets[0]);close(sockets[1]);session_fixture=false;
}
static void check_sessions(void) {
    esp_tls_cfg_t cfg={.timeout_ms=2000,.crt_bundle_attach=bundle};
    check_session("one.example",443,cfg,false,false);
    /* Every later connection to the same gateway resumes, not every other. */
    check_session("one.example",443,cfg,true,false);
    check_session("one.example",443,cfg,true,false);
    check_session("one.example",443,cfg,true,false);
    assert(sessions_allocated-sessions_freed==1);
    check_session("two.example",443,cfg,false,false);
    check_session("two.example",8443,cfg,false,false);
    cfg.common_name="verified.example";
    check_session("two.example",8443,cfg,false,false);
    check_session("two.example",8443,cfg,true,false);
    cfg.crt_bundle_attach=other_bundle;
    check_session("two.example",8443,cfg,false,false);
    clock_offset+=12LL*3600*1000000+1;
    check_session("two.example",8443,cfg,false,false);
    /* A rejected resumption drops the session; the retry is a full handshake. */
    fail_session=true;check_session("two.example",8443,cfg,true,false);fail_session=false;
    check_session("two.example",8443,cfg,false,false);
    /* The idle connection is closed by a cancel; its session still counts. */
    check_session("two.example",8443,cfg,true,true);
    check_session("two.example",8443,cfg,true,false);
    check_session("three.example",443,cfg,false,true);
    check_session("three.example",443,cfg,true,false);
    cfg.skip_common_name=true;
    check_session("two.example",8443,cfg,false,false);
    cfg.skip_common_name=false;
    check_session("two.example",8443,cfg,false,false);
    cfg.cacert_buf=(const unsigned char *)"custom";
    check_session("two.example",8443,cfg,false,false);
    cfg.cacert_buf=NULL;
    check_session("two.example",8443,cfg,false,false);
    cfg.tls_version=ESP_TLS_VER_TLS_1_2;
    check_session("two.example",8443,cfg,false,false);
    cfg.tls_version=ESP_TLS_VER_ANY;
    check_session("two.example",8443,cfg,false,false);
    const char *protocols[]={"h2",NULL};cfg.alpn_protos=protocols;
    check_session("two.example",8443,cfg,false,false);
    cfg.alpn_protos=NULL;
    check_session("two.example",8443,cfg,false,false);
    const int ciphers[]={1,0};cfg.ciphersuites_list=ciphers;
    check_session("two.example",8443,cfg,false,false);
    assert(sessions_allocated==sessions_freed && sessions_destroyed==25);
    puts("TLS tickets: repeated resumption, host/port/SNI/trust isolation, expiry, rejection fallback, cancel and cleanup PASS");
}
int main(void) {
    signal(SIGPIPE,SIG_IGN);assert(passport_tls_control_prepare()==ESP_OK);
    pair();ready=false;pthread_t t;assert(!pthread_create(&t,NULL,handshake,NULL));
    while(!atomic_load(&calls)) vTaskDelay(1);
    int64_t start=esp_timer_get_time();passport_tls_request_cancel(false);pthread_join(t,NULL);
    assert(outcome<1 && esp_timer_get_time()-start<300000);assert(fcntl(sockets[0],F_GETFL)>=0); /* caller owns close */
    close(sockets[0]);close(sockets[1]);
    pair();ready=true;handshake(NULL);assert(outcome==1 && !(fcntl(sockets[0],F_GETFL)&O_NONBLOCK));
    passport_tls_request_cancel(true);assert(passport_tls_cancel_can_write());
    assert(write(sockets[0],"x",1)==1);char c;assert(read(sockets[1],&c,1)==1 && c=='x');
    passport_tls_forget_socket();close(sockets[0]);close(sockets[1]);
    /* A later descriptor reuse must not be shut down by the old operation. */
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);passport_tls_request_cancel(false);
    assert(write(sockets[0],"y",1)==1 && read(sockets[1],&c,1)==1 && c=='y');close(sockets[0]);close(sockets[1]);
    pair();passport_tls_track_socket(sockets[0]);passport_tls_set_writing(true);passport_tls_request_cancel(true);
    assert(!passport_tls_cancel_can_write() && write(sockets[0],"z",1)<0);
    passport_tls_forget_socket();close(sockets[0]);close(sockets[1]);
    pair();full=false;plain(NULL);assert(outcome==ESP_OK && !(fcntl(sockets[0],F_GETFL)&O_NONBLOCK));
    passport_tls_forget_socket();close(sockets[0]);close(sockets[1]);
    pair();full=true;assert(!pthread_create(&t,NULL,plain,NULL));while(!atomic_load(&calls))vTaskDelay(1);
    start=esp_timer_get_time();passport_tls_request_cancel(false);pthread_join(t,NULL);
    assert(outcome!=ESP_OK && esp_timer_get_time()-start<300000);assert(fcntl(sockets[0],F_GETFL)<0);close(sockets[1]);
    check_proxy(200);check_proxy(0);
    check_proxy_write(0);check_proxy_write(1);check_proxy_write(2);
    check_sessions();
    puts("Shadowsocks route: original gateway SNI, remote DNS, encrypted target address, scoped hook and cancellation PASS");
    puts("Network cancel: handshake interruption, bounded socket waits, abort write access, descriptor reuse and single-owner cleanup PASS");
}
