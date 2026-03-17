/*
 * fLisp - a tiny yet practical Lisp interpreter.
 *
 * Based on Tiny-Lisp: https://github.com/matp/tiny-lisp, public domain
 *
 * Georg Lehner 2024, CC0 1.0
 *
 */

#include <sys/mman.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "lisp.h"

#ifdef FLISP_DOUBLE_EXTENSION
#include "double.h"
#endif

#define EXCEPTION_MEM_RESERVE 4*sizeof(Object)
//Note: debugging //#define EXCEPTION_MEM_RESERVE 8*sizeof(Object)

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS        MAP_ANON
#endif

#define CPP_XSTR(s) CPP_STR(s)
#define CPP_STR(s) #s

#define COUNTFMT long unsigned int

/* Constants */
/* Fundamentals */
Object *nil =                       &(Object) { .size = 4, .length = 0, .string = "nil" };
Object *t =                         &(Object) { .string = "t" };
/* Types */
Object *type_integer =              &(Object) { .string = "type-integer" };
Object *type_double =               &(Object) { .string = "type-double" };
Object *type_primitive =            &(Object) { .string = "type-primitive" };

Object *type_string =               &(Object) { .string = "type-string" };
Object *type_symbol =               &(Object) { .string = "type-symbol" };

Object *type_cons =                 &(Object) { .string = "type-cons" };
Object *type_vector =               &(Object) { .string = "type-vector" };
Object *type_lambda =               &(Object) { .string = "type-lambda" };
Object *type_macro =                &(Object) { .string = "type-macro" };
Object *type_error =                &(Object) { .string = "type-error" };
Object *type_stream =               &(Object) { .string = "type-stream" };
/* Error symbols */
Object *end_of_file =               &(Object) { .string = "end-of-file" };
Object *read_incomplete =           &(Object) { .string = "read-incomplete" };
Object *invalid_read_syntax =       &(Object) { .string = "invalid-read-syntax" };
Object *range_error =               &(Object) { .string = "range-error" };
Object *wrong_type_argument =       &(Object) { .string = "wrong-type-argument" };
Object *invalid_value =             &(Object) { .string = "invalid-value" };
Object *wrong_number_of_arguments = &(Object) { .string = "wrong-number-of-arguments" };
Object *arithmetic_error =          &(Object) { .string = "arithmetic-error"};
Object *out_of_memory =             &(Object) { .string = "out-of-memory" };
Object *gc_error =                  &(Object) { .string = "gc-error" };
/* I/O */
Object *io_error =                  &(Object) { .string = "io-error" };
Object *permission_denied =         &(Object) { .string = "permission-denied" };
Object *not_found =                 &(Object) { .string = "not-found" };
Object *file_exists =               &(Object) { .string = "file-exists" };
Object *read_only =                 &(Object) { .string = "read-only" };
Object *is_directory =              &(Object) { .string = "is-directory" };
/* Interpreter */
Object *debug_output =              &(Object) { .string = "*debug-output*" };
Object *standard_input =            &(Object) { .string = "*standard-input*" };
Object *standard_output =           &(Object) { .string = "*standard-output*" };
/* Internal symbols */
Object *type_env =                  &(Object) { .size = 17, .length = 0, .string = "type-environment" };
Object *type_moved =                &(Object) { .size = 11, .length = 0, .string = "type-moved" };
/* Constant strings */
Object *flisp_empty_string =        &(Object) { .size =  1, .length = 0, .string = "\0" };

bool gc_always = false;

/* List of interpreters */
Interpreter *flisp_interpreters = NULL;


void fl_fatal(char *message, int code)
{
    fputs(message, stderr);
    exit(code);
}

// DEBUG LOG ///////////////////////////////////////////////////////////////////

#ifdef __GNUC__
void fl_debug(Interpreter *, char *format, ...)
    __attribute__ ((format(printf, 2, 3)));
#endif
/** fl_debug() - fLisp debugger
 *
 * @param interp  Interpreter for which to send a debug message
 * @param format ...  printf() style debug string
 *
 * The format string is sent to the interpreters debug file descriptor - if there is one.
 *
 */
void fl_debug(Interpreter *interp, char *format, ...)
{
    if (interp->debug.fd == NULL)
        return;

    va_list(args);
    va_start(args, format);
    if (vfprintf(interp->debug.fd, format, args) < 0) {
        va_end(args);
        (void)fprintf(interp->debug.fd,
                      "fatal: failed to print debug message %s: %s", format, strerror(errno));
    }
    va_end(args);
    (void)fflush(interp->debug.fd);
}


// EXCEPTION HANDLING /////////////////////////////////////////////////////////

void resetBuf(Interpreter *);

void flisp_exception(Interpreter *interp, Object *error)
{
    interp->result = error;
    do {
        longjmp(*interp->catch, 2);
    } while(0);
}


// GARBAGE COLLECTION /////////////////////////////////////////////////////////

/* This implements Cheney's copying garbage collector, with which memory is
 * divided into two equal halves (semispaces): from- and to-space. From-space
 * is where new objects are allocated, whereas to-space is used during garbage
 * collection.
 *
 * When garbage collection is performed, objects that are still in use (live)
 * are copied from from-space to to-space. To-space then becomes the new
 * from-space and vice versa, thereby discarding all objects that have not
 * been copied.
 *
 * Our garbage collector takes as input a list of root objects. Objects that
 * can be reached by recursively traversing this list are considered live and
 * will be moved to to-space. When we move an object, we must also update its
 * pointer within the list to point to the objects new location in memory.
 *
 * However, this implies that our interpreter cannot use raw pointers to
 * objects in any function that might trigger garbage collection (or risk
 * causing a SEGV when accessing an object that has been moved). Instead,
 * objects must be added to the list and then only accessed through the
 * pointer inside the list.
 *
 * Thus, whenever we would have used a raw pointer to an object, we use a
 * pointer to the pointer inside the list instead:
 *
 *   function:              pointer to pointer inside list (Object **)
 *                                  |
 *                                  v
 *   list of root objects:  pointer to object (Object *)
 *                                  |
 *                                  v
 *   semispace:             object in memory
 *
 * Originally GC_ROOTS and GC_PARAM are used to pass the list from
 * function to function.
 *
 *
 * GC_TRACE adds an object to the list and declares a variable which points to
 * the objects pointer inside the list.
 *
 *   GC_TRACE(gcX, X):  add object X to the list and declare Object **gcX
 *                      to point to the pointer to X inside the list.
 */

Object *gcReturn(Interpreter *interp, Object *gcTop, Object *result)
{
    GC_RELEASE;
    return result;
}

/** gcCollectableObject - check if object is on heap
 *
 * @param interp  fLisp interpreter
 * @param object  object to inspect
 *
 * returns: true if object is on heap, false otherwise.
 *
 */
bool gcCollectableObject(Interpreter *interp, Object *object) {
    return (object >= (Object *) interp->memory->fromSpace &&
            object < (Object *) ((char *)interp->memory->fromSpace + interp->memory->fromOffset));
}

/** gcMoveObject - save a single object from garbage collection
 *
 * @param interp  fLisp interpreter
 * @param object  object to save
 *
 * returns: object at new location
 *
 */
typedef struct gcStats { size_t moved, constant, skipped; } gcStats;
Object *gcMoveObject(Interpreter *interp, Object *object, gcStats *stats)
{

    /* Skip object if it is not within from-space, i.e. on the stack or a constant */
    if (!gcCollectableObject(interp, object)) {
        stats->constant++;
        return object;
    }
    // if the object has already been moved, return its new location
    if (object->type == type_moved) {
        stats->skipped++;
        return object->forward;
    }
    stats->moved++;

    // copy object to to-space
    Object *forward = (Object *) ((char *)interp->memory->toSpace + interp->memory->toOffset);
    size_t size = sizeof(SimpleObject) + object->size;
    memcpy(forward, object, size);
    interp->memory->toOffset += size;

#if DEBUG_GC
    if (object->type == type_stream)
        fl_debug(interp, "moved stream %p, path %p/%s %s to %p\n",
                 (void *)object, (void *)object->path, object->path->string,
                 object->path->type->string, (void *)forward
            );
    else if (object->type == type_symbol)
        fl_debug(interp, "moved symbol %s\n", object->string);
    else
        fl_debug(interp, "moved object %p of type %s\n", (void*)object, object->type->string);
#endif
    // mark object as moved and set forwarding pointer
    object->type = type_moved;
    object->forward = forward;

    return object->forward;
}

/** gc - move all active objects to new memory page
 *
 * @param interp   fLisp interpreter
 */
void gc(Interpreter *interp)
{
    Object *object;
    gcStats stats = {0};

    fl_debug(interp, "collecting garbage\n");
    size_t free = (COUNTFMT) interp->memory->capacity - interp->memory->fromOffset;
    fl_debug(interp, "memory: %lu/%lu, free %ld/(%lu)\n",
             (COUNTFMT) interp->memory->fromOffset, (COUNTFMT) interp->memory->capacity,
             free - EXCEPTION_MEM_RESERVE, free
        );
    interp->memory->toOffset = 0;

    // move trace, symbols, root and interp result objects
#if DEBUG_GC
    fl_debug(interp, "gc trace\n");
#endif
    for (object = interp->gcTop; object != nil; object = object->cdr) {
        object->car = gcMoveObject(interp, object->car, &stats);
    }
#if DEBUG_GC
    fl_debug(interp, "symbols\n");
#endif
    interp->symbols = gcMoveObject(interp, interp->symbols, &stats);
#if DEBUG_GC
    fl_debug(interp, "environment\n");
#endif
    interp->global = gcMoveObject(interp, interp->global, &stats);
#if DEBUG_GC
    fl_debug(interp, "result object\n");
#endif
    interp->result = gcMoveObject(interp, interp->result, &stats);
#if DEBUG_GC
    fl_debug(interp, "debug/input/output stream\n");
#endif
    interp->debug.path = gcMoveObject(interp, interp->debug.path, &stats);
    interp->input.path = gcMoveObject(interp, interp->input.path, &stats);
    interp->output.path = gcMoveObject(interp, interp->output.path, &stats);
#if DEBUG_GC
    fl_debug(interp, "root objects: %lu, skipped %lu, constant %lu\n",
             stats.moved, stats.skipped, stats.constant
        );
#endif

    // iterate over objects in to-space and move all objects they reference
    for (object = interp->memory->toSpace;
         object < (Object *) ((char *)interp->memory->toSpace + interp->memory->toOffset);
         object = (Object *) ((char *)object + sizeof(SimpleObject) + object->size)) {

        if (object->size != 0)
            for (size_t i = 0; i < object->length; i++)
                object->objects[i] = gcMoveObject(interp, object->objects[i], &stats);
    }
    // swap from- and to-space
    void *swap = interp->memory->fromSpace;
    interp->memory->fromSpace = interp->memory->toSpace;
    interp->memory->toSpace = swap;

    /* report before overwriting offset difference */
    fl_debug(interp,  "collected %lu objects, skipped %lu, constants %lu, saved %lu bytes\n",
             (COUNTFMT) stats.moved, (COUNTFMT) stats.skipped, (COUNTFMT) stats.constant,
             (COUNTFMT) interp->memory->fromOffset - interp->memory->toOffset);
    free = (COUNTFMT) interp->memory->capacity - interp->memory->toOffset;
    fl_debug(interp, "memory: %lu/%lu, free: %ld/(%lu)\n",
             (COUNTFMT) interp->memory->toOffset, (COUNTFMT) interp->memory->capacity,
             free - EXCEPTION_MEM_RESERVE, free
        );

    interp->memory->fromOffset = interp->memory->toOffset;
}


// MEMORY MANAGEMENT //////////////////////////////////////////////////////////

