/*
 * fLisp - a tiny yet practical Lisp interpreter.
 *
 * Based on Tiny-Lisp: https://github.com/matp/tiny-lisp, public domain
 *
 * Georg Lehner <jorge@magma-soft.at> 2024, CC0 1.0
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

#include "double.h"
#include "posix.h"
#include "string.h"

//#define EXCEPTION_MEM_RESERVE 4*sizeof(Object)
// Note: debugging //#define EXCEPTION_MEM_RESERVE 8*sizeof(Object)
// Note: no exception in gc anymore
#define EXCEPTION_MEM_RESERVE 0

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS        MAP_ANON
#endif

#define COUNTFMT long unsigned int

/* Constants */
/* Fundamentals */
FLISP_DEFINE_CONSTANT(nil,nil);
FLISP_DEFINE_CONSTANT(t,t);

/* Error symbols */
FLISP_DEFINE_CONSTANT(end_of_file,end-of-file);
FLISP_DEFINE_CONSTANT(read_incomplete,read-incomplete);
FLISP_DEFINE_CONSTANT(invalid_read_syntax,invalid-read-syntax);
FLISP_DEFINE_CONSTANT(range_error,range-error);
FLISP_DEFINE_CONSTANT(wrong_type_argument,wrong-type-argument);
FLISP_DEFINE_CONSTANT(invalid_value,invalid-value);
FLISP_DEFINE_CONSTANT(wrong_number_of_arguments,wrong-number-of-arguments);
FLISP_DEFINE_CONSTANT(arithmetic_error,arithmetic-error);
FLISP_DEFINE_CONSTANT(out_of_memory,out-of-memory);
FLISP_DEFINE_CONSTANT(gc_error,gc-error);
/* I/O */
FLISP_DEFINE_CONSTANT(io_error,io-error);
FLISP_DEFINE_CONSTANT(permission_denied,permission-denied);
FLISP_DEFINE_CONSTANT(not_found,not-found);
FLISP_DEFINE_CONSTANT(file_exists,file-exists);
FLISP_DEFINE_CONSTANT(read_only,read-only);
FLISP_DEFINE_CONSTANT(is_directory,is-directory);
/* Traps */
FLISP_DEFINE_CONSTANT(trap_countdown,trap-countdown);
/* Interpreter */
FLISP_DEFINE_CONSTANT(debug_output,*debug-output*);

/* Types */

/* Simple */
FLISP_DEFINE_TYPE(integer);
FLISP_DEFINE_TYPE(double);
FLISP_DEFINE_TYPE(primitive);
FLISP_DEFINE_TYPE(str);
FLISP_DEFINE_TYPE(ptr);

/* Extended */
FLISP_DEFINE_TYPE(type);
FLISP_DEFINE_TYPE(string);
FLISP_DEFINE_TYPE(symbol);
FLISP_DEFINE_TYPE(cons);
FLISP_DEFINE_TYPE(vector);
FLISP_DEFINE_TYPE(lambda);
FLISP_DEFINE_TYPE(macro);
FLISP_DEFINE_TYPE(error);
FLISP_DEFINE_TYPE(stream);

/* Internal */
FLISP_DEFINE_TYPE(env);
FLISP_DEFINE_TYPE(interpreter);
FLISP_DEFINE_TYPE(extension);
FLISP_DEFINE_TYPE(values);
FLISP_DEFINE_TYPE(moved);

/* Constant Objects */
Object *flisp_integer_zero = (Object*)&(SimpleObject) { .type = &type_integer_obj, .size =  0, .value = 0 };
Object *flisp_empty_string =                &(Object) { .type = &type_string_obj,  .size =  1, .length = 0, .string = "\0" };
Object *flisp_empty_vector = (Object*)&(SimpleObject) { .type = &type_vector_obj,  .size =  0, .length = 0  };

Object *flisp_debug_stream =  &(Object) {
    .type = &type_stream_obj,
    .size = sizeof(SimpleObject) + sizeof(StreamExt),
    .length = 1,
    .stream.path = (Object*)&debug_output_obj,
    .stream.fd = NULL,
    .stream.buf = NULL,
    .stream.len = 0
};

