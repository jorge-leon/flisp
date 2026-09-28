#ifndef PRINC_H
#define PRINC_H
/*
 * fLisp object writer
 *
 * leg20260921, CC0 1.0
 *
 */

#include "lisp.h"

#define FLISP_PRINC_VERSION "0.1"
extern Object *extension_princ;

extern Object *flisp_princ_init(Object *, Object *);

extern void flisp_princ(Object *, FILE *);

#endif
/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
