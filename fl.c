/*
 * fLisp - Tiny interpreter
 *
 * Georg Lehner <jorge@magma-soft.at> 2026, CC0 1.0
 *
 */

#include <stdlib.h>
#include <errno.h>
#include "lisp.h"

int main(int argc, char **argv)
{
    Interpreter *interp;

    fputs("\n" FL_NAME " " FL_VERSION "\n", stderr);

    interp = flisp_new((size_t) 0, argv, NULL, stdin, stdout, stderr);
    if (interp == NULL) {
        fputs("fLisp interpreter initialization failed", stderr);
        return 1;
    }
    if (flisp_error(interp)) {
        flisp_write_error(interp, stderr);
        return 1;
    }
    for (;;) {
        flisp_eval(interp, NULL);
        if (flisp_error(interp))
            flisp_write_error(interp, stderr);
        else
            break;
    }
}

/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
