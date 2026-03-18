/*
 * fLisp string extension: utf-8 and matching
 *
 * leg20260315, CC0 1.0
 *
 */

#include <ctype.h>
#include <string.h>

#include "lisp.h"
#include "string.h"

Object *stringCharLength(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    size_t len;

    if (FLISP_ARG_ONE->size < 2)
        return newError(interp, invalid_value, FLISP_ARG_ONE,
                            "(char-length string) - string is empty");
    len = flisp_char_length(*(FLISP_ARG_ONE->string));
    if (len == 0)
        return newError(interp, range_error, FLISP_ARG_ONE,
                            "char-length string) - invalid UTF-8 encoding");
    return newInteger(interp, len);
}

/** flisp_code_char() - convert Unicode code point to character.
 *
 * @param code  .. code point
 * @param string .. pointer to character array where to store the character.
 *
 * @returns size of character string or -1 on error
 *
 * If string is NULL, only the size is returned.
 *
 */
size_t flisp_code_char(int64_t code, char *string)
{
    int64_t mask = 0x3F, prefix = 0xFF80;
    size_t len;

    if (code < 0 || code > 0x10FFFF)
        return -1;
    if (code >= 0x010000)
        len = 4;
    else if (code >= 0x0800)
        len = 3;
    else if (code >= 0x80)
        len = 2;
    else {
        if (string) {
            string[0] = code;
            return 1;
        }
    }
    if (string == NULL)
        return len;

    while (--len) {
        string[len] = 0x80 + (0x3F & code);
        code >>=6;
        mask >>=1;
        prefix >>=1;
    }
    string[0] = prefix + (code & mask);
    return len;
}

Object *stringCodeChar(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    size_t len = 0;
    char string[5] = { 0 };

    len = flisp_code_char(FLISP_ARG_ONE->value, string);
    if (len == -1)
        return newError(interp, FLISP_ARG_ONE, range_error,
                            "(code-char n) - n out of Unicode range");
    fl_debug(interp, "%d: %hhX %hhX %hhX %hhX %hhX\n",
             len, string[0], string[1], string[2], string[3], string[4]
        );
    return newString(interp, string);
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

Object *stringCharCode(Interpreter *interp, Object **args, Object **env, size_t nArgs)
{
    int64_t code;

    if (*(FLISP_ARG_ONE->string) == '\0')
        return newError(interp, FLISP_ARG_ONE, invalid_value,
                            "(char-code string) - string is empty");
    code = flisp_char_code(FLISP_ARG_ONE->string);
    if (code == -1)
        return newError(interp, FLISP_ARG_ONE, invalid_value,
                            "(char-code string) - string invalid UTF-8 encoding");
    return newInteger(interp, code);
}

/** strspn */
Object *stringStrspn(Interpreter *interp, Object** args, Object **env, size_t nArgs)
{
    int64_t i = strspn(FLISP_ARG_ONE->string, FLISP_ARG_TWO->string);
    return flisp_char_count(interp, FLISP_ARG_ONE, i);
}


/** strcspn */
Object *stringStrcspn(Interpreter *interp, Object** args, Object **env, size_t nArgs)
{
    int64_t i = strcspn(FLISP_ARG_ONE->string, FLISP_ARG_TWO->string);
    return flisp_char_count(interp, FLISP_ARG_ONE, i);
}

Object *string_extension = &(Object) { .string = "extension-string" };

bool flisp_string_register(Interpreter *interp)
{
    flisp_register_constant(interp, string_extension, newString(interp, FLISP_STRING_VERSION));

    return
        flisp_register_primitive(   interp, "char-length",  1, 1, type_string,  stringCharLength)
        && flisp_register_primitive(interp, "code-char",    1, 1, type_integer, stringCodeChar)
        && flisp_register_primitive(interp, "char-code",    1, 1, type_string,  stringCharCode)
        && flisp_register_primitive(interp, "strspn",       2, 2, type_string,  stringStrspn)
        && flisp_register_primitive(interp, "strcspn",       2, 2, type_string,  stringStrcspn);
}


/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