size_t memoryAlign(size_t size, size_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

/** memoryAllocObject() - Acquire memory for a new Lisp object
 *
 * Lisp object space is divided intox 'from' space and 'to' space.
 * Objects are always allocated in 'from' space. If memory there is
 * exhausted, active objects are garbage collected into 'to' space and
 * 'to' and 'from' spaces are swapped by gc().
 *
 * If gc() does not release sufficient space, 'from' and 'to' space
 * are increased by a multiple of FLISP_MEMORY_INC_SIZE.
 *
 */
Object *memoryAllocObject(Interpreter *interp, Object *type, size_t size)
{
    size = memoryAlign(size, sizeof(void *));
    int blocks = ((size + EXCEPTION_MEM_RESERVE) / FLISP_MEMORY_INC_SIZE) + 1;
    size_t memory = blocks * FLISP_MEMORY_INC_SIZE;

    /* If not done already allocate to space */
    if (!interp->memory->fromSpace) {
        if (memory > interp->memory->capacity)
            interp->memory->capacity = memory;
        fl_debug(interp, "memoryAllocObject: allocate fromSpace: %zu bytes\n", interp->memory->capacity);
        if (!(interp->memory->fromSpace = mmap(NULL, interp->memory->capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)))
            fl_fatal("OOM, allocating from space, exiting\n", 64);
        interp->memory->fromOffset = 0;
        goto allocateObject;
    }
    /* Run garbage collection if capacity exceeded */
    if (
        (interp->memory->fromOffset + size + EXCEPTION_MEM_RESERVE >= interp->memory->capacity)
#if DEBUG_GC_ALWAYS
        || gc_always
#endif
        ) {
        fl_debug(interp, "memoryAllocObject: need %lu bytes more then available, requesting garbage collection\n", (COUNTFMT) size);
        /* If not done already allocate to space */
        if (!interp->memory->toSpace) {
            if (!(interp->memory->toSpace = mmap(NULL, interp->memory->capacity,
                                                 PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                                                 -1, 0)))
                fl_fatal("OOM allocating to space, exiting\n", 65);
        }
        gc(interp);
    }
    /* Check if we now have enough space */
    if (interp->memory->fromOffset + size + EXCEPTION_MEM_RESERVE < interp->memory->capacity)
        goto allocateObject;

    fl_debug(interp, "memoryAllocObject: still %lu bytes more needed, increasing memory by %lu\n",
             (COUNTFMT) size, (COUNTFMT) memory
        );
    /* Increase to space */
    void *new;
    new = mmap(NULL, interp->memory->capacity + memory, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (new == (void *) -1) {
        /* Note: fake that we have more memory and throw exception, then let's hope the best. */
        interp->memory->capacity+= EXCEPTION_MEM_RESERVE;
        flisp_exception(interp, newError(interp, out_of_memory, nil, "OOM reallocating toSpace: %s", strerror(errno)));
    }
    if (munmap(interp->memory->toSpace, interp->memory->capacity) == -1) {
        interp->memory->capacity+= EXCEPTION_MEM_RESERVE;
        flisp_exception(interp, newError(interp, out_of_memory, nil, "munmap(toSpace) failed: %s", strerror(errno)));
    }
    interp->memory->toSpace = new;
    interp->memory->capacity += memory;
    interp->memory->toOffset = 0;
    gc(interp);
    if (munmap(interp->memory->toSpace, interp->memory->capacity - memory) == -1) {
        interp->memory->capacity+= EXCEPTION_MEM_RESERVE;
        flisp_exception(interp, newError(interp, out_of_memory, nil, "munmap(fromSpace) failed: %s", strerror(errno)));
    }
    interp->memory->toSpace = NULL;

allocateObject:
    ;
    /* Allocate object in from-space */
    Object *object = (Object *) ((char *)interp->memory->fromSpace + interp->memory->fromOffset);
    object->type = type;
    object->size = size;
    interp->memory->fromOffset += size;

    return object;
}


// CONSTRUCTING OBJECTS ///////////////////////////////////////////////////////

/** newObject - allocate a new object in the Lisp object store and set
 * it's type.
 *
 * @param interp  fLisp Interpreter
 * @param type    Type object to set in the new Object
 *
 * @returns New Object
 */
Object *newObject(Interpreter *interp, Object *type, size_t size)
{
    Object *object = memoryAllocObject(interp, type, sizeof(SimpleObject)+size);
    object->size = size;
    return object;
}

/// Simple objects
/** newInteger - allocate a new Integer object in the Lisp object store and set it's value
 *
 * @param interp  fLisp Interpreter
 * @param number  Integer value of the object.
 *
 * @returns New Object
 */
Object *newInteger(Interpreter *interp, int64_t value)
{
    Object *object = newObject(interp, type_integer, 0);
    object->value = value;
    return object;
}
Object *newDouble(Interpreter *interp, double number)
{
    Object *object = newObject(interp, type_double, 0);
    object->number = number;
    return object;
}
Object *newPrimitive(Interpreter *interp, Primitive* primitive)
{
    Object *object = newObject(interp, type_primitive, 0);
    object->primitive = primitive;
    return object;
}

/// Extended objects

/** flisp_ext_obj(interp, type, list, length, extra) - create and initialize an extended object.
 *
 * @param interp   .. Interpreter in which to create the object.
 * @param obj_list .. List of initializer objects.
 * @param length    .. Number of objects in the list to use for initialization.
 * @param extra    .. Number of additional space in bytes to allocate for extension data.
 *
 * If there are more then length objects in the list, only the first length ones will be used.
 * If there are less then length objects in the list, nil is used to initialize the respective slot.
 *
 * The extra space is not initialized.
 *
 * @returns Object
 *
 */
Object *flisp_ext_obj(Interpreter *interp, Object *type, Object **list, size_t length, size_t extra)
{
    GC_CHECKPOINT;
    GC_TRACE(gcType, type);
    GC_TRACE(gcObjs, *list);
    Object *object = newObject(interp, *gcType, (sizeof(Object *) * length) + extra);
    GC_RELEASE;
    object->length = length;
    size_t i;
    for(i = 0; i < length && (*gcObjs) != nil; (*gcObjs) = (*gcObjs)->cdr)
        object->objects[i++] = (*gcObjs)->car;
    while(i < length)
        object->objects[i++] = nil;
    return object;
}
/*
 * flisp_ext_obj(interp, type_vector, list, (length list), 0) creates a vector
 */
Object *newCons(Interpreter *interp, Object ** car, Object ** cdr)
{
    GC_CHECKPOINT;
    GC_TRACE(gcCar, *car);
    GC_TRACE(gcCdr, *cdr);
    Object *cons = newObject(interp, type_cons, sizeof(Object *[2]));
    GC_RELEASE;
    cons->length = 2;
    cons->car = *gcCar;
    cons->cdr = *gcCdr;
    return cons;
}
Object *newClosure(Interpreter *interp, Object *type, Object ** args, Object **env)
{
    Object *o;
    char *type_string = (type == type_lambda) ? "lambda" : "macro";

    /* Cover: (closure (a b ..) body) and (closure (a b . ?) body) */
    for (o = (*args)->car; o->type == type_cons;  o = o->cdr) {
        if (o->car->type != type_symbol)
            return newError(interp, o->car, wrong_type_argument,
                                  "(%s params body) - param is not a symbol", type_string);
        if (!gcCollectableObject(interp, o->car))
            return newError(interp, o->car, invalid_value, "(%s params body) - param cannot be used as a parameter");
    }

    /* Cover: (closure a body) and (closure (a b . c) body) */
    if (o != nil && o->type != type_symbol)
        return newError(interp, o, wrong_type_argument, "(%s params body) - param is not a symbol");

    /* Note: check with GC_ALWAYS */
    o = flisp_ext_obj(interp, type, &nil, 3, 0);
    o->objects[0] = (*args)->car;
    o->objects[1] = (*args)->cdr;
    o->objects[2] = *env;
    return o;
}
Object *newEnv(Interpreter *interp, Object ** func, Object ** vals)
{
    Object *environment = newObject(interp, type_env, sizeof(Object*[3]));
    environment->length = 3;
    if ((*func) == nil) {
        environment->parent = environment->vars = environment->vals = nil;
        return environment;
    }
    Object *param = (*func)->params, *val = *vals;
    int nArgs = 0;
    while (
        (param == nil && val == nil)
        && (param != nil && param->type == type_symbol)
        ) {
        if (val != nil && val->type != type_cons)
            return newError(interp, val, wrong_type_argument, "(env f args) - args[%d] is not a list", nArgs);
        if (param == nil && val != nil)
            return newError(interp, *vals, wrong_number_of_arguments, "(env f args) - args, f expects at most %d arguments", nArgs);
        if (param != nil && val == nil) {
            for (; param->type == type_cons; param = param->cdr, ++nArgs);
            return newError(interp, *vals, wrong_number_of_arguments, "(env f args) - args, f expects at least %d arguments", nArgs);
        }
           param = param->cdr;
           val = val->cdr;
           ++nArgs;
    }
    environment->parent = (*func)->env;
    environment->vars = (*func)->params;
    environment->vals = *vals;
    return environment;
}

/** unescapeString() - copy a string, converting escaped symbols
 *
 * @param dst    destination
 * @param src    escaped string to copy
 * @param len    length of the string
 *
 */
size_t unescapeString(char *dst, char *src, size_t len)
{
    size_t r, w;
    for (r = 1, w = 0; r <= len; ++r) {
        if (src[r - 1] == '\\' && r < len) {
            switch (src[r]) {
            case '\\':
                dst[w++] = '\\';
                r++;
                break;
            case '"':
                dst[w++] = '"';
                r++;
                break;
            case 't':
                dst[w++] = '\t';
                r++;
                break;
            case 'r':
                dst[w++] = '\r';
                r++;
                break;
            case 'n':
                dst[w++] = '\n';
                r++;
                break;
            default:
                dst[w++] = '\\';
                break;
            }
        } else
            dst[w++] = src[r - 1];
    }
    dst[w] = '\0';
    return w;
}
Object *newStringWithLength(Interpreter *interp, char *string, size_t length)
{
    if (length == 0)
        return flisp_empty_string;

    /* Note: we allocate the original string, let unescapeString do
     *   the counting and and set the size correctly afterwards. The
     *   next GC cycle will discard the extra bytes.
     */
    Object *object = newObject(interp, type_string, length + 1);
    object->length = 0;
    object->size = unescapeString(object->string, string, length);
    return object;
}
Object *newString(Interpreter *interp, char *string)
{
    return newStringWithLength(interp, string, strlen(string));
}

Object *flisp_find_symbol(Interpreter *interp, char *string, size_t length)
{
    for (Object *symbols = interp->symbols; symbols != nil; symbols = symbols->cdr)
        if (symbols->car->size == length + 1 && strncmp(symbols->car->string, string, length) == 0)
            return symbols->car;
    return NULL;
}
Object *newSymbolWithLength(Interpreter *interp, char *string, size_t length)
{
    Object *symbol = flisp_find_symbol(interp, string, length);
    if (symbol != NULL)  return symbol;

    GC_CHECKPOINT;
    GC_TRACE(gcSymbol, newObject(interp, type_symbol, length + 1));
    (*gcSymbol)->length = 0;
    strncpy((*gcSymbol)->string, string, length);
    (*gcSymbol)->string[length + 1] = '\0';
    interp->symbols = newCons(interp, gcSymbol, &interp->symbols);
    GC_RELEASE;
    return *gcSymbol;
}
Object *newSymbol(Interpreter *interp, char *string)
{
    return newSymbolWithLength(interp, string, strlen(string));
}

#define FLISP_FORMAT_ERROR_MESSAGE "failed to format error message"
Object *newError(Interpreter *interp, Object *error, Object *culprit, char *format, ...)
{
    size_t written;
    size_t len = sizeof(interp->message.string);
    char *message;

    GC_CHECKPOINT;
    GC_TRACE(gcErrorType, error);
    GC_TRACE(gcCulprit, culprit);
    GC_TRACE(gcMessage, flisp_empty_string);
    GC_TRACE(gcError, flisp_ext_obj(interp, type_error, &nil, 3, 0));

    if (format != NULL && format[0] != '\0') {
        message = interp->message.string;
        va_list(args);
        va_start(args, format);
        written = vsnprintf(message, len, format, args);
        va_end(args);
        if (written > len) {
            strcpy(message+len-4, "...");
            written = len;
        } else if (written < 0) {
            message = FLISP_FORMAT_ERROR_MESSAGE;
            len = sizeof(FLISP_FORMAT_ERROR_MESSAGE);
        }
        *gcMessage = newStringWithLength(interp, message, len);
    }
    (*gcError)->error = *gcErrorType;
    (*gcError)->message = *gcMessage;
    (*gcError)->culprit = *gcCulprit;
    GC_RETURN(*gcError);
}
/** newStreamObject - create stream object from file descriptor and path
 *
 * @param fd .. FILE * stream descriptor to register
 * @param name .. NULL or name of the file associated with fd
 * @param buf .. NULL or string to convert into an input file stream.
 */
Object *newStreamObject(Interpreter *interp, FILE *fd, char *path)
{
    GC_CHECKPOINT;
    GC_TRACE(gcPath, newString(interp, path));
    Object *stream = flisp_ext_obj(interp, type_stream, &nil, 1,
                                   sizeof(FILE*) +
                                   sizeof(char*) +
                                   sizeof(size_t));
    GC_RELEASE;
    stream->fd = fd;
    stream->buf = NULL;
    stream->len = 0;
    stream->path = *gcPath;

    return stream;
}

// ENVIRONMENT ////////////////////////////////////////////////////////////////

/* An environment consists of a pointer to its parent environment (if any) and
 * two parallel lists - vars and vals.
 *
 * Case 1 - vars is a regular list:
 *   vars: (a b c), vals: (1 2 3)        ; a = 1, b = 2, c = 3
 *
 * Case 2 - vars is a dotted list:
 *   vars: (a b . c), vals: (1 2)        ; a = 1, b = 2, c = nil
 *   vars: (a b . c), vals: (1 2 3)      ; a = 1, b = 2, c = (3)
 *   vars: (a b . c), vals: (1 2 3 4 5)  ; a = 1, b = 2, c = (3 4 5)
 *
 * Case 3 - vars is a symbol:
 *   vars: a, vals: nil                  ; a = nil
 *   vars: a, vals: (1)                  ; a = (1)
 *   vars: a, vals: (1 2 3)              ; a = (1 2 3)
 *
 * Case 4 - vars and vals are both nil:
 *   vars: nil, vals: nil
 */

Object *envLookup(Interpreter *interp, Object *var, Object *env)
{
    for (; env != nil; env = env->parent) {
        Object *vars = env->vars, *vals = env->vals;

        for (; vars->type == type_cons; vars = vars->cdr, vals = vals->cdr)
            if (vars->car == var)
                return vals->car;

        if (vars == var)
            return vals;
    }

    return newError(interp, invalid_value, var, "has no value");
}
Object *envAdd(Interpreter *interp, Object ** var, Object ** val, Object **env)
{
    GC_CHECKPOINT;
    GC_TRACE(gcEnv, *env);
    GC_TRACE(gcVar, *var);
    GC_TRACE(gcVal, *val);
    GC_TRACE(gcVars, newCons(interp, gcVar, &nil));
    Object *vals = newCons(interp, gcVal, &nil);
    GC_RELEASE;

    (*gcVars)->cdr = (*gcEnv)->vars, (*gcEnv)->vars = *gcVars;
    vals->cdr = (*gcEnv)->vals, (*gcEnv)->vals = vals;

    return *gcVal;
}
/** envSet sets a list of variables to corresponding values
 *
 * @param interp .. fLisp interpreter
 * @param var .. List of variable names
 * @param val .. List of values
 * @param env .. Environment in which to set the variables
 * @param top .. If true, set undefined variables in the top level environement, otherwise in the current.
 *
 * returns: the last assigned value.
 */
Object *envSet(Interpreter *interp, Object ** var, Object ** val, Object **env, bool top)
{
    for (;;) {
        Object *vars = (*env)->vars, *vals = (*env)->vals;

        for (; vars->type == type_cons; vars = vars->cdr, vals = vals->cdr) {
            if (vars->car == *var)
                return vals->car = *val;
            if (vars->cdr == *var)
                return vals->cdr = *val;
        }

        if ((*env)->parent == nil || !top) {
            GC_CHECKPOINT;
            GC_TRACE(gcEnv, *env);
            GC_RETURN(envAdd(interp, var, val, gcEnv));
        } else
            *env = (*env)->parent;
    }
}


// READING S-EXPRESSIONS //////////////////////////////////////////////////////

Object *evalExpr(Interpreter *, Object **, Object **);

int isSymbolChar(int ch)
{
    static const char *valid = "!#$%&*+-./:<=>?@^_|~";
    return isalnum(ch) || strchr(valid, ch);
}

Object *reverseList(Interpreter *interp, Object *list)
{
    Object *object = nil;

    while (list != nil) {
        if (list->type != type_cons)
            return newError(interp, invalid_value, list,  "(nreverse list) - list not a proper list");
        Object *swap = list;
        list = list->cdr;
        swap->cdr = object;
        object = swap;
    }

    return object;
}

/** Interpreter Read Buffer
 *
 * The read buffer is used to incrementally capture numbers, strings
 * or symbols from the input stream.
 *
 * A parse resets the buffer by setting interp->len = 0, than adds
 * characters with addCharToBuf().
 *
 */

/** addCharToBuf - add a character to interpreters read buffer
 *
 * @param: interp  fLisp interpreter
 * @param: c       character to add to buffer
 * @returns: success
 *
 * addCharToBuf() allocates memory in BUFSIZ chunks.
 */
bool addCharToBuf(Interpreter *interp, int c)
{
    if (!interp->capacity || interp->len >= interp->capacity) {
        interp->capacity += BUFSIZ;
        if ((interp->buf = realloc(interp->buf, interp->capacity)) == NULL)
            return false;
    }
    interp->buf[interp->len++] = c;
    return true;
}

/** streamPeek - get the next character from input file descriptor, but stay at the current offset
 * @param interp  fLisp interpreter
 * @param fd      open readable file descriptor
 * @returns next character in stream or EOF
 */
int streamPeek(FILE *fd)
{
    int c = fgetc(fd);
    if (c != EOF)
        c = ungetc(c, fd);
    return c;
}
/** skipToNext - skip comments and spaces in input file
 * @param interp  fLisp interpreter
 * @param fd      open readable file descriptor
 * @returns: next not space, not comment character
 */
int skipToNext(Interpreter *interp, FILE *fd)
{
    for (;;) {
        int ch = fgetc(fd);
        if (ch == EOF)  return ch;
        if (ch == ';')
            while ((ch = fgetc(fd)) != EOF && ch != '\n');
        if (isspace(ch))  continue;
        return ch;
    }
}
/** peekNext - skip to last space or comment character in input file
 * @param interp  fLisp interpreter
 * @param fd      open readable file descriptor
 * @returns next not space, not comment character
 */
int peekNext(Interpreter *interp, FILE *fd)
{
    int c = skipToNext(interp, fd);
    if (c != EOF)
        c = ungetc(c, fd);
    return c;
}
/** readWhile - read characters up to next charater not fullfilling a predicate in input file
 * @param interp     fLisp interpreter
 * @param fd      open readable file descriptor
 * @param predicate  function returning 0 if a character matches *predicate*
 * @returns: next character not fullfilling *predicate* or EOF
 */
int readWhile(Interpreter *interp, FILE *fd, int (*predicate) (int ch))
{
    for (;;) {
        int ch = streamPeek(fd);
        if (ch == EOF)  return ch;
        if (!predicate(ch))  return ch;
        if (!addCharToBuf(interp, fgetc(fd)))  return EOF;
    }
}

/* Object readers */

/** readInteger - add an integer from the read buffer to the  interpreter
 * @param interp fLisp interpreter
 * @returns: Integer object or error
 * @errors: range-error, out-of-memory
 */
Object *readInteger(Interpreter *interp)
{
    if (!addCharToBuf(interp, '\0'))
        return newError(interp, out_of_memory, nil, "OOM while reading integer literal");

    /* Note: strtoimax might actually use a different size then
     *   int64_t. So this approach should be revised.  We want to
     *   provide int64_t on any platform.
     */
    errno = 0;
    int64_t n = strtoimax(interp->buf, NULL, 0);
    if (errno == ERANGE)
        return newError(interp, range_error, nil, "integer out of range,: %"PRId64, n);
    return newInteger(interp, n);
}
/** readDouble - add a float from the read buffer to the interpreter
 * @param interp  fLisp interpreter
 * @returns: double object
 * @errors: range-error, out-of-memory
 */
Object *readDouble(Interpreter *interp)
{
    if (!addCharToBuf(interp, '\0'))
        return newError(interp, out_of_memory, nil, "OOM while reading double literal");
    errno = 0;
    double d = strtod(interp->buf, NULL);
    if (errno == ERANGE)
        return newError(interp, range_error, nil, "double out of range,: %f", d);
    // Note: purposely not dealing with NaN
    return newDouble(interp, d);
}

/** readString - read string object from input file
 * @param interp  fLisp interpreter
 * @param fd      open readable file descriptor
 * @returns: string object
 * @errors: read-incomplete, io_error
 * @trows: out-of-memory
 */
Object *readString(Interpreter *interp, FILE *fd)
{
    bool isEscaped = false;
    int ch;

    interp->len = 0;

    for (;;) {
        ch = fgetc(fd);
        if (ch == EOF) {
            if (ferror(fd))
                return newError(interp, io_error, nil, "I/O error while reading string literal");
            return newError(interp, read_incomplete, nil, "unexpected end of stream in string literal");
        }
        if (ch == '"' && !isEscaped)
            return newStringWithLength(interp, interp->buf, interp->len);

        isEscaped = (ch == '\\' && !isEscaped);
        if (!addCharToBuf(interp, ch))
            return newError(interp, out_of_memory, nil, "OOM while reading string literal");
    }
}
/** readNumberOrSymbol - return integer, double or symbol from input file
 *
 * @param interp  fLisp interpreter
 * @param fd      open readable file descriptor
 *
 * returns: number object, symbol or error.
 *
 * @errors: io-error, read-incomplete, range-error, out-of-memory
 */
Object *readNumberOrSymbol(Interpreter *interp, FILE *fd)
{
    int ch = streamPeek(fd);
    if (ch == EOF)  { if (ferror(fd)) goto io_error; else goto eof_error; }

    interp->len = 0;
    /* skip optional leading sign */
    if (ch == '+' || ch == '-') {
        if (!addCharToBuf(interp, fgetc(fd)))  goto oom_error;
        ch = streamPeek(fd);
        if (ch == EOF && ferror(fd))  goto io_error;
    }
    /* Try to read a number in integer or decimal (float) format.
     * C notation applies: 010 = 8, 0x10 = 16
     */
    if (ch == '.' || isdigit(ch)) {
        if (ch == '0') {
            if (!addCharToBuf(interp, fgetc(fd)))
                return newError(interp, out_of_memory, nil, "OOM reading number or symbol");
            ch = streamPeek(fd);
            if ((ch == EOF) && ferror(fd))  goto io_error;
            if (ch == 'x') {
                if (!addCharToBuf(interp, fgetc(fd)))  goto oom_error;
                ch = streamPeek(fd);
                if (ch == EOF) { if (ferror(fd)) goto io_error; else goto eof_error; }
            }
        }
        if (isdigit(ch)) {
            ch = readWhile(interp, fd, isdigit);
            if (ch == EOF) { if (ferror(fd)) goto io_error; else if (interp-> buf == NULL)  goto oom_error; }
            if (interp->len == 0) goto eof_error;
        }
        if (!isSymbolChar(ch))
            return readInteger(interp);
        if (ch == '.') {
            if (!addCharToBuf(interp, ch))  goto oom_error;
            ch = streamPeek(fd);
            if (ch == EOF && ferror(fd))  goto io_error;
            if (isdigit(ch)) {
                if (!addCharToBuf(interp, ch))  goto oom_error;
                ch = readWhile(interp, fd, isdigit);
                if (ch == EOF) { if (ferror(fd)) goto io_error; else if (interp->buf == NULL)  goto oom_error; }
                if (interp->len == 0) goto eof_error;
                if (!isSymbolChar(ch))
                    return readDouble(interp);
            }
        }
    }
    /* non-numeric character encountered, read a symbol */
    if (EOF == readWhile(interp, fd, isSymbolChar)) { if (ferror(fd))  goto io_error; else goto oom_error; }
    return newSymbolWithLength(interp, interp->buf, interp->len);

oom_error:
    return newError(interp, out_of_memory, nil, "OOM while reading number of symbol");
io_error:
    return newError(interp, io_error, nil, "I/O error while reading number or symbol");
eof_error:
    return newError(interp, read_incomplete, nil, "EOF while reading number or symbol");
}

/* Composed object readers */

Object *readExpr(Interpreter *, FILE *);

/** readList - return list from input file
 *
 * @param interp  fLisp interpreter
 * @param fd      open readable file descriptor
 *
 * @returns: list or error
 *
 * @errors: io-error, read-incomplete, range-error, out-of-memory
 */
Object *readList(Interpreter *interp, FILE *fd)
{
    Object *last = nil;
    Object *list = nil;
    for (;;) {
        interp->len = 0;

        int ch = skipToNext(interp, fd);
        if (ch == EOF) { if (ferror(fd)) goto io_error; else
                return newError(interp, read_incomplete, nil, "unexpected end of stream in list"); }
        if (ch == ')')
            return (list == nil) ? nil : reverseList(interp, list);
        if (ch == '.') {
            ch = streamPeek(fd);
            if (!isSymbolChar(ch)) {
                if (ch == EOF) { if (ferror(fd)) goto dotted_io_error; else
                        return newError(interp, read_incomplete, nil, "unexpected end of stream in dotted list"); }
                if (last == nil)
                    return newError(interp, invalid_read_syntax, nil, "unexpected dot at start of list");
                if ((ch = peekNext(interp, fd)) == ')')
                    return newError(interp, invalid_read_syntax, nil, "expected object at end of dotted list");
                GC_CHECKPOINT;
                GC_TRACE(gcList, list);
                last = readExpr(interp, fd);
                GC_RELEASE;
                if (!last)
                    return newError(interp, read_incomplete, nil, "unexpected end of stream in dotted list");
                if (last->type == type_error)
                    return newError(interp, invalid_value, last, "read error while reading expression in dotted list");
                ch = peekNext(interp, fd);
                if (ch == EOF && ferror(fd))  goto dotted_io_error;
                if (ch != ')')
                    return newError(interp, invalid_read_syntax, nil, "unexpected object at end of dotted list");
                (void)skipToNext(interp, fd);
                list = reverseList(interp, *gcList);
                (*gcList)->cdr = last;
                return list;
            }
        } else {
            if (ungetc(ch, fd) == EOF)
                return newError(interp, io_error, nil, "readList: failed to ungetc, errno: %s", strerror(errno));
            GC_CHECKPOINT;
            GC_TRACE(gcList, list);
            GC_TRACE(gcLast, last);
            *gcLast = readExpr(interp, fd);
            if ((*gcLast)->type == type_error)
                return newError(interp, invalid_value, last, "read error while reading expression in list");
            list = newCons(interp, gcLast, gcList);
            GC_RELEASE;
            last = *gcLast;
        }
    }
io_error:
    return newError(interp, io_error, nil, "I/O error while reading list");
dotted_io_error:
    return newError(interp, io_error, nil, "I/O error while reading dotted list");
}
/** readUnary - return an unary operator together with the next
 *     expression from input file
 *
 * @param interp  fLisp interpreter
 * @param fd      open readable file descriptor
 * @param symbol  symbol to be inserted
 *
 * returns: unary operator expression
 *
 * throws: io-error, read-incomplete, range-error,
 *     out-of-memory
 */
Object *readUnary(Interpreter *interp, FILE *fd, char *symbol)
{
    if (peekNext(interp, fd) == EOF) {
        if (ferror(fd))
            return newError(interp, io_error, nil, "I/O error while reading list");
        else
            return newError(interp, read_incomplete, nil, "unexpected end of stream in readUnary(%s)", symbol);
    }
    GC_CHECKPOINT;
    GC_TRACE(gcSymbol, newSymbol(interp, symbol));
    GC_TRACE(gcObject, readExpr(interp, fd));

    *gcObject = newCons(interp, gcObject, &nil);
    *gcObject = newCons(interp, gcSymbol, gcObject);
    GC_RELEASE;

    return *gcObject;
}

Object *doReaderMacro(Interpreter *interp, FILE *fd)
{
    int ch = streamPeek(fd);
    if (ch == EOF) {
        if (ferror(fd))
            return newError(interp, io_error, nil, "I/O error while reading macro");
        return newError(interp, read_incomplete, nil, "unexpected end of stream while reading macro");
    } else if (ch == '!') {
        while ((ch = fgetc(fd)) != EOF && ch != '\n');
        return NULL;
    }
    else
        if (ch & 0x80)
            return newError(interp, invalid_read_syntax, nil, "unknown read macro: #0x%02X", ch);
        else
            return newError(interp, invalid_read_syntax, nil, "unknown read macro: #%c", ch);
}

/** readExpr - return next lisp sexp object from stream or from interpreter input file
 *
 * @param interp  fLisp interpreter
 * @param fd      open readable file descriptor
 *
 * returns: sexp object or NULL if EOF
 *
 * @errors: io-error, read-incomplete, range-error,
 *     out-of-memory
 */
Object *readExpr(Interpreter *interp, FILE *fd)
{
    Object *object;
    for (;;) {

        int ch = skipToNext(interp, fd);

        if (ch == EOF) { if (ferror(fd)) goto io_error; else return NULL; }
        if (ch == '#') {
            object = doReaderMacro(interp, fd);
            if (object == NULL) continue;
            return object;
        }
        if (ch == '\'' || ch == ':')
            return readUnary(interp, fd, "quote");
        if (ch == '`')
            return readUnary(interp, fd, "quasiquote");
        if (ch == ',') {
            ch = streamPeek(fd);
            if (ch == EOF) { if (ferror(fd)) goto io_error; else return NULL; }
            if (ch == '@') {
                if (!addCharToBuf(interp, fgetc(fd))) { if (ferror(fd)) goto io_error; else
                        return newError(interp, out_of_memory, nil, "OOM while reading expression"); }
                return readUnary(interp, fd, "splice-unquote");
            }
            else
                return readUnary(interp, fd, "unquote");
        }
        if (ch == '"')
            return readString(interp, fd);
        if (ch == '(')
            return readList(interp, fd);
        if (isSymbolChar(ch) && (ch != '.' || isSymbolChar(streamPeek(fd)))) {
            if (ungetc(ch, fd) == EOF)
                return newError(interp, io_error, nil, "I/O error while reading expression, ungetc(), errno: %s", strerror(errno));
            return readNumberOrSymbol(interp, fd);
        }
        else
            if (ch & 0x80)
                return newError(interp, invalid_read_syntax, nil, "unexpected character: 0x%02X", ch);
            else
                return newError(interp, invalid_read_syntax, nil, "unexpected character: '%c'", ch);
    }
io_error:
    return newError(interp, io_error, nil, "I/O error while reading expression");
}

/** (read [stream [eofv]]) - read one object from input stream
 * @param interp  fLisp interpreter.
 * @param stream  input stream to read, if nil, use interp input stream.
 * @param eofv    On EOF: if nil throw exception, else value to return.
 * @returns: Object
 * @errors: invalid-value, io-error, end-of-file
 */
Object *primitiveRead(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    Object *eofv = nil;
    Object *stream = nil;
    FILE *fd = interp->input.fd;

    GC_CHECKPOINT;
    if (nArgs--) {
        if (FLISP_ARG_ONE != nil) {
            FLISP_ARG_TYPECHECK(FLISP_ARG_ONE, type_stream, "(read[ stream[ eofv]]) - stream)");
            stream = FLISP_ARG_ONE;
            fd = FLISP_ARG_ONE->fd;
        }
        if (nArgs)
            eofv = FLISP_ARG_TWO;
    }
    GC_TRACE(gcStream, stream);
    GC_TRACE(gcEofv, eofv);
    Object *result = readExpr(interp, fd);
    GC_RELEASE;

    if (result == NULL) {
        if (*gcEofv == nil)
            return newError(interp, end_of_file, *gcStream, "(read[ stream[ eofv]) - input exhausted");
        else
            result = *gcEofv;
    }
    return result;
}


// EVALUATION /////////////////////////////////////////////////////////////////

// Special forms handled by evalExpr. Must be in the same order as above.
enum {
    PRIMITIVE_QUOTE,
    PRIMITIVE_BIND,
    PRIMITIVE_PROGN,
    PRIMITIVE_COND,
    PRIMITIVE_LAMBDA,
    PRIMITIVE_MACRO,
    PRIMITIVE_MACROEXPAND,
    PRIMITIVE_CATCH
};

/* Scheme-style tail recursive evaluation. evalProgn and evalCond
 * return the object in the tail recursive position to be evaluated by
 * evalExpr. Macros are expanded in-place the first time they are evaluated.
 */

size_t flisp_list_length(Object *list)
{
    int l = 0;
    for (Object *e = list; e != nil; e = e->cdr, l++);
    return l;
}

/** (bind symbol object[ globalb]) - creates or finds symbol and set's its value
 *
 * @param symbol  ..  Symbol to find or create.
 * @param object  ..  Value to bind to symbol.
 * @param globalp ..  If not nil create new objects in the root
 *                    environment, else in the current environment.
 *
 * @returns value
 * @errors: wrong-type-argument
 */
Object *evalBind(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    bool globalp = false;

    FLISP_ARG_TYPECHECK(FLISP_ARG_ONE, type_symbol, "(bind symbol object[ globalp]) - symbol");
    if (!gcCollectableObject(interp, FLISP_ARG_ONE))
        return newError(interp, wrong_type_argument, FLISP_ARG_ONE,
                            "(bind symbol object[ globalp] - symbol: is a constant and cannot be redefined");
    if (nArgs == 3)
        globalp = (FLISP_ARG_THREE != nil);

    GC_CHECKPOINT;
    GC_TRACE(gcEnv, *env);
    GC_TRACE(gcVar, FLISP_ARG_ONE);
    GC_TRACE(gcVal, FLISP_ARG_TWO);
    *gcVal = evalExpr(interp, gcVal, gcEnv);
    envSet(interp, gcVar, gcVal, gcEnv, globalp);
    GC_RETURN(*gcVal);
}

/** (progn[ ..]) => o: return last value of list */
Object *evalProgn(Interpreter *interp, Object **args, Object **env)
{
    if (*args == nil)
        return nil;

    if ((*args)->type != type_cons)
        return newError(interp, wrong_type_argument, *args, "(progn args) args is not a list");

    if ((*args)->cdr == nil)
        return (*args)->car;

    GC_CHECKPOINT;
    GC_TRACE(gcObject, (*args)->car);
    GC_TRACE(gcArgs, (*args)->cdr);
    GC_TRACE(gcEnv, *env);
    evalExpr(interp, gcObject, gcEnv);
    GC_RETURN(evalProgn(interp, gcArgs, gcEnv));
}

/** (cond [clause ..]), clause: (pred [action]) - generic conditional
 *
 * (cond arg):
 * () => (cond)
 * (nil) => nil
 * (pred) => pred
 * (pred action) => nil|* .. nil|(progn action)
 */
Object *evalCond(Interpreter *interp, Object **args, Object **env)
{
    if (*args == nil)
        return nil;

    Object *clause = (*args)->car;
    Object *next_clause = (*args)->cdr;

    if (clause == nil)
        goto next_clause;

    if (clause->type != type_cons)
        return newError(interp, wrong_type_argument, clause, "(cond clause ..) - is not a list: clause");

    Object *action = clause->cdr;
    if (action != nil && action->type != type_cons)
        return newError(interp, wrong_type_argument, clause, "(cond (pred action) ..) action is not a list");

    Object *pred = clause->car;
    if (pred == nil)
        goto next_clause;

    GC_CHECKPOINT;
    GC_TRACE(gcPred, pred);
    GC_TRACE(gcAction, action);
    GC_TRACE(gcEnv, *env);
    GC_TRACE(gcNext, next_clause);
    pred = evalExpr(interp, gcPred, gcEnv);
    GC_RELEASE;
    if (pred == nil) {
        *env = *gcEnv;
        next_clause = *gcNext;
        goto next_clause;
    }
    if (*gcAction == nil)
        return *gcPred;

    if ((*gcAction)->type == type_cons)
        return evalProgn(interp, gcAction, gcEnv);
    return evalExpr(interp, gcAction, gcEnv);

next_clause:
    if (next_clause == nil) return nil;
    return evalCond(interp, &next_clause, env);
}

Object *expandMacro(Interpreter *interp, Object ** macro, Object **args)
{
    GC_CHECKPOINT;
    GC_TRACE(gcBody, (*macro)->body);
    GC_TRACE(gcEnv, newEnv(interp, macro, args));

    GC_TRACE(gcObject, evalProgn(interp, gcBody, gcEnv));
    GC_RETURN(evalExpr(interp, gcObject, gcEnv));
}

Object *expandMacroTo(Interpreter *interp, Object ** macro, Object **args)
{
    Object *object = expandMacro(interp, macro, args);

    if (object->type == type_cons)
        return object;

    GC_CHECKPOINT;
    GC_TRACE(gcBody, object);
    GC_TRACE(gcCons, newCons(interp, gcBody, &nil));
    GC_TRACE(gcProg, newSymbol(interp, "progn"));
    GC_RETURN(newCons(interp, gcProg, gcCons));
}

Object *evalMacroExpand(Interpreter *interp, Object **args, Object **env)
{
    if ((*args)->type != type_cons)
        return evalExpr(interp, args, env);

    GC_CHECKPOINT;
    GC_TRACE(gcArgs, (*args)->cdr);
    GC_TRACE(gcMacro, evalExpr(interp, &(*args)->car, env));
    if ((*gcMacro)->type != type_macro)
        GC_RETURN(*gcMacro);

    GC_RETURN(expandMacro(interp, gcMacro, gcArgs));
}

Object *evalList(Interpreter *interp, Object **args, Object **env)
{
    if ((*args)->type != type_cons)
        return evalExpr(interp, args, env);
    else {
        GC_CHECKPOINT;
        GC_TRACE(gcEnv, *env);
        GC_TRACE(gcCdr, (*args)->cdr);
        GC_TRACE(gcObject, evalExpr(interp, &(*args)->car, gcEnv));
        *gcCdr = evalList(interp, gcCdr, gcEnv);
        GC_RETURN(newCons(interp, gcObject, gcCdr));
    }
}

Object *evalCatch(Interpreter *interp, Object **args, Object **env)
{
    jmp_buf exceptionEnv, *prevEnv;

    prevEnv = interp->catch;
    interp->catch = &exceptionEnv;
    interp->result = nil;
    GC_CHECKPOINT;
    if (setjmp(exceptionEnv)) {
        /* Note: we must protect against invalid values here */
        fl_debug(interp, "catch:%s: '%s'\n", (FLISP_ERROR_TYPE(interp)), FLISP_ERROR_MESSAGE(interp));
    } else {
        do {
            interp->result = evalExpr(interp, &(*args)->car, env);
        } while(0);
    }
    GC_RELEASE;
    interp->catch = prevEnv;
    return interp->result;
}


//Primitive primitives[];

Object *evalExpr(Interpreter *interp, Object ** object, Object **env)
{
    GC_CHECKPOINT;
    GC_TRACE(gcObject, *object);
    GC_TRACE(gcEnv, *env);

    GC_TRACE(gcFunc, nil);
    GC_TRACE(gcArgs, nil);
    GC_TRACE(gcBody, nil);

    for (;;) {
        if ((*gcObject)->type == type_symbol)
            GC_RETURN(envLookup(interp, *gcObject, *gcEnv));
        if ((*gcObject)->type != type_cons)
            GC_RETURN(*gcObject);

        *gcFunc = (*gcObject)->car;
        *gcArgs = (*gcObject)->cdr;

        *gcFunc = evalExpr(interp, gcFunc, gcEnv);
        *gcBody = nil;

        if ((*gcFunc)->type == type_lambda) {
            *gcBody = (*gcFunc)->body;
            *gcArgs = evalList(interp, gcArgs, gcEnv);
            *gcEnv = newEnv(interp, gcFunc, gcArgs);
            if ((*gcEnv)->type == type_error)
                return *gcEnv;
            *gcObject = evalProgn(interp, gcBody, gcEnv);
        } else if ((*gcFunc)->type == type_macro) {
            *gcObject = expandMacroTo(interp, gcFunc, gcArgs);
            if ((*gcObject)->type == type_error)
                return *gcObject;
        } else if ((*gcFunc)->type == type_primitive) {
            Primitive *primitive = (*gcFunc)->primitive;
            size_t nArgs = 0;
            Object *args;

            for (args = *gcArgs; args != nil; args = args->cdr, nArgs++) {
                if (args->type != type_cons)
                    return newError(interp, wrong_type_argument, args,
                                        "(%s args) - args is not a list: arg %d",
                                        primitive->name, nArgs);
                if (args->car->type == type_moved || args->cdr->type == type_moved)
                    return newError(interp, gc_error, args->car,
                                        "(%s args) - arg %d is already disposed off",
                                        primitive->name, nArgs);
            }
            if (nArgs < primitive->nMinArgs)
                return newError(interp, wrong_number_of_arguments, *gcFunc,
                                    "expects at least %d arguments", primitive->nMinArgs);
            if (nArgs > primitive->nMaxArgs && primitive->nMaxArgs >= 0)
                return newError(interp, wrong_number_of_arguments, *gcFunc,
                                    "expects at most %d arguments", primitive->nMaxArgs);
            if (primitive->nMaxArgs < 0 && nArgs % -primitive->nMaxArgs)
                return newError(interp, wrong_number_of_arguments, *gcFunc,
                                    "expects a multiple of %d arguments", -primitive->nMaxArgs);

            switch ((uintptr_t)primitive->eval) {
            case PRIMITIVE_QUOTE:
                GC_RETURN((*gcArgs)->car);
            case PRIMITIVE_BIND:
                GC_RETURN(evalBind(interp, gcArgs, gcEnv, nArgs));
            case PRIMITIVE_PROGN:
                *gcObject = evalProgn(interp, gcArgs, gcEnv);
                break;
            case PRIMITIVE_COND:
                *gcObject = evalCond(interp, gcArgs, gcEnv);
                break;
            case PRIMITIVE_LAMBDA:
                GC_RETURN(newClosure(interp, type_lambda, gcArgs, gcEnv));
            case PRIMITIVE_MACRO:
                GC_RETURN(newClosure(interp, type_macro, gcArgs, gcEnv));
            case PRIMITIVE_MACROEXPAND:
                GC_RETURN(evalMacroExpand(interp, gcArgs, gcEnv));
            case PRIMITIVE_CATCH:
                GC_RETURN(evalCatch(interp, gcArgs, gcEnv));
            default:
                *gcArgs = evalList(interp, gcArgs, gcEnv);
                size_t i = 1;
                if (primitive->argsType != nil)
                    for (args = *gcArgs; args != nil; args = args->cdr, i++)
                        if (args->car->type != primitive->argsType)
                            return newError(interp, wrong_type_argument, args->car, "(%s args) - arg %d expected %s, got: %s",
                                                primitive->name, i,
                                                primitive->argsType->string,
                                                args->car->type->string
                                );
#if FLISP_TRACE
                fl_debug(interp, "trace: (%s", primitive->name);
                for (args = *gcArgs; args != nil; args = args->cdr) {
                    fl_debug(interp, " ");
                    flisp_write_object(interp, interp->debug.fd, args->car, true);
                }
                fl_debug(interp, ")\n");
#endif
                GC_RETURN(primitive->eval(interp, gcArgs, gcEnv, nArgs));
            }
        } else {
            return newError(interp, wrong_type_argument, *gcFunc, "is not a function");
        }
    }
}

Object *primitiveEval(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return evalExpr(interp, &(*args)->car, env);
}


// Write /////////////////////////////////////////////////////////////////////////////////

// Output ////////

/** writeChar - write character to file descriptor
 *
 * @param interp  fLisp interpreter
 * @param fd      open writeable file descriptor or NULL
 * @param ch      character to write
 *
 * throws: io-error
 */
void writeChar(Interpreter *interp, FILE *fd, char ch)
{
    if (fd == NULL) return;

    if(fputc(ch, fd) == EOF)
        flisp_exception(interp, newError(interp, io_error, nil, "failed to write character 0x%02X, errno: %s", ch, strerror(errno)));
}

/** writeString - write string to file descriptor
 *
 * @param interp  fLisp interpreter
 * @param fd      open writeable file descriptor or NULL
 * @param str     string to write
 *
 * throws: io-error
 *
 */
void writeString(Interpreter *interp, FILE *fd, char *str)
{
    if (fd == NULL) return;

    if(fputs(str, fd) == EOF)
        flisp_exception(interp, newError(interp, io_error, nil, "failed to write string %s",strerror(errno)));
}
/** writeFmt - write printf formatted string to file descriptor
 *
 * @param interp  fLisp interpreter
 * @param fd      open writeable file descriptor or NULL
 * @param format ... printf like format string
 *
 * throws: io-error
 */
#ifdef __GNUC__
void writeFmt(Interpreter *, FILE *, char *format, ...)
    __attribute__ ((format(printf, 3, 4)));
#endif
void writeFmt(Interpreter *interp, FILE *fd, char *format, ...)
{
    int len;

    if (fd == NULL) return;

    va_list(args);
    va_start(args, format);
    len = vfprintf(fd, format, args);
    va_end(args);
    if (len < 0)
        flisp_exception(interp, newError(interp, io_error, nil, "writeFmt(): failed to fprintf, %s", strerror(errno)));
}


// WRITING OBJECTS ////////////////////////////////////////////////////////////

/** flisp_write_object - format and write object to file descriptor
 *
 * @param interp  fLisp interpreter
 * @param fd      open writeable file descriptor or NULL
 * @param object  object to be serialized
 * @param readably  if true, write in a format which can be read back
 *
 * throws: gc-error, io-error
 *
 */
void flisp_write_object(Interpreter *interp, FILE *fd, Object *object, bool readably)
{
    if (fd == NULL) return;

    if (object->type == type_integer)
        writeFmt(interp, fd, "%"PRId64, object->value);
    else if (object->type == type_double)
        writeFmt(interp, fd, "%g", object->number);
    else if (object->type == type_primitive)
        writeFmt(interp, fd, "#<Primitive %s>", object->primitive->name);

    else if (object->type == type_vector)
        writeFmt(interp, fd, "#<Vector %lu>", object->length);
    else if (object->type == type_cons) {
        writeChar(interp, fd, '(');
        flisp_write_object(interp, fd, object->car, readably);
        while (object->cdr != nil) {
            object = object->cdr;
            if (object->type == type_cons) {
                writeChar(interp, fd, ' ');
                flisp_write_object(interp, fd, object->car, readably);
            } else {
                writeString(interp, fd, " . ");
                flisp_write_object(interp, fd, object, readably);
                break;
            }
        }
        writeChar(interp, fd, ')');
    } else if (object->type == type_lambda) {
        writeFmt(interp, fd, "#<Lambda ");
        flisp_write_object(interp, fd, object->params, readably);
        writeChar(interp, fd, '>');
    } else if (object->type == type_macro) {
        writeFmt(interp, fd, "#<Macro ");
        flisp_write_object(interp, fd, object->params, readably);
        writeChar(interp, fd, '>');
    } else if (object->type == type_env) {
        /* Note: rather print as name = value pairs */
        writeFmt(interp, fd, "<#Env ");
        Object *symbols = object->vars, *values = object->vals;
        while (symbols != nil) {
            flisp_write_object(interp, fd, symbols->car, readably);
            writeFmt(interp, fd, "  ");
            flisp_write_object(interp, fd, values->car, readably);
            symbols = symbols->cdr;
            values = values->cdr;
            if (symbols != nil)
                writeFmt(interp, fd, ",  ");
        }
        writeChar(interp, fd, '>');
    } else if (object->type == type_string)
        if (!readably) writeString(interp, fd, object->string); else {
            writeChar(interp, fd, '"');
            char *string;
            for (string = object->string; *string; ++string) {
                switch (*string) {
                case '"':
                    writeString(interp, fd, "\\\"");
                    break;
                case '\t':
                    writeString(interp, fd, "\\t");
                    break;
                case '\r':
                    writeString(interp, fd, "\\r");
                    break;
                case '\n':
                    writeString(interp, fd, "\\n");
                    break;
                case '\\':
                    writeString(interp, fd, "\\\\");
                    break;
                default:
                    writeChar(interp, fd, *string);
                    break;
                }
            }
            writeChar(interp, fd, '"');
        }
    else if (object->type == type_symbol)
        writeString(interp, fd, object->string);
    else if (object->type == type_stream)
        writeFmt(interp, fd, "#<Stream %"PRIXPTR" %s>",
                 (uintptr_t) object->fd,
                 object->path->string
            );
    else if (object->type == type_error) {
        writeFmt(interp, fd, "#<Error ");
        flisp_write_object(interp, fd, object->error, readably);
        writeString(interp, fd, ": ");
        flisp_write_object(interp, fd, object->message, readably);
        writeString(interp, fd, ", ");
        flisp_write_object(interp, fd, object->culprit, readably);
        writeChar(interp, fd, '>');
    } else if (object->type == type_moved) {
        fl_debug(interp, " => ");
        flisp_write_object(interp, fd, object->forward, readably);
    } else
        fl_fatal("lisp-write_error(): unidentifiable object", 66);

    fflush(fd);
}

/** (write o[ p[ fd]]) - write object
 *
 * @param o   Object to write.
 * @param p   If not nil escape strings.
 * @param fd  Stream to write to, else output stream.
 *
 * @returns: o
 *
 * throws: wrong-num-of-arguments, io-error, gc-error
 *
 * If no stream is specified the interpreters output file descriptor is used.
 * If the interpreters output file descriptor is NULL, no output is written.
 */
Object *primitiveWrite(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    bool readably = false;
    FILE *fd = interp->output.fd;

    if (nArgs > 1) {
        readably = (FLISP_ARG_TWO != nil);
    }
    if (nArgs > 2) {
        FLISP_ARG_TYPECHECK(FLISP_ARG_THREE, type_stream, "(write o [p [fd]]) - fd");
        if (FLISP_ARG_THREE->fd == NULL)
            return newError(interp, invalid_value, nil, "(write o[ p [fd]) - fd already closed");
        fd = FLISP_ARG_THREE->fd;
    }
    flisp_write_object(interp, fd, FLISP_ARG_ONE, readably);
    return FLISP_ARG_ONE;
}


// PRIMITIVES /////////////////////////////////////////////////////////////////

Object *primitiveNullP(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG_ONE == nil) ? t : nil;
}

Object *primitiveTypeOf(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG_ONE->type);
}
Object *primitiveConsP(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG_ONE->type == type_cons) ? t : nil;
}

