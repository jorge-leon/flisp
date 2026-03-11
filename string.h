#ifndef STRING_H
#define STRING_H

#include "lisp.h"

extern bool flisp_string_register(Interpreter *);

extern int64_t flisp_char_code(char *);
extern size_t flisp_code_char(int64_t, char *);

#endif
/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
