/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2009,2010,2011 Intel Corporation.
 * Copyright (C) 1999-2005  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 */
/*
 * Copyright (C) 1999-2005  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Note: a lot of this code is based on code that was taken from Mesa.
 *
 * Changes compared to the original code from Mesa:
 *
 * - instead of allocating matrix->m and matrix->inv using malloc, our
 *   public CoglMatrix typedef is large enough to directly contain the
 *   matrix, its inverse, a type and a set of flags.
 * - instead of having a _cogl_matrix_analyse which updates the type,
 *   flags and inverse, we have _cogl_matrix_update_inverse which
 *   essentially does the same thing (internally making use of
 *   _cogl_matrix_update_type_and_flags()) but with additional guards in
 *   place to bail out when the inverse matrix is still valid.
 * - when initializing a matrix with the identity matrix we don't
 *   immediately initialize the inverse matrix; rather we just set the
 *   dirty flag for the inverse (since it's likely the user won't request
 *   the inverse of the identity matrix)
 */

#include "cogl-config.h"

#include <cogl-util.h>
#include <cogl-debug.h>
#include <cogl-matrix.h>
#include <cogl-matrix-private.h>

#include <glib.h>
#include <math.h>
#include <string.h>

#include <cogl-gtype-private.h>
COGL_GTYPE_DEFINE_BOXED (Matrix, matrix,
                         cogl_matrix_copy,
                         cogl_matrix_free);

/*
 * Symbolic names to some of the entries in the matrix
 *
 * These are handy for the viewport mapping, which is expressed as a matrix.
 */
#define MAT_SX 0
#define MAT_SY 5
#define MAT_SZ 10
#define MAT_TX 12
#define MAT_TY 13
#define MAT_TZ 14

/*
 * These identify different kinds of 4x4 transformation matrices and we use
 * this information to find fast-paths when available.
 */
enum CoglMatrixType {
   COGL_MATRIX_TYPE_GENERAL,	/**< general 4x4 matrix */
   COGL_MATRIX_TYPE_IDENTITY,	/**< identity matrix */
   COGL_MATRIX_TYPE_3D_NO_ROT,	/**< orthogonal projection and others... */
   COGL_MATRIX_TYPE_PERSPECTIVE,	/**< perspective projection matrix */
   COGL_MATRIX_TYPE_2D,		/**< 2-D transformation */
   COGL_MATRIX_TYPE_2D_NO_ROT,	/**< 2-D scale & translate only */
   COGL_MATRIX_TYPE_3D,		/**< 3-D transformation */
   COGL_MATRIX_N_TYPES
} ;

#define DEG2RAD (G_PI/180.0)

/* Dot product of two 2-element vectors */
#define DOT2(A,B)  ( (A)[0]*(B)[0] + (A)[1]*(B)[1] )

/* Dot product of two 3-element vectors */
#define DOT3(A,B)  ( (A)[0]*(B)[0] + (A)[1]*(B)[1] + (A)[2]*(B)[2] )

#define CROSS3(N, U, V) \
do { \
    (N)[0] = (U)[1]*(V)[2] - (U)[2]*(V)[1]; \
    (N)[1] = (U)[2]*(V)[0] - (U)[0]*(V)[2]; \
    (N)[2] = (U)[0]*(V)[1] - (U)[1]*(V)[0]; \
} while (0)

#define SUB_3V(DST, SRCA, SRCB) \
do { \
    (DST)[0] = (SRCA)[0] - (SRCB)[0]; \
    (DST)[1] = (SRCA)[1] - (SRCB)[1]; \
    (DST)[2] = (SRCA)[2] - (SRCB)[2]; \
} while (0)

#define LEN_SQUARED_3FV( V ) ((V)[0]*(V)[0]+(V)[1]*(V)[1]+(V)[2]*(V)[2])

/*
 * \defgroup MatFlags MAT_FLAG_XXX-flags
 *
 * Bitmasks to indicate different kinds of 4x4 matrices in CoglMatrix::flags
 */
#define MAT_FLAG_IDENTITY       0     /*< is an identity matrix flag.
                                       *   (Not actually used - the identity
                                       *   matrix is identified by the absense
                                       *   of all other flags.)
                                       */
#define MAT_FLAG_GENERAL        0x1   /*< is a general matrix flag */
#define MAT_FLAG_ROTATION       0x2   /*< is a rotation matrix flag */
#define MAT_FLAG_TRANSLATION    0x4   /*< is a translation matrix flag */
#define MAT_FLAG_UNIFORM_SCALE  0x8   /*< is an uniform scaling matrix flag */
#define MAT_FLAG_GENERAL_SCALE  0x10  /*< is a general scaling matrix flag */
#define MAT_FLAG_GENERAL_3D     0x20  /*< general 3D matrix flag */
#define MAT_FLAG_PERSPECTIVE    0x40  /*< is a perspective proj matrix flag */
#define MAT_FLAG_SINGULAR       0x80  /*< is a singular matrix flag */
#define MAT_DIRTY_TYPE          0x100  /*< matrix type is dirty */
#define MAT_DIRTY_FLAGS         0x200  /*< matrix flags are dirty */
#define MAT_DIRTY_INVERSE       0x400  /*< matrix inverse is dirty */

/* angle preserving matrix flags mask */
#define MAT_FLAGS_ANGLE_PRESERVING (MAT_FLAG_ROTATION | \
				    MAT_FLAG_TRANSLATION | \
				    MAT_FLAG_UNIFORM_SCALE)

/* geometry related matrix flags mask */
#define MAT_FLAGS_GEOMETRY (MAT_FLAG_GENERAL | \
			    MAT_FLAG_ROTATION | \
			    MAT_FLAG_TRANSLATION | \
			    MAT_FLAG_UNIFORM_SCALE | \
			    MAT_FLAG_GENERAL_SCALE | \
			    MAT_FLAG_GENERAL_3D | \
			    MAT_FLAG_PERSPECTIVE | \
	                    MAT_FLAG_SINGULAR)

/* length preserving matrix flags mask */
#define MAT_FLAGS_LENGTH_PRESERVING (MAT_FLAG_ROTATION | \
				     MAT_FLAG_TRANSLATION)


/* 3D (non-perspective) matrix flags mask */
#define MAT_FLAGS_3D (MAT_FLAG_ROTATION | \
		      MAT_FLAG_TRANSLATION | \
		      MAT_FLAG_UNIFORM_SCALE | \
		      MAT_FLAG_GENERAL_SCALE | \
		      MAT_FLAG_GENERAL_3D)

/* dirty matrix flags mask */
#define MAT_DIRTY_ALL      (MAT_DIRTY_TYPE | \
			    MAT_DIRTY_FLAGS | \
			    MAT_DIRTY_INVERSE)


/*
 * Test geometry related matrix flags.
 *
 * @mat a pointer to a CoglMatrix structure.
 * @a flags mask.
 *
 * Returns: non-zero if all geometry related matrix flags are contained within
 * the mask, or zero otherwise.
 */
#define TEST_MAT_FLAGS(mat, a)  \
    ((MAT_FLAGS_GEOMETRY & (~(a)) & ((mat)->flags) ) == 0)



/*
 * Names of the corresponding CoglMatrixType values.
 */
static const char *types[] = {
   "COGL_MATRIX_TYPE_GENERAL",
   "COGL_MATRIX_TYPE_IDENTITY",
   "COGL_MATRIX_TYPE_3D_NO_ROT",
   "COGL_MATRIX_TYPE_PERSPECTIVE",
   "COGL_MATRIX_TYPE_2D",
   "COGL_MATRIX_TYPE_2D_NO_ROT",
   "COGL_MATRIX_TYPE_3D"
};


/*
 * Identity matrix.
 */
static float identity[16] = {
   1.0, 0.0, 0.0, 0.0,
   0.0, 1.0, 0.0, 0.0,
   0.0, 0.0, 1.0, 0.0,
   0.0, 0.0, 0.0, 1.0
};


#define A(row,col)  a[(col<<2)+row]
#define B(row,col)  b[(col<<2)+row]
#define R(row,col)  result[(col<<2)+row]

/*
 * Perform a full 4x4 matrix multiplication.
 *
 * <note>It's assumed that @result != @b. @product == @a is allowed.</note>
 *
 * <note>KW: 4*16 = 64 multiplications</note>
 */
static void
matrix_multiply4x4 (float *result, const float *a, const float *b)
{
  int i;
  for (i = 0; i < 4; i++)
    {
      const float ai0 = A(i,0),  ai1=A(i,1),  ai2=A(i,2),  ai3=A(i,3);
      R(i,0) = ai0 * B(0,0) + ai1 * B(1,0) + ai2 * B(2,0) + ai3 * B(3,0);
      R(i,1) = ai0 * B(0,1) + ai1 * B(1,1) + ai2 * B(2,1) + ai3 * B(3,1);
      R(i,2) = ai0 * B(0,2) + ai1 * B(1,2) + ai2 * B(2,2) + ai3 * B(3,2);
      R(i,3) = ai0 * B(0,3) + ai1 * B(1,3) + ai2 * B(2,3) + ai3 * B(3,3);
    }
}

/*
 * Multiply two matrices known to occupy only the top three rows, such
 * as typical model matrices, and orthogonal matrices.
 *
 * @a matrix.
 * @b matrix.
 * @product will receive the product of \p a and \p b.
 */
static void
matrix_multiply3x4 (float *result, const float *a, const float *b)
{
  int i;
  for (i = 0; i < 3; i++)
    {
      const float ai0 = A(i,0), ai1 = A(i,1), ai2 = A(i,2), ai3 = A(i,3);
      R(i,0) = ai0 * B(0,0) + ai1 * B(1,0) + ai2 * B(2,0);
      R(i,1) = ai0 * B(0,1) + ai1 * B(1,1) + ai2 * B(2,1);
      R(i,2) = ai0 * B(0,2) + ai1 * B(1,2) + ai2 * B(2,2);
      R(i,3) = ai0 * B(0,3) + ai1 * B(1,3) + ai2 * B(2,3) + ai3;
    }
  R(3,0) = 0;
  R(3,1) = 0;
  R(3,2) = 0;
  R(3,3) = 1;
}