Object *primitiveIntern(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return newSymbol(interp, FLISP_ARG_ONE->string);
}

Object *primitiveSymbolName(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    GC_CHECKPOINT;
    GC_TRACE(gcFirst, FLISP_ARG_ONE);
    GC_RETURN(newString(interp, (*gcFirst)->string));
}

/** (same o1 o2) - object comparison
 *
 * @param o1  object
 * @param o2  object
 *
 * @returns t if o1 is the same object as o2, nil otherwise.
 */
Object *primitiveSame(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG_ONE == FLISP_ARG_TWO) ? t : nil;
}

Object *primitiveCar(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    if (FLISP_ARG_ONE == nil)
        return nil;
    FLISP_ARG_TYPECHECK(FLISP_ARG_ONE, type_cons, "(car o) - o");
    return FLISP_ARG_ONE->car;
}
Object *primitiveCdr(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    if (FLISP_ARG_ONE == nil)
        return nil;
    FLISP_ARG_TYPECHECK(FLISP_ARG_ONE, type_cons, "(cdr o) - o");
    return FLISP_ARG_ONE->cdr;
}
Object *primitiveObjectSize(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG_ONE->size);
}
Object *primitiveObjectLength(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG_ONE->length);
}
/** (vector ..]) => v */
Object *primitiveVector(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return flisp_ext_obj(interp, type_vector, args, nArgs, 0);
}
/** (object-list object[ start[ end]]) => list of contained object */
Object *primitiveVectorRange(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    int64_t i = 0, end = FLISP_ARG_ONE->length;
    if (nArgs > 1) {
        FLISP_ARG_TYPECHECK(FLISP_ARG_TWO, type_integer, "(object-list object[ start[ end]]) - start");
        i = (FLISP_ARG_TWO->value < 0) ? end + FLISP_ARG_TWO->value : FLISP_ARG_TWO->value;
        if (i < 0 || i >= end)
            return newError(interp, range_error, FLISP_ARG_TWO,
                                  "(object-list object[ start[ end]]) - start out of range [0, %lu]: %ld", end-1, FLISP_ARG_TWO->value);
    }
    if (nArgs > 2) {
        int64_t j = (FLISP_ARG_THREE->value < 0) ? end + FLISP_ARG_THREE->value : FLISP_ARG_THREE->value;
        if (j < 0 || j >= end)
            return newError(interp, range_error, FLISP_ARG_THREE,
                            "(object-list object[ start[ end]]) - end out of range [0, %lu]: %ld", end-1, FLISP_ARG_THREE->value);
        end = j;
    }
    if (i == end)
        return nil;
    if (i > end)
        return newError(interp, range_error, FLISP_ARG_TWO, "(object-list object[ start[ end]]) - start > end: [%lu, %lu]", i, end-1);

    GC_CHECKPOINT;
    GC_TRACE(gcObject, FLISP_ARG_ONE);
    GC_TRACE(gcList, nil);
    while (i < end)
        *gcList = newCons(interp, &(*gcObject)->objects[i++], gcList);
    GC_RETURN(reverseList(interp, *gcList));
}
Object *primitiveCons(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return flisp_ext_obj(interp, type_cons, args, 2, 0);
}
Object *primitiveNreverse(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return reverseList(interp, (*args)->car);
}
Object *primitiveError(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ARG_TYPECHECK(FLISP_ARG_ONE, type_symbol, "(error type message[ object]) - result");
    FLISP_ARG_TYPECHECK(FLISP_ARG_TWO, type_string, "(error type message[ object]) - message");

    return flisp_ext_obj(interp, type_error, args, 3, 0);
}