#define FLISP_DEFINE_STR(NAME,STR) \
    SimpleObject NAME = { .type = &type_str_obj, .size = 0, .str = #STR }

FLISP_DEFINE_STR(init_oom_message,"failed to allocate memory for the interpreter");
FLISP_DEFINE_STR(object_oom_message,"failed to allocate memory for object");
FLISP_DEFINE_STR(fmt_oom_message,"failed to allocate memory for the formatter");
FLISP_DEFINE_STR(fmt_invalid_base,"invalid number base");
FLISP_DEFINE_STR(fmt_invalid_length,"invalid formatting length");
FLISP_DEFINE_STR(eval_no_input,"no input stream configured");
FLISP_DEFINE_STR(eval_input_open,"fmemopen() for input string failed");
FLISP_DEFINE_STR(write_char_failed,"failed to write character");
FLISP_DEFINE_STR(write_string_failed,"failed to write string");
FLISP_DEFINE_STR(write_invalid_object,"invalid object");

Object init_error;

char *flisp_integer_char_map = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

Object *flisp_static_error(Object *error, SimpleObject *message)
{
    init_error.type = type_error;
    init_error.error.type = error;
    init_error.error.message = (Object *)message;
    init_error.error.culprit = nil;
    return &init_error;
}

/* Note: to be replaced by scratchpad */
static char error_message[PATH_MAX]; /* error message format area */

// Scratchpad ////

Scratchpad *scratchpad = &(Scratchpad) {0};

void initPad(Scratchpad *pad)
{
    pad->size = 0;
}

/** addCharToPad - add a character to a scratchpad
 *
 * @param: pad     scratchpad
 * @param: c       character to add to buffer
 * @returns: success
 *
 * addCharToPad() allocates memory in BUFSIZ chunks.
 */
bool addCharToPad(Scratchpad *pad, int c)
{
    if (!pad->capacity || pad->size >= pad->capacity) {
        pad->capacity += BUFSIZ;
        if ((pad->string = realloc(pad->string, pad->capacity)) == NULL)
            return false;
    }
    pad->string[pad->size++] = c;
    return true;
}
bool assurePad(Scratchpad *pad, size_t size)
{
    if (pad->capacity <= size) {
        pad->capacity = size + BUFSIZ;
        if ((pad->string = realloc(pad->string, pad->capacity)) == NULL)
            return false;
    }
    return true;
}
bool addStringToPad(Scratchpad *pad, char *string)
{
    size_t size = strlen(string);
    if (!assurePad(pad, pad->size+size))  return false;
    (void)strcpy(pad->string + pad->size, string);
    pad->size +=size;
    return true;
}
/** fmtInteger() - encode 64 bit integer as ascii string with base 2 to 36
 *
 * @param integer  .. Integer to convert
 * @param base     .. Number base to use
 * @param map      .. Conversion map to use
 * @param pad_char .. Character to use for left-padding the integer string: eq.: ' ', 0, x, X, b, B
 * @param length   .. Max length of output string, -1 for no padding, 0 to add a '0' before first pad char.
 *
 * @return: index to first digit within pad or error: NULL = OOM, -1, -2  range error for base or length.
 */

char *fmtInteger(int64_t integer, int64_t base, char *map, char pad_char, size_t length)
{
#define INTEGER_PAD_SIZE 67
    /* in binary we need 64 characters plus an optional "0b" prefix and "-" sign*/
    bool negative;
    int64_t d = INTEGER_PAD_SIZE;
    static char pad[INTEGER_PAD_SIZE];
    char *i = pad;

    if (base < 2 || base > 36)  return (char *)-1; /* range_error */

    if ((negative = integer < 0))  integer = -integer;

    /* Note: reuse digit as counter for padding */
    while (d--)  *i++ = pad_char;
        *i = '\0';

    do {
        d = integer % base;
        *--i = map[d];
    } while ((integer = integer / base));

    if (negative)  *--i = '-';

    if (length == -1)  return i;
    if (length == 0) { i-=2; *i = '0'; return i; }
    if (length <= 67)  return &pad[67-length];
    return (char *)-2;
}

// DEBUG LOG ///////////////////////////////////////////////////////////////////

#ifdef __GNUC__
void flisp_debug(Object *, char *format, ...)
    __attribute__ ((format(printf, 2, 3)));
#endif
/** flisp_debug() - fLisp debugger
 *
 * @param interp  Interpreter for which to send a debug message
 * @param format ...  printf() style debug string
 *
 * The format string is sent to the interpreters debug file descriptor - if there is one.
 *
 */
void flisp_debug(Object *interp, char *format, ...)
{
    if (FLISP_DEBUG_OUTPUT.fd == NULL)
        return;

    va_list(args);
    va_start(args, format);
    if (vfprintf(FLISP_DEBUG_OUTPUT.fd, format, args) < 0) {
        va_end(args);
        (void)fprintf(FLISP_DEBUG_OUTPUT.fd,
                      "fatal: failed to print debug message %s: %s", format, strerror(errno));
    }
    va_end(args);
    (void)fflush(FLISP_DEBUG_OUTPUT.fd);
}


#if 0
// EXCEPTION HANDLING /////////////////////////////////////////////////////////

void resetBuf(Object *);
#endif

#define CAR(OBJECT) OBJECT->cons.car
#define CDR(OBJECT) OBJECT->cons.cdr


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

Object *gcReturn(Object *interp, Object *gcTop, Object *result)
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
bool gcCollectableObject(Object *interp, Object *object) {
    return (object >= (Object *) FLISP_INTERP.memory->fromSpace &&
            object < (Object *) ((char *)FLISP_INTERP.memory->fromSpace + FLISP_INTERP.memory->fromOffset));
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
Object *gcMoveObject(Object *interp, Object *object, gcStats *stats)
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
    Object *forward = (Object *) ((char *)FLISP_INTERP.memory->toSpace + FLISP_INTERP.memory->toOffset);
    size_t size = sizeof(SimpleObject) + object->size;
    memcpy(forward, object, size);
    FLISP_INTERP.memory->toOffset += size;

    // mark object as moved and set forwarding pointer
    object->type = type_moved;
    object->forward = forward;

    return object->forward;
}

/** gc - move all active objects to new memory page
 *
 * @param interp   fLisp interpreter
 */
void gc(Object *interp)
{
    Object *object;
    gcStats stats = {0};
    size_t i;

    flisp_debug(interp, "collecting garbage\n");
    size_t free = (COUNTFMT) FLISP_INTERP.memory->capacity - FLISP_INTERP.memory->fromOffset;
    flisp_debug(interp, "memory: %lu/%lu, free %ld/(%lu)\n",
             (COUNTFMT) FLISP_INTERP.memory->fromOffset, (COUNTFMT) FLISP_INTERP.memory->capacity,
             free - EXCEPTION_MEM_RESERVE, free
        );
    FLISP_INTERP.memory->toOffset = 0;
#if DEBUG_GC
    flisp_debug(interp, "gc trace\n");
#endif
    for (object = FLISP_INTERP.gcTop; object != nil; object = CDR(object)) {
        CAR(object) = gcMoveObject(interp, CAR(object), &stats);
    }
#if DEBUG_GC
    flisp_debug(interp, "moving %lu root objects\n", FLISP_INTERP.length);
#endif
    for (i = 0; i < interp->length; i++)
        /* Note: pending interp = Object */
        ((Object *)interp)->objects[i] = gcMoveObject(interp, ((Object *)interp)->objects[i], &stats);

#if DEBUG_GC
    flisp_debug(interp, "root objects: %lu, skipped %lu, constant %lu\n",
             stats.moved, stats.skipped, stats.constant
        );
#endif

    // iterate over objects in to-space and move all objects they reference
    for (object = FLISP_INTERP.memory->toSpace;
         object < (Object *) ((char *)FLISP_INTERP.memory->toSpace + FLISP_INTERP.memory->toOffset);
         object = (Object *) ((char *)object + sizeof(SimpleObject) + object->size)) {

        if (object->size != 0)
            for (i = 0; i < object->length; i++)
                object->objects[i] = gcMoveObject(interp, object->objects[i], &stats);
    }
    // swap from- and to-space
    void *swap = FLISP_INTERP.memory->fromSpace;
    FLISP_INTERP.memory->fromSpace = FLISP_INTERP.memory->toSpace;
    FLISP_INTERP.memory->toSpace = swap;

    /* report before overwriting offset difference */
    flisp_debug(interp,  "collected %lu objects, skipped %lu, constants %lu, saved %lu bytes\n",
             (COUNTFMT) stats.moved, (COUNTFMT) stats.skipped, (COUNTFMT) stats.constant,
             (COUNTFMT) FLISP_INTERP.memory->fromOffset - FLISP_INTERP.memory->toOffset);
    free = (COUNTFMT) FLISP_INTERP.memory->capacity - FLISP_INTERP.memory->toOffset;
    flisp_debug(interp, "memory: %lu/%lu, free: %ld/(%lu)\n",
             (COUNTFMT) FLISP_INTERP.memory->toOffset, (COUNTFMT) FLISP_INTERP.memory->capacity,
             free - EXCEPTION_MEM_RESERVE, free
        );

    FLISP_INTERP.memory->fromOffset = FLISP_INTERP.memory->toOffset;
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
Object *memoryAllocObject(Object *interp, TypeObject *type, size_t size)
{
    size = memoryAlign(size, sizeof(void *));
    size_t blocks = ((size + EXCEPTION_MEM_RESERVE) / FLISP_MEMORY_INC_SIZE) + 1;
    size_t memory = blocks * FLISP_MEMORY_INC_SIZE;

    /* If not done already allocate to space */
    if (!FLISP_INTERP.memory->fromSpace) {
        if (memory > FLISP_INTERP.memory->capacity)
            FLISP_INTERP.memory->capacity = memory;
        flisp_debug(interp, "memoryAllocObject: allocate fromSpace: %zu bytes\n", FLISP_INTERP.memory->capacity);
        if (MAP_FAILED == (FLISP_INTERP.memory->fromSpace = mmap(NULL, FLISP_INTERP.memory->capacity,
                                                            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)))
            return flisp_static_error(nil, &init_oom_message);
        FLISP_INTERP.memory->fromOffset = 0;
        goto allocateObject;
    }
    /* Run garbage collection if capacity exceeded */
    if (
        (FLISP_INTERP.memory->fromOffset + size + EXCEPTION_MEM_RESERVE >= FLISP_INTERP.memory->capacity)
        || FLISP_INTERP.gc_always
        ) {
        flisp_debug(interp, "memoryAllocObject: need %lu bytes more then available, requesting garbage collection\n", (COUNTFMT) size);
        /* If not done already allocate to space */
        if (!FLISP_INTERP.memory->toSpace) {
            if (MAP_FAILED == (FLISP_INTERP.memory->toSpace = mmap(NULL, FLISP_INTERP.memory->capacity,
                                                 PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                                                 -1, 0)))
                return flisp_static_error(nil, &init_oom_message);
        }
        gc(interp);
    }
    /* Check if we now have enough space */
    if (FLISP_INTERP.memory->fromOffset + size + EXCEPTION_MEM_RESERVE < FLISP_INTERP.memory->capacity)
        goto allocateObject;

    flisp_debug(interp, "memoryAllocObject: still %lu bytes more needed, increasing memory by %lu\n",
             (COUNTFMT) size, (COUNTFMT) memory
        );
    /* Increase to space */
    void *new;
    if (MAP_FAILED == (new = mmap(NULL, FLISP_INTERP.memory->capacity + memory,
                                  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0))) {
        /* Note: fake that we have more memory return an error and then hope the best. */
        FLISP_INTERP.memory->capacity+= EXCEPTION_MEM_RESERVE;
        return newError2(interp, gc_error, out_of_memory, "OOM reallocating toSpace: ", strerror(errno));
    }
    if (munmap(FLISP_INTERP.memory->toSpace, FLISP_INTERP.memory->capacity) == -1) {
        FLISP_INTERP.memory->capacity+= EXCEPTION_MEM_RESERVE;
        return newError2(interp, gc_error, out_of_memory, "munmap(toSpace) failed: ", strerror(errno));
    }
    FLISP_INTERP.memory->toSpace = new;
    FLISP_INTERP.memory->capacity += memory;
    FLISP_INTERP.memory->toOffset = 0;
    gc(interp);
    if (munmap(FLISP_INTERP.memory->toSpace, FLISP_INTERP.memory->capacity - memory) == -1) {
        FLISP_INTERP.memory->capacity+= EXCEPTION_MEM_RESERVE;
        return newError2(interp, gc_error, out_of_memory, "munmap(fromSpace) failed: ", strerror(errno));
    }
    FLISP_INTERP.memory->toSpace = NULL;

allocateObject:
    ;
    /* Allocate object in from-space */
    Object *object = (Object *) ((char *)FLISP_INTERP.memory->fromSpace + FLISP_INTERP.memory->fromOffset);
    object->type = type;
    FLISP_INTERP.memory->fromOffset += size;

    return object;
}

#define GC_CHECK_ERR(OBJECT) if FLISP_IS_ERR(OBJECT) GC_RETURN(OBJECT)
/* Note: Do we have to distinguish? */
#define CHECK_OOM(OBJECT) if FLISP_IS_OOM(OBJECT) return OBJECT
#define GC_CHECK_OOM(OBJECT) if FLISP_IS_OOM(OBJECT) GC_RETURN(OBJECT)


// CONSTRUCTING OBJECTS ///////////////////////////////////////////////////////

/** newObject - allocate a new object in the Lisp object store and set
 * it's type.
 *
 * @param interp  fLisp Interpreter
 * @param type    Type object to set in the new Object
 *
 * @returns New Object
 */
Object *newObject(Object *interp, TypeObject *type, size_t size)
{
    Object *object = memoryAllocObject(interp, type, sizeof(SimpleObject)+size);
    CHECK_OOM(object);
    object->size = size;
    return object;
}
// Simple objects //
/** newInteger - allocate a new Integer object in the Lisp object store and set it's value
 *
 * @param interp  fLisp Interpreter
 * @param number  Integer value of the object.
 *
 * @returns New Object
 */
Object *newInteger(Object *interp, int64_t value)
{
    Object *object = newObject(interp, type_integer, 0);
    CHECK_OOM(object);
    object->value = value;
    return object;
}
Object *newDouble(Object *interp, double number)
{
    Object *object = newObject(interp, type_double, 0);
    CHECK_OOM(object);
    object->number = number;
    return object;
}
Object *newPrimitive(Object *interp, Primitive* primitive)
{
    Object *object = newObject(interp, type_primitive, 0);
    CHECK_OOM(object);
    object->primitive = primitive;
    return object;
}
// Extended objects //

/** flisp_ext_obj(interp, type, list, length, extra) - create and initialize an extended object.
 *
 * @param interp   .. Interpreter in which to create the object.
 * @param obj_list .. List of initializer objects or object. Dottet lists are flattened.
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
Object *flisp_ext_obj(Object *interp, TypeObject *type, Object **list, size_t length, size_t extra)
{
    if (type == type_vector && length == 0 && extra == 0)
        return flisp_empty_vector;

    GC_CHECKPOINT;
    GC_TRACE(gcType, (Object *)type);
    GC_TRACE(gcObjs, *list);
    Object *object = newObject(interp, (TypeObject *)*gcType, (sizeof(Object *) * length) + extra);
    GC_RELEASE;
    CHECK_OOM(object);
    size_t i;
    object->length = length;
    for(i = 0; i < length && (*gcObjs) != nil; (*gcObjs) = (*gcObjs)->cdr)
        if ((*gcObjs)-> type == type_cons)
            object->objects[i++] = (*gcObjs)->car;
        else {
            object->objects[i++] = *gcObjs;
            break;
        }
    while(i < length)
        object->objects[i++] = nil;
    return object;
}
Object *newCons(Object *interp, Object ** car, Object ** cdr)
{
    GC_CHECKPOINT;
    GC_TRACE(gcCar, *car);
    GC_TRACE(gcCdr, *cdr);
    Object *cons = newObject(interp, type_cons, sizeof(Object *[2]));
    GC_RELEASE;
    CHECK_OOM(cons);
    cons->length = 2;
    cons->car = *gcCar;
    cons->cdr = *gcCdr;
    return cons;
}
Object *newClosure(Object *interp, TypeObject *type, Object ** args, Object **env)
{
    Object *o;

    /* Covers: (closure (a b ..) body) and (closure (a b . ?) body) */
    for (o = (*args)->car; o->type == type_cons;  o = o->cdr) {
        if (o->car->type != type_symbol)
            return newError2(interp, wrong_type_argument, o->car,
                               (type == type_lambda) ? "(lambda" : "(macro",
                               " params body) - param is not a symbol");
        if (!gcCollectableObject(interp, o->car))
            return newError2(interp, invalid_value, o->car,
                            (type == type_lambda) ? "(lambda" : "(macro",
                            " params body) - param cannot be used as a parameter");
    }

    /* Cover: (closure a body) and (closure (a b . c) body) */
    if (o != nil && o->type != type_symbol)
        return newError(interp, o, wrong_type_argument, "(%s params body) - param is not a symbol");

    o = flisp_ext_obj(interp, type, &nil, 3, 0);
    CHECK_OOM(o);
    o->objects[0] = (*args)->car;
    o->objects[1] = (*args)->cdr;
    o->objects[2] = *env;
    return o;
}

int64_t flisp_list_length(Object *list)
{
    int64_t i;
    for (i = 0; list->type == type_cons; list = list->cdr)  i++;
    if (i)
        return (list == nil) ? i : ++i;
    return 0;
}

/* Clone List, if end is cons use it instead of the nil terminator
 * If end is not nil or cons err
 */
Object *cloneList(Object *interp, Object *list, Object *end)
{
    FLISP_ASSERT(list, type_cons, "(cloneList list end) - list");
    if (end != nil) FLISP_ASSERT(end, type_cons, "(cloneList list end) - end");

    GC_CHECKPOINT;
    GC_TRACE(gcList, list);
    GC_TRACE(gcEnd, end);
    GC_TRACE(gcCons, newCons(interp, &(*gcList)->car, &nil));
    GC_CHECK_OOM(*gcCons);
    GC_TRACE(gcNew, *gcCons);
    while((*gcList)->cdr->type == type_cons) {
        (*gcList) = (*gcList)->cdr;
        (*gcCons)->cdr = newCons(interp, &(*gcList)->car, &nil);
        GC_CHECK_OOM(*gcCons);
        *gcCons = (*gcCons)->cdr;
    }
    GC_RELEASE;
    if ((*gcList)->cdr != nil)
        return newError(interp, invalid_value, list, "(cloneList list end) - list is not a proper list");
    (*gcCons)->cdr = *gcEnd;
    return *gcNew;
}

/* If vals is a simple list just check max/min args.
 * If vals contain (values), splice them in recursively
*/
Object *checkParams(Object *interp, Object *param, Object** vals, size_t nArgs)
{
    Object *prev = nil, *val = *vals;
    bool check = true;

    for (;;) {
        if (param == nil && val == nil) break;

        if (val != nil && val->type != type_cons)
            return newErrorI(interp, wrong_type_argument, val, "(f args) - args[", nArgs, "] is not a list");

        check = !(param != nil && param->type == type_symbol);

        if (check) {
            if (param == nil && val != nil)
                return newErrorI(interp, wrong_number_of_arguments, *vals, "(f args) - args, f expects at most ", nArgs, " arguments");
            if (param != nil && val == nil) {
                for (; param->type == type_cons; param = param->cdr, ++nArgs);
                return newErrorI(interp, wrong_number_of_arguments, *vals, "(f args) - args, f expects at least ", nArgs, " arguments");
            }
        } else if (val == nil) break;
        if (val->car->type == type_values) {
            if (val->cdr == nil) {
                /* Special case, if values is at end of parameter list, destructively insert its arguments and restart checking */
                if (prev == nil)
                    *vals = val = val->car->objects[0];
                else
                    prev->cdr = val = val->car->objects[0];
            } else {
                /* splice in a copy of vals */
                if (prev == nil)
                    *vals = val = cloneList(interp, val->car->objects[0], val->cdr);
                else {
                    prev->cdr = val = cloneList(interp, val->car->objects[0], val->cdr);
                }
                CHECK_OOM(val);
            }
            continue;
        }
        if (check)  param = param->cdr;
        prev = val;
        val = val->cdr;
        ++nArgs;
    }
    return nil;
}

Object *newEnv(Object *interp, Object ** func, Object ** vals)
{
    Object *environment = newObject(interp, type_env, sizeof(Object*[3]));
    CHECK_OOM(environment);
    environment->length = 3;
    if ((*func) == nil) {
        environment->env.parent = environment->env.vars = environment->env.vals = nil;
        return environment;
    }

    FLISP_CHECK_ERR(checkParams(interp, (*func)->closure.params, vals, 0));
    environment->env.parent = (*func)->closure.env;
    environment->env.vars = (*func)->closure.params;
    environment->env.vals = *vals;
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
Object *newStringWithLength(Object *interp, char *string, size_t length)
{
    if (length == 0)
        return flisp_empty_string;

    /* Note: we allocate the original string, let unescapeString do
     *   the counting and and set the size correctly afterwards. The
     *   next GC cycle will discard the extra bytes.
     */
    Object *object = newObject(interp, type_string, length + 1);
    CHECK_OOM(object);
    object->length = 0;
    object->size = unescapeString(object->string, string, length) + 1;
    return object;
}
Object *newString(Object *interp, char *string)
{
    return newStringWithLength(interp, string, strlen(string));
}

Object *flisp_find_symbol(Object *interp, char *string, size_t length)
{
    size_t l;
    SimpleObject *s;
    for (Object *symbols = FLISP_INTERP.symbols; symbols != nil; symbols = symbols->cdr) {
        if (symbols->car->size) {
            if (symbols->car->size == length + 1 && strncmp(symbols->car->string, string, length) == 0)
                return symbols->car;
        } else {
            s = (SimpleObject*)(symbols->car);
            //l = strnlen(s->str, length);
            l = strlen(s->str);
            if (l == length && strncmp(s->str, string, length) == 0)
                return symbols->car;
        }
    }
    return NULL;
}
Object *newSymbolWithLength(Object *interp, char *string, size_t length)
{
    Object *symbol = flisp_find_symbol(interp, string, length);
    if (symbol != NULL)  return symbol;

    GC_CHECKPOINT;
    GC_TRACE(gcSymbol, newObject(interp, type_symbol, length + 1));
    GC_CHECK_OOM(*gcSymbol);
    (*gcSymbol)->length = 0;
    strncpy((*gcSymbol)->string, string, length);
    (*gcSymbol)->string[length] = '\0';
    FLISP_INTERP.symbols = newCons(interp, gcSymbol, &FLISP_INTERP.symbols);
    GC_CHECK_OOM(*gcSymbol);
    GC_RELEASE;
    return *gcSymbol;
}
Object *newSymbol(Object *interp, char *string)
{
    return newSymbolWithLength(interp, string, strlen(string));
}

Object *newError(Object *interp, Object *error_type, Object *culprit, char *message)
{
    GC_CHECKPOINT;
    GC_TRACE(gcErrorType, error_type);
    GC_TRACE(gcCulprit, culprit);
    GC_TRACE(gcMessage, newString(interp, message));
    GC_CHECK_ERR(*gcMessage);
    Object *error = flisp_ext_obj(interp, type_error, gcErrorType, 3, 0);
    GC_RELEASE;
    CHECK_OOM(error);
    error->error.message = *gcMessage;
    error->error.culprit = *gcCulprit;
    return error;
}

/** Create error object with message composed of two strings */
Object *newError2(Object *interp, Object *error_type, Object *culprit, char *mess1, char *mess2)
{
    initPad(scratchpad);
    if (!addStringToPad(scratchpad, mess1)) return flisp_static_error(out_of_memory, &fmt_oom_message);
    if (!addStringToPad(scratchpad, mess2)) return flisp_static_error(out_of_memory, &fmt_oom_message);
    return newError(interp, error_type, culprit, scratchpad->string);
}

/** Create error object with message composed of string and integer */
Object *newErrorI(Object *interp, Object *error_type, Object *culprit, char *mess1, int64_t integer, char *mess2)
{
    initPad(scratchpad);
    if (!addStringToPad(scratchpad, mess1)) return flisp_static_error(out_of_memory, &fmt_oom_message);
    char *i = fmtInteger(integer, 10, flisp_integer_char_map, ' ', -1);
    if (!i) return flisp_static_error(out_of_memory, &fmt_oom_message);
    /* Ommiting check base or pad range error i == -1 || i == -2 */
    if (!addStringToPad(scratchpad, i)) return flisp_static_error(out_of_memory, &fmt_oom_message);
    if (!addStringToPad(scratchpad, mess2)) return flisp_static_error(out_of_memory, &fmt_oom_message);
    return newError(interp, error_type, culprit, scratchpad->string);
}


/* Note: replace vsnprintf() with scratchpad */
#define FLISP_FORMAT_ERROR_MESSAGE "failed to format error message"
Object *newErrorFmt(Object *interp, Object *error, Object *culprit, char *format, ...)
{
    size_t written;
    size_t len = sizeof(error_message);
    char *message = error_message;

    GC_CHECKPOINT;
    GC_TRACE(gcErrorType, error);
    GC_TRACE(gcCulprit, culprit);
    GC_TRACE(gcMessage, flisp_empty_string);
    GC_TRACE(gcError, flisp_ext_obj(interp, type_error, gcErrorType, 3, 0));
    GC_CHECK_OOM(*gcError);

    if (format != NULL && format[0] != '\0') {
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
    GC_CHECK_ERR(*gcMessage);
    (*gcError)->error.message = *gcMessage;
    (*gcError)->error.culprit = *gcCulprit;

    GC_RETURN(*gcError);
}
/** newStreamObject - create stream object from file descriptor and path
 *
 * @param fd .. FILE * stream descriptor to register
 * @param name .. NULL or name of the file associated with fd
 * @param buf .. NULL or string to convert into an input file stream.
 */
Object *newStreamObject(Object *interp, FILE *fd, char *path)
{
    GC_CHECKPOINT;
    GC_TRACE(gcPath, newString(interp, path));
    Object *object = flisp_ext_obj(interp, type_stream, gcPath, 1,
                                   sizeof(FILE*) +
                                   sizeof(char*) +
                                   sizeof(size_t));
    GC_RELEASE;
    CHECK_OOM(object);
    object->stream.fd = fd;
    object->stream.buf = NULL;
    object->stream.len = 0;

    return object;
}

Object *newExtension(Object *interp, char *name, ExtensionInit init)
{
    GC_CHECKPOINT;
    GC_TRACE(gcName, newString(interp, name));
    Object *object = flisp_ext_obj(interp, type_extension, gcName, 2, sizeof(ExtensionInit));
    GC_RELEASE;
    CHECK_OOM(object);
    object->extension.version = nil;
    object->extension.init = init;
    return object;
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

Object *flisp_find_value(Object *interp, Object *env, Object *var) {

    for (; env != nil; env = env->env.parent) {
        Object *vars = env->env.vars, *vals = env->env.vals;

        for (; vars->type == type_cons; vars = vars->cdr, vals = vals->cdr)
            if (vars->car == var)
                return vals->car;

        if (vars == var)
            return vals;
    }
    return NULL;
}
Object *envLookup(Object *interp, Object *var, Object *env)
{
    Object *value = flisp_find_value(interp, env, var);
    if (value == NULL)
        return newError(interp, invalid_value, var, "unbound symbol");
    return value;
}
Object *flisp_lookup(Object *interp, Object *var)
{
    return envLookup(interp, var, interp->self.global);
}
Object *envAdd(Object *interp, Object ** var, Object ** val, Object **env)
{
    GC_CHECKPOINT;
    GC_TRACE(gcEnv, *env);
    GC_TRACE(gcVar, *var);
    GC_TRACE(gcVal, *val);
    GC_TRACE(gcVars, newCons(interp, gcVar, &nil));
    GC_CHECK_ERR(*gcVars);
    Object *vals = newCons(interp, gcVal, &nil);
    GC_RELEASE;
    CHECK_OOM(vals);
    (*gcVars)->cdr = (*gcEnv)->env.vars, (*gcEnv)->env.vars = *gcVars;
    vals->cdr = (*gcEnv)->env.vals, (*gcEnv)->env.vals = vals;

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
Object *envSet(Object *interp, Object ** var, Object ** val, Object **env, bool top)
{
    for (;;) {
        Object *vars = (*env)->env.vars, *vals = (*env)->env.vals;

        for (; vars->type == type_cons; vars = vars->cdr, vals = vals->cdr) {
            if (vars->car == *var)
                return vals->car = *val;
            if (vars->cdr == *var)
                return vals->cdr = *val;
        }

        if ((*env)->env.parent == nil || !top) {
            GC_CHECKPOINT;
            GC_TRACE(gcEnv, *env);
            GC_RETURN(envAdd(interp, var, val, gcEnv));
        } else
            *env = (*env)->env.parent;
    }
}

// READING S-EXPRESSIONS //////////////////////////////////////////////////////

Object *evalExpr(Object *, Object **, Object **);

int isSymbolChar(int ch, size_t dummy)
{
#if 0
    static const char *valid = "!#$%&*+-./:<=>?@^_|~";
    return isalnum(ch) || strchr(valid, ch);
#else
    /* Note: not control character, not space character, not quote, doublequote, backquote coma # */
    static const char *invalid = "\"'(){}[],` ";
    return !strchr(invalid, ch) && ((ch > 31 && ch < 127) || (ch >= 0x80));
#endif
}
int isDigitChar(int ch, size_t base)
{
    char *found = strchr(flisp_integer_char_map, toupper(ch));
    return (found != NULL && (found - flisp_integer_char_map) < base);
}
int isDoubleChar(int ch, size_t dummy)
{
    static const char *valid = "-.e";
    return isdigit(ch) || strchr(valid, ch);
}

Object *flisp_nreverse(Object *interp, Object *list)
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
int skipToNext(Object *interp, FILE *fd)
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
int peekNext(Object *interp, FILE *fd)
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
int readWhile(Object *interp, FILE *fd, int (*predicate) (int, size_t), size_t base)
{
    for (;;) {
        int ch = streamPeek(fd);
        if (ch == EOF)  return ch;
        if (!predicate(ch, base))  return ch;
        if (!addCharToPad(scratchpad, fgetc(fd)))  return EOF;
    }
}

/* Object readers */

#define READER_OOM(WHEN) return newError(interp, out_of_memory, nil, "OOM " WHEN)
#define READER_IO(WHEN)  return newError2(interp, out_of_memory, nil, "I/O error %s" WHEN, strerror(errno))
#define READER_EOF(WHEN) return newError(interp, read_incomplete, nil,"unexpected end of stream "  WHEN)

Object *readInteger(Object *interp, Scratchpad *pad)
{
    if (!addCharToPad(pad, '\0')) READER_OOM("while reading integer literal");
    /* Note: strtoimax might actually use a different size then
     *   int64_t. So this approach should be revised.  We want to
     *   provide int64_t on any platform.
     */
    errno = 0;
    int64_t n = strtoimax(pad->string, NULL, 0);
    if (errno == ERANGE)
        return newError(interp, range_error, nil, "while reading integer literal");
    return newInteger(interp, n);
}

Object *readDouble(Object *interp, Scratchpad *pad)
{
    if (!addCharToPad(pad, '\0')) READER_OOM("while reading double literal");
    errno = 0;
    double d = strtod(pad->string, NULL);
    if (errno == ERANGE)
        return newError2(interp, range_error, nil, "double out of range,: ", pad->string);
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
Object *readString(Object *interp, FILE *fd, Scratchpad *pad)
{
    bool isEscaped = false;
    int ch;

    initPad(pad);

    for (;;) {
        ch = fgetc(fd);
        if (ch == EOF) {
            if (ferror(fd)) READER_IO("while reading string literal");
            READER_EOF("while reading string literal");
        }
        if (ch == '"' && !isEscaped)
            return newStringWithLength(interp, pad->string, pad->size);

        isEscaped = (ch == '\\' && !isEscaped);
        if (!addCharToPad(pad, ch)) READER_OOM("OOM while reading string literal");
    }
}
Object *readSymbol(Object *interp, FILE *fd, Scratchpad *pad)
{
    if (EOF == readWhile(interp, fd, isSymbolChar, 10)) {
        if (ferror(fd))  READER_IO("while reading symbol");
        if (pad->string == NULL) READER_OOM("while reading symbol");
    }
    return newSymbolWithLength(interp, pad->string, pad->size);
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
Object *readNumberOrSymbol(Object *interp, FILE *fd)
{
#define WHILE_NOS "while reading number or symbol"

    size_t base = 10;

    initPad(scratchpad);

    int ch = streamPeek(fd);
    if (ch == EOF) {
        if (ferror(fd)) READER_IO(WHILE_NOS); else READER_EOF(WHILE_NOS);
    }
    /* skip optional leading sign */
    if (ch == '+' || ch == '-') {
        if (!addCharToPad(scratchpad, fgetc(fd)))  READER_OOM(WHILE_NOS);
        ch = streamPeek(fd);
        if (ch == EOF && ferror(fd))  READER_IO(WHILE_NOS);
    }
    /* Try to read a number in integer format. C notation for hex and octal applies. */
    if (ch == '0') {
        if (!addCharToPad(scratchpad, fgetc(fd)))  READER_OOM(WHILE_NOS);
        ch = streamPeek(fd);
        if (ch == EOF) {
            if (ferror(fd))  READER_IO(WHILE_NOS);
            return flisp_integer_zero;
        }
        base = (ch == 'x' || ch == 'X') ? 16 : 8;
        if (ch == 'x' || ch == 'X' || isDigitChar(ch, base))
            goto read_integer;
        else
            return flisp_integer_zero;
    }
    if (isDigitChar(ch, base))  goto read_integer;
    /* non-numeric character encountered, read a symbol */
    return readSymbol(interp, fd, scratchpad);

read_integer:
    if (!addCharToPad(scratchpad, fgetc(fd)))  READER_OOM(WHILE_NOS);
    ch = readWhile(interp, fd, isDigitChar, base);
    if (ch == EOF) {
        if (ferror(fd)) READER_IO(WHILE_NOS);
        else if (scratchpad->string == NULL)  READER_OOM(WHILE_NOS); }
    if (scratchpad->size == 0) READER_EOF(WHILE_NOS);
    return readInteger(interp, scratchpad);
}

/* Composed object readers */

Object *readExpr(Object *, FILE *);

/** readList - return list from input file
 *
 * @param interp  fLisp interpreter
 * @param fd      open readable file descriptor
 *
 * @returns: list or error
 *
 * @errors: io-error, read-incomplete, range-error, out-of-memory
 */
Object *readList(Object *interp, FILE *fd)
{
    Object *last = nil;
    Object *list = nil;
    bool start = true;

    for (;;) {
        initPad(scratchpad);

        int ch = skipToNext(interp, fd);
        if (ch == EOF) { if (ferror(fd)) READER_IO("while reading list"); else READER_EOF("while reading list"); }
        if (ch == ')')
            return (list == nil) ? nil : flisp_nreverse(interp, list);
        if (ch == '.') {
            ch = streamPeek(fd);
            if (!isSymbolChar(ch, 10)) {
                if (ch == EOF) { if (ferror(fd))  READER_IO("while reading dotted list");  else READER_EOF("while reading dotted list"); }
                if (start)
                    return newError(interp, invalid_read_syntax, nil, "unexpected dot at start of list");

                if ((ch = peekNext(interp, fd)) == ')')
                    return newError(interp, invalid_read_syntax, nil, "expected object at end of dotted list");

                GC_CHECKPOINT;
                GC_TRACE(gcList, list);
                last = readExpr(interp, fd);
                GC_RELEASE;
                if (FLISP_IS_EOF(last))
                    READER_EOF("while reading expression in dotted list");
                if (FLISP_IS_ERR(last))
                    return newError(interp, invalid_value, last, "read error while reading expression in dotted list");
                ch = peekNext(interp, fd);
                if (ch == EOF && ferror(fd))  READER_IO("while reading dotted list");
                if (ch != ')')
                    return newError(interp, invalid_read_syntax, nil, "unexpected object at end of dotted list");
                (void)skipToNext(interp, fd);
                list = flisp_nreverse(interp, *gcList);
                (*gcList)->cdr = last;
                return list;
            }
        } else {
            if (ungetc(ch, fd) == EOF) READER_IO("while reading list");
            GC_CHECKPOINT;
            GC_TRACE(gcList, list);
            GC_TRACE(gcLast, last);
            *gcLast = readExpr(interp, fd);
            if (FLISP_IS_ERR(*gcLast))  GC_RETURN(newError(interp, invalid_value, *gcLast, "read error while reading expression in list"));
            list = newCons(interp, gcLast, gcList);
            GC_CHECK_ERR(list);
            GC_RELEASE;
            last = *gcLast;
        }
        start = false;
    }
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
Object *readUnary(Object *interp, FILE *fd, char *symbol)
{
    if (peekNext(interp, fd) == EOF) {
        if (ferror(fd))
           return newError2(interp, io_error, nil, "I/O error while reading unary ", symbol);
        else
            return newError2(interp, read_incomplete, nil, "unexpected end of stream while reading unary ", symbol);
    }
    GC_CHECKPOINT;
    GC_TRACE(gcSymbol, newSymbol(interp, symbol));
    GC_CHECK_ERR(*gcSymbol);
    GC_TRACE(gcObject, readExpr(interp, fd));
    GC_CHECK_ERR(*gcObject);
    *gcObject = newCons(interp, gcObject, &nil);
    GC_CHECK_ERR(*gcSymbol);
    *gcObject = newCons(interp, gcSymbol, gcObject);
    GC_RETURN(*gcObject);
}

Object *doReaderMacro(Object *interp, FILE *fd)
{
    int ch = streamPeek(fd);
    if (ch == EOF) {
        if (ferror(fd)) READER_IO("while reading reader macro");
        READER_EOF("while reading reader macro");
    }
    fgetc(fd);
    if (ch == '!') {
        while ((ch = fgetc(fd)) != EOF && ch != '\n');
        return nil;
    }
    if (ch == '\'') {
        initPad(scratchpad);
        ch = readWhile(interp, fd, isSymbolChar, 10);
        if (ch == EOF) {
            if (ferror(fd)) READER_IO("while reading symbol");
            else if (scratchpad->string == NULL)  READER_OOM("while reading symbol");
            if (scratchpad->size == 0) READER_EOF("while reading symbol");
        }
        return newSymbolWithLength(interp, scratchpad->string, scratchpad->size);
    }
    if (ch == 'd') {
        initPad(scratchpad);
        ch = readWhile(interp, fd, isDoubleChar, 0);
        if (ch == EOF) {
            if (ferror(fd)) READER_IO("while reading double literal");
            else if (scratchpad->string == NULL)  READER_OOM("while reading double literal");
            if (scratchpad->size == 0) READER_EOF("while reading double literal");
        }
        return readDouble(interp, scratchpad);
    }
    if (ch == 'D') {
        initPad(scratchpad);
        if (!addStringToPad(scratchpad, "0x")) READER_OOM("while reading double hex literal");
        ch = readWhile(interp, fd, isDigitChar, 16);
        if (ch == EOF) {
            if (ferror(fd)) READER_IO("while reading double hex literal");
            else if (scratchpad->string == NULL)  READER_OOM("while reading double hex literal");
            if (scratchpad->size == 0) READER_EOF("while reading double hex literal");
        }
        Object *number = readInteger(interp, scratchpad);
        if (number->type != type_error)
            number->type = type_double;
        return number;
    }
    if (ch & 0x80)
        return newErrorFmt(interp, invalid_read_syntax, nil, "unknown read macro: #0x%02X", ch);
    else
        return newErrorFmt(interp, invalid_read_syntax, nil, "unknown read macro: #%c", ch);
}

/** readExpr - return next lisp sexp object from stream or from interpreter input file
 *
 * @param interp  fLisp interpreter
 * @param fd      open readable file descriptor
 *
 * returns: sexp object or error
 *
 * @errors: io-error, read-incomplete, range-error,
 *     out-of-memory, end-of-file
 */
Object *readExpr(Object *interp, FILE *fd)
{
#define WHILE_EXPR "while reading expression"
    Object *object = nil;
    for (;;) {
        initPad(scratchpad);

        int ch = skipToNext(interp, fd);

        if (ch == EOF) {
            if (ferror(fd))
                READER_IO(WHILE_EXPR);
            else
                return newError(interp, end_of_file, nil, "EOF");
        }
        if (ch == '#') {
            object = doReaderMacro(interp, fd);
            if (object == nil) continue;
            return object;
        }
        if (ch == '\'' || ch == ':')
            return readUnary(interp, fd, "quote");
        if (ch == '`')
            return readUnary(interp, fd, "quasiquote");
        if (ch == ',') {
            ch = streamPeek(fd);
            if (ch == EOF) { if (ferror(fd)) READER_IO(WHILE_EXPR); else READER_EOF(WHILE_EXPR); }
            if (ch == '@') {
                if (!addCharToPad(scratchpad, fgetc(fd))) {
                    if (ferror(fd)) READER_IO(WHILE_EXPR);  else READER_OOM(WHILE_EXPR);
                }
                return readUnary(interp, fd, "splice-unquote");
            }
            else
                return readUnary(interp, fd, "unquote");
        }
        if (ch == '"')
            return readString(interp, fd, scratchpad);
        if (ch == '(')
            return readList(interp, fd);
        if (isSymbolChar(ch, 10) && (ch != '.' || isSymbolChar(streamPeek(fd), 10))) {
            if (ungetc(ch, fd) == EOF) READER_IO(WHILE_EXPR);
            return readNumberOrSymbol(interp, fd);
        }
        else
            if (ch & 0x80)
                return newErrorFmt(interp, invalid_read_syntax, nil, "unexpected character: 0x%02X", ch);
            else
                return newErrorFmt(interp, invalid_read_syntax, nil, "unexpected character: '%c'", ch);
    }
}

/** (read [stream [eofv]]) - read one object from input stream
 * @param interp  fLisp interpreter.
 * @param stream  input stream to read, if nil, use interp input stream.
 * @param eofv    On EOF: if nil return error, else value to return.
 * @returns: Object
 * @errors: invalid-value, io-error, end-of-file
 */
Object *primitiveRead(Object *interp, Object **args, Object **env, size_t nArgs)
{
    Object *eofv = nil;
    FILE *fd = FLISP_STANDARD_INPUT.fd;

    GC_CHECKPOINT;
    if (nArgs) {
        if (FLISP_ARG1 != nil) {
            FLISP_ASSERT(FLISP_ARG1, type_stream, "(read[ stream[ eofv]]) - stream)");
            fd = FLISP_ARG1->stream.fd;
        }
    }
    if (nArgs > 1)  eofv = FLISP_ARG2;

    GC_TRACE(gcEofv, eofv);
    Object *result = readExpr(interp, fd);
    GC_RELEASE;

    if (FLISP_IS_EOF(result) && *gcEofv != nil)  return *gcEofv;
    return result;
}


// EVALUATION /////////////////////////////////////////////////////////////////

/* Scheme-style tail recursive evaluation. evalProgn and evalCond
 * return the object in the tail recursive position to be evaluated by
 * evalExpr. Macros are expanded in-place the first time they are evaluated.
 */

/** (bind p s[ o] ..) - creates or finds symbols and set's their value
 *
 * @param p ..   If p evaluates to nil objects are created in the current environment
 *               otherwise in the global environment.
 *
 * @param s  ..  Symbol to find or create.
 * @param o  ..  Value to bind to symbol.
 *
 * @returns last value bound
 * @errors: wrong-type-argument, errors from evaluating p or v
 */
Object *evalBind(Object *interp, Object **args, Object **env)
{
    if ((*args) == nil || (*args)->type != type_cons || (*args)->cdr->type != type_cons)
        return newError(interp, wrong_number_of_arguments, *args, "(bind p s[ o] ..) expects at least two arguments");

    Object *object = evalExpr(interp, &FLISP_ARG1, env);
    FLISP_CHECK_ERR(object);

    bool globalp = object  != nil;

    GC_CHECKPOINT;
    GC_TRACE(gcEnv, *env);
    GC_TRACE(gcArg, (*args)->cdr);
    GC_TRACE(gcVal, nil);
    for (;(*gcArg) != nil; *gcArg = (*gcArg)->cdr) {
        if ((*gcArg)->type != type_cons)
            GC_RETURN(newError(interp, invalid_value, *args, "(bind p s[ o] ..) - s, arguments are not a proper list"));
        FLISP_ASSERT((*gcArg)->car, type_symbol, "(bind p s[ o] ..) - s");
        if (!gcCollectableObject(interp, (*gcArg)->car))
            return newError(interp, wrong_type_argument, (*gcArg)->car,
                            "(bind p s[ o[ s[ ..]]]) - s is a constant and cannot be redefined");
        object = (*gcArg)->cdr;
        if (object == nil)
            *gcVal = nil;
        else {
            if (object->type != type_cons)
                GC_RETURN(newError(interp, invalid_value, *args, "(bind p s[ o] ..) - o, arguments are not a proper list"));
            *gcVal = evalExpr(interp, &object->car, gcEnv);
            GC_CHECK_ERR(*gcVal);
        }
        GC_CHECK_ERR(envSet(interp, &(*gcArg)->car, gcVal, gcEnv, globalp));
        if (object == nil) break;
        *gcArg = (*gcArg)->cdr;
    }
    GC_RETURN(*gcVal);
}

/** (progn[ ..]) => o: return last value of list */
Object *evalProgn(Object *interp, Object **args, Object **env)
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
    Object *e = evalExpr(interp, gcObject, gcEnv);
    GC_CHECK_ERR(e);
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
Object *evalCond(Object *interp, Object **args, Object **env)
{
#define CLAUSE (*gcArgs)->car
#define PRED   CLAUSE->car
#define ACTION CLAUSE->cdr
    Object *result;
    size_t nArgs = 1;

    GC_CHECKPOINT;
    GC_TRACE(gcArgs, *args);
    while((*gcArgs != nil)) {
        if ((*gcArgs)->type != type_cons)
            GC_RETURN(newErrorI(interp, wrong_type_argument, *gcArgs, "(cond args) - args is not a list, arg ", nArgs, ""));

        if (CLAUSE == nil)  goto next_clause;

        GC_CHECK_ERR(CLAUSE);

        if (CLAUSE->type != type_cons)
            return newError(interp, wrong_type_argument, CLAUSE,
                            "(cond clause ..) - clause is not a list");

        if (ACTION != nil && ACTION->type != type_cons)
            GC_RETURN(newError(interp, wrong_type_argument, CLAUSE, "(cond (pred action) ..) action is not a list"));
        GC_CHECK_ERR(ACTION);
        GC_CHECK_ERR(PRED);

        if (PRED == nil)  goto next_clause;

        result = evalExpr(interp, &PRED, env);
        GC_CHECK_ERR(result);

        if (result == nil) goto next_clause;

        if (ACTION == nil)  GC_RETURN(result);

        result = (ACTION->type == type_cons)
            ? evalProgn(interp, &ACTION, env) : evalExpr(interp, &ACTION, env);
        GC_RETURN(result);
    next_clause:
        (*gcArgs) = (*gcArgs)->cdr;
        nArgs++;
    }
    GC_RELEASE;
    return nil;
}

Object *expandMacro(Object *interp, Object ** macro, Object **args)
{
    GC_CHECKPOINT;
    GC_TRACE(gcBody, (*macro)->closure.body);
    GC_TRACE(gcEnv, newEnv(interp, macro, args));
    GC_CHECK_ERR(*gcEnv);
    GC_TRACE(gcObject, evalProgn(interp, gcBody, gcEnv));
    GC_CHECK_ERR(*gcObject);
    GC_RETURN(evalExpr(interp, gcObject, gcEnv));
}

Object *expandMacroTo(Object *interp, Object ** macro, Object **args)
{
    Object *object = expandMacro(interp, macro, args);

    if (object->type == type_cons)
        return object;

    GC_CHECKPOINT;
    GC_TRACE(gcBody, object);
    GC_TRACE(gcCons, newCons(interp, gcBody, &nil));
    GC_CHECK_ERR(*gcCons);
    GC_TRACE(gcProg, newSymbol(interp, "progn"));
    GC_CHECK_ERR(*gcCons);
    GC_RETURN(newCons(interp, gcProg, gcCons));
}

Object *evalMacroExpand(Object *interp, Object **args, Object **env)
{
    if ((*args)->type != type_cons)
        return evalExpr(interp, args, env);

    GC_CHECKPOINT;
    GC_TRACE(gcArgs, (*args)->cdr);
    GC_TRACE(gcMacro, evalExpr(interp, &(*args)->car, env));
    GC_CHECK_ERR(*gcMacro);
    if ((*gcMacro)->type != type_macro)
        GC_RETURN(*gcMacro);

    GC_RETURN(expandMacro(interp, gcMacro, gcArgs));
}

/* Used to evaluate each argument of an argument list */
Object *evalList(Object *interp, Object **args, Object **env)
{
    /* Note: expand values here */
    if (*args == nil)  return nil;
    if ((*args)->type != type_cons)  return evalExpr(interp, args, env);

    GC_CHECKPOINT;
    GC_TRACE(gcEnv, *env);
    GC_TRACE(gcCdr, (*args)->cdr);
    GC_TRACE(gcObject, evalExpr(interp, &(*args)->car, gcEnv));
    GC_CHECK_OOM(*gcObject);
    *gcCdr = evalList(interp, gcCdr, gcEnv);  /* Note: only CHECK_OOM? shouldn't it be GC_CHECK_ERR? */
    GC_CHECK_OOM(*gcCdr);
    GC_RETURN(newCons(interp, gcObject, gcCdr));
}

#if 0
/* Note: Exceptions are temporary parked */
void x(Object *interp, Object **args, Object **env)
{
    flisp_debug(interp, "trying\n");
    FLISP_INTERP.result = evalExpr(interp, &FLISP_ARG1, env);
    flisp_w_object(FLISP_DEBUG_OUTPUT.fd, FLISP_INTERP.result, true);
}
Object *evalCatch(Object *interp, Object **args, Object **env)
{
    jmp_buf exceptionEnv, *prevEnv;

    prevEnv = FLISP_INTERP.catch;
    FLISP_INTERP.catch = &exceptionEnv;
    FLISP_INTERP.exception = nil;
    GC_CHECKPOINT;
    GC_TRACE(gcTag, FLISP_ARG2);
    if (setjmp(exceptionEnv)) {
        flisp_debug(interp, "catched\n");
    } else {
        x(interp, args, env);
        /* do { */
        /*     FLISP_INTERP.result = evalExpr(interp, &(*args)->car, env); */
        /* } while(0); */
    }
    GC_RELEASE;
    if (FLISP_INTERP.exception->car == *gcTag)
        FLISP_INTERP.result = FLISP_INTERP.exception->cdr->car;
    else {
        flisp_debug(interp, "not matched\n");
        /* do { */
        /*     longjmp(*FLISP_INTERP.catch, 2); */
        /* } while(0); */
    }
    flisp_debug(interp, "result: ");
    flisp_w_object(FLISP_DEBUG_OUTPUT.fd, FLISP_INTERP.result, true);
    FLISP_INTERP.catch = prevEnv;
    return FLISP_INTERP.result;
}
#endif

// Special forms handled by evalExpr.
enum {
    PRIMITIVE_QUOTE,
    PRIMITIVE_BIND,
    PRIMITIVE_PROGN,
    PRIMITIVE_COND,
    PRIMITIVE_LAMBDA,
    PRIMITIVE_MACRO,
    PRIMITIVE_MACROEXPAND,
    PRIMITIVE_CATCH,
};

Object *evalExpr(Object *interp, Object ** object, Object **env)
{
    GC_CHECKPOINT;
    GC_TRACE(gcObject, *object);
    GC_TRACE(gcEnv, *env);

    GC_TRACE(gcFunc, nil);
    GC_TRACE(gcArgs, nil);
    GC_TRACE(gcBody, nil);

    for (;;) {
        if (FLISP_INTERP.countdown && !--FLISP_INTERP.countdown)
            return newError(interp, trap_countdown, *gcObject, "Trap: countdown reached");

        if ((*gcObject)->type == type_symbol)
            GC_RETURN(envLookup(interp, *gcObject, *gcEnv));
        if ((*gcObject)->type != type_cons)
            GC_RETURN(*gcObject);

        *gcFunc = (*gcObject)->car;
        *gcArgs = (*gcObject)->cdr;

        *gcFunc = evalExpr(interp, gcFunc, gcEnv);
        GC_CHECK_ERR(*gcFunc);
        *gcBody = nil;

        if ((*gcFunc)->type == type_lambda) {
            *gcBody = (*gcFunc)->closure.body;
            *gcArgs = evalList(interp, gcArgs, gcEnv);
            GC_CHECK_ERR(*gcArgs);
            *gcEnv = newEnv(interp, gcFunc, gcArgs);
            GC_CHECK_ERR(*gcEnv);
            *gcObject = evalProgn(interp, gcBody, gcEnv);
            GC_CHECK_ERR(*gcArgs);
        } else if ((*gcFunc)->type == type_macro) {
            *gcObject = expandMacroTo(interp, gcFunc, gcArgs);
            GC_CHECK_ERR(*gcObject);
        } else if ((*gcFunc)->type == type_primitive) {
            Primitive *primitive = (*gcFunc)->primitive;
            size_t nArgs = 0;

            switch ((uintptr_t)primitive->eval) {
            case PRIMITIVE_QUOTE:
                if ((*gcArgs)->cdr != nil)
                    GC_RETURN(newError(interp, wrong_number_of_arguments, *gcArgs, "quote expects exactly 1 argument"));
                GC_RETURN((*gcArgs)->car);
            case PRIMITIVE_BIND:
                /* no arg check at all */
                GC_RETURN(evalBind(interp, gcArgs, gcEnv));
            case PRIMITIVE_PROGN:
                /* no arg check at all */
                *gcObject = evalProgn(interp, gcArgs, gcEnv);
                GC_CHECK_ERR(*gcObject);
                break;
            case PRIMITIVE_COND:
                *gcObject = evalCond(interp, gcArgs, gcEnv);
                GC_CHECK_ERR(*gcObject);
                break;
            case PRIMITIVE_LAMBDA:
                /* Note: no arg check at all, is done in newEnv */
                GC_RETURN(newClosure(interp, type_lambda, gcArgs, gcEnv));
            case PRIMITIVE_MACRO:
                /* Note: no arg check at all, is done in newEnv */
                GC_RETURN(newClosure(interp, type_macro, gcArgs, gcEnv));
            case PRIMITIVE_MACROEXPAND:
                nArgs = flisp_list_length(*gcArgs);
                if (nArgs > 2)
                    GC_RETURN(newError(interp, wrong_number_of_arguments, *gcArgs, "macroexpand-1 expects at most 2 arguments"));
                GC_RETURN(evalMacroExpand(interp, gcArgs, gcEnv));
#if 0
            case PRIMITIVE_CATCH:
                GC_RETURN(evalCatch(interp, gcArgs, gcEnv));
#endif
            default:
                *gcArgs = evalList(interp, gcArgs, gcEnv);
                Object *args = *gcArgs;
                for (Object *prev = nil; args != nil; prev = args, args = args->cdr, nArgs++) {
                    if (args->type != type_cons)
                        return newErrorFmt(interp, wrong_type_argument, args,
                                           "(%s args) - args is not a list, arg %d",
                                           primitive->name, nArgs);
                    if (args->car->type == type_moved || args->cdr->type == type_moved)
                        return newErrorFmt(interp, gc_error, args->car,
                                           "(%s args) - arg %d is already disposed off",
                                           primitive->name, nArgs);

                    if (args->car->type == type_values) {
                        if (args->cdr == nil) {
                            /* Special case, if values is at end of parameter list, destructively insert its arguments and restart checking */
                            if (prev == nil)
                                *gcArgs = args = args->car->objects[0];
                            else
                                prev->cdr = args = args->car->objects[0];
                        } else {
                            /* splice in a copy of the values list */
                            if (prev == nil) {
                                *gcArgs = args = cloneList(interp, args->car->objects[0], args->cdr);
                            } else {
                                prev->cdr = args = cloneList(interp, args->car->objects[0], args->cdr);
                            }
                            GC_CHECK_OOM(args);
                        }
                        continue;
                    }

                    if (primitive->argsType != (TypeObject *)nil && args->car->type != primitive->argsType)
                        /* Note: looks very similar to FLISP_ASSERT() */
                        return newErrorFmt(interp, wrong_type_argument, args->car, "(%s args) - arg %d expected %s, got %s",
                                           primitive->name, nArgs+1,
                                           primitive->argsType->type.name->string,
                                           args->car->type->type.name->string
                            );
                }
                if (nArgs < primitive->nMinArgs)
                    return newErrorI(interp, wrong_number_of_arguments, *gcFunc,
                                     "expects at least ", primitive->nMinArgs, " arguments");
                if (nArgs > primitive->nMaxArgs && primitive->nMaxArgs >= 0)
                    return newErrorI(interp, wrong_number_of_arguments, *gcFunc,
                                     "expects at most ", primitive->nMaxArgs, " arguments");
                if (primitive->nMaxArgs < 0 && nArgs % -primitive->nMaxArgs)
                    return newErrorI(interp, wrong_number_of_arguments, *gcFunc,
                                     "expects a multiple of ", -primitive->nMaxArgs, " arguments");

                if (FLISP_INTERP.trace_primitives) {
                    flisp_debug(interp, "trace: (%s", primitive->name);
                    for (*gcObject = *gcArgs; *gcObject != nil; *gcObject = (*gcObject)->cdr) {
                        flisp_debug(interp, " ");
                        GC_CHECK_ERR(flisp_write_object(interp, (*gcObject)->car, t, interp->self.debug));
                    }
                    flisp_debug(interp, ")\n");
                }
                GC_RETURN(primitive->eval(interp, gcArgs, gcEnv, nArgs));
            }
        } else {
            return newError(interp, wrong_type_argument, *gcFunc, "is not a function");
        }
    }
}

Object *primitiveEval(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return evalExpr(interp, &(*args)->car, env);
}

// Write /////////////////////////////////////////////////////////////////////////////////

// Output ////////

// Result assertion //
/** flisp_not_same() - result assertion
 * @param e .. points to test object, normally nil.
 * @param r .. result of an operation
 *
 * @returns: true if result r is not test *e
 *
 * Sideffect: e is set to the err'd object
 */
bool flisp_not_same(Object **e, Object *r)
{
    return (*e != r) && (*e = r);
}
bool flisp_is_error(Object **e, Object *r)
{
    return FLISP_IS_ERR(r) && (*e = r);
}
/** FLISP_WHILE_OK - While ok: result assertion
 * @param F .. operation
 *
 * Breaks from loop if result of F is not equal to test object
 */
/* Usage see below */

/** writeChar - write character to file descriptor
 *
 * @param fd      open writeable file descriptor or NULL
 * @param ch      character to write
 *
 * returns: io-error
 */
Object *writeChar(FILE *fd, char ch)
{
    if (fd == NULL) return nil;

    if(fputc(ch, fd) == EOF)
        return flisp_static_error(io_error, &write_char_failed);
    return nil;
}

/** writeString - write string to file descriptor
 *
 * @param fd      open writeable file descriptor or NULL
 * @param str     string to write
 *
 * returns: io-error
 *
 */
Object *writeString(FILE *fd, char *str)
{
    if (fd == NULL) return nil;

    if(fputs(str, fd) == EOF)
        return flisp_static_error(io_error, &write_string_failed);
    return nil;
}

Object *writeStringReadably(FILE *fd, char *string)
{
    char *escape;
    Object *e = nil;
    if (flisp_not_same(&e, writeChar(fd, '"'))) return e;

    for (; *string; ++string) {
        switch (*string) {
        case '"':
            escape = "\\\"";
            break;
        case '\t':
            escape = "\\t";
            break;
        case '\r':
            escape = "\\r";
            break;
        case '\n':
            escape = "\\n";
            break;
        case '\\':
            escape = "\\\\";
            break;
        default:
            if (flisp_not_same(&e, writeChar(fd, *string))) return e;
            continue;
        }
        if (flisp_not_same(&e, writeString(fd, escape))) return e;
    }
    return writeChar(fd, '"');
}

/* print_*() are helper functions for the writer primitives. We
 * comment them as if they were Lisp primitives, but they aren't:
 * The readably and the stream come optionally from **args, but what
 * to write comes after the nArgs parameter.
 * They return either nil or an error object
 */
/* (print_fmt o[ p[ s]])  */
Object *print_fmt(Object *interp, Object **args, size_t nArgs, char *format, ...)
{
    Object *output = interp->self.output;

    if (nArgs > 2) {
        FLISP_ASSERT(FLISP_ARG3, type_stream, "(print_fmt o[ p[ stream]]) - stream");
        output = FLISP_ARG3;
    }

    int result = 0;
    va_list(fmt_args);
    va_start(fmt_args, format);
    result = vfprintf(output->stream.fd, format, fmt_args);
    va_end(fmt_args);
    if (result < 0)
        return newError2(interp, io_error, output, "print_fmt failed: ", strerror(errno));
    return nil;
}
/* (print-string o[ p[ s]])*/
Object *print_string(Object *interp, Object **args, size_t nArgs, char *string)
{
    Object *output = interp->self.output;

    if (nArgs > 2) {
        FLISP_ASSERT(FLISP_ARG3, type_stream, "(print_string o[ p[ stream]]) - stream");
        output = FLISP_ARG3;
    }
    if (fputs(string, output->stream.fd) == EOF)
        return newError2(interp, io_error, output, "print_string failed: ", strerror(errno));
    return nil;
}
/* Consider (print-string-with-len), using fwrite() */
Object *print_string_readably(Object *interp, Object **args, size_t nArgs, char *string)
{
    Object *output = interp->self.output;

    if (nArgs > 2) {
        FLISP_ASSERT(FLISP_ARG3, type_stream, "(print_string o[ p[ stream]]) - stream");
        output = FLISP_ARG3;
    }
    return writeStringReadably(output->stream.fd, string);
}

/* (write-/type/ obj[ readably[ stream]) */
Object *primitiveWInteger(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_integer, "(write-integer o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_fmt(interp, args, nArgs, "%"PRId64, FLISP_ARG1->value));
    return FLISP_ARG1;
}
Primitive w_i_p = { .name = "write-integer", .nMinArgs = 2, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWInteger };
SimpleObject write_integer = { .type = &type_primitive_obj, .size = 0, .primitive = &w_i_p };


/* (print_strp o[ p[ s]])  print string taking into account readabl p'redicate */
Object *print_strp(Object *interp, Object **args, size_t nArgs, char *string)
{
    bool readably = false;
    if (nArgs > 1) {
        FLISP_CHECK_ERR(FLISP_ARG2);
        readably = FLISP_ARG2 != nil;
    }
    if (readably)
        return print_string_readably(interp, args, nArgs, string);
    else
        return print_string(interp, args, nArgs, string);
}

Object *primitiveWString(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_string, "(write-string o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_strp(interp, args, nArgs, FLISP_ARG1->string));
    return FLISP_ARG1;
}
Primitive w_string_p = { .name = "write-string", .nMinArgs = 2, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWString };
static SimpleObject write_string = { .type = &type_primitive_obj, .size = 0, .primitive = &w_string_p };

Object *primitiveWStr(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_str, "(write-str o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_strp(interp, args, nArgs, ((SimpleObject*)FLISP_ARG1)->str));
    return FLISP_ARG1;
}
Primitive w_str_p = { .name = "write-str", .nMinArgs = 2, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWStr };
static SimpleObject write_str = { .type = &type_primitive_obj, .size = 0, .primitive = &w_str_p };

/* (write-symbol o[ p[ s]])*/
Object *primitiveWSymbol(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_symbol, "(write-symbol o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_string(interp, args, nArgs, FLISP_ARG1->size ? FLISP_ARG1->string : ((SimpleObject*)FLISP_ARG1)->str));
    return FLISP_ARG1;
}
Primitive w_symbol_p = { .name = "write-symbol", .nMinArgs = 2, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWSymbol };
static SimpleObject write_symbol = { .type = &type_primitive_obj, .size = 0, .primitive = &w_symbol_p };

/* (write-primitive o[ p[ s]])*/
Object *primitiveWPrimitive(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_primitive, "(write-primitive o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_fmt(interp, args, nArgs, "#<primitive %s [%zu, %zu] %s>",
                        FLISP_ARG1->primitive->name,
                        FLISP_ARG1->primitive->nMinArgs,
                        FLISP_ARG1->primitive->nMaxArgs,
                        ((SimpleObject*)((TypeObject*)FLISP_ARG1->primitive->argsType)->type.name)->str
                  ));
    return FLISP_ARG1;
}
Primitive w_primitive_p = { .name = "write-primitive", .nMinArgs = 2, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWPrimitive };
static SimpleObject write_primitive = { .type = &type_primitive_obj, .size = 0, .primitive = &w_primitive_p };

Object *primitiveWVector(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_vector, "(write-vector o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_fmt(interp, args, nArgs, "#<vector %zu>", FLISP_ARG1->length));
    return FLISP_ARG1;
}
Primitive w_vector_p = { .name = "write-vector", .nMinArgs = 1, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWVector };
static SimpleObject write_vector = { .type = &type_primitive_obj, .size = 0, .primitive = &w_vector_p };

Object *primitiveWType(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_type, "(write-type o[ p[ s]]) - o");
    if (nArgs > 1 && FLISP_ARG2 != nil)
        return print_string(interp, args, nArgs, ((SimpleObject*)((TypeObject*)FLISP_ARG1)->type.name)->str);
    FLISP_CHECK_ERR(print_fmt(interp, args, nArgs, "#<type %s>", ((SimpleObject*)((TypeObject*)FLISP_ARG1)->type.name)->str+(sizeof("type-"))-1));
    return FLISP_ARG1;
}
Primitive w_type_p = { .name = "write-type", .nMinArgs = 1, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWType };
static SimpleObject write_type = { .type = &type_primitive_obj, .size = 0, .primitive = &w_type_p };

Object *primitiveWInterp(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_interpreter, "(write-interpreter o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_fmt(interp, args, nArgs, "#<interpreter 0X%"PRIXPTR ">", (uintptr_t)interp));
    return FLISP_ARG1;
}
Primitive w_interp_p = { .name = "write-interpreter", .nMinArgs = 1, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWInterp };
static SimpleObject write_interpreter = { .type = &type_primitive_obj, .size = 0, .primitive = &w_interp_p };

Object *print_object(Object *, Object **, size_t, Object *);

/* (write-stream o[ p[ s]])*/
Object *primitiveWStream(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_stream, "(write-stream o[ p[ s]]) - o");
    GC_CHECKPOINT;
    GC_TRACE(gcArgs, *args);
    GC_CHECK_ERR(print_fmt(interp, gcArgs, nArgs, "#<stream 0X%"PRIX64" ", FLISP_ARG1->stream.fd));
    GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, FLISP_ARG1->stream.path));
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ">"));
    GC_RETURN((*gcArgs)->car);
}
Primitive w_stream_p = { .name = "write-stream", .nMinArgs = 1, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWStream };
static SimpleObject write_stream = { .type = &type_primitive_obj, .size = 0, .primitive = &w_stream_p };

