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

