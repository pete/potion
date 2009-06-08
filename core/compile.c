//
// compile.c
// ast to bytecode
//
// (c) 2008 why the lucky stiff, the freelance professor
//
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "potion.h"
#include "internal.h"
#include "pn-ast.h"
#include "opcodes.h"
#include "asm.h"

#define PN_ASM1(ins, _a)     f->asmb = (PN)potion_asm_op(P, (PNAsm *)f->asmb, (u8)ins, (int)_a, 0)
#define PN_ASM2(ins, _a, _b) f->asmb = (PN)potion_asm_op(P, (PNAsm *)f->asmb, (u8)ins, (int)_a, (int)_b)

const struct {
  const char *name;
  const u8 args;
} potion_ops[] = {
  {"noop", 0}, {"move", 2}, {"loadk", 2}, {"loadpn", 2}, {"self", 1},
  {"newtuple", 2}, {"settuple", 2}, {"getlocal", 2}, {"setlocal", 2},
  {"getupval", 2}, {"setupval", 2}, {"gettable", 2}, {"settable", 2},
  {"getpath", 2}, {"setpath", 2}, {"add", 2}, {"sub", 2}, {"mult", 2},
  {"div", 2}, {"mod", 2}, {"pow", 2}, {"not", 1}, {"cmp", 2},
  {"eq", 2}, {"neq", 2}, {"lt", 2}, {"lte", 2}, {"gt", 2}, {"gte", 2},
  {"bitl", 2}, {"bitr", 2}, {"bind", 2}, {"jump", 1}, {"test", 2},
  {"testjmp", 2}, {"notjmp", 2}, {"call", 2}, {"tailcall", 2},
  {"return", 1}, {"proto", 2},
};

PN potion_proto_call(Potion *P, PN cl, PN self, PN args) {
  return potion_vm(P, self, args, 0, NULL);
}

PN potion_proto_string(Potion *P, PN cl, PN self) {
  vPN(Proto) t = (struct PNProto *)self;
  int x = 0;
  PN_SIZE num = 1;
  PN_SIZE numcols;
  PN out = potion_byte_str(P, "; function definition");
  pn_printf(P, out, ": %p ; %u bytes\n", t, PN_FLEX_SIZE(t->asmb));
  pn_printf(P, out, "; (");
  PN_TUPLE_EACH(t->sig, i, v, {
    if (PN_IS_NUM(v)) {
      if (i == x)
        x += PN_INT(v);
      else if (v != PN_ZERO)
        pn_printf(P, out, "=%c, ", (int)PN_INT(v));
      else
        pn_printf(P, out, ", ");
    } else
      potion_bytes_obj_string(P, out, v);
  });
  pn_printf(P, out, ") %ld registers\n", PN_INT(t->stack));
  PN_TUPLE_EACH(t->locals, i, v, {
    pn_printf(P, out, ".local \"");
    potion_bytes_obj_string(P, out, v);
    pn_printf(P, out, "\" ; %u\n", i);
  });
  PN_TUPLE_EACH(t->upvals, i, v, {
    pn_printf(P, out, ".upval \"");
    potion_bytes_obj_string(P, out, v);
    pn_printf(P, out, "\" ; %u\n", i);
  });
  PN_TUPLE_EACH(t->values, i, v, {
    pn_printf(P, out, ".value ");
    potion_bytes_obj_string(P, out, v);
    pn_printf(P, out, " ; %u\n", i);
  });
  PN_TUPLE_EACH(t->protos, i, v, {
    potion_bytes_obj_string(P, out, v);
  });
  numcols = (int)ceil(log10(PN_FLEX_SIZE(t->asmb) / sizeof(PN_OP)));
  for (x = 0; x < PN_FLEX_SIZE(t->asmb) / sizeof(PN_OP); x++) {
    const int commentoffset = 20;
    int width = pn_printf(P, out, "[%*u] %-8s %d",
      numcols, num, potion_ops[PN_OP_AT(t->asmb, x).code].name, PN_OP_AT(t->asmb, x).a);

    if (potion_ops[PN_OP_AT(t->asmb, x).code].args > 1)
      width += pn_printf(P, out, " %d", PN_OP_AT(t->asmb, x).b);

    if (width < commentoffset)
      pn_printf(P, out, "%*s", commentoffset - width, "");
    else
      pn_printf(P, out, " ");

    // TODO: Byte code listing: instead of using tabs, pad with spaces to make everything line up
    switch (PN_OP_AT(t->asmb, x).code) {
      case OP_JMP:
        pn_printf(P, out, "; to %d", num + PN_OP_AT(t->asmb, x).a + 1);
        break;
      case OP_NOTJMP:
      case OP_TESTJMP:
        pn_printf(P, out, "; to %d", num + PN_OP_AT(t->asmb, x).b + 1);
        break;
      case OP_LOADPN:
        pn_printf(P, out, "; ");
        potion_bytes_obj_string(P, out, PN_OP_AT(t->asmb, x).b);
        break;
      case OP_LOADK:
        pn_printf(P, out, "; ");
        potion_bytes_obj_string(P, out, PN_TUPLE_AT(t->values, PN_OP_AT(t->asmb, x).b));
        break;
      case OP_SETLOCAL:
      case OP_GETLOCAL:
        pn_printf(P, out, "; ");
        potion_bytes_obj_string(P, out, PN_TUPLE_AT(t->locals, PN_OP_AT(t->asmb, x).b));
        break;
    }
    pn_printf(P, out, "\n");
    num++;
  }
  pn_printf(P, out, "; function end\n");
  return out;
}

