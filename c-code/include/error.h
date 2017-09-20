#ifndef _ERROR_H_
#define _ERROR_H_

// values to indicate whether or not there is an error
#define NO_ERROR 0
#define ERROR -1

// Macro function to have compiler not print unused warning
#define UNUSED(x) (void)(x)

// Macro function to check if a value is null, if it is, bail
#define BAIL_NULL(val) if((val) == NULL) goto error;

// macro function to check if a value is -1, if it is, bail
#define BAIL_NEG_ONE(val) if((val) == -1) goto error;

// macro function to bail on nonzero number
#define BAIL_NONZERO(val) if((val) != 0) goto error;

// macro function to bail on a negative number
#define BAIL_NEG(val) if((val) < 0) goto error;

#endif