#undef A
#undef B
#undef R

/*
 * Multiply a matrix by an array of floats with known properties.
 *
 * @mat pointer to a CoglMatrix structure containing the left multiplication
 * matrix, and that will receive the product result.
 * @m right multiplication matrix array.
 * @flags flags of the matrix \p m.
 *
 * Joins both flags and marks the type and inverse as dirty.  Calls
 * matrix_multiply3x4() if both matrices are 3D, or matrix_multiply4x4()
 * otherwise.
 */
static void
matrix_multiply_array_with_flags (CoglMatrix *result,
                                  const float *array,
                                  unsigned int flags)
{
  result->flags |= (flags | MAT_DIRTY_TYPE | MAT_DIRTY_INVERSE);

  if (TEST_MAT_FLAGS (result, MAT_FLAGS_3D))
    matrix_multiply3x4 ((float *)result, (float *)result, array);
  else
    matrix_multiply4x4 ((float *)result, (float *)result, array);
}

/* Joins both flags and marks the type and inverse as dirty.  Calls
 * matrix_multiply3x4() if both matrices are 3D, or matrix_multiply4x4()
 * otherwise.
 */
static void
_cogl_matrix_multiply (CoglMatrix *result,
                       const CoglMatrix *a,
                       const CoglMatrix *b)
{
  result->flags = (a->flags |
                   b->flags |
                   MAT_DIRTY_TYPE |
                   MAT_DIRTY_INVERSE);

  if (TEST_MAT_FLAGS(result, MAT_FLAGS_3D))
    matrix_multiply3x4 ((float *)result, (float *)a, (float *)b);
  else
    matrix_multiply4x4 ((float *)result, (float *)a, (float *)b);
}

void
cogl_matrix_multiply (CoglMatrix *result,
		      const CoglMatrix *a,
		      const CoglMatrix *b)
{
  _cogl_matrix_multiply (result, a, b);
  _COGL_MATRIX_DEBUG_PRINT (result);
}

#if 0
/* Marks the matrix flags with general flag, and type and inverse dirty flags.
 * Calls matrix_multiply4x4() for the multiplication.
 */
static void
_cogl_matrix_multiply_array (CoglMatrix *result, const float *array)
{
  result->flags |= (MAT_FLAG_GENERAL |
                  MAT_DIRTY_TYPE |
                  MAT_DIRTY_INVERSE |
                  MAT_DIRTY_FLAGS);

  matrix_multiply4x4 ((float *)result, (float *)result, (float *)array);
}
#endif

/*
 * Print a matrix array.
 *
 * Called by _cogl_matrix_print() to print a matrix or its inverse.
 */
static void
print_matrix_floats (const char *prefix, const float m[16])
{
  int i;
  for (i = 0;i < 4; i++)
    g_print ("%s\t%f %f %f %f\n", prefix, m[i], m[4+i], m[8+i], m[12+i] );
}

void
_cogl_matrix_prefix_print (const char *prefix, const CoglMatrix *matrix)
{
  if (!(matrix->flags & MAT_DIRTY_TYPE))
    {
      g_return_if_fail (matrix->type < COGL_MATRIX_N_TYPES);
      g_print ("%sMatrix type: %s, flags: %x\n",
               prefix, types[matrix->type], (int)matrix->flags);
    }
  else
    g_print ("%sMatrix type: DIRTY, flags: %x\n",
             prefix, (int)matrix->flags);

  print_matrix_floats (prefix, (float *)matrix);
  g_print ("%sInverse: \n", prefix);
  if (!(matrix->flags & MAT_DIRTY_INVERSE))
    {
      float prod[16];
      print_matrix_floats (prefix, matrix->inv);
      matrix_multiply4x4 (prod, (float *)matrix, matrix->inv);
      g_print ("%sMat * Inverse:\n", prefix);
      print_matrix_floats (prefix, prod);
    }
  else
    g_print ("%s  - not available\n", prefix);
}

/*
 * Dumps the contents of a CoglMatrix structure.
 */
void
cogl_debug_matrix_print (const CoglMatrix *matrix)
{
  _cogl_matrix_prefix_print ("", matrix);
}

/*
 * References an element of 4x4 matrix.
 *
 * @m matrix array.
 * @c column of the desired element.
 * @r row of the desired element.
 *
 * Returns: value of the desired element.
 *
 * Calculate the linear storage index of the element and references it.
 */
#define MAT(m,r,c) (m)[(c)*4+(r)]

/*
 * Swaps the values of two floating pointer variables.
 *
 * Used by invert_matrix_general() to swap the row pointers.
 */
#define SWAP_ROWS(a, b) { float *_tmp = a; (a)=(b); (b)=_tmp; }

/*
 * Compute inverse of 4x4 transformation matrix.
 *
 * @mat pointer to a CoglMatrix structure. The matrix inverse will be
 * stored in the CoglMatrix::inv attribute.
 *
 * Returns: %TRUE for success, %FALSE for failure (\p singular matrix).
 *
 * \author
 * Code contributed by Jacques Leroy jle@star.be
 *
 * Calculates the inverse matrix by performing the gaussian matrix reduction
 * with partial pivoting followed by back/substitution with the loops manually
 * unrolled.
 */
static gboolean
invert_matrix_general (CoglMatrix *matrix)
{
  const float *m = (float *)matrix;
  float *out = matrix->inv;
  float wtmp[4][8];
  float m0, m1, m2, m3, s;
  float *r0, *r1, *r2, *r3;

  r0 = wtmp[0], r1 = wtmp[1], r2 = wtmp[2], r3 = wtmp[3];

  r0[0] = MAT (m, 0, 0), r0[1] = MAT (m, 0, 1),
    r0[2] = MAT (m, 0, 2), r0[3] = MAT (m, 0, 3),
    r0[4] = 1.0, r0[5] = r0[6] = r0[7] = 0.0,

    r1[0] = MAT (m, 1, 0), r1[1] = MAT (m, 1, 1),
    r1[2] = MAT (m, 1, 2), r1[3] = MAT (m, 1, 3),
    r1[5] = 1.0, r1[4] = r1[6] = r1[7] = 0.0,

    r2[0] = MAT (m, 2, 0), r2[1] = MAT (m, 2, 1),
    r2[2] = MAT (m, 2, 2), r2[3] = MAT (m, 2, 3),
    r2[6] = 1.0, r2[4] = r2[5] = r2[7] = 0.0,

    r3[0] = MAT (m, 3, 0), r3[1] = MAT (m, 3, 1),
    r3[2] = MAT (m, 3, 2), r3[3] = MAT (m, 3, 3),
    r3[7] = 1.0, r3[4] = r3[5] = r3[6] = 0.0;

  /* choose pivot - or die */
  if (fabsf (r3[0]) > fabsf (r2[0]))
    SWAP_ROWS (r3, r2);
  if (fabsf (r2[0]) > fabsf (r1[0]))
    SWAP_ROWS (r2, r1);
  if (fabsf (r1[0]) > fabsf (r0[0]))
    SWAP_ROWS (r1, r0);
  if (0.0 == r0[0])
    return FALSE;

  /* eliminate first variable     */
  m1 = r1[0]/r0[0]; m2 = r2[0]/r0[0]; m3 = r3[0]/r0[0];
  s = r0[1]; r1[1] -= m1 * s; r2[1] -= m2 * s; r3[1] -= m3 * s;
  s = r0[2]; r1[2] -= m1 * s; r2[2] -= m2 * s; r3[2] -= m3 * s;
  s = r0[3]; r1[3] -= m1 * s; r2[3] -= m2 * s; r3[3] -= m3 * s;
  s = r0[4];
  if (s != 0.0) { r1[4] -= m1 * s; r2[4] -= m2 * s; r3[4] -= m3 * s; }
  s = r0[5];
  if (s != 0.0) { r1[5] -= m1 * s; r2[5] -= m2 * s; r3[5] -= m3 * s; }
  s = r0[6];
  if (s != 0.0) { r1[6] -= m1 * s; r2[6] -= m2 * s; r3[6] -= m3 * s; }
  s = r0[7];
  if (s != 0.0) { r1[7] -= m1 * s; r2[7] -= m2 * s; r3[7] -= m3 * s; }

  /* choose pivot - or die */
  if (fabsf (r3[1]) > fabsf (r2[1]))
    SWAP_ROWS (r3, r2);
  if (fabsf (r2[1]) > fabsf (r1[1]))
    SWAP_ROWS (r2, r1);
  if (0.0 == r1[1])
    return FALSE;

  /* eliminate second variable */
  m2 = r2[1] / r1[1]; m3 = r3[1] / r1[1];
  r2[2] -= m2 * r1[2]; r3[2] -= m3 * r1[2];
  r2[3] -= m2 * r1[3]; r3[3] -= m3 * r1[3];
  s = r1[4]; if (0.0 != s) { r2[4] -= m2 * s; r3[4] -= m3 * s; }
  s = r1[5]; if (0.0 != s) { r2[5] -= m2 * s; r3[5] -= m3 * s; }
  s = r1[6]; if (0.0 != s) { r2[6] -= m2 * s; r3[6] -= m3 * s; }
  s = r1[7]; if (0.0 != s) { r2[7] -= m2 * s; r3[7] -= m3 * s; }

  /* choose pivot - or die */
  if (fabsf (r3[2]) > fabsf (r2[2]))
    SWAP_ROWS (r3, r2);
  if (0.0 == r2[2])
    return FALSE;

  /* eliminate third variable */
  m3 = r3[2] / r2[2];
  r3[3] -= m3 * r2[3], r3[4] -= m3 * r2[4],
    r3[5] -= m3 * r2[5], r3[6] -= m3 * r2[6],
    r3[7] -= m3 * r2[7];

  /* last check */
  if (0.0 == r3[3])
    return FALSE;

  s = 1.0f / r3[3];             /* now back substitute row 3 */
  r3[4] *= s; r3[5] *= s; r3[6] *= s; r3[7] *= s;

  m2 = r2[3];                 /* now back substitute row 2 */
  s  = 1.0f / r2[2];
  r2[4] = s * (r2[4] - r3[4] * m2), r2[5] = s * (r2[5] - r3[5] * m2),
    r2[6] = s * (r2[6] - r3[6] * m2), r2[7] = s * (r2[7] - r3[7] * m2);
  m1 = r1[3];
  r1[4] -= r3[4] * m1, r1[5] -= r3[5] * m1,
    r1[6] -= r3[6] * m1, r1[7] -= r3[7] * m1;
  m0 = r0[3];
  r0[4] -= r3[4] * m0, r0[5] -= r3[5] * m0,
    r0[6] -= r3[6] * m0, r0[7] -= r3[7] * m0;

  m1 = r1[2];                 /* now back substitute row 1 */
  s  = 1.0f / r1[1];
  r1[4] = s * (r1[4] - r2[4] * m1), r1[5] = s * (r1[5] - r2[5] * m1),
    r1[6] = s * (r1[6] - r2[6] * m1), r1[7] = s * (r1[7] - r2[7] * m1);
  m0 = r0[2];
  r0[4] -= r2[4] * m0, r0[5] -= r2[5] * m0,
    r0[6] -= r2[6] * m0, r0[7] -= r2[7] * m0;

  m0 = r0[1];                 /* now back substitute row 0 */
  s  = 1.0f / r0[0];
  r0[4] = s * (r0[4] - r1[4] * m0), r0[5] = s * (r0[5] - r1[5] * m0),
    r0[6] = s * (r0[6] - r1[6] * m0), r0[7] = s * (r0[7] - r1[7] * m0);

  MAT (out, 0, 0) = r0[4]; MAT (out, 0, 1) = r0[5],
    MAT (out, 0, 2) = r0[6]; MAT (out, 0, 3) = r0[7],
    MAT (out, 1, 0) = r1[4]; MAT (out, 1, 1) = r1[5],
    MAT (out, 1, 2) = r1[6]; MAT (out, 1, 3) = r1[7],
    MAT (out, 2, 0) = r2[4]; MAT (out, 2, 1) = r2[5],
    MAT (out, 2, 2) = r2[6]; MAT (out, 2, 3) = r2[7],
    MAT (out, 3, 0) = r3[4]; MAT (out, 3, 1) = r3[5],
    MAT (out, 3, 2) = r3[6]; MAT (out, 3, 3) = r3[7];

  return TRUE;
}
#undef SWAP_ROWS

