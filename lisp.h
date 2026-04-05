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
#define FL_VERSION  "0.17α1"

#ifndef FLISPLIB
#define FLISPLIB /usr/local/share/flisp
#endif
#ifndef FLISPRC
#define FLISPRC  FLISPRC/init.lsp
#endif

/* For inserting FLISPLIP, FLISPRC */
#define CPP_XSTR(s) CPP_STR(s)
#define CPP_STR(s) #s

#ifndef FLISP_MEMORY_INC_SIZE
#define FLISP_MEMORY_INC_SIZE 8192UL  /* Increase memory by this amount if not enough */
#endif

/* buffersize for Lisp eval input */
#define INPUT_FMT_BUFSIZ 2048
/* buffersize for Lisp result output */
#define WRITE_FMT_BUFSIZ 2048

/* Debugging */
#define DEBUG_GC 0
#define DEBUG_GC_ALWAYS 0
#define FLISP_TRACE 0
#define FLISP_TRACE_READ 0

/* Lisp objects */

typedef struct Object Object;
typedef Object *(*LispEval) (Object *, Object **, Object **, size_t);
typedef Object *(*ExtensionInit) (Object *, Object *);

typedef struct Primitive {
    char *name;
    int nMinArgs, nMaxArgs;
    Object * argsType;
    LispEval eval;
} Primitive;

/** Object - Lisp object data structure
 *
 * Simple: object.size=0, value in union
 * Extended:
 * - objects.size .. number of additional bytes to allocation
 * - objects.count .. number of Object * pointers at start of extension
 * - value(s) in extension union
 */
#define FLISP_SIMPLE_OBJECT_STRUCT \
    Object *type;                  \
    size_t size;                   \
    union {                        \
    int64_t value;                 \
    double number;                 \
    Primitive *primitive;          \
    size_t length;                 \
    Object *forward;               \
    }

typedef struct ObjectHeader {
    FLISP_SIMPLE_OBJECT_STRUCT;
} ObjectHeader;

typedef struct StreamObject {
    Object *path;
    FILE *fd;
    char *buf;
    size_t len;
} StreamObject;

/* Internal */
typedef struct Memory {
    size_t capacity, fromOffset, toOffset;
    void *fromSpace, *toSpace;
} Memory;

typedef struct InterpreterObject {
    Object *input;
    Object *output;
    Object *stderr;
    Object *debug;
    Object *extensions;
    Object *symbols;
    Object *global;
    Object *gcTop;
    Memory *memory;
    int64_t countdown;
    bool trace_read : 1;
    bool trace_primitives : 1;
    bool gc_always : 1;
} InterpreterObject;

typedef struct ExtensionObject {
    Object *name;
    Object *version;
    ExtensionInit init;
} ExtensionObject;

struct Object {
    FLISP_SIMPLE_OBJECT_STRUCT;
    union {
        Object *objects[1];                                               // Vector
        struct { Object *car;    Object *cdr; };                          // Cons
        struct { Object *params; Object *body; Object *env; };            // Closure: Lambda, Macro
        struct { Object *parent; Object *vars; Object *vals; };           // Environment
        char string[PATH_MAX];                                            // String, Symbol
        struct { Object *error; Object *message; Object *culprit; }; // Error
        StreamObject stream;
        ExtensionObject extension;
        InterpreterObject self;
    };
};

typedef struct Scratchpad {
    char *string;
    size_t size;
    size_t capacity;
} Scratchpad;

// PUBLIC INTERFACE ///////////////////////////////////////////////////////
extern Object *flisp_new(size_t size, char **, FILE*, FILE*, FILE*, FILE*);
extern void flisp_destroy(Object *);
extern Object *flisp_eval(Object *, char *);
extern Object *flisp_expr(Object *, Object *);
extern Object *flisp_eval_input(Object *, bool);
extern Object *flisp_write_object(FILE *, Object *, bool);
/* Note: to be documented */
extern Object *flisp_find_symbol(Object *, char*, size_t);