#define PN_REG(f, reg) \
  if (reg >= PN_INT(f->stack)) \
    f->stack = PN_NUM(reg + 1)
#define PN_ARG(n, reg) \
  if (PN_PART(t->a[n]) == AST_EXPR && PN_PART(PN_TUPLE_AT(PN_S(t->a[n], 0), 0)) == AST_TABLE) { \
    PN test = PN_S(PN_TUPLE_AT(PN_S(t->a[n], 0), 0), 0); \
    if (!PN_IS_NIL(test)) { \
      PN_TUPLE_EACH(test, i, v, { \
        potion_source_asmb(P, f, loop, 0, (struct PNSource *)v, reg); }); \
    } \
  } else { \
    potion_source_asmb(P, f, loop, 0, (struct PNSource *)t->a[n], reg); \
  }
#define PN_BLOCK(reg, blk, sig) ({ \
  PN block = potion_send(blk, PN_compile, (PN)f, sig); \
  PN_SIZE num = PN_PUT(f->protos, block); \
  PN_ASM2(OP_PROTO, reg, num); \
  PN_TUPLE_EACH(((struct PNProto *)block)->upvals, i, v, { \
    PN_SIZE numup = PN_GET(f->upvals, v); \
    if (numup != PN_NONE) PN_ASM2(OP_GETUPVAL, reg, numup); \
    else                  PN_ASM2(OP_GETLOCAL, reg, PN_GET(f->locals, v)); \
  }); \
})
#define PN_UPVAL(name) ({ \
  PN_SIZE numl = PN_GET(f->locals, name); \
  PN_SIZE numup = PN_NONE; \
  if (numl == PN_NONE) { \
    numup = PN_GET(f->upvals, name); \
    if (numup == PN_NONE) { \
      vPN(Proto) up = f; \
      int depth = 1; \
      while (PN_IS_PROTO(up->source)) { \
        up = (struct PNProto *)up->source; \
        if (PN_NONE != (numup = PN_GET(up->locals, name))) break; \
        depth++; \
      } \
      if (numup != PN_NONE) { \
        up = f; \
        while (depth--) { \
          up->upvals = PN_PUSH(up->upvals, name); \
          up = (struct PNProto *)up->source; \
        } \
      } \
      numup = PN_GET(f->upvals, name); \
    } \
  } \
  numup; \
})

#define MAX_JUMPS 1024
struct PNLoop {
  int bjmps[MAX_JUMPS];
  int cjmps[MAX_JUMPS];

  int bjmpc;
  int cjmpc;
};

