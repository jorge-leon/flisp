/*
 * speed extension: C-implementation of some core functions
 */

#include "lisp.h"
#include "speed.h"

#define FLISP_SPEED_VERSION 0.1

/*
 * Candidates:
 *
 */

Object *speedCons(Interpreter *interp, Object **args, Object **env)
{
    return newCons(interp, &(*args)->car, &(*args)->cdr->car);
}
Object *speedErrorType(Interpreter *interp, Object **args, Object **env)
{
    return FLISP_ARG_ONE->error_type;
}
Object *speedErrorMessage(Interpreter *interp, Object **args, Object **env)
{
    return FLISP_ARG_ONE->message;
}
Object *speedErrorCulprit(Interpreter *interp, Object **args, Object **env)
{
    return FLISP_ARG_ONE->culprit;
}

Object *speed_extension = &(Object) { .string = "extension-speed" };

bool flisp_speed_register(Interpreter *interp)
{
    Object *object = newString(interp, #FLISP_SPEED_VERSON);
    flisp_register_constant(interp, speed_extension, object);
    
    return
        flisp_register_primitive(   interp, "cons",  2, 2, nil, speedCons)
        && flisp_register_primitive(interp, "error-type",    1,  1, type_error,     speedErrorType)
        && flisp_register_primitive(interp, "error-message", 1,  1, type_error,     speedErrorMessage)
        && flisp_register_primitive(interp, "error-culprit", 1,  1, type_error,     speedErrorCulprit)
        ;
}


/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