Object *primitiveThrow(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    flisp_exception(interp, (*args)->car);
    return NULL;
}

// Integer Math //////

Object *integerAdd(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG_ONE->value + FLISP_ARG_TWO->value);
}
Object *integerSubtract(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG_ONE->value - FLISP_ARG_TWO->value);
}

Object *integerMultiply(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG_ONE->value * FLISP_ARG_TWO->value);
}

Object *integerDivide(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    if (FLISP_ARG_TWO->value == 0)
        return newError(interp, arithmetic_error, FLISP_ARG_TWO, "(i/ q d) - d: division by zero");

    return newInteger(interp, FLISP_ARG_ONE->value / FLISP_ARG_TWO->value);
}

Object *integerMod(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    if (FLISP_ARG_TWO->value == 0)
        return newError(interp, arithmetic_error, FLISP_ARG_TWO, "(i%% q d) - d: division by zero");

    return newInteger(interp, FLISP_ARG_ONE->value % FLISP_ARG_TWO->value);
}

/* Note: only (zerop not <) are needed:
 *
 * a = b .. (zerop (- a b))
 * a > b .. b < a
 * a <= b .. (not (b > a))
 * ...
 */
Object *integerEqual(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG_ONE->value == FLISP_ARG_TWO->value) ? t : nil;
}