void potion_source_asmb(Potion *P, vPN(Proto) f, struct PNLoop *loop, PN_SIZE count,
                        vPN(Source) t, u8 reg) {
  PN_REG(f, reg);

  switch (t->part) {
    case AST_CODE:
    case AST_BLOCK:
    case AST_EXPR:
      PN_TUPLE_EACH(t->a[0], i, v, {
        potion_source_asmb(P, f, loop, i, (struct PNSource *)v, reg);
      });
    break;

    case AST_PROTO:
      PN_BLOCK(reg, t->a[1], t->a[0]);
    break;

    case AST_VALUE: {
      PN_OP op; op.a = t->a[0];
      if (!PN_IS_PTR(t->a[0]) && t->a[0] == (PN)op.a) {
        PN_ASM2(OP_LOADPN, reg, t->a[0]);
      } else {
        PN_SIZE num = PN_PUT(f->values, t->a[0]);
        PN_ASM2(OP_LOADK, reg, num);
      }
    }
    break;

    case AST_ASSIGN: {
      vPN(Source) lhs = (struct PNSource *)t->a[0];
      PN_SIZE num = PN_NONE;
      u8 opcode = OP_SETUPVAL;

      // TODO: handle assignment to function calls
      if (lhs->part == AST_EXPR)
        lhs = (struct PNSource *)PN_TUPLE_AT(lhs->a[0], 0);

      if (lhs->part == AST_MESSAGE || lhs->part == AST_QUERY) {
        num = PN_UPVAL(lhs->a[0]);
        if (num == PN_NONE) {
          num = PN_PUT(f->locals, lhs->a[0]);
          opcode = OP_SETLOCAL;
        }
      } else if (lhs->part == AST_PATH || lhs->part == AST_PATHQ) {
        num = PN_PUT(f->values, lhs->a[0]);
        opcode = OP_SETPATH;
      }

      potion_source_asmb(P, f, loop, 0, (struct PNSource *)t->a[1], reg);

      if (opcode == OP_SETUPVAL) {
        if (lhs->part == AST_QUERY) {
          PN_ASM2(OP_GETUPVAL, reg, num);
          PN_ASM2(OP_TESTJMP, reg, 1);
        }
      } else if (opcode == OP_SETLOCAL) {
        if (lhs->part == AST_QUERY) {
          PN_ASM2(OP_GETLOCAL, reg, num);
          PN_ASM2(OP_TESTJMP, reg, 1);
        }
      } else if (opcode == OP_SETPATH) {
        if (lhs->part == AST_PATHQ) {
          PN_ASM2(OP_GETPATH, reg, num);
          PN_ASM2(OP_TESTJMP, num, 1);
        }
      }
      PN_ASM2(opcode, reg, num);
    }
    break;

    case AST_INC: {
      u8 breg = reg;
      vPN(Source) lhs = (struct PNSource *)t->a[0];
      PN_SIZE num = PN_UPVAL(lhs->a[0]);
      u8 opcode = OP_SETUPVAL;
      if (num == PN_NONE) {
        num = PN_PUT(f->locals, lhs->a[0]);
        opcode = OP_SETLOCAL;
      }

      if (opcode == OP_SETUPVAL)
        PN_ASM2(OP_GETUPVAL, reg, num);
       else if (opcode == OP_SETLOCAL)
        PN_ASM2(OP_GETLOCAL, reg, num);
      if (PN_IS_NUM(t->a[1])) {
        breg++;
        PN_ASM2(OP_MOVE, breg, reg);
      }
      PN_ASM2(OP_LOADPN, breg + 1, (t->a[1] | PN_FNUMBER));
      PN_ASM2(OP_ADD, breg, breg + 1);
      PN_ASM2(opcode, breg, num);
      PN_REG(f, breg + 1);
    }
    break;

    case AST_CMP: case AST_EQ: case AST_NEQ:
    case AST_GT: case AST_GTE: case AST_LT: case AST_LTE:
    case AST_PLUS: case AST_MINUS: case AST_TIMES: case AST_DIV:
    case AST_REM:  case AST_POW:   case AST_BITL:  case AST_BITR: {
      PN_ARG(0, reg);
      PN_ARG(1, reg + 1);
      switch (t->part) {
        case AST_CMP:   PN_ASM2(OP_CMP, reg, reg + 1);  break;
        case AST_EQ:    PN_ASM2(OP_EQ,  reg, reg + 1);  break;
        case AST_NEQ:   PN_ASM2(OP_NEQ, reg, reg + 1);  break;
        case AST_GTE:   PN_ASM2(OP_GTE, reg, reg + 1);  break;
        case AST_GT:    PN_ASM2(OP_GT, reg, reg + 1);   break;
        case AST_LT:    PN_ASM2(OP_LT, reg, reg + 1);   break;
        case AST_LTE:   PN_ASM2(OP_LTE, reg, reg + 1);  break;
        case AST_PLUS:  PN_ASM2(OP_ADD, reg, reg + 1);  break;
        case AST_MINUS: PN_ASM2(OP_SUB, reg, reg + 1);  break;
        case AST_TIMES: PN_ASM2(OP_MULT, reg, reg + 1); break;
        case AST_DIV:   PN_ASM2(OP_DIV, reg, reg + 1);  break;
        case AST_REM:   PN_ASM2(OP_REM, reg, reg + 1);  break;
        case AST_POW:   PN_ASM2(OP_POW, reg, reg + 1);  break;
        case AST_BITL:  PN_ASM2(OP_BITL, reg, reg + 1); break;
        case AST_BITR:  PN_ASM2(OP_BITR, reg, reg + 1); break;
      }
    }
    break;

    case AST_NOT:
      PN_ARG(0, reg);
      PN_ASM2(OP_NOT, reg, reg);
    break;

    case AST_AND: case AST_OR: {
      int jmp;
      PN_ARG(0, reg);
      jmp = PN_OP_LEN(f->asmb);
      if (t->part == AST_AND)
        PN_ASM2(OP_NOTJMP, reg, 0);
      else
        PN_ASM2(OP_TESTJMP, reg, 0);
      PN_ARG(1, reg);
      PN_OP_AT(f->asmb, jmp).b = (PN_OP_LEN(f->asmb) - jmp) - 1;
    }
    break;

#define PN_ARG_TABLE(args, reg1, reg2) \
  if (arg) { \
    PN test = args; \
    if (PN_PART(test) == AST_TABLE) { \
      test = PN_S(args, 0); \
      if (!PN_IS_NIL(test)) { \
        PN_TUPLE_EACH(test, i, v, { \
          potion_source_asmb(P, f, loop, 0, (struct PNSource *)v, reg1); }); \
      } \
    } else { \
      potion_source_asmb(P, f, loop, 0, (struct PNSource *)test, reg2); \
    } \
  } else \
    PN_ASM2(OP_LOADPN, reg1, args)

    // TODO: this stuff is ugly and repetitive
    case AST_MESSAGE:
    case AST_QUERY: {
      u8 breg = reg;
      int arg = (t->a[1] != PN_NIL);
      int call = (t->a[2] != PN_NIL || arg);
      if (t->a[0] == PN_if) {
        int jmp; breg++;
        PN_ARG_TABLE(t->a[1], breg, breg);
        jmp = PN_OP_LEN(f->asmb);
        PN_ASM2(OP_NOTJMP, breg, 0);
        potion_source_asmb(P, f, loop, 0, (struct PNSource *)t->a[2], reg);
        PN_OP_AT(f->asmb, jmp).b = (PN_OP_LEN(f->asmb) - jmp) - 1;
      } else if (t->a[0] == PN_elsif) {
        int jmp1 = PN_OP_LEN(f->asmb), jmp2; breg++;
        PN_ASM2(OP_TESTJMP, breg, 0);
        PN_ARG_TABLE(t->a[1], breg, breg);
        jmp2 = PN_OP_LEN(f->asmb);
        PN_ASM2(OP_NOTJMP, breg, 0);
        potion_source_asmb(P, f, loop, 0, (struct PNSource *)t->a[2], reg);
        PN_OP_AT(f->asmb, jmp1).b = (PN_OP_LEN(f->asmb) - jmp1) - 1;
        PN_OP_AT(f->asmb, jmp2).b = (PN_OP_LEN(f->asmb) - jmp2) - 1;
      } else if (t->a[0] == PN_else) {
        int jmp = PN_OP_LEN(f->asmb); breg++;
        PN_ASM2(OP_TESTJMP, breg, 0);
        potion_source_asmb(P, f, loop, 0, (struct PNSource *)t->a[2], reg);
        PN_OP_AT(f->asmb, jmp).b = (PN_OP_LEN(f->asmb) - jmp) - 1;
      } else if (t->a[0] == PN_loop) {
        int jmp = PN_OP_LEN(f->asmb); breg++;
        PN_ARG_TABLE(t->a[1], breg, breg);
        potion_source_asmb(P, f, loop, 0, (struct PNSource *)t->a[2], reg);
        PN_ASM1(OP_JMP, (jmp - PN_OP_LEN(f->asmb)) - 1);
      } else if (t->a[0] == PN_while) {
        int jmp1, jmp2 = PN_OP_LEN(f->asmb); breg++;
        struct PNLoop l; l.bjmpc = 0; l.cjmpc = 0;
        int i;
        PN_ARG_TABLE(t->a[1], breg, breg);
        jmp1 = PN_OP_LEN(f->asmb);
        PN_ASM2(OP_NOTJMP, breg, 0);
        potion_source_asmb(P, f, &l, 0, (struct PNSource *)t->a[2], reg);
        PN_ASM1(OP_JMP, (jmp2 - PN_OP_LEN(f->asmb)) - 1);
        PN_OP_AT(f->asmb, jmp1).b = (PN_OP_LEN(f->asmb) - jmp1) - 1;
        for (i = 0; i < l.bjmpc; i++) {
          PN_OP_AT(f->asmb, l.bjmps[i]).a = (PN_OP_LEN(f->asmb) - l.bjmps[i]) - 1;
        }
        for (i = 0; i < l.cjmpc; i++) {
          PN_OP_AT(f->asmb, l.cjmps[i]).a = (jmp2 - l.cjmps[i]) - 1;
        }
      } else if (t->a[0] == PN_return) {
        PN_ARG_TABLE(t->a[1], reg, reg);
        PN_ASM1(OP_RETURN, reg);
      } else if (t->a[0] == PN_break) {
        if (loop != NULL) {
          loop->bjmps[loop->bjmpc++] = PN_OP_LEN(f->asmb);
          PN_ASM1(OP_JMP, 0);
        } else {
          // TODO: Report error: 'break' outside of loop.
        }
      } else if (t->a[0] == PN_continue) {
        if (loop != NULL) {
          loop->cjmps[loop->cjmpc++] = PN_OP_LEN(f->asmb);
          PN_ASM1(OP_JMP, 0);
        } else {
          // TODO: Report error: 'continue' outside of loop.
        }
      } else {
        u8 opcode = OP_GETUPVAL;
        PN_SIZE num = PN_UPVAL(t->a[0]);
        if (num == PN_NONE) {
          num = PN_GET(f->locals, t->a[0]);
          opcode = OP_GETLOCAL;
        }

        if (num == PN_NONE) {
          PN_ARG_TABLE(t->a[1], ++breg, breg);
          if (t->a[2] != PN_NIL)
            PN_BLOCK(++breg, t->a[2], PN_NIL);
          num = PN_PUT(f->values, t->a[0]);
          PN_ASM2(OP_LOADK, ++breg, num);
          if (count == 0) {
            PN_ASM1(OP_SELF, reg);
          }
          PN_ASM2(OP_BIND, breg, reg);
          if (t->part == AST_MESSAGE) {
            PN_ASM2(OP_CALL, reg, breg);
          } else
            PN_ASM2(OP_TEST, reg, breg);
        } else {
          if (t->part == AST_QUERY) {
            PN_ASM2(opcode, reg, num);
            PN_ASM2(OP_TEST, reg, reg);
          } else if (call) {
            PN_ARG_TABLE(t->a[1], ++breg, breg);
            if (t->a[2] != PN_NIL)
              PN_BLOCK(++breg, t->a[2], PN_NIL);
            PN_ASM2(opcode, ++breg, num);
            PN_ASM1(OP_SELF, reg);
            PN_ASM2(OP_CALL, reg, breg);
          } else
            PN_ASM2(opcode, reg, num);
        }
      }
      PN_REG(f, breg);
    }
    break;

    case AST_PATH:
    case AST_PATHQ: {
      PN_SIZE num = PN_PUT(f->values, t->a[0]);
      u8 breg = reg;
      PN_ASM2(OP_LOADK, ++breg, num);
      PN_ASM2(OP_GETPATH, breg, reg);
      if (t->part == AST_PATHQ)
        PN_ASM2(OP_TEST, reg, breg);
      else
        PN_ASM2(OP_MOVE, reg, breg);
    }
    break;

    case AST_TABLE:
      PN_ASM1(OP_NEWTUPLE, reg);
      PN_TUPLE_EACH(t->a[0], i, v, {
        if (PN_PART(v) == AST_ASSIGN) {
          vPN(Source) lhs = (struct PNSource *)PN_S(v, 0);
          potion_source_asmb(P, f, loop, i, (struct PNSource *)PN_S(v, 1), reg + 1);
          if (lhs->part == AST_EXPR && PN_TUPLE_LEN(lhs->a[0]) == 1)
          {
            lhs = (struct PNSource *)PN_TUPLE_AT(lhs->a[0], 0);
            if (lhs->part == AST_MESSAGE) {
              PN_SIZE num = PN_PUT(f->values, lhs->a[0]);
              PN_ASM2(OP_LOADK, reg + 2, num);
              lhs = NULL;
            }
          }

          if (lhs != NULL)
            potion_source_asmb(P, f, loop, 0, (struct PNSource *)lhs, reg + 2);

          PN_ASM2(OP_SETTABLE, reg, reg + 2);
          PN_REG(f, reg + 2);
        } else {
          potion_source_asmb(P, f, loop, i, (struct PNSource *)v, reg + 1);
          PN_ASM2(OP_SETTUPLE, reg, reg + 1);
          PN_REG(f, reg + 1);
        }
      });
    break;
  }
}

