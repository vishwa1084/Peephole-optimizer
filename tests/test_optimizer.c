#include "../src/optimizer.h"
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);failures++;} } while(0)
static Program parse(const char*s){Program p={0};char e[200];if(!parse_text(s,&p,e,sizeof e)){fprintf(stderr,"parse failed: %s\n",e);failures++;}return p;}
static void test_parse_errors(void){Program p={0};char e[200];CHECK(!parse_text("LD R8, x\n",&p,e,sizeof e));CHECK(strstr(e,"line 1")!=NULL);CHECK(!parse_text("LD R1,\n",&p,e,sizeof e));CHECK(!parse_text("JMP nowhere\n",&p,e,sizeof e));CHECK(!parse_text("x:\nx:\n",&p,e,sizeof e));CHECK(!parse_text("BOGUS R1\n",&p,e,sizeof e));CHECK(!parse_text("ST R1, x\n",&p,e,sizeof e));}
static void test_rule(const char*src,unsigned rule,size_t expected){Program p=parse(src);RewriteLog l={0};OptimizeStats s;char e[200];CHECK(optimize(&p,rule,&l,&s,e,sizeof e));if(p.count!=expected||l.count==0)fprintf(stderr,"rule test failed for: %s (count=%zu expected=%zu rewrites=%zu)\n",src,p.count,expected,l.count);CHECK(p.count==expected);CHECK(l.count>0);program_free(&p);rewrite_log_free(&l);}
int main(void){test_parse_errors();
 test_rule("MOV R1, R1\n",RULE_SELF_MOVE,0);
 test_rule("ST x, R1\nLD R1, x\n",RULE_LOAD_AFTER_STORE,1);
 test_rule("ST x, R1\nLD R2, x\n",RULE_LOAD_COPY,2);
 test_rule("ST x, R1\nST x, R1\n",RULE_REDUNDANT_STORE,1);
 test_rule("ADD R1, R2, #0\n",RULE_ALGEBRA,1);
 test_rule("MUL R1, R2, #0\n",RULE_MUL_ZERO,1);
 test_rule("LD R2, #2\nMUL R1, R2, #8\n",RULE_STRENGTH,2);
 test_rule("JMP L\nL:\nHALT\n",RULE_NEXT_JUMP,2);
 test_rule("JMP A\nA:\nJMP B\nB:\nHALT\n",RULE_JUMP_CHAIN,5);
 test_rule("JMP L\nADD R1, R2, #1\nL:\nHALT\n",RULE_UNREACHABLE,3);
 test_rule("ST x, R1\nST x, R2\n",RULE_DEAD_STORE,1);
 Program p=parse("ADD R1, R2, #0\n");RewriteLog l={0};OptimizeStats s;char e[200];CHECK(optimize(&p,RULE_ALL,&l,&s,e,sizeof e));CHECK(s.passes>=1);program_free(&p);rewrite_log_free(&l);
 p=parse("LD R2, #6\nADD R1, R2, #3\n");l=(RewriteLog){0};CHECK(optimize(&p,RULE_FOLD,&l,&s,e,sizeof e));CHECK(l.count==1&&p.items[1].op==OP_LD&&p.items[1].arg[1].imm==9);program_free(&p);rewrite_log_free(&l);
 p=parse("NOP\n");CHECK(p.count==1);program_free(&p);
 p=parse("ST x, R1\nblock:\nLD R2, x\n");l=(RewriteLog){0};CHECK(optimize(&p,RULE_LOAD_COPY,&l,&s,e,sizeof e));CHECK(l.count==0&&p.items[1].op==OP_LABEL);program_free(&p);rewrite_log_free(&l);
 p=parse("JMP A\nA:\nJMP B\nB:\nJMP A\n");l=(RewriteLog){0};CHECK(optimize(&p,RULE_JUMP_CHAIN,&l,&s,e,sizeof e));CHECK(s.passes==1&&l.count==0&&p.count==5);program_free(&p);rewrite_log_free(&l);
 p=parse("JMP trampoline\nbody:\nNOP\ntrampoline:\nJMP finish\nfinish:\nHALT\n");l=(RewriteLog){0};CHECK(optimize(&p,RULE_ALL,&l,&s,e,sizeof e));CHECK(l.count>0&&validate_labels(&p,e,sizeof e));CHECK(p.count>=2&&p.items[0].op==OP_LABEL&&strcmp(p.items[0].arg[0].name,"finish")==0&&p.items[1].op==OP_HALT);program_free(&p);rewrite_log_free(&l);
 if(failures){fprintf(stderr,"%d test(s) failed\n",failures);return 1;}puts("all C tests passed");return 0;}
