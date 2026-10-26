#ifndef LISP_H
#define LISP_H
/*
 * fLisp - a tiny yet practical Lisp interpreter.
 *
 * Based on Tiny-Lisp: https://github.com/matp/tiny-lisp, public domain
 *
 * Georg Lehner <jorge@magma-soft.at> 2024, CC0 1.0
 *
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>

#define FL_NAME     "fLisp"
#define FL_VERSION  "0.18α1"

#ifndef FLISP_MEMORY_INC_SIZE
#define FLISP_MEMORY_INC_SIZE 16384UL  /* Increase memory by this amount if not enough */
#endif

/* buffersize for Lisp eval input */
#define INPUT_FMT_BUFSIZ 2048
/* buffersize for Lisp result output */
#define WRITE_FMT_BUFSIZ 2048

/* Lisp objects */

typedef struct Object Object;
typedef Object *(*LispEval) (Object *, Object **, Object **, size_t);
typedef Object *(*ExtensionInit) (Object *, Object *);

typedef struct Primitive {
    char *name;
    int nMinArgs, nMaxArgs;
    Object *argsType;
    LispEval eval;
} Primitive;

/** Object - Lisp object data structure
 *
 * Simple:
 * - object.type
 * - object.size=0,
 * - value stored in union
 * Extended:
 * - object.type
 * - object.size .. number of additional bytes to allocation
 * - object.length .. number of Object * pointers at start of extension
 * - value(s) stored in extension union
 */
typedef struct SimpleObject {
    Object *type;
    size_t size;
    union {
        /* Flisp */
        int64_t value;
        double number;
        Primitive *primitive;
        char *str;       /* pointer to byte array */
        void *ptr;       /* generic pointer */
        /* convenience so we don't need to cast from object */
        size_t length;   /* (byte) length */
        Object *forward; /* garbage collector */
    };
} SimpleObject;

typedef struct TypeExt {
    Object *name;       /* symbol or str object */
    Object *length;     /* length default */
    Object *new;        /* primitive or function to create the object */
    Object *fmt;        /* primitive or function, (format stream object) */
} TypeExt;

typedef struct ConsExt {
    Object *car;
    Object *cdr;
} ConsExt;

typedef struct EnvExt {
    Object *parent;
    Object *vars;
    Object *vals;
} EnvExt;

typedef struct ClosureExt {
    Object *params;
    Object *body;
    Object *env;
} ClosureExt;

typedef struct ErrorExt {
    Object *type;
    Object *message;
    Object *culprit;
} ErrorExt;

typedef struct StreamExt {
    Object *path;
    Object *fd;
    Object *buf;
    Object *len;
} StreamExt;

/* Internal */
typedef struct Memory {
    size_t capacity, fromOffset, toOffset;
    void *fromSpace, *toSpace;
} Memory;

typedef struct InterpreterExt {
    Object *input;
    Object *extensions;
    Object *symbols;
    Object *global;
    /* vector ends here */
    Object *gcTop;
    Memory *memory;
    int64_t countdown;
    bool gc_always : 1;
} InterpreterExt;

typedef struct ExtensionExt {
    Object *name;
    Object *version;
    ExtensionInit init;
} ExtensionExt;

struct Object {
    Object *type;
    size_t size;
    union {
        size_t length;
        Object *forward;
        /* assure compatibilty with simple object */
        int64_t value;
        double number;
        /* convenience so we don't need to cast from simple object */
        Primitive * primitive;
        char *str;
        void *ptr;
    };
    union {
        Object *objects[1];                      // Vector
        struct { Object *car;    Object *cdr; }; // Cons
        char string[sizeof(InterpreterExt)];     // String, Symbol
        Object *values;
        TypeExt t;
        ConsExt cons;
        EnvExt env;
        ClosureExt closure; /* Lambda, Macro */
        ErrorExt error;
        StreamExt stream;
        ExtensionExt extension;
        InterpreterExt self;
    };
};