/* (write-extension o[ p[ s]])*/
Object *primitiveWExtension(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_extension, "(write-extension o[ p[ s]]) - o");
    GC_CHECKPOINT;
    GC_TRACE(gcArgs, *args);
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, "#<extension "));
    GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcArgs)->car->extension.name));
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ", "));
    GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcArgs)->car->extension.version));
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ">"));
    GC_RETURN((*gcArgs)->car);
}
Primitive w_extension_p = { .name = "write-extension", .nMinArgs = 1, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWExtension };
static SimpleObject write_extension = { .type = &type_primitive_obj, .size = 0, .primitive = &w_extension_p };

/* (write-cons o[ p[ s]])*/
Object *primitiveWCons(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_cons, "(write-cons o[ p[ s]]) - o");
    GC_CHECKPOINT;
    GC_TRACE(gcArgs, *args);
    GC_TRACE(gcCons, FLISP_ARG1);
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, "("));
    GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcCons)->car));
    while ((*gcCons)->cdr != nil) {
        *gcCons = (*gcCons)->cdr;
        if ((*gcCons)->type == type_cons) {
            GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, " "));
            GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcCons)->car));
        } else {
            GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, " . "));
            GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, *gcCons));
            break;
        }
    }
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ")"));
    GC_RETURN((*gcArgs)->car);
}
Primitive w_cons_p = { .name = "write-cons", .nMinArgs = 1, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWCons };
static SimpleObject write_cons = { .type = &type_primitive_obj, .size = 0, .primitive = &w_cons_p };

