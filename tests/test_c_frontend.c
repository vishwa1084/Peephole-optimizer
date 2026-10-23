#include "../src/parser.h"
#include "../src/optimizer.h"
#include "../src/encoder.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int check_invalid(const char *path, const char *needle) {
    Program p={0}; char err[256];
    if(parse_c_file(path,&p,err,sizeof err)){fprintf(stderr,"accepted invalid C input %s\n",path);program_free(&p);return 1;}
    if(!strstr(err,needle)){fprintf(stderr,"wrong diagnostic for %s: %s\n",path,err);return 1;}
    return 0;
}
static int64_t run_integer_program(const Program *p) {
    int64_t r[8]={0}, values[128]={0}; char names[128][MAX_NAME]; size_t used=0,pc=0; int relation=0;
    while(pc<p->count){
        const Instruction *in=&p->items[pc]; Operand a=in->arg[0],b=in->arg[1],c=in->arg[2]; int64_t rhs=0; size_t next=pc+1;
        if(in->op==OP_LABEL){pc++;continue;}
        if(in->op==OP_HALT)break;
        if(in->op==OP_LD) {
            if(b.kind==OPER_IMM)r[a.reg]=b.imm;
            else {size_t k=0;while(k<used&&strcmp(names[k],b.name))k++;r[a.reg]=k<used?values[k]:0;}
        } else if(in->op==OP_ST) {
            size_t k=0;while(k<used&&strcmp(names[k],a.name))k++;
            if(k==used){snprintf(names[used],MAX_NAME,"%s",a.name);used++;}values[k]=r[b.reg];
        } else if(in->op==OP_MOV)r[a.reg]=r[b.reg];
        else if(in->op==OP_CMP)relation=r[a.reg]<r[b.reg]?-1:r[a.reg]>r[b.reg]?1:0;
        else if(in->op==OP_JMP||isa_is_conditional_branch(in->op)) {
            bool take=in->op==OP_JMP;
            if(in->op==OP_JZ)take=r[a.reg]==0;
            if(in->op==OP_JNZ)take=r[a.reg]!=0;
            if(in->op==OP_JLT)take=relation<0;
            if(in->op==OP_JGT)take=relation>0;
            if(in->op==OP_JLE)take=relation<=0;
            if(in->op==OP_JGE)take=relation>=0;
            if(in->op==OP_JE)take=relation==0;
            if(in->op==OP_JNE)take=relation!=0;
            if(take){const char *target=in->op==OP_JZ||in->op==OP_JNZ?b.name:a.name;size_t k=0;while(k<p->count&&(p->items[k].op!=OP_LABEL||strcmp(p->items[k].arg[0].name,target)))k++;if(k==p->count)return INT64_MIN;next=k+1;}
        } else if(in->op==OP_ADD||in->op==OP_SUB||in->op==OP_MUL||in->op==OP_DIV||in->op==OP_SHL||in->op==OP_SHR) {
            rhs=c.kind==OPER_IMM?c.imm:r[c.reg];
            if(in->op==OP_ADD)r[a.reg]=r[b.reg]+rhs;
            if(in->op==OP_SUB)r[a.reg]=r[b.reg]-rhs;
            if(in->op==OP_MUL)r[a.reg]=r[b.reg]*rhs;
            if(in->op==OP_DIV)r[a.reg]=r[b.reg]/rhs;
            if(in->op==OP_SHL)r[a.reg]=r[b.reg]<<rhs;
            if(in->op==OP_SHR)r[a.reg]=r[b.reg]>>rhs;
        }
        pc=next;
    }
    return r[0];
}
static uint64_t decode_u64(const unsigned char *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static int check_binary_targets(const Program *p){
    FILE *f=tmpfile();if(!f)return 1;char err[256];
    if(!encode_binary(p,f,err,sizeof err)){fprintf(stderr,"binary encoding failed: %s\n",err);fclose(f);return 1;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return 1;}long n=ftell(f);if(n<16||fseek(f,0,SEEK_SET)!=0){fclose(f);return 1;}
    unsigned char *bytes=malloc((size_t)n);if(!bytes){fclose(f);return 1;}size_t got=fread(bytes,1,(size_t)n,f);fclose(f);
    size_t executable=0;for(size_t i=0;i<p->count;i++)if(p->items[i].op!=OP_LABEL)executable++;
    if(got!=(size_t)n||memcmp(bytes,"PHISA\1\r\n",8)||decode_u64((unsigned char[]){bytes[8],bytes[9],bytes[10],bytes[11],0,0,0,0})!=executable||got!=16+32*executable){free(bytes);return 1;}
    size_t record=0;
    for(size_t i=0;i<p->count;i++)if(p->items[i].op!=OP_LABEL){
        const Instruction *in=&p->items[i];const unsigned char *r=bytes+16+32*record++;
        if(r[0]!=(unsigned char)in->op||r[1]!=(unsigned char)in->argc){free(bytes);return 1;}
        for(int a=0;a<in->argc;a++)if(in->arg[a].kind==OPER_LABEL){size_t target=0,j=0;while(j<p->count&&(p->items[j].op!=OP_LABEL||strcmp(p->items[j].arg[0].name,in->arg[a].name))){if(p->items[j].op!=OP_LABEL)target++;j++;}if(j==p->count||decode_u64(r+2+(size_t)a*10+1)!=target){fprintf(stderr,"encoded branch target mismatch for %s\n",opcode_name(in->op));free(bytes);return 1;}}
    }
    free(bytes);return 0;
}
static int check_result(const char *path,int64_t expected){
    Program p={0},opt={0};char err[256];RewriteLog log={0};OptimizeStats stats;
    if(!parse_c_file(path,&p,err,sizeof err)){fprintf(stderr,"%s: %s\n",path,err);return 1;}
    if(!validate_labels(&p,err,sizeof err)){fprintf(stderr,"invalid generated branch target: %s\n",err);program_free(&p);return 1;}
    if(check_binary_targets(&p)){program_free(&p);return 1;}
    opt.items=malloc(p.count*sizeof(*p.items));if(!opt.items){program_free(&p);return 1;}memcpy(opt.items,p.items,p.count*sizeof(*p.items));opt.count=opt.capacity=p.count;
    if(!optimize(&opt,RULE_ALL,&log,&stats,err,sizeof err)||!validate_labels(&opt,err,sizeof err)){fprintf(stderr,"optimized branch validation failed: %s\n",err);program_free(&p);program_free(&opt);rewrite_log_free(&log);return 1;}
    if(check_binary_targets(&opt)){program_free(&p);program_free(&opt);rewrite_log_free(&log);return 1;}
    int64_t before=run_integer_program(&p),after=run_integer_program(&opt);
    if(before!=expected||after!=expected){fprintf(stderr,"%s returned %lld before and %lld after optimization; expected %lld\n",path,(long long)before,(long long)after,(long long)expected);program_free(&p);program_free(&opt);rewrite_log_free(&log);return 1;}
    program_free(&p);program_free(&opt);rewrite_log_free(&log);return 0;
}
int main(void) {
    Program p={0}; char err[256];
    if(!parse_c_file("samples/input.c",&p,err,sizeof err)){fprintf(stderr,"C sample rejected: %s\n",err);return 1;}
    if(!p.count){fprintf(stderr,"C translation emitted no instructions\n");program_free(&p);return 1;}
    for(size_t i=0;i<p.count;i++)if(!isa_validate(&p.items[i],err,sizeof err)){fprintf(stderr,"invalid translated instruction: %s\n",err);program_free(&p);return 1;}
    int arithmetic=0;for(size_t i=0;i<p.count;i++)if(p.items[i].op==OP_ADD||p.items[i].op==OP_MUL||p.items[i].op==OP_SUB)arithmetic++;
    if(arithmetic<2){fprintf(stderr,"translation lost arithmetic operations before optimization\n");program_free(&p);return 1;}
    Program optimized={0};
    optimized.items=malloc(p.count*sizeof(*p.items));
    if(!optimized.items){program_free(&p);return 1;}
    memcpy(optimized.items,p.items,p.count*sizeof(*p.items)); optimized.count=optimized.capacity=p.count;
    int64_t before=run_integer_program(&p); RewriteLog log={0}; OptimizeStats stats;
    if(!optimize(&optimized,RULE_ALL,&log,&stats,err,sizeof err)){fprintf(stderr,"C program optimization failed: %s\n",err);program_free(&p);program_free(&optimized);rewrite_log_free(&log);return 1;}
    int64_t after=run_integer_program(&optimized);
    if(before!=200||after!=before){fprintf(stderr,"original/optimized C execution mismatch: %lld vs %lld; expected 200\n",(long long)before,(long long)after);program_free(&p);program_free(&optimized);rewrite_log_free(&log);return 1;}
    bool folded_result=false;for(size_t i=0;i<optimized.count;i++)if(optimized.items[i].op==OP_LD&&optimized.items[i].arg[1].kind==OPER_IMM&&optimized.items[i].arg[1].imm==200)folded_result=true;
    if(stats.changes==0||!folded_result){fprintf(stderr,"constant propagation did not fold the sample's a*b expression to 200\n");program_free(&p);program_free(&optimized);rewrite_log_free(&log);return 1;}
    program_free(&optimized);rewrite_log_free(&log);
    program_free(&p);
    if(!parse_c_file("tests/precedence.c",&p,err,sizeof err)){fprintf(stderr,"precedence input rejected: %s\n",err);return 1;}
    size_t mul=SIZE_MAX,add=SIZE_MAX;for(size_t i=0;i<p.count;i++){if(p.items[i].op==OP_MUL)mul=i;if(p.items[i].op==OP_ADD)add=i;}
    if(mul==SIZE_MAX||add==SIZE_MAX||mul>add){fprintf(stderr,"multiplication did not precede addition in generated instructions\n");program_free(&p);return 1;}program_free(&p);
    if(!parse_c_file("tests/parentheses.c",&p,err,sizeof err)){fprintf(stderr,"parenthesized input rejected: %s\n",err);return 1;}
    mul=SIZE_MAX;add=SIZE_MAX;for(size_t i=0;i<p.count;i++){if(p.items[i].op==OP_MUL)mul=i;if(p.items[i].op==OP_ADD)add=i;}
    if(mul==SIZE_MAX||add==SIZE_MAX||add>mul){fprintf(stderr,"parenthesized addition order was not preserved\n");program_free(&p);return 1;}program_free(&p);
    if(check_invalid("tests/invalid_missing_semicolon.c","line")||check_invalid("tests/invalid_brace.c","line")||check_invalid("tests/invalid_if.c","line")||check_invalid("tests/invalid_loop.c","line")||check_invalid("tests/loop_for_scope.c","undeclared variable")||check_invalid("tests/undeclared_variable.c","undeclared variable")||check_invalid("tests/duplicate_variable.c","duplicate local variable")||check_invalid("tests/division_by_zero.c","division by zero"))return 1;
    if(check_result("tests/unsupported_if.c",1)||check_result("tests/if_true.c",30)||check_result("tests/if_false.c",7)||check_result("tests/if_else_true.c",30)||check_result("tests/if_else_false.c",-10)||check_result("tests/if_nested.c",42)||check_result("tests/comparisons.c",7)||check_result("tests/if_constants.c",9)||check_result("tests/if_scope.c",1)||check_result("tests/if_unknown_after_merge.c",2)||check_result("tests/if_reassigned.c",2))return 1;
    if(parse_c_file("tests/if_unknown_after_merge.c",&p,err,sizeof err)){RewriteLog branch_log={0};OptimizeStats branch_stats;if(!optimize(&p,RULE_ALL,&branch_log,&branch_stats,err,sizeof err)){program_free(&p);rewrite_log_free(&branch_log);return 1;}bool unknown_branch=false;for(size_t i=0;i<p.count;i++)if(p.items[i].op==OP_JNE)unknown_branch=true;if(!unknown_branch){fprintf(stderr,"optimizer guessed a value across a control-flow merge\n");program_free(&p);rewrite_log_free(&branch_log);return 1;}program_free(&p);rewrite_log_free(&branch_log);}else{fprintf(stderr,"unknown-merge sample rejected: %s\n",err);return 1;}
    puts("C frontend tests passed"); return 0;
}
