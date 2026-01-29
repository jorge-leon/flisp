
#include "lisp.h"
#include "string.h"

Object *stringCharLength(Interpreter *interp, Object **args, Object **env)
{
    size_t len;
    
    if (*(FLISP_ARG_ONE->string) == '\0')
        exceptionWithObject(interp, FLISP_ARG_ONE, invalid_value,
                            "(char-length string) - string is empty");
    len = flisp_char_length(*(FLISP_ARG_ONE->string));
    if (len == 0)
        exceptionWithObject(interp, FLISP_ARG_ONE, range_error,
                            "char-length string) - invalid UTF-8 encoding");
    return newInteger(interp, len);
}


Object *stringCodeChar(Interpreter *interp, Object **args, Object **env)
{
    return nil;
}

Object *stringCharCode(Interpreter *interp, Object **args, Object **env)
{
    if (*(FLISP_ARG_ONE->string) == '\0')
        exceptionWithObject(interp, FLISP_ARG_ONE, invalid_value,
                            "(char-code string) - string is empty");
    return nil;
}



bool flisp_string_register(Interpreter *interp)
{
    return
        flisp_register_primitive(   interp, "char-length",  1, 1, type_string, stringCharLength)
        && flisp_register_primitive(interp, "code-char",    1, 1, type_integer, stringCodeChar)
        && flisp_register_primitive(interp, "char-code",    1, 1, type_integer, stringCharCode);
}


/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
