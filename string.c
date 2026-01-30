#include <ctype.h>
#include <string.h>

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

int64_t flisp_char_code(char *string)
{
    size_t len, i = 0;
    int64_t code = 0, mask = 0x01F, min = 0x08;

    len = flisp_char_length(*string);
    if (len == 0)
        return -1;
    if (strlen(string) < len)
        return -1;

    if (len == 1)
        return string[0] & 0x7F;

    while (++i < len) {
        if ((string[i] & 0xC0) != 0x80)
            return -1;

        code <<=6;
        code += (string[i] & 0x3F);
        mask >>=1;
        min <<=4;
    }
    code += (string[0] & mask)<<(6*(len-1));
    if (len == 4)
        min <<=1;
    if (code < min)
        return -1;
    return code;
}

Object *stringCharCode(Interpreter *interp, Object **args, Object **env)
{
    int64_t code;

    if (*(FLISP_ARG_ONE->string) == '\0')
        exceptionWithObject(interp, FLISP_ARG_ONE, invalid_value,
                            "(char-code string) - string is empty");
    code = flisp_char_code(FLISP_ARG_ONE->string);
    if (code == -1)
        exceptionWithObject(interp, FLISP_ARG_ONE, invalid_value,
                            "(char-code string) - string invalid UTF-8 encoding");
    return newInteger(interp, code);
}



bool flisp_string_register(Interpreter *interp)
{
    return
        flisp_register_primitive(   interp, "char-length",  1, 1, type_string,  stringCharLength)
        && flisp_register_primitive(interp, "code-char",    1, 1, type_integer, stringCodeChar)
        && flisp_register_primitive(interp, "char-code",    1, 1, type_string,  stringCharCode);
}


/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
