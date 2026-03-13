#include <errno.h>
#include <stdlib.h>
#include <math.h>

#include "lisp.h"
#include "double.h"

/* Constants */
/* Types */

Object *newDouble(Interpreter *interp, double number)
{
    Object *object = newObject(interp, type_double, 0);
    object->number = number;
    return object;
}

/** readDouble - add a float from the read buffer to the interpreter
 *
 * @param interp  fLisp interpreter
 *
 * returns: double object
 *
 * throws: range-error
 */
Object *readDouble(Interpreter *interp)
{
    double d;
    Object *number;

    addCharToBuf(interp, '\0');
    errno = 0;
    d = strtod(interp->buf, NULL);
    if (errno == ERANGE)
        exception(interp, range_error, "integer out of range,: %f", d);
    // Note: purposely not dealing with NaN
    number = newDouble(interp, d);
    resetBuf(interp);
    return number;
}

// Number Type Conversion /////
Object *integerFromDouble(Interpreter *interp, Object **args, Object **env)
{
    return newInteger(interp, (int64_t) FLISP_ARG_ONE->number);
}

Object *doubleFromInteger(Interpreter *interp, Object **args, Object **env)
{
    return newDouble(interp, (double) FLISP_ARG_ONE->value);
}
// Double Math ///////
#define FLISP_DOUBLE_MATHOP(name, op)                                         \
Object *name(Interpreter *interp, Object **args, Object **env)                \
{                                                                             \
    return newDouble(interp, FLISP_ARG_ONE->number op FLISP_ARG_TWO->number); \
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

Object *doubleMod(Interpreter *interp, Object **args, Object **env)
{
    return newDouble(interp, fmod(FLISP_ARG_ONE->number, FLISP_ARG_TWO->number));
}

bool flisp_double_register(Interpreter *interp)
{
    return
        flisp_register_primitive(   interp, "integer", 1,  1, type_double,  integerFromDouble)
        && flisp_register_primitive(interp, "double",  1,  1, type_integer, doubleFromInteger)
        && flisp_register_primitive(interp, "d+",      2,  2, type_double, doubleAdd)
        && flisp_register_primitive(interp, "d-",      2,  2, type_double, doubleSubtract)
        && flisp_register_primitive(interp, "d*",      2,  2, type_double, doubleMultiply)
        && flisp_register_primitive(interp, "d/",      2,  2, type_double, doubleDivide)
        && flisp_register_primitive(interp, "d%",      2,  2, type_double, doubleMod)
        && flisp_register_primitive(interp, "d=",      2,  2, type_double, doubleEqual)
        && flisp_register_primitive(interp, "d<",      2,  2, type_double, doubleLess)
        && flisp_register_primitive(interp, "d<=",     2,  2, type_double, doubleLessEqual)
        && flisp_register_primitive(interp, "d>",      2,  2, type_double, doubleGreater)
        && flisp_register_primitive(interp, "d>=",     2,  2, type_double, doubleGreaterEqual);
}


/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