typedef struct Scratchpad {
    char *string;
    size_t size;
    size_t capacity;
} Scratchpad;

// PUBLIC INTERFACE ///////////////////////////////////////////////////////
extern Object *flisp_interpreter(size_t size, char **, FILE*);
extern void flisp_destroy(Object *);
extern Object *flisp_eval_object(Object *, Object *);
extern Object *flisp_read_expr(Object *);
extern Object *flisp_eval_expr(Object *, Object *);
extern Object *flisp_eval_input(Object *, Object *);
extern Object *flisp_write_object(Object *, Object *, Object *, Object *);
extern Object *flisp_lookup(Object *, Object *);
/* Note: to be documented */
extern char *flisp_str(Object *, Object **);
extern Object *flisp_find_symbol(Object *, char*, size_t);
extern Object *flisp_nreverse(Object *, Object *);
extern char *flisp_symbol_string(Object *);
extern Object *file_fopen(Object *, char *, char*);
extern int file_fclose(Object *, Object *);
extern int64_t flisp_list_length(Object*);

extern SimpleObject nil_obj;
extern Object type_primitive_obj;
extern SimpleObject flisp_init_invalid;

/* Candidates
 * Object *flisp_find_value(interp, symbol) -> NULL if not bound
 * Object *flisp_lookup(interp, symbol) -> error if not bound
 * SimpleObject type_init_invalid: primitive for non initializable types
 */
/* Extensions */
#define FLISP_IS_ERR(OBJECT) ((OBJECT)->type == type_error)
#define FLISP_CHECK_ERR(OBJECT) if FLISP_IS_ERR(OBJECT) return OBJECT

#define FLISP_IS_EOF(OBJECT) (FLISP_IS_ERR(OBJECT) && (OBJECT)->error.type == end_of_file)
/* Note: for speed reasons we could use a single static error object and compare pointers */
#define FLISP_IS_OOM(OBJECT) (FLISP_IS_ERR(OBJECT) && (OBJECT)->error.type == gc_error)

extern Object *flisp_register_extension(Object *, Object *, ExtensionInit);

extern Object *flisp_register_constant(Object *, Object *, Object *);
extern Object *flisp_register_primitive(Object *, char *, int, int, Object *, LispEval);
extern Object *flisp_register_type(Object *, char *, Object *, Object *, Object *, Object *);


// PROGRAMMING INTERFACE ////////////////////////////////////////////////

/* Constants */
/* Fundamentals */
extern Object *nil;
extern Object *t;
/* Types */
extern Object *type_integer;
extern Object *type_primitive;
/* extensible */
extern Object *type_vector;
extern Object *type_cons;
extern Object *type_lambda;
extern Object *type_macro;
extern Object *type_env;
extern Object *type_type;
extern Object *type_string;
extern Object *type_symbol;
extern Object *type_error;
extern Object *type_stream;
/* embedding */
extern Object *type_extension; /* opaque object */
extern Object *type_str; /* C string / ASCII or UTF-8 */
extern Object *type_ptr; /* C pointer */

#define type_any (Object*)&nil_obj

/* Exceptions */
extern Object *end_of_file;
extern Object *range_error;
extern Object *wrong_type_argument;
extern Object *invalid_value;
extern Object *wrong_number_of_arguments;
extern Object *io_error;
extern Object *out_of_memory;
extern Object *gc_error;
/* I/O */
extern Object *permission_denied;
extern Object *not_found;
extern Object *file_exists;
extern Object *read_only;
extern Object *is_directory;
/* utility */
extern Object *flisp_empty_string;
extern Object *flisp_integer_zero;
extern Object *flisp_empty_vector;

extern Object *flisp_new(Object *, Object *, Object **, size_t, size_t);
/* Note: flisp_' ify these names */
extern Object *newObject(Object *, Object *, size_t);
extern Object *newInteger(Object *, int64_t);
extern Object *newStringWithLength(Object *, char *, size_t);
extern Object *newString(Object *, char *);
extern Object *newCons(Object *, Object **, Object **);
extern Object *newSymbol(Object *, char *);
extern Object *newError(Object *, Object *, Object *, char *);
extern Object *newError2(Object *, Object *, Object *, char *, char *);
extern Object *newErrorI(Object *, Object *, Object *, char *, int64_t, char *);
extern Object *newError8(Object *, Object *, Object *, char *, char *, char *, char *, char *, char *, char *, char *);