/*
 * Compute inverse of a general 3d transformation matrix.
 *
 * @mat pointer to a CoglMatrix structure. The matrix inverse will be
 * stored in the CoglMatrix::inv attribute.
 *
 * Returns: %TRUE for success, %FALSE for failure (\p singular matrix).
 *
 * \author Adapted from graphics gems II.
 *
 * Calculates the inverse of the upper left by first calculating its
 * determinant and multiplying it to the symmetric adjust matrix of each
 * element. Finally deals with the translation part by transforming the
 * original translation vector using by the calculated submatrix inverse.
 */
static gboolean
invert_matrix_3d_general (CoglMatrix *matrix)
{
  const float *in = (float *)matrix;
  float *out = matrix->inv;
  float pos, neg, t;
  float det;

  /* Calculate the determinant of upper left 3x3 submatrix and
   * determine if the matrix is singular.
   */
  pos = neg = 0.0;
  t =  MAT (in,0,0) * MAT (in,1,1) * MAT (in,2,2);
  if (t >= 0.0) pos += t; else neg += t;

  t =  MAT (in,1,0) * MAT (in,2,1) * MAT (in,0,2);
  if (t >= 0.0) pos += t; else neg += t;

  t =  MAT (in,2,0) * MAT (in,0,1) * MAT (in,1,2);
  if (t >= 0.0) pos += t; else neg += t;

  t = -MAT (in,2,0) * MAT (in,1,1) * MAT (in,0,2);
  if (t >= 0.0) pos += t; else neg += t;

  t = -MAT (in,1,0) * MAT (in,0,1) * MAT (in,2,2);
  if (t >= 0.0) pos += t; else neg += t;

  t = -MAT (in,0,0) * MAT (in,2,1) * MAT (in,1,2);
  if (t >= 0.0) pos += t; else neg += t;

  det = pos + neg;

  if (det*det < 1e-25)
    return FALSE;

  det = 1.0f / det;
  MAT (out,0,0) =
    (  (MAT (in, 1, 1)*MAT (in, 2, 2) - MAT (in, 2, 1)*MAT (in, 1, 2) )*det);
  MAT (out,0,1) =
    (- (MAT (in, 0, 1)*MAT (in, 2, 2) - MAT (in, 2, 1)*MAT (in, 0, 2) )*det);
  MAT (out,0,2) =
    (  (MAT (in, 0, 1)*MAT (in, 1, 2) - MAT (in, 1, 1)*MAT (in, 0, 2) )*det);
  MAT (out,1,0) =
    (- (MAT (in,1,0)*MAT (in,2,2) - MAT (in,2,0)*MAT (in,1,2) )*det);
  MAT (out,1,1) =
    (  (MAT (in,0,0)*MAT (in,2,2) - MAT (in,2,0)*MAT (in,0,2) )*det);
  MAT (out,1,2) =
    (- (MAT (in,0,0)*MAT (in,1,2) - MAT (in,1,0)*MAT (in,0,2) )*det);
  MAT (out,2,0) =
    (  (MAT (in,1,0)*MAT (in,2,1) - MAT (in,2,0)*MAT (in,1,1) )*det);
  MAT (out,2,1) =
    (- (MAT (in,0,0)*MAT (in,2,1) - MAT (in,2,0)*MAT (in,0,1) )*det);
  MAT (out,2,2) =
    (  (MAT (in,0,0)*MAT (in,1,1) - MAT (in,1,0)*MAT (in,0,1) )*det);

  /* Do the translation part */
  MAT (out,0,3) = - (MAT (in, 0, 3) * MAT (out, 0, 0) +
                    MAT (in, 1, 3) * MAT (out, 0, 1) +
                    MAT (in, 2, 3) * MAT (out, 0, 2) );
  MAT (out,1,3) = - (MAT (in, 0, 3) * MAT (out, 1, 0) +
                    MAT (in, 1, 3) * MAT (out, 1, 1) +
                    MAT (in, 2, 3) * MAT (out, 1, 2) );
  MAT (out,2,3) = - (MAT (in, 0, 3) * MAT (out, 2 ,0) +
                    MAT (in, 1, 3) * MAT (out, 2, 1) +
                    MAT (in, 2, 3) * MAT (out, 2, 2) );

  return TRUE;
}

/*
 * Compute inverse of a 3d transformation matrix.
 *
 * @mat pointer to a CoglMatrix structure. The matrix inverse will be
 * stored in the CoglMatrix::inv attribute.
 *
 * Returns: %TRUE for success, %FALSE for failure (\p singular matrix).
 *
 * If the matrix is not an angle preserving matrix then calls
 * invert_matrix_3d_general for the actual calculation. Otherwise calculates
 * the inverse matrix analyzing and inverting each of the scaling, rotation and
 * translation parts.
 */
static gboolean
invert_matrix_3d (CoglMatrix *matrix)
{
  const float *in = (float *)matrix;
  float *out = matrix->inv;

  memcpy (out, identity, 16 * sizeof (float));

  if (!TEST_MAT_FLAGS(matrix, MAT_FLAGS_ANGLE_PRESERVING))
    return invert_matrix_3d_general (matrix);

  if (matrix->flags & MAT_FLAG_UNIFORM_SCALE)
    {
      float scale = (MAT (in, 0, 0) * MAT (in, 0, 0) +
                     MAT (in, 0, 1) * MAT (in, 0, 1) +
                     MAT (in, 0, 2) * MAT (in, 0, 2));

      if (scale == 0.0)
        return FALSE;

      scale = 1.0f / scale;

      /* Transpose and scale the 3 by 3 upper-left submatrix. */
      MAT (out, 0, 0) = scale * MAT (in, 0, 0);
      MAT (out, 1, 0) = scale * MAT (in, 0, 1);
      MAT (out, 2, 0) = scale * MAT (in, 0, 2);
      MAT (out, 0, 1) = scale * MAT (in, 1, 0);
      MAT (out, 1, 1) = scale * MAT (in, 1, 1);
      MAT (out, 2, 1) = scale * MAT (in, 1, 2);
      MAT (out, 0, 2) = scale * MAT (in, 2, 0);
      MAT (out, 1, 2) = scale * MAT (in, 2, 1);
      MAT (out, 2, 2) = scale * MAT (in, 2, 2);
    }
  else if (matrix->flags & MAT_FLAG_ROTATION)
    {
      /* Transpose the 3 by 3 upper-left submatrix. */
      MAT (out, 0, 0) = MAT (in, 0, 0);
      MAT (out, 1, 0) = MAT (in, 0, 1);
      MAT (out, 2, 0) = MAT (in, 0, 2);
      MAT (out, 0, 1) = MAT (in, 1, 0);
      MAT (out, 1, 1) = MAT (in, 1, 1);
      MAT (out, 2, 1) = MAT (in, 1, 2);
      MAT (out, 0, 2) = MAT (in, 2, 0);
      MAT (out, 1, 2) = MAT (in, 2, 1);
      MAT (out, 2, 2) = MAT (in, 2, 2);
    }
  else
    {
      /* pure translation */
      memcpy (out, identity, 16 * sizeof (float));
      MAT (out, 0, 3) = - MAT (in, 0, 3);
      MAT (out, 1, 3) = - MAT (in, 1, 3);
      MAT (out, 2, 3) = - MAT (in, 2, 3);
      return TRUE;
    }

  if (matrix->flags & MAT_FLAG_TRANSLATION)
    {
      /* Do the translation part */
      MAT (out,0,3) = - (MAT (in, 0, 3) * MAT (out, 0, 0) +
                        MAT (in, 1, 3) * MAT (out, 0, 1) +
                        MAT (in, 2, 3) * MAT (out, 0, 2) );
      MAT (out,1,3) = - (MAT (in, 0, 3) * MAT (out, 1, 0) +
                        MAT (in, 1, 3) * MAT (out, 1, 1) +
                        MAT (in, 2, 3) * MAT (out, 1, 2) );
      MAT (out,2,3) = - (MAT (in, 0, 3) * MAT (out, 2, 0) +
                        MAT (in, 1, 3) * MAT (out, 2, 1) +
                        MAT (in, 2, 3) * MAT (out, 2, 2) );
    }
  else
    MAT (out, 0, 3) = MAT (out, 1, 3) = MAT (out, 2, 3) = 0.0;

  return TRUE;
}

