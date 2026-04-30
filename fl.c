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
 *                        Otherwise if set or stdin is a tty write
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
void write_string(FILE *fd, char *string)
{
    if (!fd)  return;
    fputs(string, fd);
    fflush(fd);
}
int main(int argc, char **argv)
{
    char *env;
    bool interactive = false;
    FILE *debug_fd = NULL, *input_fd = stdin;
    long long size = 0;
    Object *interp, *e = nil;

    if ((env = getenv("FLISP_SIZE")) != NULL) {
        size = strtoll(env, NULL, 16);
        if (errno == ERANGE || errno == EINVAL)  fatal("invalid FLISP_SIZE");
    }
    if ((env = getenv("FLISP_DEBUG")) != NULL) {
        if (env[0] == '\0')
            ;
        else if (env[0] == '-')
            debug_fd = stdout;
        else if (env[0] == '&')
            debug_fd = stderr;
        else if ((debug_fd = fopen(env, "w")) == NULL) {
            fatal("failed to open debug file");
        }
    }

    if (argc > 1) {
        if (argv[1][0] == '-')
            interactive = isatty(fileno(input_fd));
        else {
            if ((input_fd = fopen(argv[1], "r")) == NULL)
                fatal("failed to open input file");
        }
    } else
        interactive = true;
        
    do {
        FLISP_UNLESS_ERR(interp = flisp_new((size_t) size, argv, input_fd, stdout, stderr, debug_fd));
        FLISP_UNLESS_ERR(flisp_register_extension(interp, "string", flisp_string_init));
        FLISP_UNLESS_ERR(flisp_register_extension(interp, "double", flisp_double_init));
        FLISP_UNLESS_ERR(flisp_register_extension(interp, "posix", flisp_posix_init));
    } while (0);
    if (FLISP_IS_ERR(e)) {
        /* Note: could write error string here */
        fatal("fLisp interpreter initialization failed");
    }

    if (interactive)
        interp->self.print = ((env = getenv("FLISP_PRINT")) == NULL || env[0] != '0');
    else
        interp->self.print = ((env = getenv("FLISP_PRINT")) != NULL && env[0] != '0');

    if (interactive) write_string(FLISP_STANDARD_OUTPUT.fd, FL_NAME " " FL_VERSION "\n");

    Object *result = nil;
    for (;;) {
        if (interactive)  write_string(FLISP_STANDARD_OUTPUT.fd, "> ");
        fflush(NULL);

        result = flisp_eval_input(interp, interactive ? nil : t);
        if (FLISP_IS_EOF(result)) {
            if (interactive) write_string(FLISP_STANDARD_OUTPUT.fd, "\n");
            return 0;
        }
        if (!interactive) return 1;
    }
}

/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
