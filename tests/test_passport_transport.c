/* Fault injection runs the actual project transport, including WS framing. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CONFIG_WS_BUFFER_SIZE 1024
#include "../components/passport_transport/transport_ws.c"
static unsigned char input[4096], output[4096];
static size_t input_size, cursor, available, output_size, write_limit;
static int poll_calls, write_calls, fail_write_call;
static int write_wait_ms;
static int temporary_write, temporary_calls;
static int64_t clock_offset_us;
void *esp_transport_get_context_data(esp_transport_handle_t t) { return t->data; }
int64_t esp_timer_get_time(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1000000LL+ts.tv_nsec/1000+clock_offset_us; }
ssize_t getrandom(void *p,size_t n,unsigned flags) { (void)flags;memset(p,0x35,n);return n; }
int esp_transport_read(esp_transport_handle_t t,char *p,int n,int timeout) {
    (void)t;(void)timeout;
    size_t take=available-cursor; if(take>(size_t)n)take=n;
    memcpy(p,input+cursor,take);cursor+=take;return (int)take;
}
int esp_transport_write(esp_transport_handle_t t,const char *p,int n,int timeout) {
    (void)t;(void)timeout;
    write_calls++;
    if (write_calls == fail_write_call) return -1;
    if (temporary_calls) { temporary_calls--;return temporary_write; }
    if((size_t)n>write_limit)n=(int)write_limit;
    assert(output_size+n<=sizeof(output));memcpy(output+output_size,p,n);output_size+=n;return n;
}
int esp_transport_poll_read(esp_transport_handle_t t,int timeout) { (void)t;(void)timeout;poll_calls++;return cursor<available; }
int esp_transport_poll_write(esp_transport_handle_t t,int timeout) {
    (void)t;
    int elapsed=write_wait_ms<timeout?write_wait_ms:timeout;
    write_wait_ms-=elapsed;clock_offset_us+=(int64_t)elapsed*1000;
    return !write_wait_ms;
}
static transport_ws_t ws;
static struct esp_transport_item_t parent, handle;
static void reset(const unsigned char *wire,size_t n) {
    memset(&ws,0,sizeof(ws));if(n)memcpy(input,wire,n);input_size=n;cursor=available=output_size=0;write_limit=sizeof(output);poll_calls=write_calls=fail_write_call=write_wait_ms=0;clock_offset_us=0;
    ws.parent=&parent;handle.data=&ws;
    temporary_write=temporary_calls=0;
}
static void splits(const unsigned char *wire,size_t n,const char *expected,size_t len) {
    for(size_t split=0;split<=n;split++) {
        reset(wire,n);available=split;
        char received[512];size_t used=0;
        for(int pass=0;pass<2;pass++) {
            for(int attempt=0;attempt<16;attempt++) {
                int got=ws_read(&handle,received+used,sizeof(received)-used,1);
                assert(got>=0);used+=got;
                if(!got)break;
            }
            available=input_size;
        }
        assert(used==len && !memcmp(received,expected,len));
    }
}
int main(void) {
    const unsigned char plain[]={0x81,5,'h','e','l','l','o'};
    splits(plain,sizeof(plain),"hello",5);
    unsigned char extended[134]={0x81,126,0,130};memset(extended+4,'x',130);
    char expected[130];memset(expected,'x',130);splits(extended,sizeof(extended),expected,130);
    const unsigned char long_header[]={0x81,127,0,0,0,0,0,0,0,2,'o','k'};
    splits(long_header,sizeof(long_header),"ok",2);
    /* Coalesced upgrade+challenge: the socket may have no readable bytes. */
    reset(NULL,0);ws.buffer=(char *)plain;ws.buffer_len=sizeof(plain);char buf[32];
    assert(ws_poll_read(&handle,3000)>0 && !poll_calls);
    assert(ws_read(&handle,buf,sizeof(buf),3000)==5 && !memcmp(buf,"hello",5) && !poll_calls);
    assert(passport_transport_ws_get_read_opcode(&handle)==WS_TRANSPORT_OPCODES_TEXT);
    /* A ping split across two reads must yield exactly one correct pong. */
    const unsigned char ping[]={0x89,3,'a','b','c',0x81,2,'o','k'};
    reset(ping,sizeof(ping));available=4;
    assert(ws_read(&handle,buf,sizeof(buf),1)==0 && output_size==0);
    assert(ws_read(&handle,buf,sizeof(buf),1)==0 && output_size==0);
    available=input_size;
    assert(ws_read(&handle,buf,sizeof(buf),1)==0 && output_size==9);
    assert(passport_transport_ws_last_receive_us(&handle)>0);
    assert(output[0]==0x8a && output[1]==0x83);
    for(int i=0;i<3;i++)assert((output[6+i]^output[2+i%4])=="abc"[i]);
    assert(ws_read(&handle,buf,sizeof(buf),1)==2 && !memcmp(buf,"ok",2));
    reset(ping,sizeof(ping));available=input_size;write_wait_ms=4;
    assert(ws_read(&handle,buf,sizeof(buf),1)==0 && output_size==9);
    assert(!strcmp(passport_transport_ws_failure(&handle),"none"));
    assert(ws_read(&handle,buf,sizeof(buf),1)==2 && !memcmp(buf,"ok",2));
    reset(ping,sizeof(ping));available=input_size;write_wait_ms=2000;
    assert(ws_read(&handle,buf,sizeof(buf),1)<0 && !output_size);
    assert(!strcmp(passport_transport_ws_failure(&handle),"pong-write"));
    /* Fragmented text remains aligned around an unsolicited ping. */
    const unsigned char fragmented[]={0x01,1,'a',0x89,0,0x80,1,'b'};
    reset(fragmented,sizeof(fragmented));available=input_size;
    assert(ws_read(&handle,buf,sizeof(buf),1)==1 && buf[0]=='a');
    assert(ws_read(&handle,buf,sizeof(buf),1)==0);
    assert(ws_read(&handle,buf,sizeof(buf),1)==1 && buf[0]=='b');
    /* Short TLS writes must not leave an unfinished header or masked payload. */
    reset(NULL,0);write_limit=1;char payload[]="hello";
    assert(_ws_write(&handle,WS_FIN|WS_OPCODE_TEXT,WS_MASK,payload,5,1000)==5);
    assert(output_size==11 && !strcmp(payload,"hello"));
    for(int i=0;i<5;i++)assert((output[6+i]^output[2+i%4])=="hello"[i]);
    char scratch[WS_BUFFER_SIZE];
    reset(NULL,0);ws.buffer=scratch;
    assert(_ws_write(&handle,WS_FIN|WS_OPCODE_TEXT,WS_MASK,payload,5,1000)==5);
    assert(write_calls==1 && output_size==11 && !strcmp(payload,"hello"));
    for(int i=0;i<5;i++)assert((output[6+i]^output[2+i%4])=="hello"[i]);
    reset(NULL,0);ws.buffer=scratch;write_limit=1;
    assert(_ws_write(&handle,WS_FIN|WS_OPCODE_TEXT,WS_MASK,payload,5,1000)==5);
    assert(output_size==11 && !strcmp(payload,"hello"));
    reset(NULL,0);ws.buffer=scratch;fail_write_call=1;
    assert(_ws_write(&handle,WS_FIN|WS_OPCODE_TEXT,WS_MASK,payload,5,1000)<0);
    assert(!strcmp(passport_transport_ws_failure(&handle),"write-frame"));
    assert(!strcmp(payload,"hello"));
    reset(NULL,0);ws.buffer=scratch;ws.buffer_len=3;memcpy(scratch,"rx!",3);
    assert(_ws_write(&handle,WS_FIN|WS_OPCODE_TEXT,WS_MASK,payload,5,1000)==5);
    assert(write_calls==2 && !memcmp(scratch,"rx!",3) && ws.buffer_len==3);
    reset(NULL,0);ws.buffer=scratch;
    char maximum[1016];memset(maximum,'x',sizeof(maximum));
    assert(_ws_write(&handle,WS_OPCODE_CONT,WS_MASK,maximum,sizeof(maximum),1000)==sizeof(maximum));
    assert(write_calls==1 && output_size==1024);
    for(size_t index=0;index<sizeof(maximum);index++) assert(maximum[index]=='x');
    reset(NULL,0);ws.buffer=scratch;
    assert(_ws_write(&handle,WS_FIN|WS_OPCODE_PONG,WS_MASK,NULL,0,1000)==0 && write_calls==1 && output_size==6);
    reset(NULL,0);ws.buffer=scratch;
    assert(_ws_write(&handle,WS_OPCODE_CONT,WS_MASK,payload,-1,1000)<0 && !write_calls);
    assert(_ws_write(&handle,WS_OPCODE_CONT,WS_MASK,NULL,1,1000)<0 && !write_calls);
    reset(NULL,0);ws.buffer=scratch;fail_write_call=2;ws.buffer_len=1;
    assert(_ws_write(&handle,WS_OPCODE_CONT,WS_MASK,payload,5,1000)<0 && !strcmp(payload,"hello"));
    for (int direction=0;direction<2;direction++) {
        reset(NULL,0);ws.buffer=scratch;write_limit=1;temporary_calls=3;
        temporary_write=direction?ESP_TLS_ERR_SSL_WANT_READ:ESP_TLS_ERR_SSL_WANT_WRITE;
        assert(_ws_write(&handle,WS_FIN|WS_OPCODE_TEXT,WS_MASK,payload,5,1000)==5);
        assert(write_calls==14 && output_size==11 && !strcmp(payload,"hello"));
        for (size_t index=0;index<5;index++) assert((output[6+index]^output[2+index%4])==payload[index]);
    }
    reset(NULL,0);ws.buffer=scratch;write_wait_ms=400;
    assert(_ws_write(&handle,WS_FIN|WS_OPCODE_TEXT,WS_MASK,payload,5,1500)==5);
    reset(NULL,0);ws.buffer=scratch;write_wait_ms=2000;
    assert(_ws_write(&handle,WS_FIN|WS_OPCODE_TEXT,WS_MASK,payload,5,1500)<0 && !output_size);
    const unsigned char invalid[]={0x89,126,0,126};reset(invalid,sizeof(invalid));available=input_size;
    assert(ws_read(&handle,buf,sizeof(buf),1)<0);
    assert(!strcmp(passport_transport_ws_failure(&handle),"frame-header"));
    const unsigned char close_frame[]={0x88,0};reset(close_frame,sizeof(close_frame));available=input_size;
    assert(ws_read(&handle,buf,sizeof(buf),1)<0);
    assert(!strcmp(passport_transport_ws_failure(&handle),"peer-close"));
    puts("Passport WebSocket: split headers/payload, timeout recovery, buffered upgrade, ping/pong, close and short writes PASS");
}