PN potion_sig_compile(Potion *P, vPN(Proto) f, PN src) {
  PN sig = PN_TUP0();
  vPN(Source) t = (struct PNSource *)src;
  if (t->part == AST_TABLE && t->a[0] != PN_NIL) {
    sig = PN_PUSH(sig, PN_NUM(0));
    PN_TUPLE_EACH(t->a[0], i, v, {
      vPN(Source) expr = (struct PNSource *)v;
      if (expr->part == AST_EXPR) {
        vPN(Source) name = (struct PNSource *)PN_TUPLE_AT(expr->a[0], 0);
        if (name->part == AST_MESSAGE)
        {
          PN_PUT(f->locals, name->a[0]);
          sig = PN_PUSH(sig, name->a[0]);
        }
      } else if (expr->part == AST_ASSIGN) {
        vPN(Source) name = (struct PNSource *)expr->a[0];
        if (name->part == AST_MESSAGE)
        {
          PN_PUT(f->locals, name->a[0]);
          sig = PN_PUSH(sig, name->a[0]);
        }
      }
      sig = PN_PUSH(sig, PN_NUM(0));
    });
    PN_TUPLE_AT(sig, 0) = PN_NUM(PN_TUPLE_LEN(sig) - 1);
  }
  return sig;
}

PN potion_source_compile(Potion *P, PN cl, PN self, PN source, PN sig) {
  vPN(Proto) f;
  vPN(Source) t = (struct PNSource *)self;

  switch (t->part) {
    case AST_CODE:
    case AST_BLOCK: break;
    default: return PN_NIL; // TODO: error
  }

  f = PN_ALLOC(PN_TPROTO, struct PNProto);
  f->source = source;
  f->stack = PN_NUM(1);
  f->protos = PN_TUP0();
  f->locals = PN_TUP0();
  f->upvals = PN_TUP0();
  f->values = PN_TUP0();
  f->sig = (sig == PN_NIL ? PN_TUP0() : potion_sig_compile(P, f, sig));
  f->asmb = (PN)potion_asm_new(P);

  potion_source_asmb(P, f, NULL, 0, t, 0);
  PN_ASM1(OP_RETURN, 0);

  f->localsize = PN_TUPLE_LEN(f->locals);
  f->upvalsize = PN_TUPLE_LEN(f->upvals);
  return (PN)f;
}

