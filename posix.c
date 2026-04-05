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
    FILE *fd = FLISP_STANDARD_OUTPUT.fd;

    if (nArgs)
        if (FLISP_ARG1 == t)
            fd = NULL;
        else {
            FLISP_ASSERT(FLISP_ARG1, type_stream,  "(fflush[ stream]) - stream");
            if (FLISP_ARG1->stream.fd == NULL)
                return newError(interp, invalid_value, FLISP_ARG1, "(fflush[ stream]) - stream already closed");
            fd = FLISP_ARG1->stream.fd;
        }
    else if (fd == NULL)
        return newError(interp, invalid_value, FLISP_ARG1, "(fflush[ stream]) - output stream not set");

    if (fflush(fd) == EOF)
        return newError2(interp, io_error, FLISP_ARG1, "(fflush[ stream]) - fflush() failed: ", strerror(errno));

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
    Object *object = FLISP_ARG1;

    if (object == nil) {
        object = FLISP_INTERP.input;
        if (object->stream.fd == NULL)
            return newError(interp, invalid_value, object, "(fseek stream offset[ relativep]) - input stream not set");
    } else {
        FLISP_ASSERT(object, type_stream,  "(fseek stream offset) - stream");
        if (object->stream.fd == NULL)
            return newError(interp, invalid_value, object, "(fseek stream) - stream already closed");
    }
    FLISP_ASSERT(FLISP_ARG2, type_integer, "(fseek stream offset) - offset");

    if (nArgs > 2 && FLISP_ARG3 != nil)
        whence = SEEK_CUR;
    else if (FLISP_ARG2->value < 0)
        whence = SEEK_END;
    result = fseeko(object->stream.fd, FLISP_ARG2->value, whence);
    if (result == -1)
        return newError2(interp, io_error, object, "(fseek stream offset) - fseeko() failed: ", strerror(errno));

    if ((pos = ftello(object->stream.fd)) == -1)
        return newError2(interp, io_error, object, "(fseek stream offset) - ftello() failed: ", strerror(errno));

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
    Object *object = FLISP_INTERP.input;
    off_t pos;

    if (nArgs)
        object = FLISP_ARG1;

    if (object->stream.fd == NULL)
        return newError(interp, invalid_value, object, "(ftell[ stream]) - stream already closed");

    if ((pos = ftello(object->stream.fd)) == -1)
        return newError2(interp, io_error, object, "(ftell[ stream]) - ftello() failed: ", strerror(errno));

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
    Object *object = FLISP_INTERP.input;

    if (nArgs)
        object = FLISP_ARG1;
    if (object->stream.fd == NULL)
        return newError(interp, invalid_value, object, "(feof[ stream]) - stream already closed");

    return (feof(object->stream.fd)) ? end_of_file : nil;
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
    Object *object = FLISP_INTERP.input;

    if (nArgs) {
        object = FLISP_ARG1;
        if (FLISP_ARG1->stream.fd == NULL)
            return newError(interp, invalid_value, object, "(fgetc[ stream]) - stream already closed");
    }
    
    c = fgetc(object->stream.fd);
    if (c == EOF) {
        if (ferror(object->stream.fd))
            return newError2(interp, io_error, object, "(fgetc[ stream]) - stream I/O error: ", strerror(errno));
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
    Object *object = FLISP_INTERP.input;

    FLISP_ASSERT(FLISP_ARG1, type_integer, "(fungetc char[ stream] - char)");
    c = (int)FLISP_ARG2->value;
    
    if (nArgs > 1) {
        FLISP_ASSERT(FLISP_ARG2, type_stream, "(fungetc char[ stream] - stream)");
        object = FLISP_ARG2;
    }
    if (object->stream.fd == NULL)
        return newError(interp, invalid_value, object, "(fungetc char [ stream]) - stream already closed");

    c = ungetc(c, object->stream.fd);
    if (c == EOF)
        return newError(interp, io_error, object, "(fungetc char [ stream]) - ungetc() failed");

    return newInteger(interp, FLISP_ARG1->value);
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
    Object *object = FLISP_INTERP.input;

    if (nArgs) {
        object = FLISP_ARG1;
        FLISP_ASSERT(object, type_stream, "(fgets[ stream] - stream)");
        if (object->stream.fd == NULL)
            return newError(interp, invalid_value, object, "(fgets[ stream]) - stream already closed");
    }
    input = malloc(INPUT_FMT_BUFSIZ);
    if(input == NULL)
        return newError2(interp, out_of_memory, object, "fgets() failed, ", strerror(errno));

    *input = '\0';

    if(fgets(input, INPUT_FMT_BUFSIZ, object->stream.fd) != NULL) {
        string = newString(interp, input);
        free(input);
        return string;
    }
    free(input);
    if (!feof(object->stream.fd))
        return newError2(interp, io_error, object, "fgets() failed: ", strerror(errno));
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

    FLISP_ASSERT(FLISP_ARG1, type_string,  "(fstat path[ linkp]) - stream");

    if (nArgs > 1 && FLISP_ARG2 != nil)
        result = lstat(FLISP_ARG1->string, &info);
    else
        result = stat(FLISP_ARG1->string, &info);

    if (result == -1) {
        switch(errno) {
        case EACCES:
            return newError2(interp, permission_denied, FLISP_ARG1, "(fstat path[ linkp]): ", strerror(errno));
        case ENOENT:
        case ENOTDIR:
            return newError2(interp, not_found, FLISP_ARG1, "(fstat path[ linkp]): ", strerror(errno));
            break;
        case ENAMETOOLONG:
            return newError2(interp, FLISP_ARG1, invalid_value, "(fstat path[ linkp]): ", strerror(errno));
        }
        return newError2(interp, io_error, FLISP_ARG1, "(fstat path[ linkp]): l/stat() failed: ", strerror(errno));
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
    FILE* fd = FLISP_INTERP.input->stream.fd;
    if (nArgs)
        fd = FLISP_ARG1->stream.fd;
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
    FLISP_ASSERT(FLISP_ARG1, type_string,  "(fmkdir path[ mode) - path");
    if (nArgs > 1) {
        FLISP_ASSERT(FLISP_ARG2, type_integer,  "(fmkdir path[ mode) - mode");
        mode = FLISP_ARG2->value;
    }
    if (mkdir(FLISP_ARG1->string, mode) == -1) {
        switch(errno) {
        case EACCES:
        case EROFS:
            return newError2(interp, permission_denied, FLISP_ARG1,
                                "(fmkdir path[ mode]): ", strerror(errno));
        case EEXIST:
            return newError2(interp, FLISP_ARG1, file_exists,
                                "(fmkdir path[ mode]): ", strerror(errno));
        case ENAMETOOLONG:
        case ENOENT:
        case ENOTDIR:
            return newError2(interp, invalid_value, FLISP_ARG1,
                                "(fmkdir path[ mode]): ", strerror(errno));
        }
        return newError2(interp, io_error, FLISP_ARG1,
                            "(fmkdir path[ mode]): ", strerror(errno));
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
        if (strcmp(FLISP_ARG2->string, "r") && strcmp(FLISP_ARG2->string, "w"))
            return newError2(interp, invalid_value, FLISP_ARG2,
                      "(popen path[ mode]) - mode must be \"r\" or \"w\", got: %s",
                      FLISP_ARG2->string);
        mode = FLISP_ARG2->string;
    }

    fd = popen(FLISP_ARG1->string, mode);
    if (fd == NULL)
        return newError2(interp, io_error, FLISP_ARG1, "(popen path[ mode]) - popen() failed: ", strerror(errno));

    return newStreamObject(interp, fd, FLISP_ARG1->string);
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
    int result = pclose(FLISP_ARG1->stream.fd);

    if (result == -1)
        return newError2(interp, io_error, FLISP_ARG1, "pclose() failed: ", strerror(errno));

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
    return newInteger(interp, system(FLISP_ARG1->string));
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
    char *e = getenv(FLISP_ARG1->string);
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
        return newError2(interp, io_error, nil, "getcwd() failed: ", strerror(errno));
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

    FLISP_ASSERT(FLISP_ARG1, type_string, "(fnmatch pattern string[ flags]) - pattern");
    FLISP_ASSERT(FLISP_ARG2, type_string, "(fnmatch pattern string[ flags]) - string");
    
    if (nArgs > 2)
        flags = FLISP_ARG3->value;
    result = fnmatch(FLISP_ARG1->string, FLISP_ARG2->string, flags);
    if (result == 0)
        return t;
    if (result == FNM_NOMATCH)
        return nil;
    return newError(interp, invalid_value, nil, "(fnmatch pattern string[ flags]) - error");
}

Object *flisp_posix_init(Object *interp, Object *extension)
{
    if (extension->extension.version != nil) return extension->extension.version;

    Object *e = nil;
    GC_CHECKPOINT;
    GC_TRACE(gcExt, extension);
    do {
        FLISP_UNLESS_ERR(flisp_register_constant(interp, fnm_pathname, newInteger(interp, FNM_PATHNAME)));
        FLISP_UNLESS_ERR(flisp_register_constant(interp, fnm_noescape, newInteger(interp, FNM_NOESCAPE)));
        FLISP_UNLESS_ERR(flisp_register_constant(interp, fnm_period, newInteger(interp, FNM_PERIOD)));
    
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "fflush",  0, 1, type_stream, posixFflush));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "fseek",   2, 3, nil,         posixFseek));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "ftell",   0, 1, type_stream, posixFtell));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "feof",    0, 1, type_stream, posixFeof));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "fgetc",   0, 1, type_stream, posixFgetc));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "fungetc", 1, 2, nil,         posixFungetc));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "fgets",   0, 1, type_stream, posixFgets));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "fstat",   1, 2, nil,         posixFstat));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "fttyp",   0, 1, type_stream, posixFttyP));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "fmkdir",  1, 2, nil,         posixMkdir));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "popen",   1, 2, type_string, posixPopen));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "pclose",  1, 1, type_stream, posixPclose));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "system",  1, 1, type_string, posixSystem));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "getenv",  1, 1, type_string, posixGetenv));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "getcwd",  0, 0, nil,         posixGetcwd));
        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "fnmatch", 2, 3, nil,         posixFnmatch));
        
        FLISP_UNLESS_ERR((*gcExt)->extension.version = newString(interp, FLISP_POSIX_VERSION));
    } while (0);
    GC_RELEASE;
    return e;
}

/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
