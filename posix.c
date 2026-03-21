/*
 * fLisp posix extension: Bag of POSIX libc wrappers
 *
 * leg20260315, CC0 1.0
 *
 */

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fnmatch.h>

#include "posix.h"

/* Bag of POSIX libc wrappers */

/** (fflush[ stream]) - flush stream, output or all streams
 *
 * @param stream  Stream to flush. If t all streams are flushed, if
 *                not given the interpreter output is flushed.
 *
 * @returns t
 * @throws io-error
 */
Object *posixFflush(Object *interp, Object** args, Object **env, size_t nArgs)
{
    FILE *fd = interp->output->fd;

    if (nArgs)
        if (FLISP_ARG_ONE == t)
            fd = NULL;
        else {
            FLISP_ARG_TYPECHECK(FLISP_ARG_ONE, type_stream,  "(fflush[ stream]) - stream");
            if (FLISP_ARG_ONE->fd == NULL)
                return newError(interp, invalid_value, FLISP_ARG_ONE, "(fflush[ stream]) - stream already closed");
            fd = FLISP_ARG_ONE->fd;
        }
    else if (fd == NULL)
        return newError(interp, invalid_value, FLISP_ARG_ONE, "(fflush[ stream]) - output stream not set");

    if (fflush(fd) == EOF)
        return newError(interp, io_error, FLISP_ARG_ONE, "(fflush[ stream]) - fflush() failed: %s", strerror(errno));

    return t;
}
/** (fseek stream offset[ relativep]) - seek position in stream or input
 *
 * @param stream     stream object, if nil interpreter input stream.
 * @param offset     offset from start if positive, from end if
 *                   negative.
 * @param relativep  if given and not nil seek from current position.
 *
 * @return new position in the stream.
 */
Object *posixFseek(Object *interp, Object** args, Object **env, size_t nArgs)
{
    int result, whence = SEEK_SET;
    off_t pos;
    Object *stream = FLISP_ARG_ONE;

    if (stream == nil) {
        stream = interp->input;
        if (stream->fd == NULL)
            return newError(interp, invalid_value, stream, "(fseek stream offset[ relativep]) - input stream not set");
    } else {
        FLISP_ARG_TYPECHECK(stream, type_stream,  "(fseek stream offset) - stream");
        if (stream->fd == NULL)
            return newError(interp, invalid_value, stream, "(fseek stream) - stream already closed");
    }
    FLISP_ARG_TYPECHECK(FLISP_ARG_TWO, type_integer, "(fseek stream offset) - offset");

    if (nArgs > 2 && FLISP_ARG_THREE != nil)
        whence = SEEK_CUR;
    else if (FLISP_ARG_TWO->value < 0)
        whence = SEEK_END;
    result = fseeko(stream->fd, FLISP_ARG_TWO->value, whence);
    if (result == -1)
        return newError(interp, io_error, stream, "(fseek stream offset) - fseeko() failed: %s", strerror(errno));

    if ((pos = ftello(stream->fd)) == -1)
        return newError(interp, io_error, stream, "(fseek stream offset) - ftello() failed: %s", strerror(errno));

    return newInteger(interp, pos);
}
/** (ftell[ stream]) - return current position in stream or input
 *
 * @param  stream  stream. If not given the input stream is used.
 *
 * @returns current position in stream.
 *
 * @throws
 * - invalid-value  if stream is already closed.
 * - io-error       if ftello fails.
 */
Object *posixFtell(Object *interp, Object** args, Object **env, size_t nArgs)
{
    Object *stream = interp->input;
    off_t pos;

    if (nArgs)
        stream = FLISP_ARG_ONE;

    if (stream->fd == NULL)
        return newError(interp, invalid_value, stream, "(ftell[ stream]) - stream already closed");

    if ((pos = ftello(stream->fd)) == -1)
        return newError(interp, io_error, stream, "(ftell[ stream]) - ftello() failed: %s", strerror(errno));

    return newInteger(interp, pos);
}
/** (feof[ stream]) - return end-of-file status of stream or input
 *
 * @param stream  stream. If not given the input stream is used.
 *
 * @returns  nil or end-of-file
 */
