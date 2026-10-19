/* Test-only host adapter: expose OutputDebugString assertions on captured stdout.
 * Guest builds continue using their actual Windows debug-output implementation. */
__declspec(dllimport) void *GetStdHandle(unsigned);
__declspec(dllimport) int WriteFile(void *,const void *,unsigned,unsigned *,void *);
static void write_log(const char *s){unsigned n=0,done;while(s[n])n++;WriteFile(GetStdHandle((unsigned)-11),s,n,&done,0);}
void (*__imp_ReferenceDebugLog)(const char *)=write_log;