/*
 * Compute inverse of an identity transformation matrix.
 *
 * @mat pointer to a CoglMatrix structure. The matrix inverse will be
 * stored in the CoglMatrix::inv attribute.
 *
 * Returns: always %TRUE.
 *
 * Simply copies identity into CoglMatrix::inv.
 */
static gboolean
invert_matrix_identity (CoglMatrix *matrix)
{
  memcpy (matrix->inv, identity, 16 * sizeof (float));
  return TRUE;
}

/*
 * Compute inverse of a no-rotation 3d transformation matrix.
 *
 * @mat pointer to a CoglMatrix structure. The matrix inverse will be
 * stored in the CoglMatrix::inv attribute.
 *
 * Returns: %TRUE for success, %FALSE for failure (\p singular matrix).
 *
 * Calculates the
 */
static gboolean
invert_matrix_3d_no_rotation (CoglMatrix *matrix)
{
  const float *in = (float *)matrix;
  float *out = matrix->inv;

  if (MAT (in,0,0) == 0 || MAT (in,1,1) == 0 || MAT (in,2,2) == 0)
    return FALSE;

  memcpy (out, identity, 16 * sizeof (float));
  MAT (out,0,0) = 1.0f / MAT (in,0,0);
  MAT (out,1,1) = 1.0f / MAT (in,1,1);
  MAT (out,2,2) = 1.0f / MAT (in,2,2);

  if (matrix->flags & MAT_FLAG_TRANSLATION)
    {
      MAT (out,0,3) = - (MAT (in,0,3) * MAT (out,0,0));
      MAT (out,1,3) = - (MAT (in,1,3) * MAT (out,1,1));
      MAT (out,2,3) = - (MAT (in,2,3) * MAT (out,2,2));
    }

  return TRUE;
}

/*
 * Compute inverse of a no-rotation 2d transformation matrix.
 *
 * @mat pointer to a CoglMatrix structure. The matrix inverse will be
 * stored in the CoglMatrix::inv attribute.
 *
 * Returns: %TRUE for success, %FALSE for failure (\p singular matrix).
 *
 * Calculates the inverse matrix by applying the inverse scaling and
 * translation to the identity matrix.
 */
static gboolean
invert_matrix_2d_no_rotation (CoglMatrix *matrix)
{
  const float *in = (float *)matrix;
  float *out = matrix->inv;

  if (MAT (in, 0, 0) == 0 || MAT (in, 1, 1) == 0)
    return FALSE;

  memcpy (out, identity, 16 * sizeof (float));
  MAT (out, 0, 0) = 1.0f / MAT (in, 0, 0);
  MAT (out, 1, 1) = 1.0f / MAT (in, 1, 1);

  if (matrix->flags & MAT_FLAG_TRANSLATION)
    {
      MAT (out, 0, 3) = - (MAT (in, 0, 3) * MAT (out, 0, 0));
      MAT (out, 1, 3) = - (MAT (in, 1, 3) * MAT (out, 1, 1));
    }

  return TRUE;
}

#if 0
/* broken */
static gboolean
invert_matrix_perspective (CoglMatrix *matrix)
{
  const float *in = matrix;
  float *out = matrix->inv;

  if (MAT (in,2,3) == 0)
    return FALSE;

  memcpy( out, identity, 16 * sizeof(float) );

  MAT (out, 0, 0) = 1.0f / MAT (in, 0, 0);
  MAT (out, 1, 1) = 1.0f / MAT (in, 1, 1);

  MAT (out, 0, 3) = MAT (in, 0, 2);
  MAT (out, 1, 3) = MAT (in, 1, 2);

  MAT (out,2,2) = 0;
  MAT (out,2,3) = -1;

  MAT (out,3,2) = 1.0f / MAT (in,2,3);
  MAT (out,3,3) = MAT (in,2,2) * MAT (out,3,2);

  return TRUE;
}
#endif

/*
 * Matrix inversion function pointer type.
 */
typedef gboolean (*inv_mat_func)(CoglMatrix *matrix);

/*
 * Table of the matrix inversion functions according to the matrix type.
 */
static inv_mat_func inv_mat_tab[7] = {
    invert_matrix_general,
    invert_matrix_identity,
    invert_matrix_3d_no_rotation,
#if 0
    /* Don't use this function for now - it fails when the projection matrix
     * is premultiplied by a translation (ala Chromium's tilesort SPU).
     */
    invert_matrix_perspective,
#else
    invert_matrix_general,
#endif
    invert_matrix_3d,		/* lazy! */
    invert_matrix_2d_no_rotation,
    invert_matrix_3d
};

#define ZERO(x) (1<<x)
#define ONE(x)  (1<<(x+16))

#define MASK_NO_TRX      (ZERO(12) | ZERO(13) | ZERO(14))
#define MASK_NO_2D_SCALE ( ONE(0)  | ONE(5))

#define MASK_IDENTITY    ( ONE(0)  | ZERO(4)  | ZERO(8)  | ZERO(12) |\
                          ZERO(1)  |  ONE(5)  | ZERO(9)  | ZERO(13) |\
                          ZERO(2)  | ZERO(6)  |  ONE(10) | ZERO(14) |\
                          ZERO(3)  | ZERO(7)  | ZERO(11) |  ONE(15) )

#define MASK_2D_NO_ROT   (           ZERO(4)  | ZERO(8)  |           \
                          ZERO(1)  |            ZERO(9)  |           \
                          ZERO(2)  | ZERO(6)  |  ONE(10) | ZERO(14) |\
                          ZERO(3)  | ZERO(7)  | ZERO(11) |  ONE(15) )

#define MASK_2D          (                      ZERO(8)  |           \
                          ZERO(9)  |           \
                          ZERO(2)  | ZERO(6)  |  ONE(10) | ZERO(14) |\
                          ZERO(3)  | ZERO(7)  | ZERO(11) |  ONE(15) )


#define MASK_3D_NO_ROT   (           ZERO(4)  | ZERO(8)  |           \
                          ZERO(1)  |            ZERO(9)  |           \
                          ZERO(2)  | ZERO(6)  |                      \
                          ZERO(3)  | ZERO(7)  | ZERO(11) |  ONE(15) )

#define MASK_3D          (                                           \
                          \
                          \
                          ZERO(3)  | ZERO(7)  | ZERO(11) |  ONE(15) )


#define MASK_PERSPECTIVE (           ZERO(4)  |            ZERO(12) |\
                          ZERO(1)  |                       ZERO(13) |\
                          ZERO(2)  | ZERO(6)  |                      \
                          ZERO(3)  | ZERO(7)  |            ZERO(15) )

#define SQ(x) ((x)*(x))

/*
 * Determine type and flags from scratch.
 *
 * This is expensive enough to only want to do it once.
 */
static void
analyse_from_scratch (CoglMatrix *matrix)
{
  const float *m = (float *)matrix;
  unsigned int mask = 0;
  unsigned int i;

  for (i = 0 ; i < 16 ; i++)
    {
      if (m[i] == 0.0) mask |= (1<<i);
    }

  if (m[0] == 1.0f) mask |= (1<<16);
  if (m[5] == 1.0f) mask |= (1<<21);
  if (m[10] == 1.0f) mask |= (1<<26);
  if (m[15] == 1.0f) mask |= (1<<31);

  matrix->flags &= ~MAT_FLAGS_GEOMETRY;

  /* Check for translation - no-one really cares
  */
  if ((mask & MASK_NO_TRX) != MASK_NO_TRX)
    matrix->flags |= MAT_FLAG_TRANSLATION;

  /* Do the real work
  */
  if (mask == (unsigned int) MASK_IDENTITY)
    matrix->type = COGL_MATRIX_TYPE_IDENTITY;
  else if ((mask & MASK_2D_NO_ROT) == (unsigned int) MASK_2D_NO_ROT)
    {
      matrix->type = COGL_MATRIX_TYPE_2D_NO_ROT;

      if ((mask & MASK_NO_2D_SCALE) != MASK_NO_2D_SCALE)
        matrix->flags |= MAT_FLAG_GENERAL_SCALE;
    }
  else if ((mask & MASK_2D) == (unsigned int) MASK_2D)
    {
      float mm = DOT2 (m, m);
      float m4m4 = DOT2 (m+4,m+4);
      float mm4 = DOT2 (m,m+4);

      matrix->type = COGL_MATRIX_TYPE_2D;

      /* Check for scale */
      if (SQ (mm-1) > SQ (1e-6) ||
          SQ (m4m4-1) > SQ (1e-6))
        matrix->flags |= MAT_FLAG_GENERAL_SCALE;

      /* Check for rotation */
      if (SQ (mm4) > SQ (1e-6))
        matrix->flags |= MAT_FLAG_GENERAL_3D;
      else
        matrix->flags |= MAT_FLAG_ROTATION;

    }
  else if ((mask & MASK_3D_NO_ROT) == (unsigned int) MASK_3D_NO_ROT)
    {
      matrix->type = COGL_MATRIX_TYPE_3D_NO_ROT;

      /* Check for scale */
      if (SQ (m[0]-m[5]) < SQ (1e-6) &&
          SQ (m[0]-m[10]) < SQ (1e-6))
        {
          if (SQ (m[0]-1.0) > SQ (1e-6))
            matrix->flags |= MAT_FLAG_UNIFORM_SCALE;
        }
      else
        matrix->flags |= MAT_FLAG_GENERAL_SCALE;
    }
  else if ((mask & MASK_3D) == (unsigned int) MASK_3D)
    {
      float c1 = DOT3 (m,m);
      float c2 = DOT3 (m+4,m+4);
      float c3 = DOT3 (m+8,m+8);
      float d1 = DOT3 (m, m+4);
      float cp[3];

      matrix->type = COGL_MATRIX_TYPE_3D;

      /* Check for scale */
      if (SQ (c1-c2) < SQ (1e-6) && SQ (c1-c3) < SQ (1e-6))
        {
          if (SQ (c1-1.0) > SQ (1e-6))
            matrix->flags |= MAT_FLAG_UNIFORM_SCALE;
          /* else no scale at all */
        }
      else
        matrix->flags |= MAT_FLAG_GENERAL_SCALE;

      /* Check for rotation */
      if (SQ (d1) < SQ (1e-6))
        {
          CROSS3 ( cp, m, m+4);
          SUB_3V ( cp, cp, (m+8));
          if (LEN_SQUARED_3FV(cp) < SQ(1e-6))
            matrix->flags |= MAT_FLAG_ROTATION;
          else
            matrix->flags |= MAT_FLAG_GENERAL_3D;
        }
      else
        matrix->flags |= MAT_FLAG_GENERAL_3D; /* shear, etc */
    }
  else if ((mask & MASK_PERSPECTIVE) == MASK_PERSPECTIVE && m[11]==-1.0f)
    {
      matrix->type = COGL_MATRIX_TYPE_PERSPECTIVE;
      matrix->flags |= MAT_FLAG_GENERAL;
    }
  else
    {
      matrix->type = COGL_MATRIX_TYPE_GENERAL;
      matrix->flags |= MAT_FLAG_GENERAL;
    }
}