/* (write-closure o[ p[ s]])*/
Object *primitiveWClosure(Object *interp, Object **args, Object **env, size_t nArgs)
{
    Object *closure = FLISP_ARG1;
    if (closure->type != type_lambda && closure->type != type_macro)
        return newErrorFmt(interp, wrong_type_argument, closure,
                           "(write-closure o[ p[ s]]) - o expected type-lambda or type-macro, got %s",
                           ((SimpleObject*)(closure)->type->type.name)->str);
    GC_CHECKPOINT;
    GC_TRACE(gcArgs, *args);
    GC_CHECK_ERR(print_fmt(interp, gcArgs, nArgs, "#<%s ",
                         ((SimpleObject*)((TypeObject*)closure->type)->type.name)->str+(sizeof("type-"))-1));
    GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, closure->closure.params));
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ">"));
    GC_RETURN((*gcArgs)->car);
}
Primitive w_closure_p = { .name = "write-closure", .nMinArgs = 1, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWClosure };
static SimpleObject write_closure = { .type = &type_primitive_obj, .size = 0, .primitive = &w_closure_p };

/* (write-env o[ p[ s]])*/
Object *primitiveWEnv(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_env, "(write-env o[ p[ s]]) - o");
    GC_CHECKPOINT;
    GC_TRACE(gcArgs, *args);
    GC_TRACE(gcSymbols, FLISP_ARG1->env.vars);
    GC_TRACE(gcValues, FLISP_ARG1->env.vals);

    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, "<#Env "));
    while (*gcSymbols != nil) {
        GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcSymbols)->car));
        GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, " "));
        GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcValues)->car));
        if ((*gcSymbols)->cdr != nil) GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ",  "));
        *gcSymbols = (*gcSymbols)->cdr;
        *gcValues = (*gcValues)->cdr;
    }
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ">"));
    GC_RETURN((*gcArgs)->car);
}
Primitive w_env_p = { .name = "write-env", .nMinArgs = 1, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWEnv };
static SimpleObject write_env = { .type = &type_primitive_obj, .size = 0, .primitive = &w_env_p };

