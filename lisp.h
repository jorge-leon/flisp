#ifndef LISP_H
#define LISP_H
/*
 * fLisp - a tiny yet practical Lisp interpreter.
 *
 * Based on Tiny-Lisp: https://github.com/matp/tiny-lisp, public domain
 *
 * Georg Lehner 2024, CC0 1.0
 *
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>

#define FL_NAME     "fLisp"
#define FL_VERSION  "0.16"

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
#define FLISP_TRACE 1

/* Lisp objects */

typedef struct Object Object;
typedef struct Interpreter Interpreter;
typedef Object *(*LispEval) (Interpreter *, Object **, Object **);

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
typedef struct SimpleObject {
    Object *type;
    size_t size; /*0*/
    union {                   // Simple Objects, or helpers
        int64_t integer;      // integer
        double number;        // double
        Primitive *primitive; // primitive
        size_t count;
        Object *forward;      // GC forwarding pointer to collected object in to-space
    };
} SimpleObject;
struct Object {
    Object *type;
    size_t size;
    union {                   // Simple Objects, or helpers
        int64_t value;      // integer
        double number;        // double
        Primitive *primitive; // primitive
        size_t count;
        Object *forward;      // GC forwarding pointer to collected object in to-space
    };
    union {
        struct { Object *car;    Object *cdr; };
        struct { Object *params; Object *body; Object *env; };
        struct { Object *parent; Object *vars; Object *vals; };
        char string[PATH_MAX];
        struct { Object *path; FILE *fd; char *buf; size_t len; };
        struct { Object *error_type; Object *message; Object *culprit; };
        Object *objects[1];
    };
};

/* Internal */
typedef struct Memory {
    size_t capacity, fromOffset, toOffset;
    void *fromSpace, *toSpace;
} Memory;

typedef struct Interpreter {

    /* private */
    Object *result;                  /* result or error object */
    Object message;

    Object input;                    /* default input stream object */
    Object output;                   /* default output stream object */
    Object debug;                    /* default debug output stream object */

    /* globals */
    Object *symbols;                 /* symbols list */
    Object *global;                  /* global environment */
    /* GC */
    Object *gcTop;                   /* dynamic gc trace stack */
    Memory *memory;                  /* memory available for object
                                      * allocation, cleaned up by
                                      * garbage collector */
    /* exeptions */
    jmp_buf exceptionEnv;  /* exception handling */
    jmp_buf *catch;
    /* reader */
    struct { char *buf; size_t len; size_t capacity; };  /* read buffer */
    /* interpreters */
    struct Interpreter *next;    /* linked list of interpreters */
} Interpreter;

/*@null@*/extern Interpreter *flisp_interpreters;

// PROGRAMMING INTERFACE ////////////////////////////////////////////////
/* Constants */
/* Fundamentals */
extern Object *nil;
extern Object *t;
/* Types */
extern Object *type_integer;
extern Object *type_double;
extern Object *type_string;
extern Object *type_symbol;
extern Object *type_cons;
extern Object *type_lambda;
extern Object *type_macro;
extern Object *type_error;
extern Object *type_primitive;
extern Object *type_stream;
/* internal */
extern Object *type_env;
extern Object *type_moved;
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

extern Object *newObject(Interpreter *, Object *, size_t);
extern Object *newInteger(Interpreter *, int64_t);
extern Object *newStringWithLength(Interpreter *, char *, size_t);
extern Object *newString(Interpreter *, char *);
extern Object *newCons(Interpreter *, Object **, Object **);
extern Object *newSymbol(Interpreter *, char *);
extern Object *newError(Interpreter *, Object *, Object *, char *, ...);
extern Object *newStreamObject(Interpreter *, FILE *, char *);

extern int streamGetc(Interpreter *interp, FILE *fd);
extern void resetBuf(Interpreter *);
extern size_t addCharToBuf(Interpreter *, int);

#define GC_PASTE1(name, id)  name ## id
#define GC_PASTE2(name, id)  GC_PASTE1(name, id)
#define GC_UNIQUE(name)      GC_PASTE2(name, __LINE__)

#define GC_CHECKPOINT Object *gcTop = interp->gcTop
#define GC_RELEASE interp->gcTop = gcTop
extern Object *gcReturn(Interpreter *, Object *, Object *);
#define GC_RETURN(expr)  return gcReturn(interp, gcTop, expr)

#define GC_TRACE(name, init)                                            \
    Object GC_UNIQUE(gcTrace) = { type_cons, .car = init, .cdr = interp->gcTop }; \
    interp->gcTop = &GC_UNIQUE(gcTrace);                                \
    Object **name = &GC_UNIQUE(gcTrace).car;

void fl_debug(Interpreter *, char *, ...);


#define FLISP_ARG_ONE (*args)->car
#define FLISP_ARG_TWO (*args)->cdr->car
#define FLISP_ARG_THREE (*args)->cdr->cdr->car

#define FLISP_HAS_ARGS *args != nil
#define FLISP_HAS_ARG_TWO ((*args)->cdr != nil)
#define FLISP_HAS_ARG_THREE ((*args)->cdr->cdr != nil)

#define FLISP_ARG_TYPECHECK(PARAM, TYPE, SIGNATURE)                     \
    if (PARAM->type != TYPE)                                            \
        return newError(interp, wrong_type_argument, PARAM,     \
            SIGNATURE " expected %s, got: %s", TYPE->string, PARAM->type->string)

/* UTF-8 handling */
extern size_t flisp_char_length(char);
extern size_t flisp_char_index(Interpreter *, char *, size_t);
extern size_t flisp_char_count(Interpreter *, char *, size_t);

// PUBLIC INTERFACE ///////////////////////////////////////////////////////
extern Interpreter *flisp_new(size_t size, char **, char*, FILE*, FILE*, FILE*);
extern void flisp_destroy(Interpreter *);
extern void flisp_eval(Interpreter *, char *);
/* Note: experimental */
extern bool flisp_error(Interpreter *);
extern void flisp_expr(Interpreter *, Object *);
extern void flisp_write_object(Interpreter *, FILE *, Object *, bool);
extern void flisp_write_error(Interpreter *, FILE *);
extern void flisp_exception(Interpreter *, Object *);

extern void flisp_register_constant(Interpreter *, Object *, Object *);
extern Primitive *flisp_register_primitive(Interpreter *, char *, int, int, Object *, LispEval);

#define FLISP_RESULT_CODE(INTERPRETER) INTERPRETER->result->error_type
#define FLISP_RESULT_MESSAGE(INTERPRETER) INTERPRETER->result->message->string
#define FLISP_RESULT_OBJECT(INTERPRETER) INTERPRETER->result->culprit

#endif
/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
