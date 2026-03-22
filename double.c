#include <errno.h>
#include <stdlib.h>
#include <math.h>

#include "lisp.h"
#include "double.h"

/* Constants */
/* Types */

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

bool flisp_double_register(Object *interp, Object *extension)
{

    if (extension->extension.version != nil) return true;

    if (flisp_register_primitive(   interp, "integer", 1,  1, type_double,  integerFromDouble)
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
        && flisp_register_primitive(interp, "d>=",     2,  2, type_double, doubleGreaterEqual)) {

        extension->extension.version = newString(interp, FLISP_DOUBLE_VERSION);
        return true;
    }
    return false;
}


/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
