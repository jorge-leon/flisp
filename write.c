/* leg20260926: This are the residues of the original/improved Lisp writer.
 *
 * For embedding, the writer is irrelevant so it is removed.
 * In the fl command line interpreter the princ extension is used, it
 * implements - again - a single case structure for printing Lisp
 * objects.
 *
 * The path forward is, to provide type object methods for string
 * serialization of objects, which can be modified from within Lisp,
 * i.e. fmt - formatters.
 *
 * The string objects from serialization can then be output with
 * fputs, et.al as required by a Lisp repl, or used as C-strings when
 * fLisp is embedded.
 */


// Write /////////////////////////////////////////////////////////////////////////////////

// Output ////////


/** writeChar - write character to file descriptor
 *
 * @param fd      open writeable file descriptor or NULL
 * @param ch      character to write
 *
 * returns: io-error
 */
Object *writeChar(FILE *fd, char ch)
{
    if (fd == NULL) return nil;

    if(fputc(ch, fd) == EOF)
	return flisp_static_error(io_error, &write_char_failed);
    return nil;
}

/** writeString - write string to file descriptor
 *
 * @param fd      open writeable file descriptor or NULL
 * @param str     string to write
 *
 * returns: io-error
 *
 */
Object *writeString(FILE *fd, char *str)
{
    if (fd == NULL) return nil;

    if(fputs(str, fd) == EOF)
	return flisp_static_error(io_error, &write_string_failed);
    return nil;
}

/* print_*() are helper functions for the writer primitives. We
 * comment them as if they were Lisp primitives, but they aren't:
 * The readably and the stream come optionally from **args, but what
 * to write comes after the nArgs parameter.
 * They return either nil or an error object
 */
/* (print_fmt o[ p[ s]])  */
Object *print_fmt(Object *interp, Object **args, size_t nArgs, char *format, ...)
{
    Object *output = interp->self.output;

    if (nArgs > 2) {
	FLISP_ASSERT(FLISP_ARG3, type_stream, "(print_fmt o[ p[ stream]]) - stream");
	output = FLISP_ARG3;
    }

    int result = 0;
    va_list(fmt_args);
    va_start(fmt_args, format);
    result = vfprintf(output->stream.fd, format, fmt_args);
    va_end(fmt_args);
    if (result < 0)
	return newError2(interp, io_error, output, "print_fmt failed: ", strerror(errno));
    return nil;
}
/* (print-string o[ p[ s]])*/
Object *print_string(Object *interp, Object **args, size_t nArgs, char *string)
{
    Object *output = interp->self.output;

    if (nArgs > 2) {
	FLISP_ASSERT(FLISP_ARG3, type_stream, "(print_string o[ p[ stream]]) - stream");
	output = FLISP_ARG3;
    }
    if (fputs(string, output->stream.fd) == EOF)
	return newError2(interp, io_error, output, "print_string failed: ", strerror(errno));
    return nil;
}
/* (write-/type/ obj[ readably[ stream]) */
Object *primitiveWInteger(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_integer, "(write-integer o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_fmt(interp, args, nArgs, "%"PRId64, FLISP_ARG1->value));
    return FLISP_ARG1;
}
Primitive w_i_p = { .name = "write-integer", .nMinArgs = 2, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWInteger };
SimpleObject write_integer = { .type = &type_primitive_obj, .size = 0, .primitive = &w_i_p };


Object *primitiveWString(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_string, "(write-string o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_string(interp, args, nArgs, FLISP_ARG1->string));
    return FLISP_ARG1;
}
Primitive w_string_p = { .name = "write-string", .nMinArgs = 2, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWString };
static SimpleObject write_string = { .type = &type_primitive_obj, .size = 0, .primitive = &w_string_p };

Object *primitiveWStr(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_str, "(write-str o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_string(interp, args, nArgs, FLISP_ARG1->str));
    return FLISP_ARG1;
}
Primitive w_str_p = { .name = "write-str", .nMinArgs = 2, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWStr };
static SimpleObject write_str = { .type = &type_primitive_obj, .size = 0, .primitive = &w_str_p };

