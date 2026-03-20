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

    interp = (Interpreter *)flisp_new((size_t) 0, argv, NULL, stdin, stdout, getenv("FLISP_DEBUG") ? stderr : NULL);
    if (interp == NULL) {
        fputs("fLisp interpreter initialization failed", stderr);
        return 1;
    }
    if (interp->type == type_error) {
        flisp_write_object(stderr, (Object *)interp, true);
        return 1;
    }
    Object *result;
    do {
        result = flisp_eval(interp, NULL);
        if (interp->debug.fd)  fflush(interp->debug.fd);
        if (result->type == type_error) {
            flisp_write_object(stderr, result, true);
            fputs("", stderr);
        }
    } while (!feof(interp->input.fd));
}

/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