#define READ_U8(ptr) ({u8 rpu = *ptr; ptr += sizeof(u8); rpu;})
#define READ_PN(pn, ptr) ({PN rpn = *(PN *)ptr; ptr += pn; rpn;})
#define READ_CONST(pn, ptr) ({ \
    PN val = READ_PN(pn, ptr); \
    if (PN_IS_PTR(val)) { \
      if (val & 2) { \
        size_t len = ((val ^ 2) >> 4) - 1; \
        vPN(Decimal) n = PN_ALLOC_N(PN_TNUMBER, struct PNDecimal, sizeof(PN) * len); \
        n->len = len; n->sign = READ_U8(ptr); \
        PN_MEMCPY_N(n->digits, ptr, PN, len); \
        val = (PN)n; \
        ptr += sizeof(PN) * len; \
      } else { \
        size_t len = (val >> 4) - 1; \
        val = potion_str2(P, (char *)ptr, len); \
        ptr += len; \
      } \
    } \
    val; \
  })

#define READ_TUPLE(ptr) \
  long i = 0, count = READ_U8(ptr); \
  PN tup = potion_tuple_with_size(P, (PN_SIZE)count); \
  for (; i < count; i++)
#define READ_VALUES(pn, ptr) ({ \
    READ_TUPLE(ptr) PN_TUPLE_AT(tup, i) = READ_CONST(pn, ptr); \
    tup; \
  })
