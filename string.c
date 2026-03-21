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

/** flisp_char_length() Number of chars used by an utf-8 encoded code point
 *
 * @param c .. First characater
 * @returns [1, 4] if valid, or 0 if encoding is invalid.
 *
 */
size_t flisp_code_length(char c)
{
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xC0) == 0xC0) return 2;
    if ((c & 0xE0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF8) return 4;
    return 0;

}
Object *stringCodeLength(Object *interp, Object **args, Object **env, size_t nArgs)
{
    size_t len;

    if (FLISP_ARG_ONE->size < 2)
        return newError(interp, invalid_value, FLISP_ARG_ONE,
                            "(code-length string) - string is empty");
    len = flisp_code_length(*(FLISP_ARG_ONE->string));
    if (len == 0)
        return newError(interp, range_error, FLISP_ARG_ONE,
                            "code-length string) - invalid UTF-8 encoding");
    return newInteger(interp, len);
}

/** flisp_char_offset() - char offset of code point at Unicode string index
 *
 * @param interp .. Interpreter where to create the result
 * @param string .. String to index.
 * @param index  .. Number of encoded characters for which to find offset.
 *
 * @returns: character offset into string corresponding to utf8
 *   encoded character at position index
 * @errors: invalid-value when string is not utf-8 encoded.
 */
Object *flisp_char_offset(Object *interp, char *string, size_t index)
{
    size_t n = 0, i = 0, l = 0;

    while (string[i] != '\0' && n < index) {
        l = flisp_code_length(string[i]);
        if (l == 0)
            return newError(interp, invalid_value, nil, "flisp_char_offset(): string not utf-8 encoded");
        i += l;
        n++;
    }
    return newInteger(interp, i);
}

/** (char-offset string index)
 *
 */
Object *stringCharOffset(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ARG_TYPECHECK(FLISP_ARG_ONE, type_string, "(char-offset string index) - string");
    FLISP_ARG_TYPECHECK(FLISP_ARG_TWO, type_integer, "(char-offset string index) - index");
    
    return flisp_char_offset(interp, FLISP_ARG_ONE->string, FLISP_ARG_TWO->value); 
}

/** flisp_string_length() - number of Unicode characters in string
 *
 * @param interp .. interpreter where to create the result
 * @param string .. string in which to count encoded characters.
 * @param len    .. maximum number of char's to check.
 *
 * @returns: count of encoded characters, i.e. len of UTF-8 encoded unicode string.
 * @errors: invalid-value when string is not utf-8 encoded.
 */
Object *flisp_string_length(Object *interp, Object *string, size_t len)
{
    size_t n = 0, i = 0, l = 0;

    while (string->string[i] != '\0' && i < len) {
        l = flisp_code_length(string->string[i]);
        if (l == 0)
            return newError(interp, invalid_value, string, "flisp_char_count(): string not utf-8 encoded");
        i += l;
        n++;
    }
    return newInteger(interp, n);
}

Object *stringLength(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return flisp_string_length(interp, FLISP_ARG_ONE, FLISP_ARG_ONE->size);
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

Object *stringCodeChar(Object *interp, Object **args, Object **env, size_t nArgs)
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

    len = flisp_code_length(*string);
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

Object *stringCharCode(Object *interp, Object **args, Object **env, size_t nArgs)
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

/** (string-search needle haystack)
 *
 */
Object *stringSearch(Object *interp, Object **args, Object **env, size_t nArgs)
{
    char *pos;

    pos = strstr(FLISP_ARG_TWO->string, FLISP_ARG_ONE->string);
    if (pos == NULL)  return nil;

    return flisp_string_length(interp, FLISP_ARG_TWO, pos - FLISP_ARG_TWO->string);
}

/** strspn */
Object *stringStrspn(Object *interp, Object** args, Object **env, size_t nArgs)
{
    int64_t i = strspn(FLISP_ARG_ONE->string, FLISP_ARG_TWO->string);
    return flisp_string_length(interp, FLISP_ARG_ONE, i);
}


/** strcspn */
Object *stringStrcspn(Object *interp, Object** args, Object **env, size_t nArgs)
{
    int64_t i = strcspn(FLISP_ARG_ONE->string, FLISP_ARG_TWO->string);
    return flisp_string_length(interp, FLISP_ARG_ONE, i);
}

Object *string_extension = &(Object) { .string = "extension-string" };

bool flisp_string_register(Object *interp)
{
    flisp_register_constant(interp, string_extension, newString(interp, FLISP_STRING_VERSION));

    return
        flisp_register_primitive(   interp, "code-length",   1, 1, type_string,  stringCodeLength)
        && flisp_register_primitive(interp, "char-offset",   2, 2, nil,          stringCharOffset)
        && flisp_register_primitive(interp, "string-length", 1, 1, type_string,  stringLength)
        && flisp_register_primitive(interp, "code-char",     1, 1, type_integer, stringCodeChar)
        && flisp_register_primitive(interp, "char-code",     1, 1, type_string,  stringCharCode)
        && flisp_register_primitive(interp, "string-search", 2, 2, type_string,  stringSearch)
        && flisp_register_primitive(interp, "strspn",        2, 2, type_string,  stringStrspn)
        && flisp_register_primitive(interp, "strcspn",       2, 2, type_string,  stringStrcspn);
}


/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
