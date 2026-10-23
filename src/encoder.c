#include "encoder.h"
#include "parser.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { RECORD_SIZE = 32, HEADER_SIZE = 16 };
static const unsigned char magic[8] = {'P','H','I','S','A',1,'\r','\n'};
static void put64(unsigned char *p, uint64_t v) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (unsigned char)(v >> (i * 8));
}
static bool fail(char *err, size_t cap, const char *msg) {
    if (cap) snprintf(err, cap, "%s", msg);
    return false;
}
static bool write_record(const unsigned char record[RECORD_SIZE], FILE *out) {
    return fwrite(record, 1, RECORD_SIZE, out) == RECORD_SIZE;
}
static bool encode(const Program *p, FILE *out, char *err, size_t cap, bool hex) {
    if (!p || !out) return fail(err, cap, "invalid encoder argument");
    size_t executable = 0;
    for (size_t i = 0; i < p->count; ++i) if (p->items[i].op != OP_LABEL) ++executable;
    if (executable > UINT32_MAX) return fail(err, cap, "too many instructions for binary format");
    for (size_t i=0;i<p->count;i++) {
        const Instruction *in=&p->items[i];
        char why[128]; if(!isa_validate(in,why,sizeof why)) return fail(err,cap,why);
        for(int a=0;a<in->argc;a++) {
            const Operand *o=&in->arg[a];
            if(o->kind==OPER_MEM && strlen(o->name)>8) return fail(err,cap,"memory operand exceeds 8-byte binary name limit");
            if(o->kind==OPER_LABEL) {
                bool found=false;
                for(size_t j=0;j<p->count;j++) if(p->items[j].op==OP_LABEL && strcmp(p->items[j].arg[0].name,o->name)==0){found=true;break;}
                if(!found) return fail(err,cap,"branch target is undefined");
            }
        }
    }
    if (hex) {
        if (fprintf(out, "# PHISA v1: 16-byte header, then 32-byte little-endian records\n") < 0) return fail(err, cap, "error writing hex header");
        unsigned char header[HEADER_SIZE] = {0}; memcpy(header, magic, sizeof magic);
        for (unsigned i=0;i<4;i++) header[8+i]=(unsigned char)((uint32_t)executable>>(8*i));
        for (size_t i=0;i<HEADER_SIZE;i++) if (fprintf(out, "%02x%s", header[i], (i+1)%16 ? " " : "\n") < 0) return fail(err,cap,"error writing hex output");
    } else {
        unsigned char header[HEADER_SIZE] = {0}; memcpy(header, magic, sizeof magic);
        for (unsigned i=0;i<4;i++) header[8+i]=(unsigned char)((uint32_t)executable>>(8*i));
        if (fwrite(header,1,sizeof header,out)!=sizeof header) return fail(err,cap,"error writing binary header");
    }
    uint64_t pc = 0;
    for (size_t i=0;i<p->count;i++) if (p->items[i].op!=OP_LABEL) ++pc;
    (void)pc;
    for (size_t i=0;i<p->count;i++) {
        const Instruction *in=&p->items[i]; if(in->op==OP_LABEL) continue;
        char why[128]; if(!isa_validate(in,why,sizeof why)) return fail(err,cap,why);
        unsigned char rec[RECORD_SIZE]={0}; rec[0]=(unsigned char)in->op; rec[1]=(unsigned char)in->argc;
        for(int a=0;a<in->argc;a++) {
            const Operand *o=&in->arg[a]; unsigned char *slot=&rec[2+(size_t)a*10]; slot[0]=(unsigned char)o->kind; uint64_t val=0;
            if(o->kind==OPER_REG) val=(uint64_t)o->reg;
            else if(o->kind==OPER_IMM) val=(uint64_t)o->imm;
            else if(o->kind==OPER_MEM) {
                size_t n=strlen(o->name); if(n>8) return fail(err,cap,"memory operand exceeds 8-byte binary name limit");
                memcpy(slot+1,o->name,n); continue;
            } else if(o->kind==OPER_LABEL) {
                bool found=false; uint64_t target=0;
                for(size_t j=0;j<p->count;j++) {
                    if(p->items[j].op==OP_LABEL && strcmp(p->items[j].arg[0].name,o->name)==0){found=true;break;}
                    if(p->items[j].op!=OP_LABEL) ++target;
                }
                if(!found) return fail(err,cap,"branch target is undefined");
                val=target;
            }
            put64(slot+1,val);
        }
        if(hex) {
            for(size_t b=0;b<RECORD_SIZE;b++) if(fprintf(out,"%02x%s",rec[b],(b+1)%RECORD_SIZE?" ":"\n")<0) return fail(err,cap,"error writing hex output");
        } else if(!write_record(rec,out)) return fail(err,cap,"error writing binary instruction record");
    }
    return !ferror(out) || fail(err,cap,"error writing encoded output");
}
bool encode_binary(const Program *p, FILE *out, char *err, size_t cap) { return encode(p,out,err,cap,false); }
bool write_hex(const Program *p, FILE *out, char *err, size_t cap) { return encode(p,out,err,cap,true); }
