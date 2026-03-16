/*
 * flisp.c, Georg Lehner, Public Domain, 2024
 */

#include <stdlib.h>
#include <errno.h>
#include "lisp.h"

void fatal(char *msg)
{
    fputs("\n" FL_NAME " " FL_VERSION ": ", stderr);
    fputs(msg, stderr);
    fputc('\n', stderr);
    exit(1);
}

int main(int argc, char **argv)
{
    char *debug_file;
    FILE *debug_fd = NULL;
    long long size = 0;
    Interpreter *interp;

    fputs("\n" FL_NAME " " FL_VERSION "\n", stderr);

    if ((debug_file=getenv("FLISP_DEBUG")) != NULL)
        if ((debug_fd = fopen(debug_file, "w")) == NULL)
            fatal("failed to open debug file");

    interp = flisp_new((size_t) size, argv, NULL, stdin, stdout, debug_fd);
    if (interp == NULL)
        fatal("fLisp interpreter initialization failed");

    /* if (flisp_error(interp)) { */
    /*     flisp_write_error(interp, stderr); */
    /*     exit(1); */
    /* } */

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