#define READ_PROTOS(pn, ptr) ({ \
    READ_TUPLE(ptr) PN_TUPLE_AT(tup, i) = potion_proto_load(P, (PN)f, pn, &(ptr)); \
    tup; \
  })

// TODO: this byte string is volatile, need to avoid using ptr
PN potion_proto_load(Potion *P, PN up, u8 pn, u8 **ptr) {
  PN len = 0;
  PNAsm * volatile asmb = NULL;
  vPN(Proto) f = PN_ALLOC(PN_TPROTO, struct PNProto);
  f->source = READ_CONST(pn, *ptr);
  if (f->source == PN_NIL) f->source = up; 
  f->sig = READ_VALUES(pn, *ptr);
  f->stack = READ_CONST(pn, *ptr);
  f->values = READ_VALUES(pn, *ptr);
  f->locals = READ_VALUES(pn, *ptr);
  f->upvals = READ_VALUES(pn, *ptr);
  f->protos = READ_PROTOS(pn, *ptr);

  len = READ_PN(pn, *ptr);
  PN_FLEX_NEW(asmb, PNAsm, len);
  PN_MEMCPY_N(asmb->ptr, *ptr, u8, len);
  asmb->len = len;

  f->asmb = (PN)asmb;
  f->localsize = PN_TUPLE_LEN(f->locals);
  f->upvalsize = PN_TUPLE_LEN(f->upvals);
  *ptr += len;
  return (PN)f;
}

