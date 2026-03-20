/*
 * fLisp speed extension: C-implementation of some core functions
 *
 * leg20260315, CC0 1.0
 *
 */

#include "speed.h"

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

size_t flisp_list_length(Object *list)
{
    int l = 0;
    for (Object *e = list; e->type == type_cons; e = e->cdr, l++);
    return l;
}
Object *speedListLength(Interpreter *interp, Object **args, Object **env)
{
    return newInteger(interp, flisp_list_length(*args));
}


Object *speed_extension = &(Object) { .string = "extension-speed" };

bool flisp_speed_register(Interpreter *interp)
{
    flisp_register_constant(interp, speed_extension, newString(interp, #FLISP_SPEED_VERSON));
    
    return
        flisp_register_primitive(   interp, "cons",  2, 2, nil, speedCons)
        && flisp_register_primitive(interp, "error-type",    1,  1, type_error,     speedErrorType)
        && flisp_register_primitive(interp, "error-message", 1,  1, type_error,     speedErrorMessage)
        && flisp_register_primitive(interp, "error-culprit", 1,  1, type_error,     speedErrorCulprit)
        && flisp_register_primitive(interp, "list-length",   1,  1, null,           speedListLength)
        ;
}


/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