extern Object *newStreamObject(Object *, FILE *, char *);

extern void initPad(Scratchpad *);
extern bool addCharToPad(Scratchpad *, int);
extern bool addStringToPad(Scratchpad *, char *);
extern SimpleObject fmt_oom_message;
extern Object *flisp_static_error(Object *, SimpleObject *);

extern Object type_symbol_obj, type_type_obj, type_str_obj, type_string_obj;

/* Constants */
#define FLISP_DEFINE_CONSTANT(NAME,STRING)                                    \
    SimpleObject NAME##_obj = { .type = &type_symbol_obj, .size = 0, .str = STRING }; \
    Object *NAME = (Object *)&NAME##_obj

/* Types */
#define FLISP_DEFINE_TYPE(NAME)                                         \
    Object type_##NAME##_obj = {                                        \
        .type   = &type_type_obj,                                       \
        .size   = sizeof(Object*[3]),                                   \
        .length = 3,                                                    \
        .t.name = (Object*)&(SimpleObject){ .type = &type_symbol_obj, .size = 0, .str = "type-" #NAME }, \
        .t.new  = (Object*)&nil_obj,                                    \
        .t.fmt  = (Object*)&nil_obj,                                    \
    };                                                                  \
    Object *type_##NAME = &type_##NAME##_obj

/* Garbage Collector */
#define GC_PASTE1(name, id)  name ## id
#define GC_PASTE2(name, id)  GC_PASTE1(name, id)
#define GC_UNIQUE(name)      GC_PASTE2(name, __LINE__)

#define GC_CHECKPOINT Object *gcTop = interp->self.gcTop
#define GC_RELEASE interp->self.gcTop = gcTop
extern Object *gcReturn(Object *, Object *, Object *);
#define GC_RETURN(expr)  return gcReturn(interp, gcTop, expr)

#define GC_TRACE(name, init)                                            \
    Object GC_UNIQUE(gcTrace) = { .type = type_cons, .car = init, .cdr = interp->self.gcTop }; \
    interp->self.gcTop = &GC_UNIQUE(gcTrace);                           \
    Object **name = &GC_UNIQUE(gcTrace).car;

/*  do while dispatcher */
extern bool flisp_not_same(Object **, Object *);
extern bool flisp_is_error(Object **, Object *);
#define FLISP_WHILE_OK(F) if (flisp_not_same(&e, F)) break
#define FLISP_UNLESS_ERR(F) if (flisp_is_error(&e, F)) break

#define FLISP_ARG1 (*args)->car
#define FLISP_ARG2 (*args)->cdr->car
#define FLISP_ARG3 (*args)->cdr->cdr->car
#define FLISP_ARG4 (*args)->cdr->cdr->cdr->car
#define FLISP_ARG5 (*args)->cdr->cdr->cdr->cdr->car
#define FLISP_ARG6 (*args)->cdr->cdr->cdr->cdr->cdr->car

#define FLISP_ASSERT(PARAM, TYPE, SIGNATURE)                            \
    if (FLISP_IS_OOM(PARAM)) return PARAM;                              \
    if ((PARAM)->type != TYPE)                                          \
        return newError8(interp, wrong_type_argument, PARAM,            \
                         SIGNATURE,                                     \
                         " expected ",                                  \
                         flisp_symbol_string((TYPE)->t.name),        \
                         " got ",                                       \
                         flisp_symbol_string((PARAM)->type->t.name), \
                         "", "", "")

#define FLISP_INTERP interp->self
#define FLISP_STANDARD_INPUT  (FILE*)(interp->self.input->stream.fd)->ptr
#endif

/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
