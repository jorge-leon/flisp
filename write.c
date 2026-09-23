#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "double.h"
#include "write.h"


#define CHK_PRINT(PRINTER) if ((i = PRINTER) < 0) return i

int write_case(Object *object, FILE *fd)
{
    TypeObject *type = object->type;
    int i;
    int64_t n;

    if (type == type_integer)
        return fprintf(fd, "%"PRId64, object->value);
    if (type == type_double)
        return fprintf(fd, "#d%f", object->number);
    if (type == type_primitive)
        return fprintf(fd, "#<primitive: %s [%d, %d] %s>", object->primitive->name,
                       object->primitive->nMinArgs, object->primitive->nMaxArgs,
                       flisp_symbol_string(object->primitive->argsType->type.name));
    else if (type == type_str)
        return fprintf(fd, "%s", object->str);
    else if (type == type_ptr)
        return fprintf(fd, "ptr: 0X%"PRIXPTR, (uintptr_t)((SimpleObject*)object)->ptr);

    else if (type == type_type)
        return fprintf(fd, "%s", flisp_symbol_string(((TypeObject*)object)->type.name));
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
                break;
            }
        }
        return fputc(')', fd);
    }
    else if (type == type_vector) {
        CHK_PRINT(fputc('[', fd));
        for (n = 0; n < object->length; n++) {
            if (n) CHK_PRINT(fputc(' ', fd));
            CHK_PRINT(write_case(object->objects[n], fd));
        }
        return fputc(']', fd);
    }
    else if (type == type_lambda || type == type_macro) {
        CHK_PRINT(fprintf(fd, "#<%s: ", flisp_symbol_string(type->type.name)));
        CHK_PRINT(write_case(object->closure.params, fd));
        return fputc('>', fd);
    }
    else if (type == type_error) {
        CHK_PRINT(fprintf(fd, "#<error:%s: %s: ",
                          flisp_symbol_string(object->error.type),
                          object->error.message->string));
        CHK_PRINT(write_case(object->error.culprit, fd));
        return fputc('>', fd);
    }
    else if (type == type_env) {
        Object *vars = object->env.vars;
        Object *vals = object->env.vals;
        if (object->env.parent == nil) {
            CHK_PRINT(fprintf(fd, "#<global: "));
        } else {
            CHK_PRINT(fprintf(fd, "#<environment: "));
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
        return fputc('>', fd);
    }
    else if (type == type_stream) {
        return fprintf(fd, "#<stream: 0X%"PRIX64" \"%s\">",
                       (uintptr_t)object->stream.fd,
                       object->stream.path->string
            );
    }
    else if (type == type_extension) {
        return fprintf(fd, "#<extension %s %s>",
                       object->extension.name->str,
                       object->extension.version->str);
    }

    if (object->size)
        return fprintf(fd, "#<%s: %zu elements, %zu extra>", flisp_symbol_string(type->type.name),
                       object->length,
                       object->size - sizeof(Object*)*object->length
            );

    return fprintf(fd, "%s: 0X%"PRIX64, flisp_symbol_string(type->type.name), (uintptr_t)((SimpleObject*)object)->ptr);
}

void write_object(Object *object, FILE *fd)
{
    if (write_case(object, fd) < 0)
        fprintf(fd, "#<error:io-error: failed to write object: %s>", strerror(errno));
}
/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