Object *posixFeof(Object *interp, Object** args, Object **env, size_t nArgs)
{
    Object *stream = interp->input;

    if (nArgs)
        stream = FLISP_ARG_ONE;
    if (stream->fd == NULL)
        return newError(interp, invalid_value, stream, "(feof[ stream]) - stream already closed");

    return (feof(stream->fd)) ? end_of_file : nil;
}
/** (fgetc[ stream]) - read one character from stream or input
 *
 * @param stream  stream to read input from, if not given read from
 *                interpreter input stream.
 */
Object *posixFgetc(Object *interp, Object** args, Object **env, size_t nArgs)
{
    char s[] = "\0\0";
    int c;
    Object *stream = interp->input;

    if (nArgs) {
        stream = FLISP_ARG_ONE;
        if (FLISP_ARG_ONE->fd == NULL)
            return newError(interp, invalid_value, stream, "(fgetc[ stream]) - stream already closed");
    }
    
    c = fgetc(stream->fd);
    if (c == EOF) {
        if (ferror(stream->fd))
            return newError(interp, io_error, stream, "(fgetc[ stream]) - stream I/O error: %s", strerror(errno));
        return end_of_file;
    }
    s[0] = (char)c;
    return newString(interp, s);
}
/** (fungetc i[ stream]) - ungetc integer i as char to stream or input
 *
 * @param i       integer converted to unsigned char
 * @param stream  stream, if not given the interpreter input stream
 *
 * Caution: ungetc'ing the interpreter input stream will likely cause
 *          undesired results like memory exhaustion.
 *
 * @returns i
 * @throws:
 * - invalid-value  If stream is closed or interpreter input stream is
 *                  not set.
 * - io-error       When ungetc() fails.
 */
/* Note: not yet sure if (fungetc i) is a) a good idea, b) any way
 *   secure.
 */
Object *posixFungetc(Object *interp, Object** args, Object **env, size_t nArgs)
{
    int c;
    Object *stream = interp->input;

    FLISP_ARG_TYPECHECK(FLISP_ARG_ONE, type_integer, "(fungetc char[ stream] - char)");
    c = (int)FLISP_ARG_TWO->value;
    
    if (nArgs > 1) {
        FLISP_ARG_TYPECHECK(FLISP_ARG_TWO, type_stream, "(fungetc char[ stream] - stream)");
        stream = FLISP_ARG_TWO;
    }
    if (stream->fd == NULL)
        return newError(interp, invalid_value, stream, "(fungetc char [ stream]) - stream already closed");

    c = ungetc(c, stream->fd);
    if (c == EOF)
        return newError(interp, io_error, stream, "(fungetc char [ stream]) - ungetc() failed");

    return newInteger(interp, FLISP_ARG_ONE->value);
}
/** (fgets[ stream]) - read a line or up to INPUT_FMT_BUFSIZ from stream or input
 *
 * @param stream  stream to read from. If not give use the input stream.
 *
 * @returns The string read from stream or end-of-file if no input is
 *          available. If a line is read it includes the trailing \n.
 *
 * @throws
 * - invalid-value   If stream is already closed.
 * - out-of-memory   If the input buffer cannot be allocated.
 * - io-error        If fgets() failed.
 */
Object *posixFgets(Object *interp, Object** args, Object **env, size_t nArgs)
{
    Object *string = nil;
    char *input;
    Object *stream = interp->input;

    if (nArgs) {
        stream = FLISP_ARG_ONE;
        FLISP_ARG_TYPECHECK(stream, type_stream, "(fgets[ stream] - stream)");
        if (stream->fd == NULL)
            return newError(interp, invalid_value, stream, "(fgets[ stream]) - stream already closed");
    }
    input = malloc(INPUT_FMT_BUFSIZ);
    if(input == NULL)
        return newError(interp, out_of_memory, stream, "fgets() failed, %s", strerror(errno));

    *input = '\0';

    if(fgets(input, INPUT_FMT_BUFSIZ, stream->fd) != NULL) {
        string = newString(interp, input);
        free(input);
        return string;
    }
    free(input);
    if (!feof(stream->fd))
        return newError(interp, stream, io_error, "fgets() failed: %s", strerror(errno));
    return end_of_file;
}
/** (fstat path[ linkp]) - get  information about file
 *
 * @param path   String containing the path to the file to query.
 * @param linkp  If given and not null do not follow the symbolic link
 *               if path is one, return the link information instead.
 *
 * @returns A property list with size, mode uid and gid as integer, type as character:
 * - b  block device
 * - c  character device
 * - d  directory
 * - p  fifo
 * - f  regular file
 * - l  symbolic link
 * - s  socket
 * - -  unkown file type
 *
 * @trhows
 * - permission-denied
 * - not-found
 * - invalid-value      if path is to long.
 * - io-error
 */
