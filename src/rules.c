#include "rules.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void rewrite_log_free(RewriteLog*l){free(l->items);memset(l,0,sizeof *l);}
static void remove_at(Program*p,size_t i){memmove(&p->items[i],&p->items[i+1],(p->count-i-1)*sizeof *p->items);p->count--;}
static bool logit(RewriteLog*l,const char*r,size_t line,const char*x){if(!l)return true;if(l->count==l->capacity){size_t c=l->capacity?l->capacity*2:16;Rewrite*q=realloc(l->items,c*sizeof*q);if(!q)return false;l->items=q;l->capacity=c;}Rewrite*w=&l->items[l->count++];snprintf(w->rule,sizeof w->rule,"%s",r);w->line=line;snprintf(w->explanation,sizeof w->explanation,"%s",x);return true;}
static bool islabel(const Instruction*i){return i->op==OP_LABEL;}
static size_t target(const Program*p,const char*n){for(size_t j=0;j<p->count;j++)if(islabel(&p->items[j])&&!strcmp(p->items[j].arg[0].name,n))return j;return p->count;}
static bool chained_target(const Program *p, const char *start, char *result) {
 const char *name = start;
 for (size_t steps = 0; steps < p->count; steps++) {
  size_t label = target(p, name);
  if (label == p->count || label + 1 >= p->count || p->items[label + 1].op != OP_JMP) {
   if (name == start) return false;
   snprintf(result, MAX_NAME, "%s", name);
   return true;
  }
  name = p->items[label + 1].arg[0].name;
 }
 return false;
}
static bool reserve_log(RewriteLog *log, size_t additional) {
 if (!log) return true;
 if (additional > SIZE_MAX - log->count) return false;
 size_t need = log->count + additional;
 if (need <= log->capacity) return true;
 if (need > SIZE_MAX / sizeof(*log->items)) return false;
 Rewrite *items = realloc(log->items, need * sizeof(*items));
 if (!items) return false;
 log->items = items;
 log->capacity = need;
 return true;
}
static bool writes_mem(const Instruction*i,const char*n){return i->op==OP_ST&&!strcmp(i->arg[0].name,n);}
static bool reads_mem(const Instruction*i,const char*n){return i->op==OP_LD&&i->arg[1].kind==OPER_MEM&&!strcmp(i->arg[1].name,n);}
static bool same_reg(Operand a,Operand b){return a.kind==OPER_REG&&b.kind==OPER_REG&&a.reg==b.reg;}
static bool pow2(int64_t x){return x>0&&(((uint64_t)x&((uint64_t)x-1))==0);}
static bool known_register(const Program*p,size_t at,int r,int64_t*v){
 for(size_t k=at;k>0;){Instruction*q=&p->items[--k];
  if(q->op==OP_LABEL||isa_is_branch(q->op)||q->op==OP_HALT)return false;
  if(instruction_writes_register(q,r)){if(q->op==OP_LD&&q->arg[1].kind==OPER_IMM){*v=q->arg[1].imm;return true;}return false;}
 }
 return false;
}
static void mov_from(Instruction*i,int r){i->op=OP_MOV;i->argc=2;i->arg[1].kind=OPER_REG;i->arg[1].reg=r;}
typedef struct { char name[MAX_NAME]; int64_t value; } MemConstant;
static bool eval_operation(Opcode op,int64_t x,int64_t y,int64_t*z){
 switch(op){
 case OP_ADD:return !__builtin_add_overflow(x,y,z);
 case OP_SUB:return !__builtin_sub_overflow(x,y,z);
 case OP_MUL:return !__builtin_mul_overflow(x,y,z);
 case OP_DIV:if(y==0||(x==INT64_MIN&&y==-1))return false;*z=x/y;return true;
 case OP_SHL:if(y<0||y>=63||x<0||x>(INT64_MAX>>y))return false;*z=x<<y;return true;
 case OP_SHR:if(y<0||y>=63||x<0)return false;*z=x>>y;return true;
 default:return false;
 }
}
static bool get_memory_constant(const MemConstant*m,size_t n,const char*name,int64_t*v){for(size_t i=0;i<n;i++)if(!strcmp(m[i].name,name)){*v=m[i].value;return true;}return false;}
static bool set_memory_constant(MemConstant*m,size_t*n,size_t cap,const char*name,bool known,int64_t value){
 size_t i=0;while(i<*n&&strcmp(m[i].name,name))i++;
 if(!known){if(i<*n){memmove(&m[i],&m[i+1],(*n-i-1)*sizeof(*m));(*n)--;}return true;}
 if(i==*n){if(*n==cap)return false;snprintf(m[i].name,MAX_NAME,"%s",name);(*n)++;}m[i].value=value;return true;
}
static bool branch_constant(const Instruction*i,const bool reg_known[8],const int64_t regs[8],bool cmp_known,int relation,bool*result){
 switch(i->op){
 case OP_JZ:case OP_JNZ:{int r=i->arg[0].reg;if(!reg_known[r])return false;*result=i->op==OP_JZ?regs[r]==0:regs[r]!=0;return true;}
 case OP_JLT:case OP_JGT:case OP_JLE:case OP_JGE:case OP_JE:case OP_JNE:
  if(!cmp_known)return false;
  switch(i->op){case OP_JLT:*result=relation<0;break;case OP_JGT:*result=relation>0;break;case OP_JLE:*result=relation<=0;break;case OP_JGE:*result=relation>=0;break;case OP_JE:*result=relation==0;break;default:*result=relation!=0;break;}return true;
 default:return false;
 }
}
static void clear_constant_state(bool reg_known[8],size_t*memory_count,bool*cmp_known){memset(reg_known,0,8*sizeof(*reg_known));*memory_count=0;*cmp_known=false;}
static bool prune_unreachable(Program*p,RewriteLog*log,size_t*changes){
 if(!p->count)return true;
 bool*reachable=calloc(p->count,sizeof(*reachable));size_t*queue=malloc(p->count*sizeof(*queue));
 if(!reachable||!queue){free(reachable);free(queue);return false;}
 size_t head=0,tail=0;reachable[0]=true;queue[tail++]=0;
 while(head<tail){size_t i=queue[head++];const Instruction*in=&p->items[i];size_t succ[2],n=0;
  if(in->op==OP_HALT)continue;
  if(in->op==OP_JMP||isa_is_conditional_branch(in->op)){
   const char*name=(in->op==OP_JZ||in->op==OP_JNZ)?in->arg[1].name:in->arg[0].name;
   size_t t=target(p,name);if(t<p->count)succ[n++]=t;
   if(in->op!=OP_JMP&&i+1<p->count)succ[n++]=i+1;
  }else if(i+1<p->count)succ[n++]=i+1;
  for(size_t j=0;j<n;j++)if(!reachable[succ[j]]){reachable[succ[j]]=true;queue[tail++]=succ[j];}
 }
 size_t write=0,removed=0;
 for(size_t i=0;i<p->count;i++)if(reachable[i])p->items[write++]=p->items[i];else{if(!logit(log,"unreachable-block",p->items[i].line,"removed instruction outside every reachable control-flow path")){free(reachable);free(queue);return false;}removed++;}
 p->count=write;*changes+=removed;free(reachable);free(queue);return true;
}
static bool constant_propagation(Program*p,RewriteLog*log,size_t*changes){
 size_t cap=p->count?p->count:1,mem_count=0;MemConstant*memory=calloc(cap,sizeof(*memory));
 if(!memory)return false;
 bool reg_known[8]={false};int64_t regs[8]={0};bool cmp_known=false;int relation=0;
 for(size_t i=0;i<p->count;){Instruction*in=&p->items[i];
  if(in->op==OP_LABEL){clear_constant_state(reg_known,&mem_count,&cmp_known);i++;continue;}
  if(in->op==OP_LD){int dst=in->arg[0].reg;Operand*src=&in->arg[1];
   if(src->kind==OPER_IMM){reg_known[dst]=true;regs[dst]=src->imm;}
   else {int64_t v;if(get_memory_constant(memory,mem_count,src->name,&v)){src->kind=OPER_IMM;src->imm=v;src->name[0]='\0';reg_known[dst]=true;regs[dst]=v;if(!logit(log,"constant-propagation",in->line,"replaced a load with a value known from an earlier store")){free(memory);return false;}(*changes)++;}else reg_known[dst]=false;}
   cmp_known=false;i++;continue;
  }
  if(in->op==OP_ST){int r=in->arg[1].reg;if(!set_memory_constant(memory,&mem_count,cap,in->arg[0].name,reg_known[r],regs[r])){free(memory);return false;}cmp_known=false;i++;continue;}
  if(in->op==OP_MOV){int d=in->arg[0].reg,s=in->arg[1].reg;reg_known[d]=reg_known[s];if(reg_known[d])regs[d]=regs[s];cmp_known=false;i++;continue;}
  if(in->op==OP_ADD||in->op==OP_SUB||in->op==OP_MUL||in->op==OP_DIV||in->op==OP_SHL||in->op==OP_SHR){
   int d=in->arg[0].reg,s=in->arg[1].reg;Operand rhs=in->arg[2];bool y_known=rhs.kind==OPER_IMM||(rhs.kind==OPER_REG&&reg_known[rhs.reg]);int64_t y=rhs.kind==OPER_IMM?rhs.imm:(rhs.kind==OPER_REG?regs[rhs.reg]:0),z=0;
   bool algebra=(rhs.kind==OPER_IMM&&((in->op==OP_ADD&&y==0)||(in->op==OP_SUB&&y==0)||(in->op==OP_MUL&&y==1)||(in->op==OP_DIV&&y==1)));
   if(in->op==OP_MUL&&rhs.kind==OPER_IMM&&y==0){in->op=OP_LD;in->argc=2;in->arg[1]=(Operand){.kind=OPER_IMM,.imm=0};reg_known[d]=true;regs[d]=0;if(!logit(log,"algebraic-simplification",in->line,"replaced multiplication by zero with a constant zero")){free(memory);return false;}(*changes)++;cmp_known=false;i++;continue;}
   if(algebra){int src_reg=in->arg[1].reg;in->op=OP_MOV;in->argc=2;in->arg[1]=(Operand){.kind=OPER_REG,.reg=src_reg};reg_known[d]=reg_known[src_reg];if(reg_known[d])regs[d]=regs[src_reg];if(!logit(log,"algebraic-simplification",in->line,"removed an arithmetic identity operation")){free(memory);return false;}(*changes)++;cmp_known=false;i++;continue;}
   if(reg_known[s]&&y_known&&eval_operation(in->op,regs[s],y,&z)){in->op=OP_LD;in->argc=2;in->arg[1]=(Operand){.kind=OPER_IMM,.imm=z};reg_known[d]=true;regs[d]=z;if(!logit(log,"arithmetic-constant-folding",in->line,"folded arithmetic using propagated constants with overflow and division checks")){free(memory);return false;}(*changes)++;cmp_known=false;i++;continue;}
   reg_known[d]=false;cmp_known=false;i++;continue;
  }
  if(in->op==OP_CMP){int a=in->arg[0].reg,b=in->arg[1].reg;cmp_known=reg_known[a]&&reg_known[b];if(cmp_known)relation=regs[a]<regs[b]?-1:regs[a]>regs[b]?1:0;i++;continue;}
  if(isa_is_conditional_branch(in->op)){bool take=false;if(branch_constant(in,reg_known,regs,cmp_known,relation,&take)){
    if(take){Operand destination=(in->op==OP_JZ||in->op==OP_JNZ)?in->arg[1]:in->arg[0];in->op=OP_JMP;in->argc=1;in->arg[0]=destination;memset(&in->arg[1],0,2*sizeof(in->arg[1]));if(!logit(log,"constant-branch",in->line,"replaced a proven-taken conditional branch with an unconditional jump")){free(memory);return false;}(*changes)++;clear_constant_state(reg_known,&mem_count,&cmp_known);i++;}
    else {if(!logit(log,"constant-branch",in->line,"removed a conditional branch proven not to be taken")){free(memory);return false;}remove_at(p,i);(*changes)++;cmp_known=false;}
   }else {clear_constant_state(reg_known,&mem_count,&cmp_known);i++;}
   continue;
  }
  if(in->op==OP_JMP||in->op==OP_HALT){clear_constant_state(reg_known,&mem_count,&cmp_known);i++;continue;}
  cmp_known=false;i++;
 }
 free(memory);
 return prune_unreachable(p,log,changes);
}
bool apply_rules(Program*p,unsigned mask,RewriteLog*log,size_t*changes){if(!p||!changes)return false;*changes=0;if(!reserve_log(log,p->count))return false;for(size_t i=0;i<p->count;i++){
 Instruction *a=&p->items[i]; if(a->op==OP_LABEL)continue;
 if((mask&RULE_SELF_MOVE)&&a->op==OP_MOV&&same_reg(a->arg[0],a->arg[1])){logit(log,"self-move",a->line,"removed a move from a register to itself");remove_at(p,i--);(*changes)++;continue;}
 if((mask&RULE_NEXT_JUMP)&&a->op==OP_JMP&&i+1<p->count&&islabel(&p->items[i+1])&&!strcmp(a->arg[0].name,p->items[i+1].arg[0].name)){logit(log,"jump-to-next-label",a->line,"removed jump to the immediately following label");remove_at(p,i--);(*changes)++;continue;}
 if((mask&RULE_UNREACHABLE)&&a->op==OP_JMP){size_t j=i+1;while(j<p->count&&!islabel(&p->items[j]))j++;if(j>i+1){for(size_t k=i+1;k<j;k++)logit(log,"unreachable-code",p->items[k].line,"removed instruction after unconditional jump");*changes+=j-i-1;memmove(&p->items[i+1],&p->items[j],(p->count-j)*sizeof *p->items);p->count-=j-i-1;continue;}}
 if((mask&RULE_JUMP_CHAIN)&&a->op==OP_JMP){char destination[MAX_NAME];if(chained_target(p,a->arg[0].name,destination)&&strcmp(destination,a->arg[0].name)!=0){strcpy(a->arg[0].name,destination);logit(log,"jump-chain",a->line,"redirected jump through a finite chain of unconditional jumps");(*changes)++;}}
 if((mask&RULE_LOAD_AFTER_STORE)&&a->op==OP_LD&&i>0&&p->items[i-1].op==OP_ST&&reads_mem(a,p->items[i-1].arg[0].name)&&same_reg(a->arg[0],p->items[i-1].arg[1])){logit(log,"redundant-load-after-store",a->line,"removed load because the destination register already holds the stored value");remove_at(p,i--);(*changes)++;continue;}
 if((mask&RULE_LOAD_COPY)&&a->op==OP_LD&&a->arg[1].kind==OPER_MEM&&i>0&&p->items[i-1].op==OP_ST&&!strcmp(a->arg[1].name,p->items[i-1].arg[0].name)){int source=p->items[i-1].arg[1].reg;mov_from(a,source);logit(log,"load-to-register-copy",a->line,"replaced load with a copy of the register just stored to the same memory location");(*changes)++;}
 if((mask&RULE_REDUNDANT_STORE)&&a->op==OP_ST){bool removed=false;for(size_t k=i;k>0;){Instruction*q=&p->items[--k];if(q->op==OP_LABEL||isa_is_branch(q->op)||q->op==OP_HALT)break;if(q->op==OP_ST&&!strcmp(q->arg[0].name,a->arg[0].name)&&same_reg(q->arg[1],a->arg[1])){logit(log,"redundant-store",a->line,"removed repeated store of the same unchanged register value");remove_at(p,i);(*changes)++;removed=true;break;}if(writes_mem(q,a->arg[0].name)||instruction_writes_register(q,a->arg[1].reg))break;}if(removed){if(i>0)i--;else i=SIZE_MAX;continue;}}
 if((mask&RULE_DEAD_STORE)&&a->op==OP_ST){bool removed=false;for(size_t k=i+1;k<p->count;k++){Instruction*q=&p->items[k];if(q->op==OP_LABEL||isa_is_branch(q->op)||q->op==OP_HALT)break;if(reads_mem(q,a->arg[0].name))break;if(writes_mem(q,a->arg[0].name)){logit(log,"dead-store",a->line,"removed earlier store overwritten before any read");remove_at(p,i);(*changes)++;removed=true;break;}}if(removed){if(i>0)i--;else i=SIZE_MAX;continue;}}
 if(a->op==OP_ADD||a->op==OP_SUB||a->op==OP_MUL||a->op==OP_DIV||a->op==OP_SHL||a->op==OP_SHR){Operand rhs=a->arg[2];
 if(rhs.kind==OPER_IMM&&a->op==OP_ADD&&rhs.imm==0&&(mask&RULE_ALGEBRA)){mov_from(a,a->arg[1].reg);logit(log,"algebraic-identity",a->line,"replaced addition by zero with a register copy");(*changes)++;continue;}
 if(rhs.kind==OPER_IMM&&a->op==OP_MUL&&rhs.imm==1&&(mask&RULE_ALGEBRA)){mov_from(a,a->arg[1].reg);logit(log,"algebraic-identity",a->line,"replaced multiplication by one with a register copy");(*changes)++;continue;}
 if(rhs.kind==OPER_IMM&&a->op==OP_MUL&&rhs.imm==0&&(mask&RULE_MUL_ZERO)){a->op=OP_LD;a->argc=2;a->arg[1].kind=OPER_IMM;a->arg[1].imm=0;logit(log,"multiply-by-zero",a->line,"replaced multiplication by zero with loading zero");(*changes)++;continue;}
 if(rhs.kind==OPER_IMM&&a->op==OP_MUL&&(mask&RULE_STRENGTH)&&pow2(rhs.imm)){int64_t x; if(known_register(p,i,a->arg[1].reg,&x)&&x>=0&&x<=INT64_MAX/rhs.imm){int shift=0;while((INT64_C(1)<<shift)!=rhs.imm)shift++;a->op=OP_SHL;a->arg[2].imm=shift;logit(log,"strength-reduction",a->line,"replaced multiply with shift after proving the source value is nonnegative and cannot overflow");(*changes)++;continue;}}
 if(rhs.kind==OPER_IMM&&(mask&RULE_FOLD)){int64_t x,y=rhs.imm,z=0;bool ok=known_register(p,i,a->arg[1].reg,&x);if(ok)switch(a->op){case OP_ADD:if(__builtin_add_overflow(x,y,&z))ok=false;break;case OP_SUB:if(__builtin_sub_overflow(x,y,&z))ok=false;break;case OP_MUL:if(__builtin_mul_overflow(x,y,&z))ok=false;break;case OP_DIV:if(y==0||(x==INT64_MIN&&y==-1))ok=false;else z=x/y;break;case OP_SHL:if(y<0||y>=63||x<0||x>(INT64_MAX>>y))ok=false;else z=x<<y;break;case OP_SHR:if(y<0||y>=63||x<0)ok=false;else z=x>>y;break;default:ok=false;}if(ok){a->op=OP_LD;a->argc=2;a->arg[1].kind=OPER_IMM;a->arg[1].imm=z;logit(log,"constant-folding",a->line,"folded operation using a known register constant with arithmetic safety checks");(*changes)++;continue;}}
 }
 }
 if((mask&RULE_CONST_PROP)&&!constant_propagation(p,log,changes))return false;
 return true;}