/*
 * Analyze a matrix given that its flags are accurate.
 *
 * This is the more common operation, hopefully.
 */
static void
analyse_from_flags (CoglMatrix *matrix)
{
  const float *m = (float *)matrix;

  if (TEST_MAT_FLAGS(matrix, 0))
    matrix->type = COGL_MATRIX_TYPE_IDENTITY;
  else if (TEST_MAT_FLAGS(matrix, (MAT_FLAG_TRANSLATION |
                                   MAT_FLAG_UNIFORM_SCALE |
                                   MAT_FLAG_GENERAL_SCALE)))
    {
      if ( m[10] == 1.0f && m[14] == 0.0f )
        matrix->type = COGL_MATRIX_TYPE_2D_NO_ROT;
      else
        matrix->type = COGL_MATRIX_TYPE_3D_NO_ROT;
    }
  else if (TEST_MAT_FLAGS (matrix, MAT_FLAGS_3D))
    {
      if (                               m[ 8]==0.0f
          &&                             m[ 9]==0.0f
          && m[2]==0.0f && m[6]==0.0f && m[10]==1.0f && m[14]==0.0f)
        {
          matrix->type = COGL_MATRIX_TYPE_2D;
        }
      else
        matrix->type = COGL_MATRIX_TYPE_3D;
    }
  else if (                 m[4]==0.0f                 && m[12]==0.0f
           && m[1]==0.0f                               && m[13]==0.0f
           && m[2]==0.0f && m[6]==0.0f
           && m[3]==0.0f && m[7]==0.0f && m[11]==-1.0f && m[15]==0.0f)
    {
      matrix->type = COGL_MATRIX_TYPE_PERSPECTIVE;
    }
  else
    matrix->type = COGL_MATRIX_TYPE_GENERAL;
}

/*
 * Analyze and update the type and flags of a matrix.
 *
 * If the matrix type is dirty then calls either analyse_from_scratch() or
 * analyse_from_flags() to determine its type, according to whether the flags
 * are dirty or not, respectively. If the matrix has an inverse and it's dirty
 * then calls matrix_invert(). Finally clears the dirty flags.
 */
static void
_cogl_matrix_update_type_and_flags (CoglMatrix *matrix)
{
  if (matrix->flags & MAT_DIRTY_TYPE)
    {
      if (matrix->flags & MAT_DIRTY_FLAGS)
        analyse_from_scratch (matrix);
      else
        analyse_from_flags (matrix);
    }

  matrix->flags &= ~(MAT_DIRTY_FLAGS | MAT_DIRTY_TYPE);
}

/*
 * Compute inverse of a transformation matrix.
 *
 * @mat pointer to a CoglMatrix structure. The matrix inverse will be
 * stored in the CoglMatrix::inv attribute.
 *
 * Returns: %TRUE for success, %FALSE for failure (\p singular matrix).
 *
 * Calls the matrix inversion function in inv_mat_tab corresponding to the
 * given matrix type.  In case of failure, updates the MAT_FLAG_SINGULAR flag,
 * and copies the identity matrix into CoglMatrix::inv.
 */
static gboolean
_cogl_matrix_update_inverse (CoglMatrix *matrix)
{
  if (matrix->flags & MAT_DIRTY_FLAGS ||
      matrix->flags & MAT_DIRTY_INVERSE)
    {
      _cogl_matrix_update_type_and_flags (matrix);

      if (inv_mat_tab[matrix->type](matrix))
        matrix->flags &= ~MAT_FLAG_SINGULAR;
      else
        {
          matrix->flags |= MAT_FLAG_SINGULAR;
          memcpy (matrix->inv, identity, 16 * sizeof (float));
        }

      matrix->flags &= ~MAT_DIRTY_INVERSE;
    }

  if (matrix->flags & MAT_FLAG_SINGULAR)
    return FALSE;
  else
    return TRUE;
}

gboolean
cogl_matrix_get_inverse (const CoglMatrix *matrix, CoglMatrix *inverse)
{
  if (_cogl_matrix_update_inverse ((CoglMatrix *)matrix))
    {
      cogl_matrix_init_from_array (inverse, matrix->inv);
      return TRUE;
    }
  else
    {
      cogl_matrix_init_identity (inverse);
      return FALSE;
    }
}

/*
 * Generate a 4x4 transformation matrix from glRotate parameters, and
 * post-multiply the input matrix by it.
 *
 * \author
 * This function was contributed by Erich Boleyn (erich@uruk.org).
 * Optimizations contributed by Rudolf Opalla (rudi@khm.de).
 */
static void
_cogl_matrix_rotate (CoglMatrix *matrix,
                     float angle,
                     float x,
                     float y,
                     float z)
{
  float xx, yy, zz, xy, yz, zx, xs, ys, zs, one_c, s, c;
  float m[16];
  gboolean optimized;

  s = sinf (angle * DEG2RAD);
  c = cosf (angle * DEG2RAD);

  memcpy (m, identity, 16 * sizeof (float));
  optimized = FALSE;

#define M(row,col)  m[col*4+row]

  if (x == 0.0f)
    {
      if (y == 0.0f)
        {
          if (z != 0.0f)
            {
              optimized = TRUE;
              /* rotate only around z-axis */
              M (0,0) = c;
              M (1,1) = c;
              if (z < 0.0f)
                {
                  M (0,1) = s;
                  M (1,0) = -s;
                }
              else
                {
                  M (0,1) = -s;
                  M (1,0) = s;
                }
            }
        }
      else if (z == 0.0f)
        {
          optimized = TRUE;
          /* rotate only around y-axis */
          M (0,0) = c;
          M (2,2) = c;
          if (y < 0.0f)
            {
              M (0,2) = -s;
              M (2,0) = s;
            }
          else
            {
              M (0,2) = s;
              M (2,0) = -s;
            }
        }
    }
  else if (y == 0.0f)
    {
      if (z == 0.0f)
        {
          optimized = TRUE;
          /* rotate only around x-axis */
          M (1,1) = c;
          M (2,2) = c;
          if (x < 0.0f)
            {
              M (1,2) = s;
              M (2,1) = -s;
            }
          else
            {
              M (1,2) = -s;
              M (2,1) = s;
            }
        }
    }

  if (!optimized)
    {
      const float mag = sqrtf (x * x + y * y + z * z);

      if (mag <= 1.0e-4)
        {
          /* no rotation, leave mat as-is */
          return;
        }

      x /= mag;
      y /= mag;
      z /= mag;


      /*
       *     Arbitrary axis rotation matrix.
       *
       *  This is composed of 5 matrices, Rz, Ry, T, Ry', Rz', multiplied
       *  like so:  Rz * Ry * T * Ry' * Rz'.  T is the final rotation
       *  (which is about the X-axis), and the two composite transforms
       *  Ry' * Rz' and Rz * Ry are (respectively) the rotations necessary
       *  from the arbitrary axis to the X-axis then back.  They are
       *  all elementary rotations.
       *
       *  Rz' is a rotation about the Z-axis, to bring the axis vector
       *  into the x-z plane.  Then Ry' is applied, rotating about the
       *  Y-axis to bring the axis vector parallel with the X-axis.  The
       *  rotation about the X-axis is then performed.  Ry and Rz are
       *  simply the respective inverse transforms to bring the arbitrary
       *  axis back to it's original orientation.  The first transforms
       *  Rz' and Ry' are considered inverses, since the data from the
       *  arbitrary axis gives you info on how to get to it, not how
       *  to get away from it, and an inverse must be applied.
       *
       *  The basic calculation used is to recognize that the arbitrary
       *  axis vector (x, y, z), since it is of unit length, actually
       *  represents the sines and cosines of the angles to rotate the
       *  X-axis to the same orientation, with theta being the angle about
       *  Z and phi the angle about Y (in the order described above)
       *  as follows:
       *
       *  cos ( theta ) = x / sqrt ( 1 - z^2 )
       *  sin ( theta ) = y / sqrt ( 1 - z^2 )
       *
       *  cos ( phi ) = sqrt ( 1 - z^2 )
       *  sin ( phi ) = z
       *
       *  Note that cos ( phi ) can further be inserted to the above
       *  formulas:
       *
       *  cos ( theta ) = x / cos ( phi )
       *  sin ( theta ) = y / sin ( phi )
       *
       *  ...etc.  Because of those relations and the standard trigonometric
       *  relations, it is pssible to reduce the transforms down to what
       *  is used below.  It may be that any primary axis chosen will give the
       *  same results (modulo a sign convention) using thie method.
       *
       *  Particularly nice is to notice that all divisions that might
       *  have caused trouble when parallel to certain planes or
       *  axis go away with care paid to reducing the expressions.
       *  After checking, it does perform correctly under all cases, since
       *  in all the cases of division where the denominator would have
       *  been zero, the numerator would have been zero as well, giving
       *  the expected result.
       */

      xx = x * x;
      yy = y * y;
      zz = z * z;
      xy = x * y;
      yz = y * z;
      zx = z * x;
      xs = x * s;
      ys = y * s;
      zs = z * s;
      one_c = 1.0f - c;

      /* We already hold the identity-matrix so we can skip some statements */
      M (0,0) = (one_c * xx) + c;
      M (0,1) = (one_c * xy) - zs;
      M (0,2) = (one_c * zx) + ys;
      /*    M (0,3) = 0.0f; */

      M (1,0) = (one_c * xy) + zs;
      M (1,1) = (one_c * yy) + c;
      M (1,2) = (one_c * yz) - xs;
      /*    M (1,3) = 0.0f; */

      M (2,0) = (one_c * zx) - ys;
      M (2,1) = (one_c * yz) + xs;
      M (2,2) = (one_c * zz) + c;
      /*    M (2,3) = 0.0f; */

      /*
         M (3,0) = 0.0f;
         M (3,1) = 0.0f;
         M (3,2) = 0.0f;
         M (3,3) = 1.0f;
         */
    }
#undef M

  matrix_multiply_array_with_flags (matrix, m, MAT_FLAG_ROTATION);
}