Object *posixFstat(Object *interp, Object** args, Object **env, size_t nArgs)
{
    struct stat info;
    int result;
    Object *object;
    char *type;

    FLISP_ARG_TYPECHECK(FLISP_ARG_ONE, type_string,  "(fstat path[ linkp]) - stream");

    if (nArgs > 1 && FLISP_ARG_TWO != nil)
        result = lstat(FLISP_ARG_ONE->string, &info);
    else
        result = stat(FLISP_ARG_ONE->string, &info);

    if (result == -1) {
        switch(errno) {
        case EACCES:
            return newError(interp, FLISP_ARG_ONE, permission_denied, "(fstat path[ linkp]): %s", strerror(errno));
        case ENOENT:
        case ENOTDIR:
            return newError(interp, FLISP_ARG_ONE, not_found, "(fstat path[ linkp]): %s", strerror(errno));
            break;
        case ENAMETOOLONG:
            return newError(interp, FLISP_ARG_ONE, invalid_value, "(fstat path[ linkp]): %s", strerror(errno));
        }
        return newError(interp, FLISP_ARG_ONE, io_error, "(fstat path[ linkp]): l/stat() failed: %s", strerror(errno));
    }

    /* (size _size_ type _type_ mode _mode_ uid _uid_ gid _gid_ ) */
    GC_CHECKPOINT;
    if      (S_ISBLK(info.st_mode)) type = "b";
    else if (S_ISCHR(info.st_mode)) type = "c";
    else if (S_ISDIR(info.st_mode)) type = "d";
    else if (S_ISFIFO(info.st_mode)) type = "p";
    else if (S_ISREG(info.st_mode)) type = "f";
    else if (S_ISLNK(info.st_mode)) type = "l";
    else if (S_ISSOCK(info.st_mode)) type = "s";
#if 0
    /* This works with muslc, but not gnu libc */
    else if (S_TYPEISMQ(info)) type = "Q";
    else if (S_TYPEISSEM(info)) type = "S";
    else if (S_TYPEISSHM(info)) type = "M";
    else if (S_TYPEISTMO(info)) type = "T";
#endif
    else type = "-";

    object = newString(interp, type);
    GC_TRACE(gcObject, object);
    object = newCons(interp, gcObject, &nil);
    GC_TRACE(gcResult, object);

    *gcObject = newSymbol(interp, "type");
    *gcResult = newCons(interp, gcObject, gcResult);

    *gcObject = newInteger(interp, info.st_gid);
    *gcResult = newCons(interp, gcObject, gcResult);

    *gcObject = newSymbol(interp, "gid");
    *gcResult = newCons(interp, gcObject, gcResult);

    *gcObject = newInteger(interp, info.st_uid);
    *gcResult = newCons(interp, gcObject, gcResult);

    *gcObject = newSymbol(interp, "uid");
    *gcResult = newCons(interp, gcObject, gcResult);

    *gcObject = newInteger(interp, info.st_mode & 07777);
    *gcResult = newCons(interp, gcObject, gcResult);

    *gcObject = newSymbol(interp, "mode");
    *gcResult = newCons(interp, gcObject, gcResult);

    *gcObject = newInteger(interp, info.st_size);
    *gcResult = newCons(interp, gcObject, gcResult);

    *gcObject = newSymbol(interp, "size");
    *gcResult = newCons(interp, gcObject, gcResult);

    GC_RETURN(*gcResult);
}
/** (fttyp[ fd]) - check if input or stream has a tty
 *
 * @param fd  stream
 *
 * @returns t if fd is associated with a tty.
 *
 */
