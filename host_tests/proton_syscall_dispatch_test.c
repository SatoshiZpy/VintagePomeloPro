/* Execute the actual generated AArch64 dispatcher, never the sandboxed calls. */
#include <errno.h>
#include <stdio.h>
/* libc marks syscall as a leaf; our test dispatcher intentionally calls local
 * stubs. Use a plain declaration of the same assembly symbol for the harness. */
extern long probe_syscall(long, ...) __asm__("syscall");
static unsigned forwarded, optional;
static long next(long n,long a,long b,long c,long d,long e,long f) {
    ++forwarded;
    return n==172&&a==11&&b==22&&c==33&&d==44&&e==55&&f==66 ? 12345 : 54321;
}
long (*SteamOhosNextSyscall)(long,long,long,long,long,long,long)=next;
long SteamOhosSyscallUnavailable(void) {errno=ENOSYS;return -1;}
long SteamOhosFexOptionalCall(long n,long a,long b,long c,long d,long e,long f) {
    ++optional;return next(n,a,b,c,d,e,f);
}
#define STUB(name) long name(void) {return 777;}
STUB(SteamOhosLandlockCreateRuleset)
STUB(SteamOhosFexFileCall)
STUB(SteamOhosFexExecCall)
STUB(SteamOhosFexExitCall)
STUB(SteamOhosFexSignalCall)
STUB(SteamOhosOpenAt2)
STUB(SteamOhosCloseRange)
STUB(SteamOhosQueryRobustList)
int main(void) {
    const long optional_calls[]={449,282,374};
    for(unsigned i=0;i<sizeof(optional_calls)/sizeof(optional_calls[0]);++i) {
        errno=0;long result=probe_syscall(optional_calls[i],11L,22L,33L,44L,55L,66L);
        if(result!=-1||errno!=ENOSYS||forwarded||optional) {
            fprintf(stderr,"FAIL: optional syscall %ld escaped Wine fallback\n",optional_calls[i]);return 1;
        }
    }
    if(probe_syscall(172L,11L,22L,33L,44L,55L,66L)!=12345||forwarded!=1)return 2;
    if(probe_syscall(98L,11L,22L,33L,44L,55L,66L)!=54321||forwarded!=2)return 3; /* ordinary futex */
    if(probe_syscall(56L,11L,22L,33L,44L,55L,66L)!=777||forwarded!=2)return 4; /* existing file dispatch */
    puts("PASS: optional probes return ENOSYS before FEX dispatch; normal futex, argument ABI and file routing preserved");
    return 0;
}