// TODO: load from a stream
PN potion_source_load(Potion *P, PN cl, PN buf) {
  u8 *ptr;
  vPN(BHeader) h = (struct PNBHeader *)PN_STR_PTR(buf);
  if ((size_t)PN_STR_LEN(buf) <= sizeof(struct PNBHeader) || 
      strncmp((char *)h->sig, POTION_SIG, 4) != 0)
    return PN_NIL;

  ptr = h->proto;
  return potion_proto_load(P, PN_NIL, h->pn, &ptr);
}

#define WRITE_U8(un, ptr) ({*ptr = (u8)un; ptr += sizeof(u8);})
#define WRITE_PN(pn, ptr) ({*(PN *)ptr = pn; ptr += sizeof(PN);})
#define WRITE_CONST(val, ptr) ({ \
    if (PN_IS_STR(val)) { \
      PN count = (PN_STR_LEN(val)+1) << 4; \
      WRITE_PN(count, ptr); \
      PN_MEMCPY_N(ptr, PN_STR_PTR(val), char, PN_STR_LEN(val)); \
      ptr += PN_STR_LEN(val); \
    } else if (PN_IS_DECIMAL(val)) { \
      vPN(Decimal) n = (struct PNDecimal *)val; \
      PN count = ((n->len+1) << 4) | 2; \
      WRITE_PN(count, ptr); \
      WRITE_U8(n->sign, ptr); \
      PN_MEMCPY_N(ptr, n->digits, PN, n->len); \
      ptr += sizeof(PN) * n->len; \
    } else { \
      PN cval = (PN_IS_PTR(val) ? PN_NIL : val); \
      WRITE_PN(cval, ptr); \
    } \
  })
