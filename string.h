#ifndef STRING_H
#define STRING_H
/*
 * fLisp string extension: utf-8 and matching
 *
 * leg20260315, CC0 1.0
 *
 */

#include "lisp.h"

#define FLISP_STRING_VERSION "0.1"

extern Object *flisp_string_init(Object *, Object *);

extern size_t flisp_code_length(char);
extern Object *flisp_char_offset(Object *, char *, size_t);
extern size_t flisp_string_length(char *, size_t);
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