Object *integerLess(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG_ONE->value < FLISP_ARG_TWO->value) ? t : nil;
}

Object *integerLessEqual(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG_ONE->value <= FLISP_ARG_TWO->value) ? t : nil;
}

Object *integerGreater(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG_ONE->value > FLISP_ARG_TWO->value) ? t : nil;
}

Object *integerGreaterEqual(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG_ONE->value >= FLISP_ARG_TWO->value) ? t : nil;
}

// Integer bit operations //////
/* Note: only Xor and Not are needed, see De morgan */
Object *integerAnd(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG_ONE->value & FLISP_ARG_TWO->value);
}
Object *integerOr(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG_ONE->value | FLISP_ARG_TWO->value);
}
Object *integerXor(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG_ONE->value ^ FLISP_ARG_TWO->value);
}
Object *integerShiftLeft(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG_ONE->value << FLISP_ARG_TWO->value);
}
Object *integerShiftRight(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG_ONE->value >> FLISP_ARG_TWO->value);
}
Object *integerNot(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, ~FLISP_ARG_ONE->value);
}


// STREAMS //////////////////////////////////////////////////

/* Minimal stream C-API for interpreter operation */

/** file_outputMemStream - create a memory based output stream
 *
 * @param interp  fLisp Interpreter
 *
 * @returns: lisp stream object or
 * @error: out-of-memory
 *
 */