#define WRITE_TUPLE(tup, ptr) \
  long i = 0, count = PN_TUPLE_LEN(tup); \
  WRITE_U8(count, ptr); \
  for (; i < count; i++)
#define WRITE_VALUES(tup, ptr) ({ \
    WRITE_TUPLE(tup, ptr) WRITE_CONST(PN_TUPLE_AT(tup, i), ptr); \
  })
#define WRITE_PROTOS(tup, ptr) ({ \
    WRITE_TUPLE(tup, ptr) ptr += potion_proto_dump(P, PN_TUPLE_AT(tup, i), \
        out, (char *)ptr - PN_STR_PTR(out)); \
  })

long potion_proto_dump(Potion *P, PN proto, PN out, long pos) {
  vPN(Proto) f = (struct PNProto *)proto;
  char *start = PN_STR_PTR(out) + pos;
  u8 *ptr = (u8 *)start;
  WRITE_CONST(f->source, ptr);
  WRITE_VALUES(f->sig, ptr);
  WRITE_CONST(f->stack, ptr);
  WRITE_VALUES(f->values, ptr);
  WRITE_VALUES(f->locals, ptr);
  WRITE_VALUES(f->upvals, ptr);
  WRITE_PROTOS(f->protos, ptr);
  WRITE_PN(PN_FLEX_SIZE(f->asmb), ptr);
  PN_MEMCPY_N(ptr, ((PNFlex *)f->asmb)->ptr, u8, PN_FLEX_SIZE(f->asmb));
  ptr += PN_FLEX_SIZE(f->asmb);
  return (char *)ptr - start;
}

// TODO: dump to a stream
PN potion_source_dump(Potion *P, PN cl, PN proto) {
  PN pnb = potion_bytes(P, 8192);
  struct PNBHeader h;
  PN_MEMCPY_N(h.sig, POTION_SIG, u8, 4);
  h.major = POTION_MAJOR;
  h.minor = POTION_MINOR;
  h.vmid = POTION_VMID;
  h.pn = (u8)sizeof(PN);

  PN_MEMCPY(PN_STR_PTR(pnb), &h, struct PNBHeader);
  PN_STR_LEN(pnb) = (long)sizeof(struct PNBHeader) +
    potion_proto_dump(P, proto, pnb, sizeof(struct PNBHeader));
  return pnb;
}

PN potion_run(Potion *P, PN code) {
#if POTION_JIT == 1
  PN_F func = potion_jit_proto(P, code, POTION_JIT_TARGET);
  return func(P, PN_NIL, P->lobby);
#else
  return potion_vm(P, code, PN_NIL, 0, NULL);
#endif
}

PN potion_eval(Potion *P, const char *str) {
  PN bytes = potion_byte_str(P, str);
  PN code = potion_parse(P, bytes);
  code = potion_send(code, PN_compile, PN_NIL, PN_NIL);
  return potion_run(P, code);
}

void potion_compiler_init(Potion *P) {
  PN pro_vt = PN_VTABLE(PN_TPROTO);
  potion_method(pro_vt, "call", potion_proto_call, 0);
  potion_method(pro_vt, "string", potion_proto_string, 0);
}