Object *posixFttyP(Object *interp, Object** args, Object **env, size_t nArgs)
{
    FILE* fd = interp->input->fd;
    if (nArgs)
        fd = FLISP_ARG_ONE->fd;
    return (isatty(fileno(fd))) ? t : nil;
}
/** (fmkdir path[ mode]) - create directory
 *
 * @param path   String,  directory to create.
 * @param mode   Integer, mode for creating the directory, 0775 if not given.
 *
 * @returns t on success
 *
 * @throws
 * - invalid-value  If path is too long, a component of path is not an
 *                  existing directory or path is the empty string.
 * - permission-denied  If search or write permission is denied.
 * - file-exists    If the directory already exists.
 * - io-error
 *
 */
Object *posixMkdir(Object *interp, Object** args, Object **env, size_t nArgs)
{
    mode_t mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
    FLISP_ARG_TYPECHECK(FLISP_ARG_ONE, type_string,  "(fmkdir path[ mode) - path");
    if (nArgs > 1) {
        FLISP_ARG_TYPECHECK(FLISP_ARG_TWO, type_integer,  "(fmkdir path[ mode) - mode");
        mode = FLISP_ARG_TWO->value;
    }
    if (mkdir(FLISP_ARG_ONE->string, mode) == -1) {
        switch(errno) {
        case EACCES:
        case EROFS:
            return newError(interp, FLISP_ARG_ONE, permission_denied,
                                "(fmkdir path[ mode]): %s", strerror(errno));
        case EEXIST:
            return newError(interp, FLISP_ARG_ONE, file_exists,
                                "(fmkdir path[ mode]): %s", strerror(errno));
        case ENAMETOOLONG:
        case ENOENT:
        case ENOTDIR:
            return newError(interp, FLISP_ARG_ONE, invalid_value,
                                "(fmkdir path[ mode]): %s", strerror(errno));
        }
        return newError(interp, FLISP_ARG_ONE, io_error,
                            "(fmkdir path[ mode]): %s", strerror(errno));
    }
    return t;
}
/** (popen line[ mode]) - run command line and read from/write to it
 *
 * @param line  String containing a command line to be run by the
 *              system shell.
 * @param mode  "r" for reading from the standard output of the
 *              command. "w" for writing to the standard input of the
 *              command. If not given defaults to "r".
 *
 * @returns A stream object to read from/write to.
 *
 * @trows
 * - invalid-value  if mode is not "r" or "w".
 * - io-error
 *
 * Note: the stream must be closed with (pclose), it is an error to
 * use (fclose) on (popen) streams.
 */
Object *posixPopen(Object *interp, Object** args, Object **env, size_t nArgs)
{
    FILE *fd;
    char *mode = "r";

    if(nArgs > 1) {
        if (strcmp(FLISP_ARG_TWO->string, "r") && strcmp(FLISP_ARG_TWO->string, "w"))
            return newError(interp, invalid_value, FLISP_ARG_TWO,
                      "(popen path[ mode]) - mode must be \"r\" or \"w\", got: %s",
                      FLISP_ARG_TWO->string);
        mode = FLISP_ARG_TWO->string;
    }

    fd = popen(FLISP_ARG_ONE->string, mode);
    if (fd == NULL)
        return newError(interp, io_error, FLISP_ARG_ONE, "(popen path[ mode]) - popen() failed: %s", strerror(errno));

    return newStreamObject(interp, fd, FLISP_ARG_ONE->string);
}
/** (pclose stream) - close a stream opened with popen
 *
 * @param stream  Stream to close. Must be a stream opened with
 *                (popen).
 *
 * @returns The exit status of the command.
 *
 * @throws io-error if pclose() failed.
 */
Object *posixPclose(Object *interp, Object** args, Object **env, size_t nArgs)
{
    int result = pclose(FLISP_ARG_ONE->fd);

    if (result == -1)
        return newError(interp, io_error, FLISP_ARG_ONE, "pclose() failed: %s", strerror(errno));

    return newInteger(interp, result);
}

/* OS interface */

/** (system s) ⇒ i: run a command line in the system shell
 *
 * @param s .. Command line
 *
 * @returns The exit code of the shell.
 */
Object *posixSystem(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newInteger(interp, system(FLISP_ARG_ONE->string));
}

