/*
 * fLisp - Command line interpreter
 *
 * Georg Lehner <jorge@magma-soft.at> 2024, CC0 1.0
 *
 * Commandline arguments are passed to the fLisp interpreter.
 * Environment:
 * - FLISP_SIZE        .. Number of bytes to allocate on start, defaults to zero.
 * - FLISP_QUIET       .. If not '' or '0' set interpreter stdout to NULL.
 * - FLISP_INTERACTIVE .. If set to '0' interactive mode is off.
 *                        Oterwise if set or stdin is a tty write
 *                        version at start and prompt after each
 *                        error.
 * - FLISP_DEBUG       .. File name to use for debugging. Special cases:
 *     - '-' .. stdout
 *     - '&' .. stderr
 *     - ''  .. no debugging
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
    if (!fd)  return;
    flisp_write_object(fd, object, readably);
    fputs("\n", fd);
    fflush(fd);
}
void write_string(FILE *fd, char *string)
{
    if (!fd)  return;
    fputs(string, fd);
    fflush(fd);
}
int main(int argc, char **argv)
{
    char *env;
    FILE *debug_fd = NULL, *input_fd = stdin, *output_fd = stdout;
    long long size = 0;
    Object *interp;

    if ((env = getenv("FLISP_SIZE")) != NULL) {
        size = strtoll(env, NULL, 16);
        if (errno == ERANGE || errno == EINVAL)  fatal("invalid FLISP_SIZE");
    }
    if ((env = getenv("FLISP_DEBUG")) != NULL) {
        if (env[0] != '\0')
            ;
        else if (env[0] == '-')
            debug_fd = stdout;
        else if (env[0] == '&')
            debug_fd = stderr;
        else if ((debug_fd = fopen(env, "w")) == NULL) {
            fatal("failed to open debug file");
        }
    }
    if ((env = getenv("FLISP_QUIET")) != NULL && env[0] != '0')
        output_fd = (FILE *)NULL;

    env = getenv("FLISP_INTERACTIVE");
    bool interactive = env[0] != '0' && (env || isatty(fileno(input_fd)));

    interp = flisp_new((size_t) size, argv, input_fd, output_fd, stderr, debug_fd);
    if (interp == NULL)
        fatal("fLisp interpreter initialization failed");

    if (interp->type == type_error) {
        writeln_object(stderr, (Object *)interp, false);
        return 1;
    }

    if (interactive) write_string(output_fd, FL_NAME " " FL_VERSION "\n");

    Object *result = nil;
    for (;;) {
        if (interactive)  write_string(interp->output->fd, "> ");
        fflush(NULL);

        result = flisp_eval_input(interp, !interactive);
        if (result->type == type_error) {
            if (result->error == end_of_file) {
                if (interactive) write_string(interp->output->fd, "\n");
                return 0;
            }
            if (!interactive) return 1;
        }
    }
}

/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