/* Extensions */
#define FLISP_IS_ERR(OBJECT) ((OBJECT)->type == type_error)
#define FLISP_IS_EOF(OBJECT) (FLISP_IS_ERR(OBJECT) && (OBJECT)->error == end_of_file)
extern Object *flisp_register_extension(Object *, char *, ExtensionInit);

extern Object *flisp_register_constant(Object *, Object *, Object *);
extern Object *flisp_register_primitive(Object *, char *, int, int, Object *, LispEval);


// PROGRAMMING INTERFACE ////////////////////////////////////////////////

/* Constants */
/* Fundamentals */
extern Object *nil;
extern Object *t;
/* Types */
extern Object *type_integer;
extern Object *type_double;
extern Object *type_primitive;
/* internal */
extern Object *type_moved;
extern Object *type_interpreter;
extern Object *type_extension;

extern Object *type_vector;
extern Object *type_cons;
extern Object *type_lambda;
extern Object *type_macro;
extern Object *type_env;
extern Object *type_string;
extern Object *type_symbol;
extern Object *type_error;
extern Object *type_stream;
/* Exceptions */
extern Object *end_of_file;
extern Object *range_error;
extern Object *wrong_type_argument;
extern Object *invalid_value;
extern Object *wrong_num_of_arguments;
extern Object *io_error;
extern Object *out_of_memory;
/* I/O */
extern Object *permission_denied;
extern Object *not_found;
extern Object *file_exists;
extern Object *read_only;
extern Object *is_directory;
/* utility */
extern Object *flisp_empty_string;

/* Note: flisp_' ify these names */
extern Object *newObject(Object *, Object *, size_t);
extern Object *newInteger(Object *, int64_t);
extern Object *newDouble(Object *, double);
extern Object *newStringWithLength(Object *, char *, size_t);
extern Object *newString(Object *, char *);
extern Object *newCons(Object *, Object **, Object **);
extern Object *newSymbol(Object *, char *);
extern Object *newError(Object *, Object *, Object *, char *);
extern Object *newError2(Object *, Object *, Object *, char *, char *);
extern Object *newErrorI(Object *, Object *, Object *, char *, int64_t, char *);
extern Object *newErrorFmt(Object *, Object *, Object *, char *, ...);
extern Object *newStreamObject(Object *, FILE *, char *);

extern void resetBuf(Object *);
extern bool addCharToBuf(Object *, int);

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
    interp->self.gcTop = &GC_UNIQUE(gcTrace);                                \
    Object **name = &GC_UNIQUE(gcTrace).car;

/*  do while dispatcher */
extern bool flisp_not_same(Object **, Object *);
extern bool flisp_is_error(Object **, Object *);
#define FLISP_WHILE_OK(F) if (flisp_not_same(&e, F)) break
#define FLISP_UNLESS_ERR(F) if (flisp_is_error(&e, F)) break

void fl_debug(Object *, char *, ...);

#define FLISP_ARG1 (*args)->car
#define FLISP_ARG2 (*args)->cdr->car
#define FLISP_ARG3 (*args)->cdr->cdr->car
#define FLISP_ARG4 (*args)->cdr->cdr->cdr->car
#define FLISP_ARG5 (*args)->cdr->cdr->cdr->cdr->car

#define FLISP_ASSERT(PARAM, TYPE, SIGNATURE)                     \
    if (PARAM->type != TYPE)                                            \
        return newErrorFmt(interp, wrong_type_argument, PARAM,     \
            SIGNATURE " expected %s, got: %s", TYPE->string, PARAM->type->string)


#define FLISP_INTERP interp->self
#define FLISP_STANDARD_INPUT  interp->self.input->stream
#define FLISP_STANDARD_OUTPUT interp->self.output->stream
#define FLISP_STDERR          interp->self.stderr->stream
#define FLISP_DEBUG_OUTPUT    interp->self.debug->stream
#endif
/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
