#ifndef OMORI_JSOPS_H
#define OMORI_JSOPS_H

enum JsOp {
    JS_PROGRAM = 1,
    JS_NUM,
    JS_STR,
    JS_TRUE,
    JS_FALSE,
    JS_NULL,
    JS_UNDEF,
    JS_NAME,
    JS_THIS,
    JS_ARR,
    JS_OBJ,
    JS_FUNC,
    JS_GET,
    JS_INDEX,
    JS_CALL,
    JS_NEW,
    JS_UNARY,
    JS_DELETE,
    JS_INC,
    JS_BIN,
    JS_AND,
    JS_OR,
    JS_NULLISH,
    JS_COND,
    JS_ASSIGN,
    JS_SEQ,
    JS_SPREAD,
    JS_EXPR,
    JS_VAR,
    JS_IF,
    JS_FOR,
    JS_FORIN,
    JS_FOROF,
    JS_WHILE,
    JS_DOWHILE,
    JS_BREAK,
    JS_CONTINUE,
    JS_RETURN,
    JS_BLOCK,
    JS_SWITCH,
    JS_THROW,
    JS_TRY,
    JS_FUNCDECL,
    JS_EMPTY,
    JS_OPTGET,
    JS_OPTCALL,
    JS_REGEX,
    JS_OP_COUNT
};

enum JsBinOp {
    JB_ADD = 1,
    JB_SUB,
    JB_MUL,
    JB_DIV,
    JB_MOD,
    JB_POW,
    JB_EQ,
    JB_NE,
    JB_SEQ,
    JB_SNE,
    JB_LT,
    JB_GT,
    JB_LE,
    JB_GE,
    JB_SHL,
    JB_SHR,
    JB_USHR,
    JB_BAND,
    JB_BOR,
    JB_BXOR,
    JB_IN,
    JB_INSTANCEOF
};

enum JsUnOp { JU_NOT = 1, JU_NEG, JU_PLUS, JU_BNOT, JU_TYPEOF, JU_VOID };

enum JsIncKind { JI_PREINC = 1, JI_PREDEC, JI_POSTINC, JI_POSTDEC };

#endif
