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
void writeln_object(FILE * fd, Object *object, bool readably)
{
    flisp_write_object(fd, object, readably);
    fputs("\n", fd);
    fflush(fd);
}
int main(int argc, char **argv)
{
//    char *rcfile;
    char *debug_file, *noout, *size_string;
    FILE *debug_fd = NULL, *input_fd = stdin, *output_fd = stdout;
    long long size = 0;
    Object *interp;

    if ((size_string=getenv("FLISP_SIZE")) != NULL) {
        size = strtoll(size_string, NULL, 16);
        if (errno == ERANGE || errno == EINVAL)  fatal("invalid FLISP_SIZE");
    }
    if ((debug_file=getenv("FLISP_DEBUG")) != NULL) {
        if (debug_file[0] != '\0')
            ;
        else if (debug_file[0] == '-')
            debug_fd = stdout;
        else if (debug_file[0] == '&')
            debug_fd = stderr;
        else if ((debug_fd = fopen(debug_file, "w")) == NULL) {
            fatal("failed to open debug file");
        }
    }
    if ((noout = getenv("FLISP_QUIET")) != NULL && noout[0] != '0')
        output_fd = (FILE *)NULL;

    bool interactive = (getenv("FLISP_INTERACTIVE") != NULL || isatty(fileno(input_fd)));

    interp = flisp_new((size_t) size, argv, input_fd, output_fd, stderr, debug_fd);
    if (interp == NULL)
        fatal("fLisp interpreter initialization failed");

    if (interp->type == type_error) {
        writeln_object(stderr, (Object *)interp, false);
        return 1;
    }

    if (interactive) fputs(FL_NAME " " FL_VERSION, output_fd);

    Object *result = nil;
    for (;;) {
        if (interactive)  fputs( "\n> ", stdout);
        fflush(NULL);

        result = flisp_eval_input(interp, !interactive);
        if (result->type == type_error) {
            if (result->error == end_of_file) return 0;
            if (!interactive) return 1;
        }
        if (interp->output->fd) fputs("\n", interp->output->fd);
    }
}

/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
