#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "write.h"


#define CHK_PRINT(PRINTER) if ((i = PRINTER) < 0) return i

int write_case(Object *object, FILE *fd)
{
    TypeObject *type = object->type;
    int i;
    int64_t n;
  
    if (type == type_integer)
        return fprintf(fd, "%"PRId64, object->value);
    if (type == type_primitive)
        return fprintf(fd, "primitive: %s  [%d, %d] %s", object->primitive->name,
                       object->primitive->nMinArgs, object->primitive->nMaxArgs,
                       flisp_symbol_string(object->primitive->argsType->type.name));
    else if (type == type_str)
        return fprintf(fd, "%s", ((SimpleObject*)object)->str);
    else if (type == type_ptr)
        return fprintf(fd, "ptr: 0X%"PRIXPTR, (uintptr_t)((SimpleObject*)object)->ptr);

    else if (type == type_type)
        return fprintf(fd, "%s", flisp_symbol_string(type->type.name));
    else if (type == type_string)
        return fprintf(fd, "\"%s\"", object->string);
    else if (type == type_symbol)
        return fprintf(fd, "%s", flisp_symbol_string(object));
    else if (type == type_cons) {
        CHK_PRINT(fputc('(', fd));
        CHK_PRINT(write_case(object->car, fd));
        while (object->cdr != nil) {
            object = object ->cdr;
            if (object->type == type_cons) {
                CHK_PRINT(fputc(' ', fd));
                CHK_PRINT(write_case(object->car, fd));
            } else {
                CHK_PRINT(fputs(" . ", fd));
                CHK_PRINT(write_case(object, fd));
            }
        }
        return fputc(')', fd);
    }
    else if (type == type_vector) {
        CHK_PRINT(fputc('[', fd));
        for (n = 0; n < object->length; n++)
            CHK_PRINT(write_case(object->objects[n], fd));
        return fputc(']', fd);
    }
    else if (type == type_lambda) {
        CHK_PRINT(fprintf(fd, "lambda: "));
        return write_case(object->closure.params, fd);
    }
    else if (type == type_macro) {
        CHK_PRINT(fprintf(fd, "macro: "));
        return write_case(object->closure.params, fd);
    }
    else if (type == type_error) {
        CHK_PRINT(fprintf(fd, "error:"));
        CHK_PRINT(write_case(object->error.type, fd));
        CHK_PRINT(fprintf(fd, ": "));
        CHK_PRINT(write_case(object->error.message, fd));
        CHK_PRINT(fprintf(fd, ": "));
        return write_case(object->error.culprit, fd);
    }
    else if (type == type_env) {
        Object *vars = object->env.vars;
        Object *vals = object->env.vals;
        if (object->env.parent == nil) {
            CHK_PRINT(fprintf(fd, "global: "));
        } else {
            CHK_PRINT(fprintf(fd, "environment:"));
        }
        while (vars != nil) {
            CHK_PRINT(write_case(vars->car, fd));
            CHK_PRINT(fputc(' ', fd));
            CHK_PRINT(write_case(vals->car, fd));
            if (vars->cdr != nil)
                CHK_PRINT(fprintf(fd, ", "));
            vars = vars->cdr;
            vals = vals->cdr;
        }
    }
    else if (type == type_stream) {
        CHK_PRINT(fprintf(fd, "stream 0X%"PRIX64" ", (uintptr_t)object->stream.fd));
        return write_case(object->stream.path, fd);
    }
    else if (type == type_extension) {
        CHK_PRINT(fprintf(fd, "extension: "));
        CHK_PRINT(write_case(object->extension.name, fd));
        CHK_PRINT(fputc(' ', fd));
        return write_case(object->extension.version, fd);
    }

    if (object->size)
        return fprintf(fd, "%s: %zu, %zu>", flisp_symbol_string(type->type.name),
                       object->length,
                       object->size
            );

    return fprintf(fd, "%s: 0X%"PRIX64, flisp_symbol_string(type->type.name), (uintptr_t)((SimpleObject*)object)->ptr);
}

void write_object(Object *object, FILE *fd)
{
    if (write_case(object, fd) < 0)
        fprintf(fd, "error: failed to write object: %s", strerror(errno));
}
/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