Object *file_outputMemStream(Interpreter *interp)
{
    Object *stream = newStreamObject(interp, NULL, ">STRING");
    if (NULL == (stream->fd = open_memstream(&stream->buf, &stream->len)))
        return newError(interp, out_of_memory, nil, "failed to open_memstream() for memory output stream: %s", strerror(errno));
    fflush(stream->fd); // Note: sets stream->buf and stream->len to initial values.
    return stream;
}
/** file_inputMemStream - convert string to Lisp stream object
 *
 * @param interp  fLisp interpreter
 * @param string  string to read
 *
 * @returns: Lisp stream object or nil on failure
 * @errors:
 * - out-of-memory
 */
Object *file_inputMemStream(Interpreter *interp, char *string)
{
    size_t len = strlen(string);
    char *buf = malloc(len+1);
    if (NULL == buf)
        return newError(interp, out_of_memory, nil, "failed to allocate string buffer for memory input stream: %s", strerror(errno));
    strncpy(buf, string, len);
    buf[len] = '\0';
    Object *stream = newStreamObject(interp, NULL, "<STRING");
    stream->buf = buf;
    stream->len = len;
    if (NULL == (stream->fd = fmemopen(stream->buf, stream->len, "r"))) {
        free(stream->buf);
        return newError(interp, out_of_memory, nil, "failed to fmemopen string for memory input stream: %s", strerror(errno));
    }
    return stream;
}
/** file_fopen() - returns a stream object for the interpreter
 *
 * @param interp  fLisp interpreter
 * @param path    path to a file to open, or string for memory input
 *   buffer, or "<num" / ">num" for file descriptor input / output.
 * @param mode    see fopen(3p). One of "r", "w", "a", "r+", "w+",
 *   "a+" plus optional "b" modifier, or "<" / ">" for memory input /
 *   output buffer.
 *
 * @returns: lisp stream object
 * @errors: different io errors, invalid-value, out-of-memory
 *
 * Additionally a file associated with a string buffer can be created:
 *
 * If mode is "<", *path* is converted into a memory based stream
 * opened with mode "r". The file name of the stream is set to "<STRING".
 *
 * If mode is ">", a dynamic memory based stream is opened with mode
 * "w". The file name of the stream is set to ">STRING".
 *
 * If path is "<num" or ">num" the standard file descriptor with
 * number *num* is opened in "r" or "a" mode respectively and mode is
 * ignored.
 *
 */
Object *file_fopen(Interpreter *interp, char *path, char* mode) {
    FILE * fd;
    Object *stream, *err = nil;

    if (strcmp("<", mode) == 0) {
        if (nil == (stream = file_inputMemStream(interp, path)))
            return newError(interp, io_error, nil, "failed to open string as memory input stream: %s", strerror(errno));
        return stream;
    }
    if (strcmp(">", mode) == 0) {
        if (nil == (stream = file_outputMemStream(interp)))
            return newError(interp, io_error, nil, "failed to open memory output stream: %s", strerror(errno));
        return stream;
    }
    char c = path[0];
    if (c == '<' || c == '>') {
        char *end;
        errno = 0;
        long d = strtol(&path[1], &end, 0);
        if (errno || *end != '\0' || d < 0 || d > _POSIX_OPEN_MAX)
            return newError(interp, invalid_value, nil, "invalid I/O stream number: %s", &path[1]);
        if (NULL == (fd = fdopen((int)d, c == '<' ? "r" : "a")))
            return newError(interp, io_error, nil, "failed to open I/O stream %ld for %s", d, c == '<' ? "reading" : "writing");
    } else {
        if (NULL == (fd = fopen(path, mode))) {
            fl_debug(interp, "fopen() failed:%d: %s\n", errno, strerror(errno));
            switch(errno) {
            case EACCES:  err = permission_denied; break;
            case EEXIST:  err = file_exists; break;
            case ENOENT:  err = not_found; break;
            case EISDIR:  err = is_directory; break;
            default:      err = io_error; break;
            }
            return newError(interp, err, nil, "failed to open file '%s' with mode '%s': %s", path, mode, strerror(errno));
        }
    }
    return newStreamObject(interp, fd, path);
}
/** (open path[ mode]) - return open stream object
 *
 * @param path    path to a file to open, string for memory input
 *     or "<num" / ">num" for file descriptor input / output.
 * @param mode    see fopen(3p). Additionally "<" / ">" for memory
 *     input / output.
 *
 * @returns: stream object
 * @errors: different io errors, invalid-value, out-of-memory
 */
 Object *primitiveFopen(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    char *mode = "r";

    if (--nArgs)
        mode = FLISP_ARG_TWO->string;
    return file_fopen(interp, FLISP_ARG_ONE->string, mode);
}

/** file_fclose() - closes stream object
 *
 * @param interp  fLisp interpreter
 * @param stream  stream to close
 *
 * returns: 0 on success, else errno of fclose()
 */
int file_fclose(Interpreter *interp, Object *stream)
{
    fflush(stream->fd);
    int result = fclose(stream->fd) ? errno : 0;
    stream->fd = NULL;
    if (stream->buf != NULL) {
        free(stream->buf);
        stream->buf = NULL;
        stream->len = 0;
    }
    return result;
}
/** (close stream) - closes stream object
 *
 * @param interp  fLisp interpreter
 * @param stream  stream to close
 *
 * @returns nil on success
 * @errors: invalid-value, io-error
 */
Object *primitiveFclose(Interpreter *interp, Object**args, Object **env, size_t nArgs)
{
    int result;

    if (FLISP_ARG_ONE->fd == NULL)
        return newError(interp, invalid_value, FLISP_ARG_ONE, "(fclose stream) - stream already closed");
    if ((result = file_fclose(interp, FLISP_ARG_ONE)))
        return newError(interp, io_error, FLISP_ARG_ONE, "(fclose stream) - failed to close: %s", strerror(result));
    return nil;
}

 Object *primitiveFinfo(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    GC_CHECKPOINT;
    GC_TRACE(gcObject, (FLISP_ARG_ONE->fd == NULL) ?
             nil : newInteger(interp, (int64_t)fileno(FLISP_ARG_ONE->fd)));
    *gcObject = newCons(interp, gcObject, &nil);
    GC_TRACE(gcBuffer, (FLISP_ARG_ONE->buf == NULL) ? nil : newString(interp, FLISP_ARG_ONE->buf));
    *gcObject = newCons(interp, gcBuffer, gcObject);
    GC_RETURN(newCons(interp, &(FLISP_ARG_ONE->path), gcObject));
}

/* Strings */

// (string-append s a)
Object *stringAppend(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    int len1 = FLISP_ARG_ONE->size - 1;
    int len2 = FLISP_ARG_TWO->size - 1;
    char *new = strdup(FLISP_ARG_ONE->string);
    new = realloc(new, len1 + len2 + 1);
    if (new == NULL)
        return newError(interp, out_of_memory, FLISP_ARG_TWO,
                            "(string-append s a) - failed to allocate memory for a");
    memcpy(new + len1, FLISP_ARG_TWO->string, len2);
    new[len1 + len2] = '\0';

    Object * str = newStringWithLength(interp, new, len1 + len2);
    free(new);

    return str;
}

/// UTF-8 handling ////////////

/* encoded character size */
size_t flisp_char_length(char c)
{
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xC0) == 0xC0) return 2;
    if ((c & 0xE0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF8) return 4;
    return 0;

}

/** flisp_char_index() - char offset of string index
 *
 * @param interp .. interpreter into which to throw exceptions.
 * @param string .. string to index.
 * @param index  .. number of encoded characters for which to find offset.
 *
 * @returns: character offset into string corresponding to utf8
 * encoded character at position index */
size_t flisp_char_index(Interpreter *interp, char *string, size_t index)
{
    size_t n = 0, i = 0, l = 0;

    while (string[i] != '\0' && n < index) {
        l = flisp_char_length(string[i]);
        if (l == 0)
            flisp_exception(interp, newError(interp, invalid_value, nil, "flisp_char_index(): string not utf-8 encoded"));
        i += l;
        n++;
    }
    return i;
}

/** flisp_char_count() - number of encoded characters in string
 *
 * @param interp .. interpreter into which to throw exceptions.
 * @param string .. string in which to count encoded characters.
 * @param len    .. maximum number of char's to check.
 *
 * @returns: count of encoded characters, i.e. len of UTF-8 encoded unicode string.
 */
size_t flisp_char_count(Interpreter *interp, char *string, size_t len)
{
    size_t n = 0, i = 0, l = 0;

    while (string[i] != '\0' && i < len) {
        l = flisp_char_length(string[i]);
        if (l == 0)
            flisp_exception(interp, newError(interp, invalid_value, nil, "flisp_char_count(): string not utf-8 encoded"));
        i += l;
        n++;
    }
    return n;
}

