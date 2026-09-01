#include <errno.h>
#include <stdlib.h>
#include <math.h>

#include "lisp.h"
#include "double.h"

/* Constants */
/* Types */
FLISP_DEFINE_TYPE(double);


// Number Type Conversion /////
Object *integerFromDouble(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, (int64_t) FLISP_ARG1->number);
}

Object *doubleFromInteger(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newDouble(interp, (double) FLISP_ARG1->value);
}
// Double Math ///////
#define FLISP_DOUBLE_MATHOP(name, op)                                        \
Object *name(Object *interp, Object **args, Object **env, size_t nArgs) \
{                                                                            \
    return newDouble(interp, FLISP_ARG1->number op FLISP_ARG2->number);\
}
FLISP_DOUBLE_MATHOP(doubleAdd, +)
FLISP_DOUBLE_MATHOP(doubleSubtract, -)
FLISP_DOUBLE_MATHOP(doubleMultiply, *)
FLISP_DOUBLE_MATHOP(doubleDivide, /)
FLISP_DOUBLE_MATHOP(doubleEqual, ==)
FLISP_DOUBLE_MATHOP(doubleLess, <)
FLISP_DOUBLE_MATHOP(doubleLessEqual, <=)
FLISP_DOUBLE_MATHOP(doubleGreater, >)
FLISP_DOUBLE_MATHOP(doubleGreaterEqual, >=)

Object *doubleMod(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newDouble(interp, fmod(FLISP_ARG1->number, FLISP_ARG2->number));
}

#if 0
/* Note: The following is a double formatter, however it only does
 * binary floats. The double reader has been removed but the #d reader
 * macro provides for an alternative.  The double writer prints the #D
 * reader macro variant.
 */
Object *writeDouble(FILE *fd, uint64_t number)
{
    if (fd == NULL) return nil;

    bool sign = number & 0x8000000000000000;
    int64_t exponent = ((number & 0x7ff0000000000000) >> 52);
    int64_t fraction = number   & 0x000fffffffffffff;
    char *prefix = "1.";
    Object *e = nil;

    if (exponent == 0) {
        if (fraction)
            prefix = "0.";
        else {
            e = writeString(fd, "-0");
            return e;
        }
    }
    if (exponent == -1) {
        if (fraction)
            e = writeString(fd, "NaN");
        else {
            if (sign && ((e = writeChar(fd, '-')) != nil)) return e;
            e = writeString(fd, "∞");
        }
        return e;
    }
    exponent -=1023;
    /* Note: here is where we would start to convert to decimal */
    if (sign && ((e = writeChar(fd, '-')) != nil)) return e;
    if ((e = writeString(fd, prefix)) != nil ||
        (e = writeInteger(fd, fraction)) != nil ||
        (e = writeString(fd, "₂")) != nil) return e;
    if (exponent &&
        ((e = writeString(fd, "×2^")) != nil ||
         (e = writeInteger(fd, exponent)) != nil)) return e;
    if ((e = writeString(fd, "₂")) != nil) return e;
    return e;
}
#endif
/* (write-/type/ obj[ readably[ stream]) */
Object *primitiveWDouble(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_double, "(write-double o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_fmt(interp, args, nArgs, "%f", FLISP_ARG1->number));
    return FLISP_ARG1;
}
Primitive w_d_p = { .name = "write-double", .nMinArgs = 2, .nMaxArgs = 3, .argsType = (TypeObject*)&nil_obj, .eval = primitiveWDouble };
SimpleObject write_double = { .type = &type_primitive_obj, .size = 0, .primitive = &w_d_p };


Object *flisp_double_init(Object *interp, Object *extension)
{

    if (extension->extension.version != nil) return extension->extension.version;

    Object *e = nil;
    GC_CHECKPOINT;
    GC_TRACE(gcExt, extension);
    do {

        FLISP_WHILE_OK(flisp_register_type(interp, "type-double",      type_double, (Object*)&flisp_init_error, (Object*)&write_double));
       
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "integer", 1,  1, type_double,  integerFromDouble));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "double",  1,  1, type_integer, doubleFromInteger));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "d+",      2,  2, type_double, doubleAdd));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "d-",      2,  2, type_double, doubleSubtract));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "d*",      2,  2, type_double, doubleMultiply));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "d/",      2,  2, type_double, doubleDivide));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "d%",      2,  2, type_double, doubleMod));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "d=",      2,  2, type_double, doubleEqual));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "d<",      2,  2, type_double, doubleLess));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "d<=",     2,  2, type_double, doubleLessEqual));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "d>",      2,  2, type_double, doubleGreater));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "d>=",     2,  2, type_double, doubleGreaterEqual));

        FLISP_UNLESS_ERR((*gcExt)->extension.version = newString(interp, FLISP_DOUBLE_VERSION));
    } while (0);
    GC_RELEASE;
    return e;
}


/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
