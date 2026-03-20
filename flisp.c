/*
 * fLisp - Command line interpreter
 *
 * Georg Lehner <jorge@magma-soft.at> 2024, CC0 1.0
 *
 */

#include <stdlib.h>
#include <errno.h>
#include "lisp.h"

#ifdef FLISP_DOUBLE_EXTENSION
#include "double.h"
#endif
#include "posix.h"
#include "string.h"

void fatal(char *msg)
{
    fputs("\n" FL_NAME " " FL_VERSION ": ", stderr);
    fputs(msg, stderr);
    fputc('\n', stderr);
    exit(1);
}

int main(int argc, char **argv)
{
    char *rcfile, *debug_file, *size_string;
    FILE *debug_fd = NULL, *input_fd = stdin;
    long long size = 0;
    Interpreter *interp;

    if ((rcfile = getenv("FLISPRC")) == NULL)
        rcfile = CPP_XSTR(FLISPRC);

    if (*rcfile != '\0')
        if (!(input_fd = fopen(rcfile, "r")))
            fatal("failed to open rcfile, FLISPRC or: " CPP_XSTR(FLISPRC));

    if ((debug_file=getenv("FLISP_DEBUG")) != NULL)
        if ((debug_fd = fopen(debug_file, "w")) == NULL)
            fatal("failed to open debug file");

    if ((size_string=getenv("FLISP_SIZE")) != NULL) {
        size = strtoll(size_string, NULL, 16);
        if (errno == ERANGE || errno == EINVAL)
            fatal("invalid FLISP_SIZE");
    }
    
    interp = flisp_new((size_t) size, argv, NULL, input_fd, debug_fd, debug_fd);
    if (interp == NULL)
        fatal("fLisp interpreter initialization failed");

    if (flisp_error(interp)) {
        flisp_write_error(interp, stderr);
        exit(1);
    }

#ifdef FLISP_DOUBLE_EXTENSION
    flisp_double_register(interp);
#endif
    flisp_posix_register(interp);
    flisp_string_register(interp);

    Object *result = flisp_eval(interp, NULL);
    if (interp->output.fd && interp->output.fd != debug_fd)
        fflush(interp->output.fd);
    if (debug_fd)
        fflush(debug_fd);
    if (result->type == type_error) {
        flisp_write_object(interp, stderr, result, true);
        fputs("", stderr);
        return 1;
    }
    return 0;
}

/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
