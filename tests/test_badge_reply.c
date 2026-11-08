#include "badge_reply.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    badge_reply_tracker_t t={0};uint8_t scope[32]={1};
    badge_reply_bind(&t,"clip-1",scope);assert(!t.acknowledged);
    assert(badge_reply_ack(&t,"run-1"));
    badge_reply_bind(&t,"clip-1",scope);assert(t.acknowledged && !strcmp(t.run_id,"run-1"));
    assert(!badge_reply_ack(&t,"run-other"));
    assert(!badge_reply_terminal_text(&t,"run-1","assistant",NULL,false));
    assert(!badge_reply_terminal_text(&t,"run-1","assistant","toolUse",false));
    assert(!badge_reply_terminal_text(&t,"run-1","toolResult","stop",false));
    assert(!badge_reply_terminal_text(&t,"run-other","assistant","stop",false));
    assert(!badge_reply_terminal_text(&t,"run-1","assistant","stop",true));
    assert(badge_reply_terminal_text(&t,"run-1","assistant","stop",false));
    /* This observed reply completed at 196 seconds, after the former deadline. */
    assert(196415ULL>BADGE_REPLY_SOFT_MS && 196415ULL<BADGE_REPLY_MAX_MS);
    assert(badge_reply_poll_due(&t,0));badge_reply_poll_sent(&t,0);
    assert(!badge_reply_poll_due(&t,15000));assert(badge_reply_poll_due(&t,20000));
    badge_reply_poll_sent(&t,20000);t.poll_pending=false;assert(badge_reply_poll_due(&t,35000));
    scope[0]=2;badge_reply_bind(&t,"clip-1",scope);assert(!t.acknowledged);
    assert(badge_reply_ack(&t,"run-2"));badge_reply_bind(&t,"clip-2",scope);assert(!t.acknowledged);
    assert(!badge_reply_resume(&t,"bad\nrun",scope));
    assert(badge_reply_resume(&t,"old-accepted-run",scope) && t.acknowledged);
    char live[10]="";
    assert(badge_reply_live_apply(live,sizeof(live),"听",false) && !strcmp(live,"听"));
    assert(badge_reply_live_apply(live,sizeof(live),"见了",false) && !strcmp(live,"听见了"));
    assert(!badge_reply_live_apply(live,sizeof(live),"",false));
    assert(!badge_reply_live_apply(live,sizeof(live),"好",false) && !strcmp(live,"听见了"));
    assert(badge_reply_live_apply(live,sizeof(live),"换",true) && !strcmp(live,"换"));
    assert(!badge_reply_live_apply(live,sizeof(live),"换",true));
    assert(badge_reply_live_apply(live,sizeof(live),"新",true) && !strcmp(live,"新"));
    assert(!badge_reply_live_apply(live,sizeof(live),"\xe6\x98",false) && !strcmp(live,"新"));
    assert(!badge_reply_live_apply(live,sizeof(live),"\xed\xa0\x80",false));
    assert(!badge_reply_live_apply(live,sizeof(live),"\xf4\x90\x80\x80",false));
    assert(badge_reply_live_apply(live,sizeof(live),"",true) && !strcmp(live,""));
    assert(!badge_reply_live_apply(live,sizeof(live),"",true));
    assert(badge_reply_live_apply(live,sizeof(live),"abcdefghijklmnop",false) && strlen(live)==9);
    char quoted[128] = "";
    assert(badge_reply_live_apply(quoted,sizeof(quoted),"助手说：“",false));
    assert(badge_reply_live_apply(quoted,sizeof(quoted),"你好，‘朋友’",false));
    assert(badge_reply_live_apply(quoted,sizeof(quoted),"。” — 稍等…",false));
    assert(!strcmp(quoted,"助手说：“你好，‘朋友’。” — 稍等…"));
    char boundary[6] = "";
    assert(badge_reply_live_apply(boundary,sizeof(boundary),"“”",false) && !strcmp(boundary,"“"));
    assert(sizeof(t)<200);
    char tail[10] = "";
    assert(badge_reply_live_tail(tail,sizeof(tail),"听见了",false) && !strcmp(tail,"听见了"));
    assert(badge_reply_live_tail(tail,sizeof(tail),"你好",false) && !strcmp(tail,"了你好"));
    assert(badge_reply_live_tail(tail,sizeof(tail),"abcdefghijklmnop",true) && !strcmp(tail,"hijklmnop"));
    assert(badge_reply_live_tail(tail,sizeof(tail),"天气很好，明天晴天。",true) && !strcmp(tail,"晴天。"));
    assert(!badge_reply_live_tail(tail,sizeof(tail),"\xe6\x98",false) && !strcmp(tail,"晴天。"));
    assert(badge_reply_live_tail(tail,sizeof(tail),"新",true) && !strcmp(tail,"新"));
    assert(badge_reply_live_tail(tail,sizeof(tail),"",true) && !tail[0]);
    char tiny[2] = "";
    assert(!badge_reply_live_tail(tiny,sizeof(tiny),"你",false) && !tiny[0]);
    assert(badge_reply_live_tail(tiny,sizeof(tiny),"你a",false) && !strcmp(tiny,"a"));
    puts("Reply recovery: request/agent binding, terminal-only history, late reply, lost-poll recovery and live text PASS");
}
