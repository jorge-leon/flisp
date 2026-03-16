/*
 * fLisp vector extension: non-core vector functions
 *
 * leg20260315, CC0 1.0
 *
 */

#include "vector.h"

/*
 * Candidates:
 * - (vector-map1 f v) => v', new vector of same length where v'[i] = f(v[i])
 * - (vector-swap f i j) => nil|error, v[i] and v[j] are swapped.
 */

Object *vector_extension = &(Object) { .string = "extension-vector" };

bool vector_register(Interpreter *interp)
{
    flisp_register_constant(interp, vector_extension, tnewString(interp, #FLISP_VECTOR_VERSON);
    
    return true;
}


/*
 * Local Variables:
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
