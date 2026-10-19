#ifdef ALTERED_TOP
__declspec(dllimport) unsigned LeafValue(void);
__declspec(dllexport) unsigned ActualValue(void){return LeafValue()+1;}
#else
#ifndef ALTERED_VALUE
#define ALTERED_VALUE 321
#endif
__declspec(dllexport) unsigned LeafValue(void){return ALTERED_VALUE;}
#endif
int DllMain(void *image,unsigned reason,void *reserved){(void)image;(void)reason;(void)reserved;return 1;}
