/*****************************************************************************
 *
 *  hydro_s.h
 *
 *  Structure.
 *
 *****************************************************************************/

#ifndef HYDRO_S_H
#define HYDRO_S_H

#include <mpi.h>
#include "memory.h"

#include "io_harness.h"
#include "halo_swap.h"
#include "hydro.h"

/* Data storage: Always a 3-vector NHDIM */

#define NHDIM 3

struct hydro_s {
  int nsite;               /* Allocated sites (local) */
  int nhcomm;              /* Width of halo region for u field */
  double * u;              /* Velocity field (on host)*/
  double * f;              /* Body force field (on host) */

  pe_t * pe;               /* Parallel environment */
  cs_t * cs;               /* Coordinate system */
  lees_edw_t * le;         /* Lees Edwards */
  io_info_t * info;        /* I/O handler. */
  halo_swap_t * halo;      /* Halo driver object */
  hydro_t * target;        /* structure on target */ 
};

#endif
