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


FLISP_DEFINE_CONSTANT(extension_double,"double");
FLISP_DEFINE_CONSTANT(extension_double_version,FLISP_DOUBLE_VERSION);

Object *flisp_double_init(Object *interp, Object *extension)
{

    if (extension->extension.version != nil) return extension->extension.version;

    Object *e = nil;
    GC_CHECKPOINT;
    GC_TRACE(gcExt, extension);
    do {

        FLISP_WHILE_OK(flisp_register_type(interp, "type-double",      type_double, (Object*)&flisp_init_invalid, nil)); //(Object*)&write_double));
       
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

        FLISP_UNLESS_ERR((*gcExt)->extension.version = extension_double_version);
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