Object *stringLength(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, flisp_char_count(interp, FLISP_ARG_ONE->string, SIZE_MAX));
}

/** (string-search needle haystack)
 *
 */
Object *stringSearch(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    char *pos;

    pos = strstr(FLISP_ARG_TWO->string, FLISP_ARG_ONE->string);
    if (pos == NULL)
        return nil;

    return newInteger(
        interp,
        flisp_char_count(interp, FLISP_ARG_TWO->string, pos - FLISP_ARG_TWO->string)
        );
}

/** (substring string [start [end]]) - return substring of string within range [start, end)
 *
 * @param string   Input string
 * @param start    Start index, 0 based
 * @param end      End index, not included
 *
 * @return Substring of string starting from start until character
 * end-1. Length of string is default for *end*, 0 is default for
 * *start*.
 */
Object *stringSubstring(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    int64_t start = 0, end, len;

    FLISP_ARG_TYPECHECK(FLISP_ARG_ONE, type_string, "(substring string [start [end]]) - string");

    if (*(FLISP_ARG_ONE->string) == '\0')
        return flisp_empty_string;

    end = len = flisp_char_count(interp, FLISP_ARG_ONE->string, SIZE_MAX);

    if (nArgs > 1) {
        FLISP_ARG_TYPECHECK(FLISP_ARG_TWO, type_integer, "(substring string [start [end]]) - start");
        start = (FLISP_ARG_TWO->value);
        if (start < 0)
            start = end + start;
    }        
    if (nArgs > 2) {
        FLISP_ARG_TYPECHECK(FLISP_ARG_THREE, type_integer, "(substring string [start [end]]) - end");
    if (FLISP_ARG_THREE->value < 0)
                end = end + FLISP_ARG_THREE->value;
            else
                end = FLISP_ARG_THREE->value;
    }
    if (start < 0 || start > len)
        return newError(interp, range_error, FLISP_ARG_TWO,
                            "(substring string [start [end]]) - start out of range");
    if (end < 0 || end > len)
        return newError(interp, range_error, FLISP_ARG_THREE,
                            "(substring string [start [end]]) - end out of range");
    if (start == end)
        return flisp_empty_string;

    if (start > end)
        return newError(interp, range_error, FLISP_ARG_TWO,
                            "(substring string [start [end]]) - end > start");
    len = end - start;
    start = flisp_char_index(interp, FLISP_ARG_ONE->string, start);
    char *buf = strdup(FLISP_ARG_ONE->string+start);
    if (buf == NULL)
        return newError(interp, out_of_memory, nil, "OOM allocating buffer for (substring)\n");

    /* Repurpose len for char length */
    len = flisp_char_index(interp, buf, len);
    Object *new = newStringWithLength(interp, buf, len+1);
    free(buf);
    new->string[len] = '\0';

    return new;
}

// (string-compare s1 s2)
Object *stringCompare(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, strcmp(FLISP_ARG_ONE->string, FLISP_ARG_TWO->string));
}

// Interpreter introspection and configuration
/* Note:
 * - Maybe move this to an extension, where each sub command cmd is
 *   named interp-cmd to smplify code and type checks.
 * - flisp must load this to be able to redirect stdin etc.
 * - No tests yet.
 */
void setInterpStream(Object *stream, Object *new)
{
    stream->fd = new->fd;
    stream->path = new->path;
     stream->buf = new->buf;
    stream->len = new->len;
}

/** (interp cmd[ arg..]) - query or set interpreter internals */
Object *primitiveInterp(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ARG_TYPECHECK(FLISP_ARG_ONE, type_symbol, "(interp cmd[ arg..])");

    if (!strcmp(FLISP_ARG_ONE->string, "version")) {
        return newString(interp, FL_NAME " " FL_VERSION);
    }
    if (!strcmp(FLISP_ARG_ONE->string, "debug")) {
        if (nArgs > 1) {
            FLISP_ARG_TYPECHECK(FLISP_ARG_TWO, type_stream, "(interp :debug[ fd] - fd");
            setInterpStream(&interp->debug, FLISP_ARG_TWO);
        }
        return &(interp->debug);
    }
    if (!strcmp(FLISP_ARG_ONE->string, "input")) {
        if (nArgs > 1) {
            FLISP_ARG_TYPECHECK(FLISP_ARG_TWO, type_stream, "(interp :input[ fd] - fd");
            setInterpStream(&interp->input, FLISP_ARG_TWO);
        }
        return &(interp->input);
    }
    if (!strcmp(FLISP_ARG_ONE->string, "output")) {
        if (nArgs > 1) {
            FLISP_ARG_TYPECHECK(FLISP_ARG_TWO, type_stream, "(interp :output[ fd] - fd");
            setInterpStream(&interp->output, FLISP_ARG_TWO);
        }
        return &(interp->output);
    }
    if (!strcmp(FLISP_ARG_ONE->string, "symbols")) {
        return (interp->symbols);
    }
    if (!strcmp(FLISP_ARG_ONE->string, "global")) {
        return interp->global;
    }
    if (!strcmp(FLISP_ARG_ONE->string, "env")) {
        if (nArgs > 1) {
            FLISP_ARG_TYPECHECK(FLISP_ARG_TWO, type_symbol, "(interp env[ field[ env]]) - field");
            Object *e = *env;
            if (nArgs > 2) {
                FLISP_ARG_TYPECHECK(FLISP_ARG_THREE, type_env, "(interp env[ field[ env]]) - env");
                e = FLISP_ARG_THREE;
            }
            if (!strcmp(FLISP_ARG_TWO->string, "parent"))
                return e->parent;
            if (!strcmp(FLISP_ARG_TWO->string, "vars"))
                return e->vars;
            if (!strcmp(FLISP_ARG_TWO->string, "vals"))
                return e->vals;
            return newError(interp, invalid_value, FLISP_ARG_TWO,
                        "(interp env[ field[ env]]) - field must be one of :parent, :vars, :vals");
        }
        /* Note: This one fails in the global environment with an
         * infinite nested list of (nil "" nil) or so */
        return *env;
    }
    if (!strcmp(FLISP_ARG_ONE->string, "gc")) {
        gc(interp);
        return nil;
    }

    return newError(interp, invalid_value, FLISP_ARG_ONE,
                            "(flisp cmd[ arg..]) - unknown command");
}

// MAIN ///////////////////////////////////////////////////////////////////////

void flisp_register_constant(Interpreter *interp, Object *symbol, Object *value)
{
    symbol->type = type_symbol;
    symbol->size = strlen(symbol->string) +1;
    symbol->length = 0;
    if (value == NULL)
        value = symbol;
    envSet(interp, &symbol, &value, &interp->global, true);
    interp->symbols = newCons(interp, &symbol, &interp->symbols);
}

Primitive *flisp_register_primitive(Interpreter *interp, char *name,
                                    int min_args, int max_args, Object *args_type,
                                    LispEval func)
{
    Primitive *primitive = malloc(sizeof(Primitive));
    if (primitive == NULL)
        return NULL;
    primitive->name = strdup(name);
    if (primitive->name == NULL)
        return NULL;
    primitive->nMinArgs = min_args;
    primitive->nMaxArgs = max_args;
    primitive->argsType = args_type;
    primitive->eval = func;

    GC_CHECKPOINT;
    GC_TRACE(gcSymbol, newSymbol(interp, primitive->name));
    Object *p = newPrimitive(interp, primitive);
    GC_RELEASE;
    envSet(interp, gcSymbol, &p, &interp->global, true);
    return primitive;
}

Primitive *readPrimitive = NULL;
Primitive *evalPrimitive = NULL;
//Object *readCons = NULL;
//Object *evalApply = NULL;
bool flisp_primitives_register(Interpreter *interp)
{
    if (readPrimitive != NULL)         /* Already loaded */
        return true;
    if (!
        (flisp_register_primitive(   interp, "quote",         1,  1, nil, (LispEval) PRIMITIVE_QUOTE)
         && flisp_register_primitive(interp, "bind",          2,  3, nil, (LispEval) PRIMITIVE_BIND  /* special form */ )
         && flisp_register_primitive(interp, "progn",         0, -1, nil, (LispEval) PRIMITIVE_PROGN /* special form */ )
         && flisp_register_primitive(interp, "cond",          0, -1, nil, (LispEval) PRIMITIVE_COND  /* special form */ )
         && flisp_register_primitive(interp, "lambda",        1, -1, nil, (LispEval) PRIMITIVE_LAMBDA /* special form */ )
         && flisp_register_primitive(interp, "macro",         1, -1, nil, (LispEval) PRIMITIVE_MACRO  /* special form */ )
         && flisp_register_primitive(interp, "macroexpand-1", 1,  2, nil, (LispEval) PRIMITIVE_MACROEXPAND /* special form */ )
         && flisp_register_primitive(interp, "catch",         1,  1, nil, (LispEval) PRIMITIVE_CATCH  /*special form */ )
         && flisp_register_primitive(interp, "null",          1,  1, nil,            primitiveNullP)
         && flisp_register_primitive(interp, "type-of",       1,  1, nil,            primitiveTypeOf)
         && flisp_register_primitive(interp, "consp",         1,  1, nil,            primitiveConsP)
         && flisp_register_primitive(interp, "nreverse",      1,  1, nil,            primitiveNreverse)
         && flisp_register_primitive(interp, "intern",        1,  1, type_string,    primitiveIntern)
         && flisp_register_primitive(interp, "symbol-name",   1,  1, type_symbol,    primitiveSymbolName)
         && flisp_register_primitive(interp, "same",          2,  2, nil,            primitiveSame)
         && flisp_register_primitive(interp, "car",           1,  1, nil,            primitiveCar) /* Note: nil|cons */
         && flisp_register_primitive(interp, "cdr",           1,  1, nil,            primitiveCdr) /* Note: nil|cons */
         && flisp_register_primitive(interp, "object-size",   1,  1, nil,            primitiveObjectSize)
         && flisp_register_primitive(interp, "object-length", 1,  1, nil,            primitiveObjectLength)
         && flisp_register_primitive(interp, "vector",        1, -1, nil,            primitiveVector)
         && flisp_register_primitive(interp, "vector-range",  1,  3, nil,            primitiveVectorRange)
         && flisp_register_primitive(interp, "cons",          2,  2, nil,            primitiveCons)
         && flisp_register_primitive(interp, "open",          1,  2, type_string,    primitiveFopen)
         && flisp_register_primitive(interp, "close",         1,  1, type_stream,    primitiveFclose)
         && flisp_register_primitive(interp, "file-info",     1,  1, type_stream,    primitiveFinfo))
        )
        return false;
    /* Note: make the following two local then .. */
    readPrimitive = flisp_register_primitive(interp, "read", 0,  2, nil,            primitiveRead);
    evalPrimitive = flisp_register_primitive(interp, "eval", 1,  1, nil,            primitiveEval);
    /* .. call here evalApply = createEvalApply(readPrimitive, evalPrimitive); and
     * readCons = createReadCons();
     * createReadCons() and createEvalApply() would allocate Objects with malloc.
     * cerf() would then just fix readCons with the fd and return evalCatch(interp, evalApply, &interp->global);
     */
    if (readPrimitive == NULL || evalPrimitive == NULL)
        return false;
    return
        flisp_register_primitive(interp,    "write",         1,  3, nil,            primitiveWrite)
#if DEBUG_GC
        && flisp_register_primitive(interp, "gc",            0,  0, nil,            primitiveGc)
        && flisp_register_primitive(interp, "gctrace",       0,  0, nil,            primitiveGcTrace)
#endif
        && flisp_register_primitive(interp, "error",         2,  3, nil,            primitiveError)
        && flisp_register_primitive(interp, "throw",         1,  1, type_error,     primitiveThrow)
        && flisp_register_primitive(interp, "i+",            2,  2, type_integer,   integerAdd)
        && flisp_register_primitive(interp, "i-",            2,  2, type_integer,   integerSubtract)
        && flisp_register_primitive(interp, "i*",            2,  2, type_integer,   integerMultiply)
        && flisp_register_primitive(interp, "i/",            2,  2, type_integer,   integerDivide)
        && flisp_register_primitive(interp, "i%",            2,  2, type_integer,   integerMod)
        && flisp_register_primitive(interp, "i=",            2,  2, type_integer,   integerEqual)
        && flisp_register_primitive(interp, "i<",            2,  2, type_integer,   integerLess)
        && flisp_register_primitive(interp, "i<=",           2,  2, type_integer,   integerLessEqual)
        && flisp_register_primitive(interp, "i>",            2,  2, type_integer,   integerGreater)
        && flisp_register_primitive(interp, "i>=",           2,  2, type_integer,   integerGreaterEqual)
        && flisp_register_primitive(interp, "&",             2,  2, type_integer,   integerAnd)
        && flisp_register_primitive(interp, "|",             2,  2, type_integer,   integerOr)
        && flisp_register_primitive(interp, "^",             2,  2, type_integer,   integerXor)
        && flisp_register_primitive(interp, "<<",            2,  2, type_integer,   integerShiftLeft)
        && flisp_register_primitive(interp, ">>",            2,  2, type_integer,   integerShiftRight)
        && flisp_register_primitive(interp, "~",             1,  1, type_integer,   integerNot)
        && flisp_register_primitive(interp, "string-append", 2,  2, type_string,    stringAppend)
        && flisp_register_primitive(interp, "string-length", 1,  1, type_string,    stringLength)
        && flisp_register_primitive(interp, "string-search", 2,  2, type_string,    stringSearch)
        && flisp_register_primitive(interp, "substring",     1,  3, nil,            stringSubstring)
        && flisp_register_primitive(interp, "string-compare",2,  2, type_string,    stringCompare)
        && flisp_register_primitive(interp, "interp",        1, -1, nil,            primitiveInterp);
}

