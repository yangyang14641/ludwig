/*****************************************************************************
 *
 *  lc_droplet.h
 *
 *  Routines related to liquid crystal droplet free energy
 *  and molecular field.
 *
 *  $Id:               $
 *
 *  Edinburgh Soft Matter and Statistical Physics Group and
 *  Edinburgh Parallel Computing Centre
 *
 *  (c) 2012 The University of Edinburgh
 *
 *****************************************************************************/

#ifndef LC_DROPLET_H
#define LC_DROPLET_H

#include "coords.h"
#include "free_energy.h"
#include "field.h"
#include "field_grad.h"
#include "hydro.h"

int lc_droplet_phi_set(field_t * phi, field_grad_t * dphi);
int lc_droplet_q_set(field_t * q, field_grad_t * dq);

void lc_droplet_set_parameters(double gamma0, double delta, double W);
double lc_droplet_get_gamma0(void);
double lc_droplet_free_energy_density(const int index);
double lc_droplet_gamma_calculate(const double phi);
double lc_droplet_anchoring_energy_density(double q[3][3], double dphi[3]);
void lc_droplet_molecular_field(const int index, double h[3][3]);
void lc_droplet_anchoring_molecular_field(const int index, double h[3][3]);
double lc_droplet_chemical_potential(const int index, const int nop);
double lc_droplet_chemical_potential_lc(const int index);
void lc_droplet_chemical_stress(const int index, double sth[3][3]);
void lc_droplet_bodyforce(hydro_t * hydro, double dt);
void lc_droplet_chemical_stress_lc(const int index, double sth[3][3]);
void blue_phase_symmetric_stress(const int index, double sth[3][3]);
void blue_phase_antisymmetric_stress(const int index, double sth[3][3]);
int lc_droplet_extract_total_force(coords_t * cs, hydro_t * hydro);

#endif