/* (write-symbol o[ p[ s]])*/
Object *primitiveWSymbol(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_symbol, "(write-symbol o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_string(interp, args, nArgs, flisp_symbol_string(FLISP_ARG1)));
    return FLISP_ARG1;
}
Primitive w_symbol_p = { .name = "write-symbol", .nMinArgs = 2, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWSymbol };
static SimpleObject write_symbol = { .type = &type_primitive_obj, .size = 0, .primitive = &w_symbol_p };

/* (write-primitive o[ p[ s]])*/
Object *primitiveWPrimitive(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_primitive, "(write-primitive o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_fmt(interp, args, nArgs, "#<primitive %s [%zu, %zu] %s>",
			      FLISP_ARG1->primitive->name,
			      FLISP_ARG1->primitive->nMinArgs,
			      FLISP_ARG1->primitive->nMaxArgs,
			      flisp_symbol_string(FLISP_ARG1->primitive->argsType->type.name)
			));
    return FLISP_ARG1;
}
Primitive w_primitive_p = { .name = "write-primitive", .nMinArgs = 2, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWPrimitive };
static SimpleObject write_primitive = { .type = &type_primitive_obj, .size = 0, .primitive = &w_primitive_p };

Object *primitiveWVector(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_vector, "(write-vector o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_fmt(interp, args, nArgs, "#<vector %zu>", FLISP_ARG1->length));
    return FLISP_ARG1;
}
Primitive w_vector_p = { .name = "write-vector", .nMinArgs = 1, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWVector };
static SimpleObject write_vector = { .type = &type_primitive_obj, .size = 0, .primitive = &w_vector_p };

Object *primitiveWValues(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_values, "(write-values o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_fmt(interp, args, nArgs, "#<values %zu>", flisp_list_length(FLISP_ARG1->values)));
    return FLISP_ARG1;
}
Primitive w_values_p = { .name = "write-values", .nMinArgs = 1, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWValues };
static SimpleObject write_values = { .type = &type_primitive_obj, .size = 0, .primitive = &w_values_p };

Object *primitiveWType(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_type, "(write-type o[ p[ s]]) - o");
    char *name = flisp_symbol_string(((TypeObject*)FLISP_ARG1)->type.name);
    if (nArgs > 1 && FLISP_ARG2 != nil)
	return print_string(interp, args, nArgs, name);
    /* Note: this *requires* the type name to be prefixed with "type-" */
    FLISP_CHECK_ERR(print_fmt(interp, args, nArgs, "#<type %s>", name+(sizeof("type-"))-1));
    return FLISP_ARG1;
}
Primitive w_type_p = { .name = "write-type", .nMinArgs = 1, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWType };
static SimpleObject write_type = { .type = &type_primitive_obj, .size = 0, .primitive = &w_type_p };

Object *primitiveWInterp(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_interpreter, "(write-interpreter o[ p[ s]]) - o");
    FLISP_CHECK_ERR(print_fmt(interp, args, nArgs, "#<interpreter 0X%"PRIXPTR ">", (uintptr_t)interp));
    return FLISP_ARG1;
}
Primitive w_interp_p = { .name = "write-interpreter", .nMinArgs = 1, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWInterp };
static SimpleObject write_interpreter = { .type = &type_primitive_obj, .size = 0, .primitive = &w_interp_p };

Object *print_object(Object *, Object **, size_t, Object *);

/* (write-stream o[ p[ s]])*/
Object *primitiveWStream(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_stream, "(write-stream o[ p[ s]]) - o");
    GC_CHECKPOINT;
    GC_TRACE(gcArgs, *args);
    GC_CHECK_ERR(print_fmt(interp, gcArgs, nArgs, "#<stream 0X%"PRIX64" ", FLISP_ARG1->stream.fd));
    GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, FLISP_ARG1->stream.path));
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ">"));
    GC_RETURN((*gcArgs)->car);
}
Primitive w_stream_p = { .name = "write-stream", .nMinArgs = 1, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWStream };
static SimpleObject write_stream = { .type = &type_primitive_obj, .size = 0, .primitive = &w_stream_p };