void
cogl_matrix_rotate (CoglMatrix *matrix,
		    float angle,
		    float x,
		    float y,
		    float z)
{
  _cogl_matrix_rotate (matrix, angle, x, y, z);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_rotate_euler (CoglMatrix *matrix,
                          const graphene_euler_t *euler)
{
  CoglMatrix rotation_transform;

  cogl_matrix_init_from_euler (&rotation_transform, euler);
  cogl_matrix_multiply (matrix, matrix, &rotation_transform);
}

/*
 * Apply a perspective projection matrix.
 *
 * Creates the projection matrix and multiplies it with matrix, marking the
 * MAT_FLAG_PERSPECTIVE flag.
 */
static void
_cogl_matrix_frustum (CoglMatrix *matrix,
                      float left,
                      float right,
                      float bottom,
                      float top,
                      float nearval,
                      float farval)
{
  float x, y, a, b, c, d;
  float m[16];

  x = (2.0f * nearval) / (right - left);
  y = (2.0f * nearval) / (top - bottom);
  a = (right + left) / (right - left);
  b = (top + bottom) / (top - bottom);
  c = -(farval + nearval) / ( farval - nearval);
  d = -(2.0f * farval * nearval) / (farval - nearval);  /* error? */

#define M(row,col)  m[col*4+row]
  M (0,0) = x;     M (0,1) = 0.0f;  M (0,2) = a;      M (0,3) = 0.0f;
  M (1,0) = 0.0f;  M (1,1) = y;     M (1,2) = b;      M (1,3) = 0.0f;
  M (2,0) = 0.0f;  M (2,1) = 0.0f;  M (2,2) = c;      M (2,3) = d;
  M (3,0) = 0.0f;  M (3,1) = 0.0f;  M (3,2) = -1.0f;  M (3,3) = 0.0f;
#undef M

  matrix_multiply_array_with_flags (matrix, m, MAT_FLAG_PERSPECTIVE);
}

void
cogl_matrix_frustum (CoglMatrix *matrix,
                     float       left,
                     float       right,
                     float       bottom,
                     float       top,
                     float       z_near,
                     float       z_far)
{
  _cogl_matrix_frustum (matrix, left, right, bottom, top, z_near, z_far);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_perspective (CoglMatrix *matrix,
                         float       fov_y,
                         float       aspect,
                         float       z_near,
                         float       z_far)
{
  float ymax = z_near * tan (fov_y * G_PI / 360.0);

  cogl_matrix_frustum (matrix,
                       -ymax * aspect,  /* left */
                       ymax * aspect,   /* right */
                       -ymax,           /* bottom */
                       ymax,            /* top */
                       z_near,
                       z_far);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

/*
 * Apply an orthographic projection matrix.
 *
 * Creates the projection matrix and multiplies it with matrix, marking the
 * MAT_FLAG_GENERAL_SCALE and MAT_FLAG_TRANSLATION flags.
 */
static void
_cogl_matrix_orthographic (CoglMatrix *matrix,
                           float x_1,
                           float y_1,
                           float x_2,
                           float y_2,
                           float nearval,
                           float farval)
{
  float m[16];

#define M(row, col)  m[col * 4 + row]
  M (0,0) = 2.0f / (x_2 - x_1);
  M (0,1) = 0.0f;
  M (0,2) = 0.0f;
  M (0,3) = -(x_2 + x_1) / (x_2 - x_1);

  M (1,0) = 0.0f;
  M (1,1) = 2.0f / (y_1 - y_2);
  M (1,2) = 0.0f;
  M (1,3) = -(y_1 + y_2) / (y_1 - y_2);

  M (2,0) = 0.0f;
  M (2,1) = 0.0f;
  M (2,2) = -2.0f / (farval - nearval);
  M (2,3) = -(farval + nearval) / (farval - nearval);

  M (3,0) = 0.0f;
  M (3,1) = 0.0f;
  M (3,2) = 0.0f;
  M (3,3) = 1.0f;
#undef M

  matrix_multiply_array_with_flags (matrix, m,
                                    (MAT_FLAG_GENERAL_SCALE |
                                     MAT_FLAG_TRANSLATION));
}

void
cogl_matrix_orthographic (CoglMatrix *matrix,
                          float x_1,
                          float y_1,
                          float x_2,
                          float y_2,
                          float near,
                          float far)
{
  _cogl_matrix_orthographic (matrix, x_1, y_1, x_2, y_2, near, far);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

/*
 * Multiply a matrix with a general scaling matrix.
 *
 * Multiplies in-place the elements of matrix by the scale factors. Checks if
 * the scales factors are roughly the same, marking the MAT_FLAG_UNIFORM_SCALE
 * flag, or MAT_FLAG_GENERAL_SCALE. Marks the MAT_DIRTY_TYPE and
 * MAT_DIRTY_INVERSE dirty flags.
 */
static void
_cogl_matrix_scale (CoglMatrix *matrix, float x, float y, float z)
{
  float *m = (float *)matrix;
  m[0] *= x;   m[4] *= y;   m[8]  *= z;
  m[1] *= x;   m[5] *= y;   m[9]  *= z;
  m[2] *= x;   m[6] *= y;   m[10] *= z;
  m[3] *= x;   m[7] *= y;   m[11] *= z;

  if (fabsf (x - y) < 1e-8 && fabsf (x - z) < 1e-8)
    matrix->flags |= MAT_FLAG_UNIFORM_SCALE;
  else
    matrix->flags |= MAT_FLAG_GENERAL_SCALE;

  matrix->flags |= (MAT_DIRTY_TYPE | MAT_DIRTY_INVERSE);
}

void
cogl_matrix_scale (CoglMatrix *matrix,
		   float sx,
		   float sy,
		   float sz)
{
  _cogl_matrix_scale (matrix, sx, sy, sz);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

/*
 * Multiply a matrix with a translation matrix.
 *
 * Adds the translation coordinates to the elements of matrix in-place.  Marks
 * the MAT_FLAG_TRANSLATION flag, and the MAT_DIRTY_TYPE and MAT_DIRTY_INVERSE
 * dirty flags.
 */
static void
_cogl_matrix_translate (CoglMatrix *matrix, float x, float y, float z)
{
  float *m = (float *)matrix;
  m[12] = m[0] * x + m[4] * y + m[8]  * z + m[12];
  m[13] = m[1] * x + m[5] * y + m[9]  * z + m[13];
  m[14] = m[2] * x + m[6] * y + m[10] * z + m[14];
  m[15] = m[3] * x + m[7] * y + m[11] * z + m[15];

  matrix->flags |= (MAT_FLAG_TRANSLATION |
                    MAT_DIRTY_TYPE |
                    MAT_DIRTY_INVERSE);
}

void
cogl_matrix_translate (CoglMatrix *matrix,
		       float x,
		       float y,
		       float z)
{
  _cogl_matrix_translate (matrix, x, y, z);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

#if 0
/*
 * Set matrix to do viewport and depthrange mapping.
 * Transforms Normalized Device Coords to window/Z values.
 */
static void
_cogl_matrix_viewport (CoglMatrix *matrix,
                       float x, float y,
                       float width, float height,
                       float zNear, float zFar, float depthMax)
{
  float *m = (float *)matrix;
  m[MAT_SX] = width / 2.0f;
  m[MAT_TX] = m[MAT_SX] + x;
  m[MAT_SY] = height / 2.0f;
  m[MAT_TY] = m[MAT_SY] + y;
  m[MAT_SZ] = depthMax * ((zFar - zNear) / 2.0f);
  m[MAT_TZ] = depthMax * ((zFar - zNear) / 2.0f + zNear);
  matrix->flags = MAT_FLAG_GENERAL_SCALE | MAT_FLAG_TRANSLATION;
  matrix->type = COGL_MATRIX_TYPE_3D_NO_ROT;
}
#endif

/*
 * Set a matrix to the identity matrix.
 *
 * @mat matrix.
 *
 * Copies ::identity into \p CoglMatrix::m, and into CoglMatrix::inv if
 * not NULL. Sets the matrix type to identity, resets the flags. It
 * doesn't initialize the inverse matrix, it just marks it dirty.
 */
static void
_cogl_matrix_init_identity (CoglMatrix *matrix)
{
  memcpy (matrix, identity, 16 * sizeof (float));

  matrix->type = COGL_MATRIX_TYPE_IDENTITY;
  matrix->flags = MAT_DIRTY_INVERSE;
}

void
cogl_matrix_init_identity (CoglMatrix *matrix)
{
  _cogl_matrix_init_identity (matrix);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

/*
 * Set a matrix to the (tx, ty, tz) translation matrix.
 *
 * @matix matrix.
 * @tx x coordinate of the translation vector
 * @ty y coordinate of the translation vector
 * @tz z coordinate of the translation vector
 */
static void
_cogl_matrix_init_translation (CoglMatrix *matrix,
                               float       tx,
                               float       ty,
                               float       tz)
{
  memcpy (matrix, identity, 16 * sizeof (float));

  matrix->xw = tx;
  matrix->yw = ty;
  matrix->zw = tz;

  matrix->type = COGL_MATRIX_TYPE_3D;
  matrix->flags = MAT_FLAG_TRANSLATION | MAT_DIRTY_INVERSE;
}

void
cogl_matrix_init_translation (CoglMatrix *matrix,
                              float       tx,
                              float       ty,
                              float       tz)
{
  _cogl_matrix_init_translation (matrix, tx, ty, tz);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

#if 0
/*
 * Test if the given matrix preserves vector lengths.
 */
static gboolean
_cogl_matrix_is_length_preserving (const CoglMatrix *m)
{
  return TEST_MAT_FLAGS (m, MAT_FLAGS_LENGTH_PRESERVING);
}

/*
 * Test if the given matrix does any rotation.
 * (or perhaps if the upper-left 3x3 is non-identity)
 */
static gboolean
_cogl_matrix_has_rotation (const CoglMatrix *matrix)
{
  if (matrix->flags & (MAT_FLAG_GENERAL |
                       MAT_FLAG_ROTATION |
                       MAT_FLAG_GENERAL_3D |
                       MAT_FLAG_PERSPECTIVE))
    return TRUE;
  else
    return FALSE;
}

static gboolean
_cogl_matrix_is_general_scale (const CoglMatrix *matrix)
{
  return (matrix->flags & MAT_FLAG_GENERAL_SCALE) ? TRUE : FALSE;
}

static gboolean
_cogl_matrix_is_dirty (const CoglMatrix *matrix)
{
  return (matrix->flags & MAT_DIRTY_ALL) ? TRUE : FALSE;
}
#endif

/*
 * Loads a matrix array into CoglMatrix.
 *
 * @m matrix array.
 * @mat matrix.
 *
 * Copies \p m into CoglMatrix::m and marks the MAT_FLAG_GENERAL and
 * MAT_DIRTY_ALL
 * flags.
 */
static void
_cogl_matrix_init_from_array (CoglMatrix *matrix, const float *array)
{
  memcpy (matrix, array, 16 * sizeof (float));
  matrix->flags = (MAT_FLAG_GENERAL | MAT_DIRTY_ALL);
}

void
cogl_matrix_init_from_array (CoglMatrix *matrix, const float *array)
{
  _cogl_matrix_init_from_array (matrix, array);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
_cogl_matrix_init_from_matrix_without_inverse (CoglMatrix *matrix,
                                               const CoglMatrix *src)
{
  memcpy (matrix, src, 16 * sizeof (float));
  matrix->type = src->type;
  matrix->flags = src->flags | MAT_DIRTY_INVERSE;
}

void
cogl_matrix_init_from_euler (CoglMatrix *matrix,
                             const graphene_euler_t *euler)
{
  /* Convert angles to radians */
  float heading_rad = graphene_euler_get_y (euler) / 180.0f * G_PI;
  float pitch_rad = graphene_euler_get_x (euler) / 180.0f * G_PI;
  float roll_rad = graphene_euler_get_z (euler) / 180.0f * G_PI;
  /* Pre-calculate the sin and cos */
  float sin_heading = sinf (heading_rad);
  float cos_heading = cosf (heading_rad);
  float sin_pitch = sinf (pitch_rad);
  float cos_pitch = cosf (pitch_rad);
  float sin_roll = sinf (roll_rad);
  float cos_roll = cosf (roll_rad);

  /* These calculations are based on the following website but they
   * use a different order for the rotations so it has been modified
   * slightly.
   * http://www.euclideanspace.com/maths/geometry/
   *        rotations/conversions/eulerToMatrix/index.htm
   */

  /* Heading rotation x=0, y=1, z=0 gives:
   *
   * [ ch   0   sh   0 ]
   * [ 0    1   0    0 ]
   * [ -sh  0   ch   0 ]
   * [ 0    0   0    1 ]
   *
   * Pitch rotation x=1, y=0, z=0 gives:
   * [ 1    0   0    0 ]
   * [ 0    cp  -sp  0 ]
   * [ 0    sp  cp   0 ]
   * [ 0    0   0    1 ]
   *
   * Roll rotation x=0, y=0, z=1 gives:
   * [ cr   -sr 0    0 ]
   * [ sr   cr  0    0 ]
   * [ 0    0   1    0 ]
   * [ 0    0   0    1 ]
   *
   * Heading matrix * pitch matrix =
   * [ ch   sh*sp    cp*sh   0  ]
   * [ 0    cp       -sp     0  ]
   * [ -sh  ch*sp    ch*cp   0  ]
   * [ 0    0        0       1  ]
   *
   * That matrix * roll matrix =
   * [ ch*cr + sh*sp*sr   sh*sp*cr - ch*sr       sh*cp       0 ]
   * [     cp*sr                cp*cr             -sp        0 ]
   * [ ch*sp*sr - sh*cr   sh*sr + ch*sp*cr       ch*cp       0 ]
   * [       0                    0                0         1 ]
   */

  matrix->xx = cos_heading * cos_roll + sin_heading * sin_pitch * sin_roll;
  matrix->yx = cos_pitch * sin_roll;
  matrix->zx = cos_heading * sin_pitch * sin_roll - sin_heading * cos_roll;
  matrix->wx = 0.0f;

  matrix->xy = sin_heading * sin_pitch * cos_roll - cos_heading * sin_roll;
  matrix->yy = cos_pitch * cos_roll;
  matrix->zy = sin_heading * sin_roll + cos_heading * sin_pitch * cos_roll;
  matrix->wy = 0.0f;

  matrix->xz = sin_heading * cos_pitch;
  matrix->yz = -sin_pitch;
  matrix->zz = cos_heading * cos_pitch;
  matrix->wz = 0;

  matrix->xw = 0;
  matrix->yw = 0;
  matrix->zw = 0;
  matrix->ww = 1;

  matrix->flags = (MAT_FLAG_GENERAL | MAT_DIRTY_ALL);
}

/*
 * Transpose a float matrix.
 */
static void
_cogl_matrix_util_transposef (float to[16], const float from[16])
{
  to[0] = from[0];
  to[1] = from[4];
  to[2] = from[8];
  to[3] = from[12];
  to[4] = from[1];
  to[5] = from[5];
  to[6] = from[9];
  to[7] = from[13];
  to[8] = from[2];
  to[9] = from[6];
  to[10] = from[10];
  to[11] = from[14];
  to[12] = from[3];
  to[13] = from[7];
  to[14] = from[11];
  to[15] = from[15];
}

void
cogl_matrix_view_2d_in_frustum (CoglMatrix *matrix,
                                float left,
                                float right,
                                float bottom,
                                float top,
                                float z_near,
                                float z_2d,
                                float width_2d,
                                float height_2d)
{
  float left_2d_plane = left / z_near * z_2d;
  float right_2d_plane = right / z_near * z_2d;
  float bottom_2d_plane = bottom / z_near * z_2d;
  float top_2d_plane = top / z_near * z_2d;

  float width_2d_start = right_2d_plane - left_2d_plane;
  float height_2d_start = top_2d_plane - bottom_2d_plane;

  /* Factors to scale from framebuffer geometry to frustum
   * cross-section geometry. */
  float width_scale = width_2d_start / width_2d;
  float height_scale = height_2d_start / height_2d;

  cogl_matrix_translate (matrix,
                         left_2d_plane, top_2d_plane, -z_2d);

  cogl_matrix_scale (matrix, width_scale, -height_scale, width_scale);
}

/* Assuming a symmetric perspective matrix is being used for your
 * projective transform this convenience function lets you compose a
 * view transform such that geometry on the z=0 plane will map to
 * screen coordinates with a top left origin of (0,0) and with the
 * given width and height.
 */
void
cogl_matrix_view_2d_in_perspective (CoglMatrix *matrix,
                                    float fov_y,
                                    float aspect,
                                    float z_near,
                                    float z_2d,
                                    float width_2d,
                                    float height_2d)
{
  float top = z_near * tan (fov_y * G_PI / 360.0);
  cogl_matrix_view_2d_in_frustum (matrix,
                                  -top * aspect,
                                  top * aspect,
                                  -top,
                                  top,
                                  z_near,
                                  z_2d,
                                  width_2d,
                                  height_2d);
}

gboolean
cogl_matrix_equal (const void *v1, const void *v2)
{
  const CoglMatrix *a = v1;
  const CoglMatrix *b = v2;

  g_return_val_if_fail (v1 != NULL, FALSE);
  g_return_val_if_fail (v2 != NULL, FALSE);

  /* We want to avoid having a fuzzy _equal() function (e.g. that uses
   * an arbitrary epsilon value) since this function noteably conforms
   * to the prototype suitable for use with g_hash_table_new() and a
   * fuzzy hash function isn't really appropriate for comparing hash
   * table keys since it's possible that you could end up fetching
   * different values if you end up with multiple similar keys in use
   * at the same time. If you consider that fuzzyness allows cases
   * such as A == B == C but A != C then you could also end up loosing
   * values in a hash table.
   *
   * We do at least use the == operator to compare values though so
   * that -0 is considered equal to 0.
   */

  /* XXX: We don't compare the flags, inverse matrix or padding */
  if (a->xx == b->xx &&
      a->xy == b->xy &&
      a->xz == b->xz &&
      a->xw == b->xw &&
      a->yx == b->yx &&
      a->yy == b->yy &&
      a->yz == b->yz &&
      a->yw == b->yw &&
      a->zx == b->zx &&
      a->zy == b->zy &&
      a->zz == b->zz &&
      a->zw == b->zw &&
      a->wx == b->wx &&
      a->wy == b->wy &&
      a->wz == b->wz &&
      a->ww == b->ww)
    return TRUE;
  else
    return FALSE;
}

CoglMatrix *
cogl_matrix_copy (const CoglMatrix *matrix)
{
  if (G_LIKELY (matrix))
    return g_slice_dup (CoglMatrix, matrix);

  return NULL;
}

void
cogl_matrix_free (CoglMatrix *matrix)
{
  g_slice_free (CoglMatrix, matrix);
}

const float *
cogl_matrix_get_array (const CoglMatrix *matrix)
{
  return (float *)matrix;
}

void
cogl_matrix_transform_point (const CoglMatrix *matrix,
                             float *x,
                             float *y,
                             float *z,
                             float *w)
{
  float _x = *x, _y = *y, _z = *z, _w = *w;

  *x = matrix->xx * _x + matrix->xy * _y + matrix->xz * _z + matrix->xw * _w;
  *y = matrix->yx * _x + matrix->yy * _y + matrix->yz * _z + matrix->yw * _w;
  *z = matrix->zx * _x + matrix->zy * _y + matrix->zz * _z + matrix->zw * _w;
  *w = matrix->wx * _x + matrix->wy * _y + matrix->wz * _z + matrix->ww * _w;
}

typedef struct _Point2f
{
  float x;
  float y;
} Point2f;

typedef struct _Point3f
{
  float x;
  float y;
  float z;
} Point3f;

typedef struct _Point4f
{
  float x;
  float y;
  float z;
  float w;
} Point4f;

static void
_cogl_matrix_transform_points_f2 (const CoglMatrix *matrix,
                                  size_t stride_in,
                                  const void *points_in,
                                  size_t stride_out,
                                  void *points_out,
                                  int n_points)
{
  int i;

  for (i = 0; i < n_points; i++)
    {
      Point2f p = *(Point2f *)((uint8_t *)points_in + i * stride_in);
      Point3f *o = (Point3f *)((uint8_t *)points_out + i * stride_out);

      o->x = matrix->xx * p.x + matrix->xy * p.y + matrix->xw;
      o->y = matrix->yx * p.x + matrix->yy * p.y + matrix->yw;
      o->z = matrix->zx * p.x + matrix->zy * p.y + matrix->zw;
    }
}

static void
_cogl_matrix_project_points_f2 (const CoglMatrix *matrix,
                                size_t stride_in,
                                const void *points_in,
                                size_t stride_out,
                                void *points_out,
                                int n_points)
{
  int i;

  for (i = 0; i < n_points; i++)
    {
      Point2f p = *(Point2f *)((uint8_t *)points_in + i * stride_in);
      Point4f *o = (Point4f *)((uint8_t *)points_out + i * stride_out);

      o->x = matrix->xx * p.x + matrix->xy * p.y + matrix->xw;
      o->y = matrix->yx * p.x + matrix->yy * p.y + matrix->yw;
      o->z = matrix->zx * p.x + matrix->zy * p.y + matrix->zw;
      o->w = matrix->wx * p.x + matrix->wy * p.y + matrix->ww;
    }
}

static void
_cogl_matrix_transform_points_f3 (const CoglMatrix *matrix,
                                  size_t stride_in,
                                  const void *points_in,
                                  size_t stride_out,
                                  void *points_out,
                                  int n_points)
{
  int i;

  for (i = 0; i < n_points; i++)
    {
      Point3f p = *(Point3f *)((uint8_t *)points_in + i * stride_in);
      Point3f *o = (Point3f *)((uint8_t *)points_out + i * stride_out);

      o->x = matrix->xx * p.x + matrix->xy * p.y +
             matrix->xz * p.z + matrix->xw;
      o->y = matrix->yx * p.x + matrix->yy * p.y +
             matrix->yz * p.z + matrix->yw;
      o->z = matrix->zx * p.x + matrix->zy * p.y +
             matrix->zz * p.z + matrix->zw;
    }
}

static void
_cogl_matrix_project_points_f3 (const CoglMatrix *matrix,
                                size_t stride_in,
                                const void *points_in,
                                size_t stride_out,
                                void *points_out,
                                int n_points)
{
  int i;

  for (i = 0; i < n_points; i++)
    {
      Point3f p = *(Point3f *)((uint8_t *)points_in + i * stride_in);
      Point4f *o = (Point4f *)((uint8_t *)points_out + i * stride_out);

      o->x = matrix->xx * p.x + matrix->xy * p.y +
             matrix->xz * p.z + matrix->xw;
      o->y = matrix->yx * p.x + matrix->yy * p.y +
             matrix->yz * p.z + matrix->yw;
      o->z = matrix->zx * p.x + matrix->zy * p.y +
             matrix->zz * p.z + matrix->zw;
      o->w = matrix->wx * p.x + matrix->wy * p.y +
             matrix->wz * p.z + matrix->ww;
    }
}

static void
_cogl_matrix_project_points_f4 (const CoglMatrix *matrix,
                                size_t stride_in,
                                const void *points_in,
                                size_t stride_out,
                                void *points_out,
                                int n_points)
{
  int i;

  for (i = 0; i < n_points; i++)
    {
      Point4f p = *(Point4f *)((uint8_t *)points_in + i * stride_in);
      Point4f *o = (Point4f *)((uint8_t *)points_out + i * stride_out);

      o->x = matrix->xx * p.x + matrix->xy * p.y +
             matrix->xz * p.z + matrix->xw * p.w;
      o->y = matrix->yx * p.x + matrix->yy * p.y +
             matrix->yz * p.z + matrix->yw * p.w;
      o->z = matrix->zx * p.x + matrix->zy * p.y +
             matrix->zz * p.z + matrix->zw * p.w;
      o->w = matrix->wx * p.x + matrix->wy * p.y +
             matrix->wz * p.z + matrix->ww * p.w;
    }
}

void
cogl_matrix_transform_points (const CoglMatrix *matrix,
                              int n_components,
                              size_t stride_in,
                              const void *points_in,
                              size_t stride_out,
                              void *points_out,
                              int n_points)
{
  /* The results of transforming always have three components... */
  g_return_if_fail (stride_out >= sizeof (Point3f));

  if (n_components == 2)
    _cogl_matrix_transform_points_f2 (matrix,
                                      stride_in, points_in,
                                      stride_out, points_out,
                                      n_points);
  else
    {
      g_return_if_fail (n_components == 3);

      _cogl_matrix_transform_points_f3 (matrix,
                                        stride_in, points_in,
                                        stride_out, points_out,
                                        n_points);
    }
}

void
cogl_matrix_project_points (const CoglMatrix *matrix,
                            int n_components,
                            size_t stride_in,
                            const void *points_in,
                            size_t stride_out,
                            void *points_out,
                            int n_points)
{
  if (n_components == 2)
    _cogl_matrix_project_points_f2 (matrix,
                                    stride_in, points_in,
                                    stride_out, points_out,
                                    n_points);
  else if (n_components == 3)
    _cogl_matrix_project_points_f3 (matrix,
                                    stride_in, points_in,
                                    stride_out, points_out,
                                    n_points);
  else
    {
      g_return_if_fail (n_components == 4);

      _cogl_matrix_project_points_f4 (matrix,
                                      stride_in, points_in,
                                      stride_out, points_out,
                                      n_points);
    }
}

gboolean
cogl_matrix_is_identity (const CoglMatrix *matrix)
{
  if (!(matrix->flags & MAT_DIRTY_TYPE) &&
      matrix->type == COGL_MATRIX_TYPE_IDENTITY)
    return TRUE;
  else
    return memcmp (matrix, identity, sizeof (float) * 16) == 0;
}

void
cogl_matrix_look_at (CoglMatrix *matrix,
                     float eye_position_x,
                     float eye_position_y,
                     float eye_position_z,
                     float object_x,
                     float object_y,
                     float object_z,
                     float world_up_x,
                     float world_up_y,
                     float world_up_z)
{
  CoglMatrix tmp;
  graphene_vec3_t forward;
  graphene_vec3_t side;
  graphene_vec3_t up;

  /* Get a unit viewing direction vector */
  graphene_vec3_init (&forward,
                      object_x - eye_position_x,
                      object_y - eye_position_y,
                      object_z - eye_position_z);
  graphene_vec3_normalize (&forward, &forward);

  graphene_vec3_init (&up, world_up_x, world_up_y, world_up_z);

  /* Take the sideways direction as being perpendicular to the viewing
   * direction and the word up vector. */
  graphene_vec3_cross (&forward, &up, &side);
  graphene_vec3_normalize (&side, &side);

  /* Now we have unit sideways and forward-direction vectors calculate
   * a new mutually perpendicular up vector. */
  graphene_vec3_cross (&side, &forward, &up);

  tmp.xx = graphene_vec3_get_x (&side);
  tmp.yx = graphene_vec3_get_y (&side);
  tmp.zx = graphene_vec3_get_z (&side);
  tmp.wx = 0;

  tmp.xy = graphene_vec3_get_x (&up);
  tmp.yy = graphene_vec3_get_y (&up);
  tmp.zy = graphene_vec3_get_z (&up);
  tmp.wy = 0;

  tmp.xz = -graphene_vec3_get_x (&forward);
  tmp.yz = -graphene_vec3_get_y (&forward);
  tmp.zz = -graphene_vec3_get_z (&forward);
  tmp.wz = 0;

  tmp.xw = 0;
  tmp.yw = 0;
  tmp.zw = 0;
  tmp.ww = 1;

  tmp.flags = (MAT_FLAG_GENERAL_3D | MAT_DIRTY_TYPE | MAT_DIRTY_INVERSE);

  cogl_matrix_translate (&tmp, -eye_position_x, -eye_position_y, -eye_position_z);

  cogl_matrix_multiply (matrix, matrix, &tmp);
}

void
cogl_matrix_transpose (CoglMatrix *matrix)
{
  float new_values[16];

  /* We don't need to do anything if the matrix is the identity matrix */
  if (!(matrix->flags & MAT_DIRTY_TYPE) &&
      matrix->type == COGL_MATRIX_TYPE_IDENTITY)
    return;

  _cogl_matrix_util_transposef (new_values, cogl_matrix_get_array (matrix));

  cogl_matrix_init_from_array (matrix, new_values);
}

GType
cogl_gtype_matrix_get_type (void)
{
  return cogl_matrix_get_gtype ();
}
