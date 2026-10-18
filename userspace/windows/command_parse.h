#ifndef KYIL_COMMAND_PARSE_H
#define KYIL_COMMAND_PARSE_H
#include <stdint.h>
#include <stddef.h>
/* Shell32 parsing, deliberately separate from CRT and executable selection.
   Input is a bounded, immutable, NUL-terminated UTF-16 snapshot. NULL outputs
   perform the sizing pass. Empty command fallback belongs to the API wrapper. */
static void wr_command_emit(uint16_t *out,size_t *n,uint16_t c){if(out)out[*n]=c;(*n)++;}
static void wr_command_parse(const uint16_t *s,uint16_t **argv,uint16_t *out,size_t *argc,size_t *units){
    size_t at=0,n=0,count=1;if(argv)argv[0]=out;
    if(s[at]=='"'){at++;while(s[at] && s[at]!='"')wr_command_emit(out,&n,s[at++]);if(s[at])at++;}
    else while(s[at] && s[at]!=' ' && s[at]!='\t')wr_command_emit(out,&n,s[at++]);
    wr_command_emit(out,&n,0);
    for(;;){while(s[at]==' ' || s[at]=='\t')at++;if(!s[at])break;
        if(argv)argv[count]=out+n;count++;unsigned quoted=0;
        while(s[at] && (quoted || (s[at]!=' ' && s[at]!='\t'))){
            size_t slashes=0;while(s[at]=='\\'){slashes++;at++;}
            if(s[at]=='"'){
                for(size_t i=0;i<slashes/2;i++)wr_command_emit(out,&n,'\\');
                if(slashes&1){wr_command_emit(out,&n,'"');at++;}
                else {unsigned quotes=quoted;while(s[at]=='"'){at++;if(++quotes==3){wr_command_emit(out,&n,'"');quotes=0;}}quoted=quotes==1;}
            }else {for(size_t i=0;i<slashes;i++)wr_command_emit(out,&n,'\\');
                if(s[at] && (quoted || (s[at]!=' ' && s[at]!='\t')))wr_command_emit(out,&n,s[at++]);}
        }
        wr_command_emit(out,&n,0);
    }
    if(argv)argv[count]=NULL;*argc=count;*units=n;
}
#endif