/* (write-extension o[ p[ s]])*/
Object *primitiveWExtension(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_extension, "(write-extension o[ p[ s]]) - o");
    GC_CHECKPOINT;
    GC_TRACE(gcArgs, *args);
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, "#<extension "));
    GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcArgs)->car->extension.name));
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ", "));
    GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcArgs)->car->extension.version));
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ">"));
    GC_RETURN((*gcArgs)->car);
}
Primitive w_extension_p = { .name = "write-extension", .nMinArgs = 1, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWExtension };
static SimpleObject write_extension = { .type = &type_primitive_obj, .size = 0, .primitive = &w_extension_p };

/* (write-cons o[ p[ s]])*/
Object *primitiveWCons(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_cons, "(write-cons o[ p[ s]]) - o");
    GC_CHECKPOINT;
    GC_TRACE(gcArgs, *args);
    GC_TRACE(gcCons, FLISP_ARG1);
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, "("));
    GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcCons)->car));
    while ((*gcCons)->cdr != nil) {
	*gcCons = (*gcCons)->cdr;
	if ((*gcCons)->type == type_cons) {
	    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, " "));
	    GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcCons)->car));
	} else {
	    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, " . "));
	    GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, *gcCons));
	    break;
	}
    }
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ")"));
    GC_RETURN((*gcArgs)->car);
}
Primitive w_cons_p = { .name = "write-cons", .nMinArgs = 1, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWCons };
static SimpleObject write_cons = { .type = &type_primitive_obj, .size = 0, .primitive = &w_cons_p };

/* (write-closure o[ p[ s]])*/
Object *primitiveWClosure(Object *interp, Object **args, Object **env, size_t nArgs)
{
    Object *closure = FLISP_ARG1;
    if (closure->type != type_lambda && closure->type != type_macro)
	return newError2(interp, wrong_type_argument, closure,
			   "(write-closure o[ p[ s]]) - o expected type-lambda or type-macro, got ",
			   (closure)->type->type.name->str);
    GC_CHECKPOINT;
    GC_TRACE(gcArgs, *args);
    GC_CHECK_ERR(print_fmt(interp, gcArgs, nArgs, "#<%s ",
			 ((SimpleObject*)((TypeObject*)closure->type)->type.name)->str+(sizeof("type-"))-1));
    GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, closure->closure.params));
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ">"));
    GC_RETURN((*gcArgs)->car);
}
Primitive w_closure_p = { .name = "write-closure", .nMinArgs = 1, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWClosure };
static SimpleObject write_closure = { .type = &type_primitive_obj, .size = 0, .primitive = &w_closure_p };

/* (write-env o[ p[ s]])*/
Object *primitiveWEnv(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_env, "(write-env o[ p[ s]]) - o");
    GC_CHECKPOINT;
    GC_TRACE(gcArgs, *args);
    GC_TRACE(gcSymbols, FLISP_ARG1->env.vars);
    GC_TRACE(gcValues, FLISP_ARG1->env.vals);

    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, "<#Env "));
    while (*gcSymbols != nil) {
	GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcSymbols)->car));
	GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, " "));
	GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcValues)->car));
	if ((*gcSymbols)->cdr != nil) GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ",  "));
	*gcSymbols = (*gcSymbols)->cdr;
	*gcValues = (*gcValues)->cdr;
    }
    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ">"));
    GC_RETURN((*gcArgs)->car);
}
Primitive w_env_p = { .name = "write-env", .nMinArgs = 1, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWEnv };
static SimpleObject write_env = { .type = &type_primitive_obj, .size = 0, .primitive = &w_env_p };

