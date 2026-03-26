/*
 * fLisp - Command line interpreter
 *
 * Georg Lehner <jorge@magma-soft.at> 2024, CC0 1.0
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "lisp.h"


#include "double.h"
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
    FILE *debug_fd = NULL, *input_fd = stdin, *output_fd = NULL;
    long long size = 0;
    Object *interp;

    if ((debug_file=getenv("FLISP_DEBUG")) != NULL) {
        if (debug_file[0] != '\0')
            ;
        else if (debug_file[0] == '-')
            debug_fd = stdout;
        else if (debug_file[0] == '=')
            debug_fd = stderr;
        else if ((debug_fd = fopen(debug_file, "w")) == NULL) {
            fputs("failed to open debug file", stderr);
            debug_fd = output_fd = stdout;
        }
    }
    if ((rcfile = getenv("FLISPRC")) == NULL)
        rcfile = CPP_XSTR(FLISPRC);

    if (*rcfile != '\0')
        if (!(input_fd = fopen(rcfile, "r"))) {
            fputs("failed to open rcfile: ", stderr);
            fputs(rcfile, stderr);
            fputs("\n", stderr);
            input_fd = stdin;
            output_fd = stdout;
        }

    if ((size_string=getenv("FLISP_SIZE")) != NULL) {
        size = strtoll(size_string, NULL, 16);
        if (errno == ERANGE || errno == EINVAL)
            fputs("invalid FLISP_SIZE", stderr);
    }

    bool interactive = (getenv("FLISP_INTERACTIVE") != NULL || isatty(fileno(input_fd)));
    if (interactive && output_fd == NULL) output_fd = stdout;

    interp = (Object *)flisp_new((size_t) size, argv, NULL, input_fd, output_fd, debug_fd);
    if (interp == NULL)
        fatal("fLisp interpreter initialization failed");

    if (interp->type == type_error) {
        flisp_write_object(stderr, (Object *)interp, false);
        exit(1);
    }

    if (interactive)  fputs(FL_NAME " " FL_VERSION, output_fd);

    Object *result = nil;

    for (;;) {
        if (interactive)   fputs( "\n> ", stdout);
        result = flisp_eval(interp, NULL);
        if (interp->output->fd && interp->output->fd != debug_fd)
            fflush(interp->output->fd);
        if (debug_fd)
            fflush(debug_fd);
        if (result->type == type_error) {
            flisp_write_object(stderr, result, false);
            if (result->error == end_of_file || feof(input_fd)) exit(0);
            fputs("", stderr);
            if (interactive)  continue;
            exit(1);
        }
        if (result == end_of_file || feof(input_fd)) exit(0);
        if (interactive)  continue;
        exit(0);
    }
}

/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