/* (write-error o[ p[ s]])*/
Object *primitiveWError(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_error, "(write-error o[ p[ s]]) - o");
    bool readably = nArgs > 1 && FLISP_ARG2 != nil;
    GC_CHECKPOINT;
    GC_TRACE(gcError, FLISP_ARG1);
    GC_TRACE(gcArgs, nil);
    if (nArgs > 2) {
        *gcArgs = newCons(interp, &FLISP_ARG3, &nil);
        *gcArgs = newCons(interp, &nil, gcArgs);
        *gcArgs = newCons(interp, gcError, gcArgs);
    } else {
        *gcArgs = newCons(interp, gcError, &nil);
        nArgs = 1;
    }
    if (readably) {
        GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, "#<error "));
        GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcError)->error.type));
        GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ": "));
        GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcError)->error.message));
        GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ", "));
        GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcError)->error.culprit));
        GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ">"));
    } else {
        GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, "error:"));
        GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcError)->error.type));
        GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ": "));
        GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcError)->error.message));
        if ((*gcError)->error.culprit != nil) {
            GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ": '"));
            GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcError)->error.culprit));
            GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, "'\n"));
        } else {
            GC_CHECK_ERR(*gcError);
        }
    }
    /* Note: if we return the error object, we get it double printed */
    //GC_RETURN(*gcError);
    return nil;
}
Primitive w_error_p = { .name = "write-error", .nMinArgs = 1, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWError };
static SimpleObject write_error = { .type = &type_primitive_obj, .size = 0, .primitive = &w_error_p };