/* (write-error o[ p[ s]])*/
Object *primitiveWError(Object *interp, Object **args, Object **env, size_t nArgs)
{
    FLISP_ASSERT(FLISP_ARG1, type_error, "(write-error o[ p[ s]]) - o");
    bool readably = nArgs > 1 && FLISP_ARG2 != nil;
    GC_CHECKPOINT;
    GC_TRACE(gcError, FLISP_ARG1);
    GC_TRACE(gcArgs, nil);
    if (nArgs > 2) {
	*gcArgs = newCons(interp, &FLISP_ARG3, &nil);
	*gcArgs = newCons(interp, &nil, gcArgs);
	*gcArgs = newCons(interp, gcError, gcArgs);
    } else {
	*gcArgs = newCons(interp, gcError, &nil);
	nArgs = 1;
    }
    if (readably) {
	GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, "#<error "));
	GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcError)->error.type));
	GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ": "));
	GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcError)->error.message));
	GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ", "));
	GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcError)->error.culprit));
	GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ">"));
    } else {
	GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, "error:"));
	GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcError)->error.type));
	GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ": "));
	GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcError)->error.message));
	if ((*gcError)->error.culprit != nil) {
	    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, ": '"));
	    GC_CHECK_ERR(print_object(interp, gcArgs, nArgs, (*gcError)->error.culprit));
	    GC_CHECK_ERR(print_string(interp, gcArgs, nArgs, "'\n"));
	} else {
	    GC_CHECK_ERR(*gcError);
	}
    }
    /* Note: if we return the error object, we get it double printed */
    //GC_RETURN(*gcError);
    return nil;
}
Primitive w_error_p = { .name = "write-error", .nMinArgs = 1, .nMaxArgs = 3, .argsType = type_any, .eval = primitiveWError };
static SimpleObject write_error = { .type = &type_primitive_obj, .size = 0, .primitive = &w_error_p };

/** (write o[ p[ fd]]) - write object
 *
 * @param o   Object to write.
 * @param p   If not nil escape strings.
 * @param fd  Stream to write to, else output stream.
 *
 * @returns: o
 *
 * throws: wrong-number-of-arguments, io-error, gc-error
 *
 * If no stream is specified the interpreters output file descriptor is used.
 * If the interpreters output file descriptor is NULL, no output is written.
 */
Object *print_object_fallback(Object *interp, Object *object, Object *output)
{
    char *type = flisp_symbol_string(object->type->type.name);

    if (object->size)
	fprintf(output->stream.fd, "#<%s, %zu, %zu>",
		type+(sizeof("type-"))-1,
		object->length,
		object->size
	    );
    else
	fprintf(output->stream.fd, "#<%s>", type+(sizeof("type-"))-1);
    return object;
}
Object *primitiveWrite(Object *interp, Object **args, Object **env, size_t nArgs)
{
    Object *output = interp->self.output;
    Object *writer = FLISP_ARG1->type->type.write;

    if (nArgs >1 && FLISP_IS_ERR(FLISP_ARG2))
	return newError(interp, invalid_value, FLISP_ARG2, "(write o[ p[ fd]]) - p");

    if (nArgs > 2) output = FLISP_ARG3;
    if (output == nil) return nil;
    FLISP_ASSERT(output, type_stream, "(write o [p [fd]]) - fd");
    if (output->stream.fd == NULL)
	return newError(interp, invalid_value, nil, "(write o[ p [fd]) - fd already closed");
    if (writer == nil)
	return print_object_fallback(interp, FLISP_ARG1, output);

    /* Note: temporary only allow primitives, later we want lambda's also. */
    FLISP_ASSERT(writer, type_primitive, "(w o[ p[ s]]) - type writer of o");

    GC_CHECKPOINT;
    GC_TRACE(gcObject, FLISP_ARG1);
    /* Note: why do we evaluate in the global environment? */
    GC_CHECK_ERR(writer->primitive->eval(interp, args, &interp->self.global, nArgs));
    GC_RETURN(*gcObject);
}
Object *print_object(Object *interp, Object **args, size_t nArgs, Object *object)
{
    if (nArgs > 2) {
	if (FLISP_ARG3 == nil) return nil;
	FLISP_ASSERT(FLISP_ARG3, type_stream, "(print_object o[ p[ s]]) - s");
	if (FLISP_ARG3->stream.fd == NULL)
	    return newError(interp, invalid_value, nil, "(print_object(o[ p[ s]]) - s already closed");
    }
    Object *newArgs;
    FLISP_CHECK_ERR(newArgs = newCons(interp, &object, &(*args)->cdr));
    return primitiveWrite(interp, &newArgs, &interp->self.global, nArgs);
}
/** flisp_write_object - format and write object to file descriptor
 *
 * @param interp    fLisp interpreter
 * @param object    object to be serialized
 * @param readably  if not nil write in a format which can be read back
 * @param stream    open writeable stream, or nil to write to interp output
 *
 * @returns nil on success, io-error, gc-error, oom-error
 *
 */