/** (getenv name) ⇒ value: get value of environment variable
 *
 * @param name .. Name of environment variable
 *
 * @returns *value* of environment variable *name* as string or `nil`
 *          if *name* does not exit.
 */
Object *posixGetenv(Object *interp, Object **args, Object **env, size_t nArgs)
{
    char *e = getenv(FLISP_ARG_ONE->string);
    if (e == NULL) return nil;
    return newStringWithLength(interp, e, strlen(e));
}

/** (getcwd) ⇒ value: get current working directory
 *
 * @returns string
 *
 * @throws different io errors
 *
 */
Object *posixGetcwd(Object *interp, Object **args, Object **env, size_t nArgs)
{
    char buf[PATH_MAX] = "";

    if (NULL == getcwd(buf, PATH_MAX))
        return newError(interp, io_error, nil, "getcwd() failed: %s", strerror(errno));
    return newString(interp, buf);
}

Object *fnm_pathname = &(Object) { .string = "FNM_PATHNAME" };
Object *fnm_noescape = &(Object) { .string = "FNM_NOESCAPE" };
Object *fnm_period = &(Object) { .string = "FNM_PERIOD" };

/** (fnmatch pattern string[ flags])
 * https://man7.org/linux/man-pages/man3/fnmatch.3p.html
 * flags: default 0
 * - FNM_PATHNAME
 * - FNM_NOESCAPE
 * - FNM_PERIOD
 **/
Object *posixFnmatch(Object *interp, Object** args, Object **env, size_t nArgs)
{
    int result, flags = 0;

    FLISP_ARG_TYPECHECK(FLISP_ARG_ONE, type_string, "(fnmatch pattern string[ flags]) - pattern");
    FLISP_ARG_TYPECHECK(FLISP_ARG_TWO, type_string, "(fnmatch pattern string[ flags]) - string");
    
    if (nArgs > 2)
        flags = FLISP_ARG_THREE->value;
    result = fnmatch(FLISP_ARG_ONE->string, FLISP_ARG_TWO->string, flags);
    if (result == 0)
        return t;
    if (result == FNM_NOMATCH)
        return nil;
    return newError(interp, invalid_value, nil, "(fnmatch pattern string[ flags]) - error");
}

Object *posix_extension = &(Object) { .string = "extension-posix" };

bool flisp_posix_register(Object *interp)
{
    flisp_register_constant(interp, posix_extension, newString(interp, FLISP_POSIX_VERSION));

    flisp_register_constant(interp, fnm_pathname, newInteger(interp, FNM_PATHNAME));
    flisp_register_constant(interp, fnm_noescape, newInteger(interp, FNM_NOESCAPE));
    flisp_register_constant(interp, fnm_period, newInteger(interp, FNM_PERIOD));
    
    return
        flisp_register_primitive(   interp, "fflush",  0, 1, type_stream, posixFflush)
        && flisp_register_primitive(interp, "fseek",   2, 3, nil,         posixFseek)
        && flisp_register_primitive(interp, "ftell",   0, 1, type_stream, posixFtell)
        && flisp_register_primitive(interp, "feof",    0, 1, type_stream, posixFeof)
        && flisp_register_primitive(interp, "fgetc",   0, 1, type_stream, posixFgetc)
        && flisp_register_primitive(interp, "fungetc", 1, 2, nil,         posixFungetc)
        && flisp_register_primitive(interp, "fgets",   0, 1, type_stream, posixFgets)
        && flisp_register_primitive(interp, "fstat",   1, 2, nil,         posixFstat)
        && flisp_register_primitive(interp, "fttyp",   0, 1, type_stream, posixFttyP)
        && flisp_register_primitive(interp, "fmkdir",  1, 2, nil,         posixMkdir)
        && flisp_register_primitive(interp, "popen",   1, 2, type_string, posixPopen)
        && flisp_register_primitive(interp, "pclose",  1, 1, type_stream, posixPclose)
        && flisp_register_primitive(interp, "system",  1, 1, type_string, posixSystem)
        && flisp_register_primitive(interp, "getenv",  1, 1, type_string, posixGetenv)
        && flisp_register_primitive(interp, "getcwd",  0, 0, nil,         posixGetcwd)
        && flisp_register_primitive(interp, "fnmatch", 2, 3, nil,         posixFnmatch);
}

/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