/** (write o[ p[ fd]]) - write object
 *
 * @param o   Object to write.
 * @param p   If not nil escape strings.
 * @param fd  Stream to write to, else output stream.
 *
 * @returns: o
 *
 * throws: wrong-number-of-arguments, io-error, gc-error
 *
 * If no stream is specified the interpreters output file descriptor is used.
 * If the interpreters output file descriptor is NULL, no output is written.
 */
Object *print_object_fallback(Object *object, Object *output)
{
    /* Note: the following assumes that type names are type-str, i.e. constants. */
    if (object->size)
        fprintf(output->stream.fd, "#<%s, %zu, %zu>",
                ((SimpleObject*)object->type->type.name)->str+(sizeof("type-"))-1,
                object->length,
                object->size
            );
    else
        fprintf(output->stream.fd, "#<%s>", ((SimpleObject*)object->type->type.name)->str+(sizeof("type-"))-1);
    return object;
}
Object *primitiveWrite(Object *interp, Object **args, Object **env, size_t nArgs)
{
    Object *output = interp->self.output;
    Object *writer = FLISP_ARG1->type->type.write;

    if (nArgs > 2) output = FLISP_ARG3;
    if (output == nil) return nil;
    FLISP_ASSERT(output, type_stream, "(write o [p [fd]]) - fd");
    if (output->stream.fd == NULL)
        return newError(interp, invalid_value, nil, "(write o[ p [fd]) - fd already closed");
    if (writer == nil)
        return print_object_fallback(FLISP_ARG1, output);

    GC_CHECKPOINT;
    GC_TRACE(gcObject, FLISP_ARG1);
    /* Note: temporary only allow primitives, later we want lambda's also. */
    FLISP_ASSERT(writer, type_primitive, "(w o[ p[ s]]) - type writer of o");
    GC_CHECK_ERR(writer->primitive->eval(interp, args, &interp->self.global, nArgs));
    GC_RETURN(*gcObject);
}
Object *print_object(Object *interp, Object **args, size_t nArgs, Object *object)
{
    if (nArgs > 2) {
        if (FLISP_ARG3 == nil) return nil;
        FLISP_ASSERT(FLISP_ARG3, type_stream, "(print_object o[ p[ s]]) - s");
        if (FLISP_ARG3->stream.fd == NULL)
            return newErrorFmt(interp, invalid_value, nil, "(print_object(o[ p[ s]]) - s already closed");
    }
    Object *newArgs;
    FLISP_CHECK_ERR(newArgs = newCons(interp, &object, &(*args)->cdr));
    return primitiveWrite(interp, &newArgs, &interp->self.global, nArgs);
}
/** flisp_write_object - format and write object to file descriptor
 *
 * @param interp    fLisp interpreter
 * @param object    object to be serialized
 * @param readably  if not nil write in a format which can be read back
 * @param stream    open writeable stream, or nil to write to interp output
 *
 * @returns nil on success, io-error, gc-error, oom-error
 *
 */
Object* flisp_write_object(Object *interp, Object *object, Object *readably, Object *stream)
{
    if (stream == nil) return nil;
    FLISP_ASSERT(stream, type_stream, "flisp_write_object(interp, object, readably, stream) - stream");
    if (stream->stream.fd == NULL)
        return newErrorFmt(interp, invalid_value, nil, "flisp_write_object(object, readaybly, stream) - stream already closed");
    GC_CHECKPOINT;
    GC_TRACE(gcObject, object);
    GC_TRACE(gcReadably, readably);
    GC_TRACE(gcArgs, newCons(interp, &stream, &nil));
    GC_CHECK_ERR(*gcArgs);
    GC_CHECK_ERR(*gcArgs = newCons(interp, gcReadably, gcArgs));
    GC_CHECK_ERR(*gcArgs = newCons(interp, gcObject, gcArgs));
    GC_RETURN(print_object(interp, gcArgs, 3, *gcObject));
}

// PRIMITIVES /////////////////////////////////////////////////////////////////

Object *primitiveNullP(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG1 == nil) ? t : nil;
}
Object *primitiveTypeOf(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return (Object *)(FLISP_ARG1->type);
}
Object *primitiveConsP(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG1->type == type_cons) ? t : nil;
}
Object *primitiveIntern(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newSymbol(interp, FLISP_ARG1->string);
}
/* Note: obsolete with (elements symbol) */
Object *primitiveSymbolName(Object *interp, Object **args, Object **env, size_t nArgs)
{
    GC_CHECKPOINT;
    GC_TRACE(gcFirst, FLISP_ARG1);
    GC_RETURN(newString(interp, (*gcFirst)->string));
}

/** (same o1 o2) - object comparison
 *
 * @param o1  object
 * @param o2  object
 *
 * @returns t if o1 is the same object as o2, nil otherwise.
 */
Object *primitiveSame(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG1 == FLISP_ARG2) ? t : nil;
}

Object *primitiveCar(Object *interp, Object **args, Object **env, size_t nArgs)
{
    if (FLISP_ARG1 == nil)
        return nil;
    FLISP_ASSERT(FLISP_ARG1, type_cons, "(car o) - o");
    return FLISP_ARG1->car;
}
Object *primitiveCdr(Object *interp, Object **args, Object **env, size_t nArgs)
{
    if (FLISP_ARG1 == nil)
        return nil;
    FLISP_ASSERT(FLISP_ARG1, type_cons, "(cdr o) - o");
    return FLISP_ARG1->cdr;
}
Object *primitiveObjectSize(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG1->size);
}
Object *primitiveObjectLength(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG1->length);
}
/** (vector[ ..]) => v */
Object *primitiveVector(Object *interp, Object **args, Object **env, size_t nArgs)
{
    Object *vector = flisp_ext_obj(interp, type_vector, args, nArgs, 0);
    CHECK_OOM(vector);
    return vector;
}
Object *primitiveValues(Object *interp, Object **args, Object **env, size_t nArgs)
{
    Object *values = flisp_ext_obj(interp, type_values, &nil, 1, 0);
    CHECK_OOM(values);
    values->objects[0] = *args;
    return values;
}

Object *firstConsElements(Object *interp, size_t n, Object *cons)
{
    GC_CHECKPOINT;
    GC_TRACE(gcCons, cons);
    GC_TRACE(gcList, nil);
    for (;n-- && (*gcCons)->type == type_cons; (*gcCons) = (*gcCons)->cdr) {
        *gcList = newCons(interp, &(*gcCons)->car, gcList);
        GC_CHECK_OOM(*gcList);
    }
    GC_RETURN(flisp_nreverse(interp, *gcList));
}
/** (elements object[ start[ end]]) => list of contained objects, sub-array of string, string range */
Object *primitiveElements(Object *interp, Object **args, Object **env, size_t nArgs)
{
    int64_t i = 0, j,  end;

    Object *o = FLISP_ARG1;
    TypeObject *t = o->type;
    if (t == type_string || t == type_symbol)
        end = o->size - 1;
    else if (t == type_cons)
        end = -1; // Later: end = flisp_list_length(o);
    else if (o->size == 0) // simple object
        return nil;
    else
        end = o->length;

    j = end;

    if (nArgs > 1) {
        FLISP_ASSERT(FLISP_ARG2, type_integer, "(elements object[ start[ end]]) - start");
        i = FLISP_ARG2->value;
        if (t == type_cons)  j = end = flisp_list_length(o);
        if (i < 0)  i += end;
        if (i > end) i = end;
    }
    if (nArgs > 2) {
        j = (FLISP_ARG3->value);
        if (j < 0) j += end;
    }

    if (i < 0) i = 0;
    if (end != -1) { /* list w/o end parameter */
        if (i > j)
            return newErrorFmt(interp, range_error, FLISP_ARG2, "(elements object[ start[ end]]) - start > end: [%lu, %lu)", i, j);
        if (j < 0) j = 0;
        if (j > end) j = end;
    }

    if (i == j) {
        if (t == type_string || t == type_symbol)
            return flisp_empty_string;
        return nil;
    }
    if (t == type_string || t == type_symbol)
        return newStringWithLength(interp, &FLISP_ARG1->string[i], j-i);

    if (t == type_cons) {
        j -= i;
        while (i-- && o->type == type_cons)
            o = o->cdr;
        if (nArgs <= 2) return o;
        return firstConsElements(interp, j, o);
    }

    GC_CHECKPOINT;
    GC_TRACE(gcObject, FLISP_ARG1);
    GC_TRACE(gcList, nil);
    while (i < j)
        *gcList = newCons(interp, &(*gcObject)->objects[i++], gcList);
    GC_RETURN(flisp_nreverse(interp, *gcList));
}
Object *primitiveCons(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return flisp_ext_obj(interp, type_cons, args, 2, 0);
}
Object *primitiveNreverse(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return flisp_nreverse(interp, (*args)->car);
}
Object *primitiveError(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_symbol, "(error type message[ object]) - result");
    FLISP_ASSERT(FLISP_ARG2, type_string, "(error type message[ object]) - message");

    return flisp_ext_obj(interp, type_error, args, 3, 0);
}

#if 0
__attribute__((noreturn))
Object *primitiveThrow(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_INTERP.exception = *args;
    do {
        longjmp(*FLISP_INTERP.catch, 2);
    } while(0);
}
#endif

// Integer Math //////

Object *integerAdd(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG1->value + FLISP_ARG2->value);
}
Object *integerSubtract(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG1->value - FLISP_ARG2->value);
}
Object *integerMultiply(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG1->value * FLISP_ARG2->value);
}
Object *integerDivide(Object *interp, Object **args, Object **env, size_t nArgs)
{
    if (FLISP_ARG2->value == 0)
        return newError(interp, arithmetic_error, FLISP_ARG2, "(i/ q d) - d: division by zero");

    return newInteger(interp, FLISP_ARG1->value / FLISP_ARG2->value);
}
Object *integerMod(Object *interp, Object **args, Object **env, size_t nArgs)
{
    if (FLISP_ARG2->value == 0)
        return newError(interp, arithmetic_error, FLISP_ARG2, "(i%% q d) - d: division by zero");

    return newInteger(interp, FLISP_ARG1->value % FLISP_ARG2->value);
}

/* Note: only (zerop not <) are needed:
 *
 * a = b .. (zerop (- a b))
 * a > b .. b < a
 * a <= b .. (not (b > a))
 * ...
 */
Object *integerZerop(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG1->value) ? nil : t;
}
Object *integerEqual(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG1->value == FLISP_ARG2->value) ? t : nil;
}
Object *integerLess(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG1->value < FLISP_ARG2->value) ? t : nil;
}
Object *integerLessEqual(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG1->value <= FLISP_ARG2->value) ? t : nil;
}
Object *integerGreater(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG1->value > FLISP_ARG2->value) ? t : nil;
}
Object *integerGreaterEqual(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return (FLISP_ARG1->value >= FLISP_ARG2->value) ? t : nil;
}
// Integer bit operations //////
/* Note: only Xor and Not are needed, see De morgan */
Object *integerAnd(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG1->value & FLISP_ARG2->value);
}
Object *integerOr(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG1->value | FLISP_ARG2->value);
}
Object *integerXor(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG1->value ^ FLISP_ARG2->value);
}
Object *integerShiftLeft(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG1->value << FLISP_ARG2->value);
}
Object *integerShiftRight(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, FLISP_ARG1->value >> FLISP_ARG2->value);
}
Object *integerNot(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, ~FLISP_ARG1->value);
}
/** (ifmt i [b [m [p [l]]]]) - format integer as ascii string.
 *
 * @param i .. Integer
 * @param b .. Conversion base, default 10.
 * @param m .. Character map to use, if missing or nil, use uppercase for digits > 9.
 * @param p .. Left padding character, first character of given string, default space.
 * @param l .. Padding length/prefix specifier, if 0 a single '0' is put before the
 *             first padding character.
 *
 * To achieve 0x1A: (ifmt 26 16 nil "x" 0)
 */