void initRootEnv(Interpreter *interp)
{
    /* Internal symbols */
    type_env->type = type_symbol;
    type_moved->type = type_symbol;
    flisp_empty_string->type = type_string;

    flisp_register_constant(interp, t, NULL);

    /* Types */
    flisp_register_constant(interp, type_integer, NULL);
    flisp_register_constant(interp, type_double, NULL);
    flisp_register_constant(interp, type_string, NULL);
    flisp_register_constant(interp, type_symbol, NULL);
    flisp_register_constant(interp, type_cons, NULL);
    flisp_register_constant(interp, type_lambda, NULL);
    flisp_register_constant(interp, type_macro, NULL);
    flisp_register_constant(interp, type_primitive, NULL);
    flisp_register_constant(interp, type_stream, NULL);
    flisp_register_constant(interp, type_error, NULL);
    flisp_register_constant(interp, type_double, NULL);
    /* Exceptions */
    flisp_register_constant(interp, end_of_file, NULL);
    flisp_register_constant(interp, read_incomplete, NULL);
    flisp_register_constant(interp, invalid_read_syntax, NULL);
    flisp_register_constant(interp, range_error, NULL);
    flisp_register_constant(interp, wrong_type_argument, NULL);
    flisp_register_constant(interp, invalid_value, NULL);
    flisp_register_constant(interp, wrong_number_of_arguments, NULL);
    flisp_register_constant(interp, arithmetic_error, NULL);
    flisp_register_constant(interp, out_of_memory, NULL);
    flisp_register_constant(interp, gc_error, NULL);
    /* I/O */
    flisp_register_constant(interp, io_error, NULL);
    flisp_register_constant(interp, permission_denied, NULL);
    flisp_register_constant(interp, not_found, NULL);
    flisp_register_constant(interp, file_exists, NULL);
    flisp_register_constant(interp, read_only, NULL);
    flisp_register_constant(interp, is_directory, NULL);
}

Memory *newMemory(size_t size)
{
    Memory *memory = malloc(sizeof(Memory));
    if (!memory) return NULL;

    memory->capacity = size;
    memory->fromSpace = NULL;
    memory->toSpace = NULL;

    return memory;
}

/*
 * Public interface for embedding fLisp into an application.
 */

Object init_env_failed =  { .size = 56, .length = 0, .string = "failed to create global environment for the interpreter" };
Object init_oom_message = { .size = 46, .length = 0, .string = "failed to allocate memory for the interpreter" };

Object eval_no_input =    { .size = 27, .length = 0, .string = "no input stream configured" };
Object eval_input_open =  { .size = 35, .length = 0, .string = "fmemopen() for input string failed" };

Object init_error;

Interpreter *interp_error(Interpreter *interp, Object *error, Object *message)
{
    init_error.type = type_error;
    init_error.error = error;
    init_error.message = message;
    init_error.culprit = nil;
    interp->result = &init_error;
    return interp;
}

/** Initialize and return an fLisp interpreter.
 *
 * @param size          Initial size of Lisp object space in bytes.
 * @param argv          null terminated array to arguments to be imported or NULL.
 * @param library_path  path to Lisp library, aka 'script_dir' or NULL for default.
 * @param input         open readable file descriptor for default input or NULL.
 * @param output        open writable file descriptor for default output or NULL.
 * @param debug         open writable file descriptor for debug output or NULL.
 *
 * @returns On success: a pointer to an fLisp interpreter structure
 * @returns On failure: NULL
 *
 * Note: at the moment we only provide a single interpreter store a
 * pointer to int in the static variable *interp* and return that variable.
 *
 */
Interpreter *flisp_new(
    size_t size,
    char **argv, char *library_path,
    FILE *input, FILE *output, FILE* debug)
{
    Interpreter *interp;
    Object *var;

    interp = malloc(sizeof(Interpreter));
    if (interp == NULL) return NULL;

    interp->result = nil;
    interp->debug.fd = debug;

    Memory *memory = newMemory((size < FLISP_MEMORY_INC_SIZE) ? FLISP_MEMORY_INC_SIZE :size);
    if (memory == NULL)
        return interp_error(interp, out_of_memory, &init_oom_message);

    interp->memory = memory;

    if (flisp_interpreters == NULL)
        interp->next = interp;
    else
        interp->next = flisp_interpreters;
    flisp_interpreters = interp;

    /* read buffer */
    interp->buf = NULL;
    interp->capacity = 0;
    interp->len = 0;

    interp->catch = &interp->exceptionEnv;

#if DEBUG_GC_ALWAYS
    gc_always = true;
#endif

    /* Fundamentals */
    nil->type = type_symbol;

    interp->gcTop = nil;
    interp->symbols = newCons(interp, &nil, &nil);
    interp->global = newEnv(interp, &nil, &nil);
    if (interp->global->type == type_error)
        return interp_error(interp, invalid_value, &init_env_failed);

    initRootEnv(interp);

    if (!flisp_primitives_register(interp)) {
        flisp_destroy(interp);
        return NULL;
    }

    GC_CHECKPOINT;
    GC_TRACE(gcVal, nil);

    if (argv != NULL) {
        /* Add argv0 to the environment */
        *gcVal = newString(interp, *argv);
        var = newSymbol(interp, "argv0");
        (void)envSet(interp, &var, gcVal, &interp->global, true);

        /* Add argv to the environement */
        *gcVal = nil;
        for (Object **j = gcVal; *++argv; j = &(*j)->cdr) {
            *j = newCons(interp, &nil, &nil);
            (*j)->car = newString(interp, *argv);
        }
        var = newSymbol(interp, "argv");
        (void)envSet(interp, &var, gcVal, &interp->global, true);
    }

    /* Add library_path to the environment */
    if (library_path == NULL)
        if ((library_path=getenv("FLISPLIB")) == NULL)
            library_path = CPP_XSTR(FLISPLIB);

    *gcVal = newString(interp, library_path);
    var = newSymbol(interp, "script_dir");
    envSet(interp, &var, gcVal, &interp->global, true);

    /* debug stream */
    interp->debug.type = type_stream;
    interp->debug.path = debug_output;
    interp->debug.buf = NULL;
    interp->debug.len = 0;
    flisp_register_constant(interp, debug_output, &interp->debug);

    /* input stream */
    interp->input.type = type_stream;
    interp->input.fd = input;
    interp->input.path = standard_input;
    interp->input.buf = NULL;
    interp->input.len = 0;
    flisp_register_constant(interp, standard_input, &interp->input);

    /* output stream */
    interp->output.type = type_stream;
    interp->output.fd = output;
    interp->output.path = standard_output;
    interp->output.buf = NULL;
    interp->output.len = 0;
    flisp_register_constant(interp, standard_output, &interp->output);

    GC_RELEASE;

    return interp;
}

/* Note: public interface, but very optimistic about:
 * - flisp_interpreters is not NULL
 * - interp is not NULL
 * - interp is on the list
 *
 * Note: should we close file descriptors other then debug?
 *
 * Note: primitives are registered dynamically, but we do not free
 *   their memory here!
 */
void flisp_destroy(Interpreter *interp)
{
    Interpreter *i;
    for (i=flisp_interpreters; i->next != interp; i=i->next);
    i->next = interp->next;
    i = NULL;
    if (flisp_interpreters == NULL) flisp_interpreters = interp->next;

    if (interp->memory->fromSpace)
        (void)munmap(interp->memory->fromSpace, interp->memory->capacity);

    if (interp->memory->toSpace)
        (void)munmap(interp->memory->toSpace, interp->memory->capacity);

    if (interp->debug.fd)
        fclose(interp->debug.fd);
    free(interp->memory);
    free(interp);
}

/** flisp_write_error - write error message to file descriptor
 *
 * @param interp  fLisp interpreter
 * @param fd      open writable file descriptor
 *
 * Formats an error message from a (catch) result and writes it to the
 * given file descriptor.  If the error object is nil, it is not
 * inserted.
 *
 */
void flisp_write_error(Interpreter *interp, FILE *fd)
{
    if (FLISP_ERROR_CULPRIT(interp) == nil)
        fprintf(fd, "error:%s: %s\n", FLISP_ERROR_TYPE(interp), FLISP_ERROR_MESSAGE(interp));
    else {
        fprintf(fd, "error:%s: %s: ", FLISP_ERROR_TYPE(interp), FLISP_ERROR_MESSAGE(interp));
        flisp_write_object(interp, fd, FLISP_ERROR_CULPRIT(interp), true);
        fputs("\n", fd);
    }
    fflush(fd);
}

/** (catch (eval (read f))) or (catch (eval (read)))
 *
 * (eval (read f)) or (eval (read))
 * (eval . (read . (f . nil)) or
 * (eval . (read . nil)
 */
Object *cerf(Interpreter *interp, FILE *fd)
{
    /* Note: find a way to not construct this all the time anew, maybe along these lines: */
#if 0
    /* segfaults though */
    Object stream = (Object) { type_stream,  .path = nil, .fd = fd};
    Object list =   (Object) { type_cons,  .car = &stream, .cdr = nil};
    Object *object = &list;
    if (fd == NULL) object->car = nil;
    object->car = primitiveRead(interp, &object, &interp->global);
    return evalCatch(interp, &object, &interp->global);
#else
    Object f =         (Object) { type_stream, .path = nil, .fd = fd };
    Object fCons =     (Object) { type_cons, .car = &f, .cdr = nil };
    Object read =      (Object) { type_primitive, .primitive = readPrimitive };
    Object readCons =  (Object) { type_cons, .car = &read, .cdr = &fCons };
    if (fd == NULL)
        readCons.cdr = nil;
    Object readApply =  (Object) { type_cons, .car = &readCons, .cdr = nil };

    Object eval =      (Object) { type_primitive, .primitive = evalPrimitive };
    Object evalCons =  (Object) { type_cons, .car = &eval, .cdr = &readApply };
    Object *evalApply = &(Object) { type_cons, .car = &evalCons, .cdr = nil };

    return evalCatch(interp, &evalApply, &interp->global);
#endif
}

/** flisp_eval() - interpret a string or file in Lisp
 *
 * @param interp  fLisp interpreter
 * @param input   string to evaluate
 *
 * If input is NULL, the interpreters input stream is evaluated
 * instead.
 *
 * After evaluation, the result of evaluation is available in
 * interp->object. It is a (catch) result which is a three element
 * list:
 *
 *   (code message result)
 *
 * If evaluation was successful, code is nil and message is an empty
 * string.  Otherwise, code is an error symbol, message is a human
 * readable error message and result the object causing the error.
 *
 * The following macros can be used to access the list elements:
 *
 * - FLISP_RESULT_CODE(INTERPRETER)
 * - FLISP_RESULT_MESSAGE(INTERPRETER)
 * - FLISP_RESULT_OBJECT(INTERPRETER)
 *
 */
void flisp_eval(Interpreter *interp, char *input)
{
    FILE *fd = NULL;

    if (input == NULL) {
        fl_debug(interp, "flisp_eval()\n");
        if (interp->input.fd  == NULL) {
            (void) interp_error(interp, invalid_value, &eval_no_input);
            return;
        }
    } else {
        fl_debug(interp, "flisp_eval(\"%s\")\n", input);
        if (NULL == (fd = fmemopen(input, strlen(input), "r")))  {
            (void) interp_error(interp, io_error, &eval_input_open);
            return;
        }
    }
    interp->gcTop = nil;
    GC_CHECKPOINT;
    GC_TRACE(gcResult, nil);
    Object *object;
    for (;;) {
        object = cerf(interp, fd);
        if (object->type == type_error) {
            if (object->error == end_of_file) {
                fl_debug(interp, "read: EOF\n");
                interp->result = *gcResult;
            }
            break;
        }
        flisp_write_object(interp, interp->output.fd, object, true);
        writeChar(interp, interp->output.fd, '\n');
        fflush(interp->output.fd);
        *gcResult = object;
    }
    GC_RELEASE;
    if (interp->output.fd) fflush(interp->output.fd);
    if (fd) fclose(fd);
    return;
}

/* Note: experimental */
void flisp_expr(Interpreter *interp, Object *object)
{
    Object *args = newCons(interp, &object, &nil);
    (void) evalCatch(interp, &args, &interp->global);
}
bool flisp_error(Interpreter *interp)
{
    return interp->result != nil && interp->result->type == type_error;
}

/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
