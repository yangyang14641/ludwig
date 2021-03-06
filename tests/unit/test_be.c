/*****************************************************************************
 *
 *  test_be.c
 *
 *  Test for Beris Edwards solver.
 *
 *  Edinburgh Soft Matter and Statistical Physics Group and
 *  Edinburgh Parallel Computing Centre
 *
 *  Contributing authors:
 *    Kevin Stratford (kevin@epcc.ed.ac.uk)
 *
 *  (c) 2013-2017 The University of Edinburgh
 *
 *****************************************************************************/

#include <assert.h>
#include <float.h>
#include <math.h>

#include "pe.h"
#include "coords.h"
#include "leesedwards.h"
#include "field.h"
#include "field_grad.h"
#include "blue_phase_beris_edwards.h"
#include "tests.h"

static int do_test_be_tmatrix(void);
static int do_test_be1(void);

/*****************************************************************************
 *
 *  test_be_suite
 *
 *  Just test of noise constants.
 *
 *****************************************************************************/

int test_be_suite(void) {

  do_test_be1();
  do_test_be_tmatrix();

  return 0;
}

/*****************************************************************************
 *
 *  do_test_be1
 *
 *****************************************************************************/

static int do_test_be1(void) {

  pe_t * pe =  NULL;
  cs_t * cs = NULL;
  lees_edw_t * le = NULL;
  beris_edw_t * be = NULL;

  pe_create(MPI_COMM_WORLD, PE_QUIET, &pe);
  cs_create(pe, &cs);
  cs_init(cs);
  lees_edw_create(pe, cs, NULL, &le); /* SHIT not required*/

  beris_edw_create(pe, cs, le, &be);
  assert(be);

  beris_edw_free(be);

  lees_edw_free(le);
  cs_free(cs);
  pe_free(pe);


  return 0;
}

/*****************************************************************************
 *
 *  do_test_be_matrix
 *
 *  Check the noise basis t^i_ab t^j_ab = \delta_ij
 *
 *****************************************************************************/

static int do_test_be_tmatrix(void) {

  int i, j, ia, ib;
  double sum, dij;
  double t[3][3][NQAB];

  beris_edw_tmatrix(t);

  for (i = 0; i < NQAB; i++) {
    for (j = 0; j < NQAB; j++) {

      sum = 0.0;
      for (ia = 0; ia < 3; ia++) {
	for (ib = 0; ib < 3; ib++) {
	  sum += t[ia][ib][i]*t[ia][ib][j];
	}
      }
      dij = (i == j);
      test_assert(fabs(sum - dij) <= DBL_EPSILON);
    }
  }

  return 0;
}