Object* flisp_write_object(Object *interp, Object *object, Object *readably, Object *stream)
{
    if (stream == nil) return nil;
    FLISP_ASSERT(stream, type_stream, "flisp_write_object(interp, object, readably, stream) - stream");
    if (stream->stream.fd == NULL)
	return newError(interp, invalid_value, nil, "flisp_write_object(object, readaybly, stream) - stream already closed");
    GC_CHECKPOINT;
    GC_TRACE(gcObject, object);
    GC_TRACE(gcReadably, readably);
    GC_TRACE(gcArgs, newCons(interp, &stream, &nil));
    GC_CHECK_ERR(*gcArgs);
    GC_CHECK_ERR(*gcArgs = newCons(interp, gcReadably, gcArgs));
    GC_CHECK_ERR(*gcArgs = newCons(interp, gcObject, gcArgs));
    GC_RETURN(print_object(interp, gcArgs, 3, *gcObject));
}
	FLISP_UNLESS_ERR(flisp_register_primitive(interp, "write",                  1,  3, type_any,      primitiveWrite));
	/* Types */
	FLISP_WHILE_OK(flisp_register_type(interp, "type-integer",     type_integer,     (Object*)&flisp_init_invalid, (Object*)&write_integer));
	FLISP_WHILE_OK(flisp_register_type(interp, "type-primitive",   type_primitive,   (Object*)&flisp_init_invalid, (Object*)&write_primitive));
	FLISP_WHILE_OK(flisp_register_type(interp, "type-str",         type_str,         (Object*)&flisp_init_invalid, (Object*)&write_str));

	FLISP_WHILE_OK(flisp_register_type(interp, "type-type",        type_type,        (Object*)&type_init_type,     (Object*)&write_type));
	FLISP_WHILE_OK(flisp_register_type(interp, "type-string",      type_string,      (Object*)&flisp_init_invalid, (Object*)&write_string));
	FLISP_WHILE_OK(flisp_register_type(interp, "type-symbol",      type_symbol,      (Object*)&flisp_init_invalid, (Object*)&write_symbol));
	FLISP_WHILE_OK(flisp_register_type(interp, "type-cons",        type_cons,        (Object*)&type_init_cons,     (Object *)&write_cons));
	FLISP_WHILE_OK(flisp_register_type(interp, "type-vector",      type_vector,      nil, (Object *)&write_vector));
	FLISP_WHILE_OK(flisp_register_type(interp, "type-lambda",      type_lambda,      (Object*)&flisp_init_invalid, (Object*)&write_closure));
	FLISP_WHILE_OK(flisp_register_type(interp, "type-macro",       type_macro,       (Object*)&flisp_init_invalid, (Object*)&write_closure));
	FLISP_WHILE_OK(flisp_register_type(interp, "type-error",       type_error,       (Object*)&type_init_error,    (Object*)&write_error));
	FLISP_WHILE_OK(flisp_register_type(interp, "type-stream",      type_stream,      (Object*)&flisp_init_invalid, (Object*)&write_stream));

	FLISP_WHILE_OK(flisp_register_type(interp, "type-env",         type_env,         (Object*)&flisp_init_invalid, (Object*)&write_env));
	FLISP_WHILE_OK(flisp_register_type(interp, "type-interpreter", type_interpreter, (Object*)&flisp_init_invalid, (Object*)&write_interpreter));
	FLISP_WHILE_OK(flisp_register_type(interp, "type-extension",   type_extension,   (Object*)&flisp_init_invalid, (Object*)&write_extension));
	FLISP_WHILE_OK(flisp_register_type(interp, "type-values",      type_values,      (Object*)&flisp_init_invalid, (Object*)&write_values));