Object *integerFmt(Object *interp, Object **args, Object **env, size_t nArgs)
{
    int64_t base = 10;
    char *map = flisp_integer_char_map, pad_char = ' ', *i;
    size_t length = -1;

    FLISP_ASSERT(FLISP_ARG1, type_integer, "(ifmt i [b [m [p [l]]]]) - i");
    if (nArgs > 1) {
        FLISP_ASSERT(FLISP_ARG2, type_integer, "(ifmt i [b [m [p [l]]]]) - b");
        base = FLISP_ARG2->value;
    }
    if (nArgs > 2 && FLISP_ARG3 != nil) {
        FLISP_ASSERT(FLISP_ARG3 , type_string, "(ifmt i [b [u [p [l]]]]) - m");
        if ((FLISP_ARG3->size - 1) < base)
            return newError(interp, invalid_value, FLISP_ARG3, "(ifmt i [b [u [p [l]]]]) - m, map has less characters then base");
        map = FLISP_ARG3->string;
    }
    if (nArgs > 3) {
        FLISP_ASSERT(FLISP_ARG4 , type_string, "(ifmt i [b [u [p [l]]]]) - p");
        pad_char = FLISP_ARG4->string[0];
    }
    if (nArgs > 4) {
        FLISP_ASSERT(FLISP_ARG5, type_integer, "(ifmt i [b [u [p [l]]]]) - l");
        length = FLISP_ARG5->value;
    }
    i = fmtInteger(FLISP_ARG1->value, base, map, pad_char, length);

    if (i == NULL)
        return newError(interp, out_of_memory, nil, "(ifmt ..) - failed to allocate format pad");
    if (i == (char *) -1)
        return newErrorI(interp, range_error, FLISP_ARG2,
                         "(ifmt i [b [u [p [l]]]]) - b must be within [2, 36]: ", FLISP_ARG2->value, "");
    if (i == (char *) -2)
        return newErrorI(interp, range_error, FLISP_ARG5,
                         "(ifmt i [b [u [p [l]]]]) - l must be within [1, 67]: ", FLISP_ARG5->value, "");
    return newString(interp, i);
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
Object *file_outputMemStream(Object *interp)
{
    Object *object = newStreamObject(interp, NULL, ">STRING");
    if (NULL == (object->stream.fd = open_memstream(&object->stream.buf, &object->stream.len)))
        return newError2(interp, out_of_memory, nil, "failed to open_memstream() for memory output stream: ", strerror(errno));
    fflush(object->stream.fd); // Note: sets stream->buf and stream->len to initial values.
    return object;
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
Object *file_inputMemStream(Object *interp, char *string)
{
    size_t len = strlen(string);
    char *buf = malloc(len+1);
    if (NULL == buf)
        return newError2(interp, out_of_memory, nil, "failed to allocate string buffer for memory input stream: ", strerror(errno));
    strncpy(buf, string, len);
    buf[len] = '\0';
    Object *object = newStreamObject(interp, NULL, "<STRING");
    CHECK_OOM(object);
    object->stream.buf = buf;
    object->stream.len = len;
    if (NULL == (object->stream.fd = fmemopen(object->stream.buf, object->stream.len, "r"))) {
        free(object->stream.buf);
        return newError2(interp, out_of_memory, nil, "failed to fmemopen string for memory input stream: ", strerror(errno));
    }
    return object;
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
Object *file_fopen(Object *interp, char *path, char* mode) {
    FILE * fd;
    Object *stream, *err = nil;

    if (strcmp("<", mode) == 0) {
        if (nil == (stream = file_inputMemStream(interp, path)))
            return newError2(interp, io_error, nil, "failed to open string as memory input stream: ", strerror(errno));
        return stream;
    }
    if (strcmp(">", mode) == 0) {
        if (nil == (stream = file_outputMemStream(interp)))
            return newError2(interp, io_error, nil, "failed to open memory output stream: ", strerror(errno));
        return stream;
    }
    char c = path[0];
    if (c == '<' || c == '>') {
        char *end;
        errno = 0;
        long d = strtol(&path[1], &end, 0);
        if (errno || *end != '\0' || d < 0 || d > _POSIX_OPEN_MAX)
            return newError2(interp, invalid_value, nil, "invalid I/O stream number: %s", &path[1]);
        if (NULL == (fd = fdopen((int)d, c == '<' ? "r" : "a")))
            return newErrorI(interp, io_error, nil, "failed to open I/O stream ", d, c == '<' ? "for reading" : "for writing");
    } else {
        flisp_debug(interp, "fopen(%s, %s)\n", path, mode);
        fd = fopen(path, mode);
        if (fd == NULL) {
            flisp_debug(interp, "fopen() failed:%d: %s\n", errno, strerror(errno));
            switch(errno) {
            case EACCES:  err = permission_denied; break;
            case EEXIST:  err = file_exists; break;
            case ENOENT:  err = not_found; break;
            case EISDIR:  err = is_directory; break;
            default:      err = io_error; break;
            }
            return newErrorFmt(interp, err, nil, "failed to open file '%s' with mode '%s': %s", path, mode, strerror(errno));
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
Object *primitiveFopen(Object *interp, Object **args, Object **env, size_t nArgs)
{
    char *path = strdup(FLISP_ARG1->string);
    char *mode = "r";

    if (nArgs > 1)
        mode = strdup(FLISP_ARG2->string);

    Object *stream = file_fopen(interp, path, mode);
    free(path);
    if (nArgs > 1)  free(mode);
    CHECK_OOM(stream);
    return stream;
}

/** file_fclose() - closes stream object
 *
 * @param interp  fLisp interpreter
 * @param stream  stream to close
 *
 * returns: 0 on success, else errno of fclose()
 */
int file_fclose(Object *interp, Object *object)
{
    fflush(object->stream.fd);
    int result = fclose(object->stream.fd) ? errno : 0;
    object->stream.fd = NULL;
    if (object->stream.buf != NULL) {
        free(object->stream.buf);
        object->stream.buf = NULL;
        object->stream.len = 0;
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
Object *primitiveFclose(Object *interp, Object**args, Object **env, size_t nArgs)
{
    int result;

    if (FLISP_ARG1->stream.fd == NULL)
        return newError(interp, invalid_value, FLISP_ARG1, "(fclose stream) - stream already closed");
    if ((result = file_fclose(interp, FLISP_ARG1)))
        return newError2(interp, io_error, FLISP_ARG1, "(fclose stream) - failed to close: ", strerror(result));
    return nil;
}

Object *primitiveFinfo(Object *interp, Object **args, Object **env, size_t nArgs)
{
    GC_CHECKPOINT;
    GC_TRACE(gcObject, (FLISP_ARG1->stream.fd == NULL) ?
             nil : newInteger(interp, (int64_t)fileno(FLISP_ARG1->stream.fd)));
    *gcObject = newCons(interp, gcObject, &nil);
    GC_TRACE(gcBuffer, (FLISP_ARG1->stream.buf == NULL) ? nil : newString(interp, FLISP_ARG1->stream.buf));
    *gcObject = newCons(interp, gcBuffer, gcObject);
    GC_RETURN(newCons(interp, &FLISP_ARG1->stream.path, gcObject));
}

/* Strings */

// (string-append s a)
Object *stringAppend(Object *interp, Object **args, Object **env, size_t nArgs)
{
    int len1 = FLISP_ARG1->size - 1;
    int len2 = FLISP_ARG2->size - 1;
    char *new = strdup(FLISP_ARG1->string);
    new = realloc(new, len1 + len2 + 1);
    if (new == NULL)
        return newError(interp, out_of_memory, FLISP_ARG2,
                        "(string-append s a) - failed to allocate memory for a");
    memcpy(new + len1, FLISP_ARG2->string, len2);
    new[len1 + len2] = '\0';

    Object * str = newStringWithLength(interp, new, len1 + len2);
    free(new);

    return str;
}

// (string-compare s1 s2)
Object *stringCompare(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, strcmp(FLISP_ARG1->string, FLISP_ARG2->string));
}

Object *primitiveLoadExtension(Object *interp, Object **args, Object **env, size_t nArgs)
{
    Object *extensions, *name = FLISP_ARG1;

    for (extensions = FLISP_INTERP.extensions; extensions != nil; extensions = extensions->cdr) {
        if (extensions->car->type == type_extension
            && strcmp(extensions->car->extension.name->string, name->string) == 0) {
            if (extensions->car->extension.version != nil)
                return extensions->car->extension.version;
            GC_CHECKPOINT;
            GC_TRACE(gcExts, extensions);
            if (!(*gcExts)->car->extension.init(interp, extensions->car))
                GC_RETURN(newError(interp, invalid_value, extensions->car, "(extension name) - failed to load"));
            GC_RETURN((*gcExts)->car->extension.version);
        }
    }
    return nil;
}
Object *flisp_register_extension(Object *interp, char *name, ExtensionInit init)
{
    GC_CHECKPOINT;
    GC_TRACE(gcObject, newExtension(interp, name, init));
    GC_CHECK_OOM(*gcObject);
    *gcObject = newCons(interp, gcObject, &FLISP_INTERP.extensions);
    GC_CHECK_OOM(*gcObject);
    GC_RETURN(FLISP_INTERP.extensions = *gcObject);
}

// Interpreter introspection and configuration

/** (interp) - return interpreter object */
Object *primitiveInterp(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return interp;
}
/** (env) - return current environment */
Object *primitiveEnv(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return *env;
}
/** (interp-version - return interpreter version  */
Object *primitiveInterpVersion(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newString(interp, FL_NAME " " FL_VERSION);
}
/** (interp-gc - invoke the garbage collector  */
Object *primitiveInterpGc(Object *interp, Object **args, Object **env, size_t nArgs)
{
    /* Note: let the garbage collector return its statistics as property list */
    /* Note: let the garbage collector receive an integer, indicating how much to increase/remove/set the memory capacity */
    gc(interp);
    return nil;
}

/** (interp-input [ stream]]) - query or set interpreter input stream */
Object *primitiveInterpInput(Object *interp, Object **args, Object **env, size_t nArgs)
{
    if (nArgs)
        FLISP_INTERP.input = FLISP_ARG1;
    return FLISP_INTERP.input;
}
/** (interp-output [ stream]]) - query or set interpreter output stream */
Object *primitiveInterpOutput(Object *interp, Object **args, Object **env, size_t nArgs)
{
    if (nArgs)
        FLISP_INTERP.output = FLISP_ARG1;
    return FLISP_INTERP.output;
}
/** (interp-debug [ stream]]) - query or set interpreter debug stream */
Object *primitiveInterpDebug(Object *interp, Object **args, Object **env, size_t nArgs)
{
    if (nArgs)
        FLISP_INTERP.debug = FLISP_ARG1;
    return FLISP_INTERP.debug;
}
/** (interp-print [ p]]) - query or set print flag */
Object *primitiveInterpPrint(Object *interp, Object **args, Object **env, size_t nArgs)
{
    if (nArgs)
        FLISP_INTERP.print = FLISP_ARG1 != nil;
    return FLISP_INTERP.print ? t : nil;
}
/** (interp-gc-always [ p]]) - query or set gc stress flag */
Object *primitiveInterpGcAlways(Object *interp, Object **args, Object **env, size_t nArgs)
{
    if (nArgs)
        FLISP_INTERP.gc_always = FLISP_ARG1 != nil;
    return FLISP_INTERP.gc_always ? t : nil;
}
/** (interp-trace-read [ p]]) - query or set trace-read flag */
Object *primitiveInterpTraceRead(Object *interp, Object **args, Object **env, size_t nArgs)
{
    if (nArgs)
        FLISP_INTERP.trace_read = FLISP_ARG1 != nil;
    return FLISP_INTERP.trace_read ? t : nil;
}
/** (interp-trace-primitives [ p]]) - query or set trace-primitives flag */
Object *primitiveInterpTracePrimitives(Object *interp, Object **args, Object **env, size_t nArgs)
{
    if (nArgs)
        FLISP_INTERP.trace_primitives = FLISP_ARG1 != nil;
    return FLISP_INTERP.trace_primitives ? t : nil;
}
int64_t flisp_countdown(Object *interp, int64_t countdown)
{
    int64_t prev = FLISP_INTERP.countdown;
    FLISP_INTERP.countdown = countdown;
    return prev;
}
/** (interp-countdown [ i]]) - query or set countdown counter */
Object *primitiveInterpCountdown(Object *interp, Object **args, Object **env, size_t nArgs)
{
    int64_t new = FLISP_INTERP.countdown;
    if (nArgs)
        new = FLISP_ARG1->value;
    return newInteger(interp, flisp_countdown(interp, new));
}

// MAIN ///////////////////////////////////////////////////////////////////////

Object *flisp_register_type(Object *interp, char *name, TypeObject *type, Object *writer)
{
    type->type.write = writer;
    GC_CHECKPOINT;
    GC_TRACE(gcType, (Object *)type);
    GC_TRACE(gcSymbol, newSymbol(interp, name));
    GC_CHECK_OOM(*gcSymbol);
    GC_CHECK_OOM(envSet(interp, gcSymbol, gcType, &FLISP_INTERP.global, true));
    Object *cons = newCons(interp, gcSymbol, &FLISP_INTERP.symbols);
    GC_RELEASE;
    FLISP_CHECK_ERR(cons);
    FLISP_INTERP.symbols = cons;
    return nil;
}

/* Bind symbol to value globally, if value is NULL pointer bind it to itself */
Object *flisp_register_constant(Object *interp, Object *symbol, Object *value)
{
    if (value == NULL)
        value = symbol;
    GC_CHECKPOINT;
    GC_TRACE(gcSymbol, symbol)
    GC_CHECK_OOM(envSet(interp, gcSymbol, &value, &FLISP_INTERP.global, true));
    Object *cons = newCons(interp, gcSymbol, &FLISP_INTERP.symbols);
    GC_RELEASE;
    FLISP_CHECK_ERR(cons);
    FLISP_INTERP.symbols = cons;
    return nil;
}

Object *flisp_register_primitive(Object *interp, char *name,
                                    int min_args, int max_args, TypeObject *args_type,
                                    LispEval func)
{
    Primitive *primitive = malloc(sizeof(Primitive));
    if (primitive == NULL)
        return flisp_static_error(out_of_memory, &object_oom_message);
    primitive->name = strdup(name);
    if (primitive->name == NULL)
        return flisp_static_error(out_of_memory, &object_oom_message);
    primitive->nMinArgs = min_args;
    primitive->nMaxArgs = max_args;
    primitive->argsType = args_type;
    primitive->eval = func;

    GC_CHECKPOINT;
    GC_TRACE(gcSymbol, newSymbol(interp, primitive->name));
    GC_CHECK_OOM(*gcSymbol);
    Object *p = newPrimitive(interp, primitive);
    GC_CHECK_OOM(p);
    GC_TRACE(gcP, p);
    Object *e = envSet(interp, gcSymbol, gcP, &FLISP_INTERP.global, true);
    GC_RELEASE;
    FLISP_CHECK_ERR(e);
    return *gcP;
}

Object *flisp_core_init(Object *interp, Object *extension)
{
    if (extension->extension.version != nil)  return extension->extension.version;
    GC_CHECKPOINT;
    GC_TRACE(gcExt, extension);
    Object *e = nil;
    do {
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "quote",                  1,  1, (TypeObject *)nil, (LispEval) PRIMITIVE_QUOTE));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "bind",                   0, -1, (TypeObject *)nil, (LispEval) PRIMITIVE_BIND  /* special form */ ));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "progn",                  0, -1, (TypeObject*)nil, (LispEval) PRIMITIVE_PROGN /* special form */ ));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "cond",                   0, -1, (TypeObject*)nil, (LispEval) PRIMITIVE_COND  /* special form */ ));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "lambda",                 1, -1, (TypeObject*)nil, (LispEval) PRIMITIVE_LAMBDA /* special form */ ));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "macro",                  1, -1, (TypeObject*)nil, (LispEval) PRIMITIVE_MACRO  /* special form */ ));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "macroexpand-1",          1,  2, (TypeObject*)nil, (LispEval) PRIMITIVE_MACROEXPAND /* special form */ ));
#if 0
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "catch",                  2,  2, (TypeObject*)nil, (LispEval) PRIMITIVE_CATCH  /*special form */ ));
#endif
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "null",                   1,  1, (TypeObject*)nil,            primitiveNullP));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "type-of",                1,  1, (TypeObject*)nil,            primitiveTypeOf));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "consp",                  1,  1, (TypeObject*)nil,            primitiveConsP));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "nreverse",               1,  1, (TypeObject*)nil,            primitiveNreverse));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "intern",                 1,  1, type_string,    primitiveIntern));
        /* Note: can be replaced by (elements symbol) */
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "symbol-name",            1,  1, type_symbol,    primitiveSymbolName));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "same",                   2,  2, (TypeObject*)nil,            primitiveSame));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "car",                    1,  1, (TypeObject*)nil,            primitiveCar  /* Note: nil|cons */ ));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "cdr",                    1,  1, (TypeObject*)nil,            primitiveCdr  /* Note: nil|cons */ ));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "cons",                   2,  2, (TypeObject*)nil,            primitiveCons));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "object-size",            1,  1, (TypeObject*)nil,            primitiveObjectSize));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "object-length",          1,  1, (TypeObject*)nil,            primitiveObjectLength));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "vector",                 1, -1, (TypeObject*)nil,            primitiveVector));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "values",                 0, -1, (TypeObject*)nil,            primitiveValues));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "elements",               1,  3, (TypeObject*)nil,            primitiveElements));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "open",                   1,  2, type_string,    primitiveFopen));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "close",                  1,  1, type_stream,    primitiveFclose));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "file-info",              1,  1, type_stream,    primitiveFinfo));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "read",                   0,  2, (TypeObject*)nil,            primitiveRead));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "eval",                   1,  1, (TypeObject*)nil,            primitiveEval));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "write",                  1,  3, (TypeObject*)nil,            primitiveWrite));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "error",                  2,  3, (TypeObject*)nil,            primitiveError));
#if 0
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "throw",                  1,  2, (TypeObject*)nil,            primitiveThrow));
#endif
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "i+",                     2,  2, type_integer,   integerAdd));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "i-",                     2,  2, type_integer,   integerSubtract));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "i*",                     2,  2, type_integer,   integerMultiply));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "i/",                     2,  2, type_integer,   integerDivide));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "i%",                     2,  2, type_integer,   integerMod));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "i=0",                    1,  1, type_integer,   integerZerop));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "i=",                     2,  2, type_integer,   integerEqual));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "i<",                     2,  2, type_integer,   integerLess));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "i<=",                    2,  2, type_integer,   integerLessEqual));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "i>",                     2,  2, type_integer,   integerGreater));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "i>=",                    2,  2, type_integer,   integerGreaterEqual));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "&",                      2,  2, type_integer,   integerAnd));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "|",                      2,  2, type_integer,   integerOr));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "^",                      2,  2, type_integer,   integerXor));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "<<",                     2,  2, type_integer,   integerShiftLeft));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, ">>",                     2,  2, type_integer,   integerShiftRight));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "~",                      1,  1, type_integer,   integerNot));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "ifmt",                   1,  5, (TypeObject*)nil,            integerFmt));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "string-append",          2,  2, type_string,    stringAppend));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "string-compare",         2,  2, type_string,    stringCompare));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "extension",              1,  1, type_symbol,    primitiveLoadExtension));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "interp-version",         0,  0, (TypeObject*)nil,            primitiveInterpVersion));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "interp",                 0,  0, (TypeObject*)nil,            primitiveInterp));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "env",                    0,  0, (TypeObject*)nil,            primitiveEnv));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "interp-input",           0,  1, type_stream,    primitiveInterpInput));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "interp-output",          0,  1, type_stream,    primitiveInterpOutput));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "interp-debug",           0,  1, type_stream,    primitiveInterpDebug));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "interp-print",           0,  1, (TypeObject*)nil,            primitiveInterpPrint));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "interp-gc-always",       0,  1, (TypeObject*)nil,            primitiveInterpGcAlways));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "interp-trace-read",      0,  1, (TypeObject*)nil,            primitiveInterpTraceRead));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "interp-trace-primitives",0,  1, (TypeObject*)nil,            primitiveInterpTracePrimitives));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "interp-gc",              0,  0, (TypeObject*)nil,            primitiveInterpGc));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "interp-countdown",       0,  1, type_integer,   primitiveInterpCountdown));

        (*gcExt)->extension.version = newString(interp, FL_VERSION);
    } while (0);
    GC_RELEASE;
    return e;
}

