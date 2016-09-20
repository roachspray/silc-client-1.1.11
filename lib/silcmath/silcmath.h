/*

  silcmath.h

  Author: Pekka Riikonen <priikone@silcnet.org>

  Copyright (C) 1997 - 2005 Pekka Riikonen

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

*/

/****h* silcmath/SILC Math Interface
 *
 * DESCRIPTION
 *
 * SILC Math interface includes various utility functions such as
 * prime generation, and conversion routines. See the silcmp.h for the
 * SILC MP interface.
 *
 ***/

#ifndef SILCMATH_H
#define SILCMATH_H

#include "silcrng.h"

/****f* silcmath/SilcMathAPI/silc_math_gen_prime
 *
 * SYNOPSIS
 *
 *    SilcBool silc_math_gen_prime(SilcMPInt *prime, SilcUInt32 bits,
 *                                 SilcBool verbose, SilcRng rng);
 *
 * DESCRIPTION
 *
 *    Find appropriate prime. It generates a number by taking random bytes.
 *    It then tests the number that it's not divisible by any of the small
 *    primes and then it performs Fermat's prime test. I thank Rieks Joosten
 *    (r.joosten@pijnenburg.nl) for such a good help with prime tests.
 *
 *    If argument verbose is TRUE this will display some status information
 *    about the progress of generation.  If the `rng' is NULL then global
 *    RNG is used, if non-NULL then `rng' is used to generate the random
 *    number number.
 *
 ***/
SilcBool silc_math_gen_prime(SilcMPInt *prime, SilcUInt32 bits,
			     SilcBool verbose, SilcRng rng);

/****f* silcmath/SilcMathAPI/silc_math_prime_test
 *
 * SYNOPSIS
 *
 *    int silc_math_prime_test(SilcMPInt *p);
 *
 * DESCRIPTION
 *
 *    Performs primality testings for given number. Returns TRUE if the
 *    number is probably a prime.
 *
 ***/
SilcBool silc_math_prime_test(SilcMPInt *p);

#endif
