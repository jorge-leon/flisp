#include "lisp.h"
#include "fmt.h"

#if 0
Object *writeStringReadably(FILE *fd, char *string)
{
    char *escape;
    Object *e = nil;
    if (flisp_not_same(&e, writeChar(fd, '"'))) return e;

    for (; *string; ++string) {
        switch (*string) {
        case '"':
            escape = "\\\"";
            break;
        case '\t':
            escape = "\\t";
            break;
        case '\r':
            escape = "\\r";
            break;
        case '\n':
            escape = "\\n";
            break;
        case '\\':
            escape = "\\\\";
            break;
        default:
            if (flisp_not_same(&e, writeChar(fd, *string))) return e;
            continue;
        }
        if (flisp_not_same(&e, writeString(fd, escape))) return e;
    }
    return writeChar(fd, '"');
}
/* Consider (print-string-with-len), using fwrite() */
Object *print_string_readably(Object *interp, Object **args, size_t nArgs, char *string)
{
    Object *output = interp->self.output;

    if (nArgs > 2) {
        FLISP_ASSERT(FLISP_ARG3, type_stream, "(print_string o[ p[ stream]]) - stream");
        output = FLISP_ARG3;
    }
    return writeStringReadably(output->stream.fd, string);
}
/* (print_strp o[ p[ s]])  print string taking into account readabl p'redicate */
Object *print_strp(Object *interp, Object **args, size_t nArgs, char *string)
{
    bool readably = false;
    if (nArgs > 1) {
        FLISP_CHECK_ERR(FLISP_ARG2);
        readably = FLISP_ARG2 != nil;
    }
    if (readably)
        return print_string_readably(interp, args, nArgs, string);
    else
        return print_string(interp, args, nArgs, string);
}
#endif
/* (fmt o[ arg..]) => string */
Object *XprimitiveFmt(Object *interp, Object **args, Object **env, size_t nArgs)
{
    return newError(interp, not_found, (Object *)FLISP_ARG1->type, "(fmt o[ arg..]) - o no formatter for this type");
}

FLISP_DEFINE_CONSTANT(extension_fmt,"fmt");
FLISP_DEFINE_CONSTANT(extension_fmt_version,FLISP_FMT_VERSION);

Object *flisp_fmt_init(Object *interp, Object *extension)
{

    if (extension->extension.version != nil) return extension->extension.version;

    Object *e = nil;
    GC_CHECKPOINT;
    GC_TRACE(gcExt, extension);
    do {

        FLISP_UNLESS_ERR(flisp_register_primitive(interp, "Xfmt", 1,  -1, type_any,  XprimitiveFmt));

        FLISP_UNLESS_ERR((*gcExt)->extension.version = extension_fmt_version);
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