Object *initRootEnv(Object *interp)
{
    Object *e = nil;

    do {
        FLISP_WHILE_OK(flisp_register_constant(interp, nil, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, t, NULL));

        /* Types */
        FLISP_WHILE_OK(flisp_register_type(interp, "type-integer",     type_integer, (Object*)&write_integer));
        FLISP_WHILE_OK(flisp_register_type(interp, "type-str",         type_str, (Object*)&write_str));
        FLISP_WHILE_OK(flisp_register_type(interp, "type-string",      type_string, (Object*)&write_string));
        FLISP_WHILE_OK(flisp_register_type(interp, "type-symbol",      type_symbol, (Object*)&write_symbol));
        FLISP_WHILE_OK(flisp_register_type(interp, "type-primitive",   type_primitive, (Object*)&write_primitive));

        FLISP_WHILE_OK(flisp_register_type(interp, "type-vector",      type_vector, (Object *)&write_vector));
        FLISP_WHILE_OK(flisp_register_type(interp, "type-type",        type_type, (Object*)&write_type));
        FLISP_WHILE_OK(flisp_register_type(interp, "type-interpreter", type_interpreter, (Object*)&write_interpreter));

        FLISP_WHILE_OK(flisp_register_type(interp, "type-stream",      type_stream, (Object*)&write_stream));
        FLISP_WHILE_OK(flisp_register_type(interp, "type-extension",   type_extension, (Object*)&write_extension));

        FLISP_WHILE_OK(flisp_register_type(interp, "type-cons",        type_cons, (Object *)&write_cons));
        FLISP_WHILE_OK(flisp_register_type(interp, "type-lambda",      type_lambda, (Object *)&write_closure));
        FLISP_WHILE_OK(flisp_register_type(interp, "type-macro",       type_macro, (Object *)&write_closure));
        FLISP_WHILE_OK(flisp_register_type(interp, "type-env",         type_env, (Object *)&write_env));
        FLISP_WHILE_OK(flisp_register_type(interp, "type-error",       type_error, (Object *)&write_error));
        /* Exceptions */
        FLISP_WHILE_OK(flisp_register_constant(interp, end_of_file, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, read_incomplete, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, invalid_read_syntax, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, range_error, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, wrong_type_argument, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, invalid_value, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, wrong_number_of_arguments, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, arithmetic_error, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, out_of_memory, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, gc_error, NULL));
        /* Traps */
        FLISP_WHILE_OK(flisp_register_constant(interp, trap_countdown, NULL));
        /* I/O */
        FLISP_WHILE_OK(flisp_register_constant(interp, io_error, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, permission_denied, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, not_found, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, file_exists, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, read_only, NULL));
        FLISP_WHILE_OK(flisp_register_constant(interp, is_directory, NULL));
    } while (0);
    return e;
}

Memory *newMemory(size_t size)
{
    Memory *memory = malloc(sizeof(Memory));
    if (!memory) return NULL;

    memory->capacity = size;
    memory->fromSpace = NULL;
    memory->toSpace = NULL;
    memory->fromOffset = 0;

    return memory;
}

/*
 * Public interface for embedding fLisp into an application.
 */

/** Initialize and return an fLisp interpreter.
 *
 * @param size          Initial size of Lisp object space in bytes.
 * @param argv          null terminated array to arguments to be imported or NULL.
 * @param input         open readable file descriptor for default input or NULL.
 * @param output        open writable file descriptor for default output or NULL.
 * @param debug         open writable file descriptor for debug output or NULL.
 *
 * @returns On success: a pointer to an fLisp interpreter object
 * @returns On failure: error
 *
 * Note: at the moment we only provide a single interpreter store a
 * pointer to int in the static variable *interp* and return that variable.
 *
 */
Object *flisp_new(
    size_t size,
    char **argv,
    FILE *input, FILE *output, FILE *error, FILE* debug)
{
    Object *interp;
    Object *e = nil, *var;

    interp = malloc(sizeof(SimpleObject) + sizeof(InterpreterExt)+20);
    if (interp == NULL) return flisp_static_error(out_of_memory, &init_oom_message);

    flisp_debug_stream->stream.fd = debug;
    FLISP_INTERP.debug = flisp_debug_stream;

    Memory *memory = newMemory((size < FLISP_MEMORY_INC_SIZE) ? FLISP_MEMORY_INC_SIZE : size);
    if (memory == NULL)
        return flisp_static_error(out_of_memory, &init_oom_message);

    interp->type = type_interpreter;
    interp->size = sizeof(InterpreterExt);
    interp->length = sizeof(InterpreterExt)/sizeof(Object *);

    FLISP_INTERP.memory = memory;

    FLISP_INTERP.countdown = 0;
    FLISP_INTERP.print = false;
    FLISP_INTERP.trace_read = false;
    FLISP_INTERP.trace_primitives = false;
    FLISP_INTERP.gc_always = false;

    /* scratchpad */
    scratchpad->string = NULL;
    scratchpad->capacity = 0;
    scratchpad->size = 0;

#if 0
    FLISP_INTERP.catch = &FLISP_INTERP.exceptionEnv;
#endif


    FLISP_INTERP.gcTop = nil;
    do {
        FLISP_UNLESS_ERR(FLISP_INTERP.symbols = newCons(interp, &nil, &nil));
        FLISP_UNLESS_ERR(FLISP_INTERP.global = newEnv(interp, &nil, &nil));
        FLISP_WHILE_OK(initRootEnv(interp));

        /* debug stream */
        FLISP_WHILE_OK(flisp_register_constant(interp, debug_output, FLISP_INTERP.debug));

        /* input stream */
        FLISP_UNLESS_ERR(FLISP_INTERP.input = newStreamObject(interp, input, "*standard-input*"));
        FLISP_UNLESS_ERR(var = newSymbol(interp, "*standard-input*"));
        FLISP_UNLESS_ERR(envSet(interp, &var, &FLISP_INTERP.input, &FLISP_INTERP.global, true));

        /* output stream */
        FLISP_UNLESS_ERR(FLISP_INTERP.output = newStreamObject(interp, output, "*standard-output*"));
        FLISP_UNLESS_ERR(var = newSymbol(interp, "*standard-output*"));
        FLISP_UNLESS_ERR(envSet(interp, &var, &FLISP_INTERP.output, &FLISP_INTERP.global, true));

        /* error stream */
        FLISP_UNLESS_ERR(FLISP_INTERP.stderr = newStreamObject(interp, error, "*standard-error*"));
        FLISP_UNLESS_ERR(var = newSymbol(interp, "*standard-error*"));
        FLISP_UNLESS_ERR(envSet(interp, &var, &FLISP_INTERP.stderr, &FLISP_INTERP.global, true));

        /* declare and load the core primitives */
        FLISP_INTERP.extensions = nil;
        FLISP_UNLESS_ERR(flisp_register_extension(interp, "core", flisp_core_init));
        FLISP_UNLESS_ERR(flisp_core_init(interp, FLISP_INTERP.extensions->car));
    } while (0);
    if (FLISP_IS_ERR(e)) {
        flisp_destroy(interp);
        return e;
    }

    GC_CHECKPOINT;
    GC_TRACE(gcVal, nil);

    if (argv != NULL) {
        do {
            /* Add argv0 to the environment */
            FLISP_UNLESS_ERR(*gcVal = newString(interp, *argv));
            FLISP_UNLESS_ERR(var = newSymbol(interp, "argv0"));
            FLISP_UNLESS_ERR(envSet(interp, &var, gcVal, &FLISP_INTERP.global, true));

            /* Add argv to the environement */
            *gcVal = nil;
            for (Object **j = gcVal; *++argv; j = &(*j)->cdr) {
                *j = newCons(interp, &nil, &nil);
                if (FLISP_IS_ERR(*j)) { e = *j; break; };
                (*j)->car = newString(interp, *argv);
                if (FLISP_IS_ERR((*j)->car)) { e = (*j)->car; break; };
            }
            if (FLISP_IS_ERR(e)) break;
            var = newSymbol(interp, "argv");
            if (FLISP_IS_ERR(var)) { e = var; break; }
            FLISP_UNLESS_ERR(envSet(interp, &var, gcVal, &FLISP_INTERP.global, true));
        } while (0);
    }
    GC_RELEASE;
    if (FLISP_IS_ERR(e)) {
        flisp_destroy(interp);
        return e;
    }

    return interp;
}

/*
 * Note: should we close file descriptors other then debug?
 *
 * Note: primitives are registered dynamically, but we do not free
 *   their memory here!
 */
void flisp_destroy(Object *interp)
{
    if (FLISP_INTERP.memory->fromSpace)
        (void)munmap(FLISP_INTERP.memory->fromSpace, FLISP_INTERP.memory->capacity);

    if (FLISP_INTERP.memory->toSpace)
        (void)munmap(FLISP_INTERP.memory->toSpace, FLISP_INTERP.memory->capacity);

    if (FLISP_DEBUG_OUTPUT.fd)
        fclose(FLISP_DEBUG_OUTPUT.fd);
    free(FLISP_INTERP.memory);
    free(interp);
}

Object *flisp_read_expr(Object *interp)
{
    Object *e = readExpr(interp, FLISP_STANDARD_INPUT.fd);

    if (FLISP_INTERP.trace_read) {
        GC_CHECKPOINT;
        GC_TRACE(gcE, e);
        flisp_debug(interp, "trace: ");
        GC_CHECK_ERR(flisp_write_object(interp, *gcE, t, interp->self.debug));
        GC_RELEASE;
        flisp_debug(interp, "\n");
        e = *gcE;
    }
    return e;
}
Object *flisp_eval_object(Object *interp, Object *object)
{
    return evalExpr(interp, &object, &FLISP_INTERP.global);
}
Object *flisp_eval_expr(Object *interp, Object *readably)
{
    GC_CHECKPOINT;
    GC_TRACE(gcObject, nil);
    GC_TRACE(gcResult, nil);
    do {
        if (FLISP_IS_ERR(*gcObject = flisp_read_expr(interp))) break;
        *gcObject = flisp_eval_object(interp, *gcObject);
    } while (0);
    if ((*gcObject)->type == type_error) {
        if ((*gcObject)->error.type != end_of_file) {
            *gcResult = flisp_write_object(interp, *gcObject, readably, interp->self.stderr);
            if ((*gcResult)->type == type_error)
                (void) flisp_write_object(interp, *gcResult, readably, interp->self.debug);
            *gcObject = flisp_write_object(interp, *gcObject, readably?t:nil, interp->self.debug);
            /* Note: could be nil? */
            if (FLISP_STDERR.fd) fputs("\n", FLISP_STDERR.fd);
        }
    } else if (interp->self.print) {
        *gcObject = flisp_write_object(interp, *gcObject, readably?t:nil, interp->self.output);
        /* Note: could be nil? */
        if (FLISP_STANDARD_OUTPUT.fd) fputs("\n", FLISP_STANDARD_OUTPUT.fd);
    }
    fflush(0);
    GC_RETURN(*gcObject);
}

Object *flisp_eval_input(Object *interp, Object *readably)
{
    Object *object;

    while ((object = flisp_eval_expr(interp, readably))->type != type_error);
    fflush(0);
    return object;
}

/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
