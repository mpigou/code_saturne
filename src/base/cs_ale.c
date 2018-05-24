/*============================================================================
 * Functions associated to ALE formulation
 *============================================================================*/

/*
  This file is part of Code_Saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2018 EDF S.A.

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation; either version 2 of the License, or (at your option) any later
  version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
  details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
  Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

/*----------------------------------------------------------------------------*/

#include "cs_defs.h"

/*----------------------------------------------------------------------------
 * Standard C library headers
 *----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Local headers
 *----------------------------------------------------------------------------*/

#include "bft_mem.h"
#include "bft_printf.h"

#include "cs_interface.h"

#include "cs_base.h"
#include "cs_boundary_conditions.h"
#include "cs_convection_diffusion.h"
#include "cs_boundary_zone.h"
#include "cs_cdo_quantities.h"
#include "cs_cdo_connect.h"
#include "cs_cdo_main.h"
#include "cs_domain.h"
#include "cs_domain_setup.h"
#include "cs_equation.h"
#include "cs_equation_iterative_solve.h"
#include "cs_face_viscosity.h"
#include "cs_field.h"
#include "cs_field_pointer.h"
#include "cs_field_operator.h"
#include "cs_gui_mobile_mesh.h"
#include "cs_log.h"
#include "cs_physical_constants.h"
#include "cs_math.h"
#include "cs_mesh.h"
#include "cs_mesh_quantities.h"
#include "cs_mesh_bad_cells.h"
#include "cs_time_step.h"

/*----------------------------------------------------------------------------
 * Header for the current file
 *----------------------------------------------------------------------------*/

#include "cs_ale.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*! \cond DOXYGEN_SHOULD_SKIP_THIS */

/*============================================================================
 * Static global variables
 *============================================================================*/

static cs_ale_bc_input_t *_input_bc = NULL;

static cs_real_3_t *_vtx_coord0 = NULL;

static bool _active = false;

/*============================================================================
 * Private function definitions
 *============================================================================*/

/*============================================================================
 * Function pointer in case of fixed velocity
 *============================================================================*/

static void
_fixed_velocity(cs_real_t           time,
                cs_lnum_t           n_pts,
                const cs_lnum_t    *pt_ids,
                const cs_real_t    *xyz,
                bool                compact,
                void               *input,
                cs_real_t          *res)
{
  CS_UNUSED(xyz);
  CS_UNUSED(time);

  cs_real_3_t vel = {0., 0., 0.};
  cs_ale_bc_input_t *input_bc = (cs_ale_bc_input_t *)input;

  /* Retrieving mesh velocity from GUI */
  cs_gui_mobile_mesh_get_fixed_velocity(input_bc->z_name,
                                        vel);

  for (cs_lnum_t p = 0; p < n_pts; p++) {
    const cs_lnum_t  id = (pt_ids != NULL) ? 3*pt_ids[p] : 3*p;
    const cs_lnum_t  off = (compact) ? 3*p : id;

    res[off  ] = vel[0];
    res[off+1] = vel[1];
    res[off+2] = vel[2];
  }
}

/*============================================================================
 * Function pointer in case of fixed displacement
 *============================================================================*/

static void
_fixed_displacement(cs_real_t           time,
                    cs_lnum_t           n_pts,
                    const cs_lnum_t    *pt_ids,
                    const cs_real_t    *xyz,
                    bool                compact,
                    void               *input,
                    cs_real_t          *res)
{
  CS_UNUSED(xyz);
  CS_UNUSED(time);

  cs_real_3_t *disale = (cs_real_3_t *)cs_field_by_name("disale")->val;
  cs_real_3_t *vtx_coord = (cs_real_3_t *)cs_glob_mesh->vtx_coord;
  cs_real_t ddep = 0.;

  cs_real_3_t *resv = (cs_real_3_t *)res;

  for (cs_lnum_t p = 0; p < n_pts; p++) {
    const cs_lnum_t id = (pt_ids != NULL) ? pt_ids[p] : p;
    const cs_lnum_t off = (compact) ? p : id;

    for (int c_id = 0; c_id < 3; c_id++) {
      ddep = disale[id][c_id] + _vtx_coord0[id][c_id] - vtx_coord[id][c_id];
      resv[off][c_id] = ddep / cs_glob_time_step_options->dtref;
    }
  }
}

/*============================================================================
 * Function pointer in case of free surface
 *============================================================================*/

static void
_free_surface(cs_real_t           time,
              cs_lnum_t           n_pts,
              const cs_lnum_t    *pt_ids,
              const cs_real_t    *xyz,
              bool                compact,
              void               *input,
              cs_real_t          *res)
{
  CS_UNUSED(xyz);
  CS_UNUSED(time);

  cs_mesh_t *m = cs_glob_mesh;
  cs_mesh_quantities_t *mq = cs_glob_mesh_quantities;
  cs_ale_bc_input_t *bc_input = (cs_ale_bc_input_t *)input;
  const cs_zone_t *z = cs_boundary_zone_by_name(bc_input->z_name);
  const cs_real_3_t *b_face_normal = (const cs_real_3_t *)mq->b_face_normal;
  cs_real_t ddep = 0.;
  const cs_real_t *grav = cs_glob_physical_constants->gravity;

  /* Boundary mass flux */
  int iflmab = cs_field_get_key_int(CS_F_(u),
                                    cs_field_key_id("boundary_mass_flux_id"));
  const cs_real_t *b_mass_flux = cs_field_by_id(iflmab)->val;

  assert(pt_ids != NULL);

  /* Transform face flux to vertex displacement */
  cs_real_3_t *_mesh_vel = NULL;

  BFT_MALLOC(_mesh_vel, m->n_vertices, cs_real_3_t);

  for (cs_lnum_t v_id = 0; v_id < m->n_vertices; v_id++) {
    _mesh_vel[v_id][0] = 0;
    _mesh_vel[v_id][1] = 0;
    _mesh_vel[v_id][2] = 0;
  }

  for (cs_lnum_t elt_id = 0; elt_id < z->n_elts; elt_id++) {
    cs_lnum_t face_id = z->elt_ids[elt_id];
    cs_real_t distance = b_mass_flux[face_id]
                       / CS_F_(rho_b)->val[face_id];
    cs_real_t g_dot_s = cs_math_3_dot_product(grav, b_face_normal[face_id]);
    cs_lnum_t s = m->b_face_vtx_idx[face_id];
    cs_lnum_t e = m->b_face_vtx_idx[face_id+1];
    for (cs_lnum_t k = s; k < e; k++) {
      cs_lnum_t v_id = m->b_face_vtx_lst[k];
      for (int i = 0; i < 3; i++)
        _mesh_vel[v_id][i] += b_mass_flux[face_id] * grav[i]
          / (g_dot_s * CS_F_(rho_b)->val[face_id]); //FIXME portion
    }
  }

  /* Handle parallelism */
  if (m->vtx_interfaces != NULL)
    cs_interface_set_sum(m->vtx_interfaces,
                         m->n_vertices,
                         3,
                         true,
                         CS_REAL_TYPE,
                         _mesh_vel);

  cs_real_3_t *resv = (cs_real_3_t *)res;

  for (cs_lnum_t p = 0; p < n_pts; p++) {
    const cs_lnum_t  v_id = (pt_ids != NULL) ? pt_ids[p] : p;
    const cs_lnum_t  off = (compact) ? p : v_id;

    for (int i = 0; i < 3; i++) {
      resv[off][i] = _mesh_vel[v_id][i];//FIXME  v_id or off ?
    }
  }
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief This subroutine performs the solving of a Poisson equation
 * on the mesh velocity for ALE module. It also updates the mesh displacement
 * so that it can be used to update mass fluxes (due to mesh displacement).
 *
 * \param[in]     domain        domain quantities
 * \param[in]     impale        Indicator for fixed node displacement
 * \param[in]     ale_bc_type   Type of boundary for ALE
 */
/*----------------------------------------------------------------------------*/
void
_ale_solve_poisson_cdo(const cs_domain_t  *domain,
                       const int           impale[],
                       const int           ale_bc_type[])
{
  cs_real_3_t *disale, *disala;

  /* Retrieving fields */
  disale = (cs_real_3_t *)cs_field_by_name("disale")->val;
  disala = (cs_real_3_t *)cs_field_by_name("disale")->val_pre;

  if (_vtx_coord0 == NULL) {

    BFT_MALLOC(_vtx_coord0, cs_glob_mesh->n_vertices, cs_real_3_t);

    memcpy(_vtx_coord0, cs_glob_mesh->vtx_coord, cs_glob_mesh->n_vertices * sizeof(cs_real_3_t));

    cs_ale_setup_boundaries(impale, ale_bc_type);

    cs_equation_initialize(domain->mesh,
                           domain->connect,
                           domain->cdo_quantities,
                           domain->time_step);
  }

  /* Build and solve equation on the mesh velocity */
  cs_equation_t *eq = cs_equation_by_name("mesh_velocity");

  if (cs_equation_uses_new_mechanism(eq))
    cs_equation_solve_steady_state(
        domain->mesh,
        eq);

  else { /* Deprecated */

    /* Define the algebraic system */
    cs_equation_build_system(
        domain->mesh,
        domain->time_step,
        domain->dt_cur,
        eq);

    /* Solve the algebraic system */
    cs_equation_solve_deprecated(eq);

  }

  cs_real_3_t *m_vel = (cs_real_3_t *) (cs_field_by_name("mesh_velocity")->val);

  for (cs_lnum_t inod = 0; inod < cs_glob_mesh->n_vertices; inod++) {
    if (impale[inod] == 0) {
      for (int c_id = 0; c_id < 3; c_id++) {
        disale[inod][c_id] =  disala[inod][c_id]
                            + m_vel[inod][c_id]*cs_glob_time_step_options->dtref;
      }
    }
  }
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Solve a Poisson equation on the mesh velocity in ALE framework.
 *
 * It also updates the mesh displacement
 * so that it can be used to update mass fluxes (due to mesh displacement).
 *
 * \param[in]       domain        domain quantities
 * \param[in]       iterns        Navier-Stokes iteration number
 * \param[in]       impale        Indicator for fixed node displacement
 * \param[in]       ale_bc_type   Type of boundary for ALE
 */
/*----------------------------------------------------------------------------*/

void
_ale_solve_poisson_legacy(const cs_domain_t *domain,
                          const int          iterns,
                          const int         *impale,
                          const int         *ale_bc_type)
{
  cs_mesh_t *m = domain->mesh;
  cs_mesh_quantities_t *mq = domain->mesh_quantities;
  const cs_lnum_t n_cells_ext = m->n_cells_with_ghosts;
  const cs_lnum_t n_vertices = m->n_vertices;
  const cs_lnum_t n_i_faces = m->n_i_faces;
  const cs_lnum_t n_b_faces = m->n_b_faces;
  const cs_lnum_t *b_face_cells = (const cs_lnum_t *)m->b_face_cells;
  const cs_real_t *b_dist = (const cs_real_t *)mq->b_dist;
  const cs_real_3_t *b_face_normal = (const cs_real_3_t *)mq->b_face_normal;
  const cs_real_t *grav = cs_glob_physical_constants->gravity;
  const int key_cal_opt_id = cs_field_key_id("var_cal_opt");

  /* The mass flux is necessary to call cs_equation_iterative_solve_vector
     but not used (iconv = 0), except for the free surface, where it is used
     as a boundary condition */

  const int kimasf = cs_field_key_id("inner_mass_flux_id");
  const int kbmasf = cs_field_key_id("boundary_mass_flux_id");
  const cs_real_t *i_massflux
    = cs_field_by_id(cs_field_get_key_int(CS_F_(u), kimasf))->val;
  const cs_real_t *b_massflux
    = cs_field_by_id(cs_field_get_key_int(CS_F_(u), kbmasf))->val;

  /* 1. Initialization */

  cs_real_3_t rinfiv =
  { cs_math_infinite_r,
    cs_math_infinite_r,
    cs_math_infinite_r};

  cs_real_3_t *smbr;
  cs_real_33_t *fimp;
  BFT_MALLOC(smbr, n_cells_ext, cs_real_3_t);
  BFT_MALLOC(fimp, n_cells_ext, cs_real_33_t);

  cs_real_3_t *mshvel = (cs_real_3_t *)CS_F_(mesh_u)->val;
  cs_real_3_t *mshvela = (cs_real_3_t *)CS_F_(mesh_u)->val_pre;

  cs_real_3_t *disale = (cs_real_3_t *)cs_field_by_name("disale")->val;
  cs_real_3_t *disala = (cs_real_3_t *)cs_field_by_name("disale")->val_pre;

  cs_var_cal_opt_t var_cal_opt;
  cs_field_get_key_struct(CS_F_(mesh_u), key_cal_opt_id, &var_cal_opt);

  if (var_cal_opt.iwarni >= 1) {
    bft_printf("\n   ** SOLVING MESH VELOCITY\n"
               "      ---------------------\n");
  }

  /* We compute the boundary condition on the mesh velocity at the free surface
   * from the new mass flux. */

  /* Density at the boundary */
  cs_real_t *brom = CS_F_(rho_b)->val;

  cs_field_bc_coeffs_t *bc_coeffs = CS_F_(mesh_u)->bc_coeffs;

  cs_real_3_t  *bc_a   = (cs_real_3_t  *)bc_coeffs->a;
  cs_real_3_t  *bc_af  = (cs_real_3_t  *)bc_coeffs->af;
  cs_real_33_t *bc_b   = (cs_real_33_t *)bc_coeffs->b;
  cs_real_33_t *bc_bf  = (cs_real_33_t *)bc_coeffs->bf;

  int idftnp = var_cal_opt.idften;

  /* The mesh moves in the direction of the gravity in case of free-surface */
  for (cs_lnum_t face_id = 0; face_id < n_b_faces; face_id++) {
    if (ale_bc_type[face_id] == CS_FREE_SURFACE) {
      cs_lnum_t cell_id = b_face_cells[face_id];
      cs_real_t distbf = b_dist[face_id];

      cs_real_6_t hintt = {0., 0., 0., 0., 0., 0.};
      if (idftnp & CS_ISOTROPIC_DIFFUSION) {
        for (int isou = 0; isou < 3; isou++)
          hintt[isou] = CS_F_(vism)->val[cell_id] / distbf;
      } else if (idftnp & CS_ANISOTROPIC_LEFT_DIFFUSION) {
          for (int isou = 0; isou < 6; isou++)
            hintt[isou] = CS_F_(vism)->val[6*cell_id+isou] / distbf;
     }

     cs_real_t prosrf = cs_math_3_dot_product(grav, b_face_normal[face_id]);

     cs_real_3_t pimpv;
     for (int i = 0; i < 3; i++)
       pimpv[i] = grav[i]*b_massflux[face_id]/(brom[face_id]*prosrf);

     cs_boundary_conditions_set_dirichlet_vector_aniso((bc_a[face_id]),
                                                       (bc_af[face_id]),
                                                       (bc_b[face_id]),
                                                       (bc_bf[face_id]),
                                                       pimpv,
                                                       hintt,
                                                       rinfiv);
    }
  }

  /* 2. Solving of the mesh velocity equation */

  if (var_cal_opt.iwarni >= 1)
    bft_printf("\n\n           SOLVING VARIABLE %s\n\n",
               CS_F_(mesh_u)->name);

  for (cs_lnum_t cell_id = 0; cell_id < n_cells_ext; cell_id++) {
    for (int isou = 0; isou < 3; isou++) {
      smbr[cell_id][isou] = 0.;
      for (int jsou = 0; jsou < 3; jsou++)
        fimp[cell_id][jsou][isou] = 0.;
    }
  }

  cs_real_t *i_visc = NULL, *b_visc = NULL;

  BFT_MALLOC(b_visc, n_b_faces, cs_real_t);

  if (idftnp & CS_ISOTROPIC_DIFFUSION) {
    BFT_MALLOC(i_visc, n_i_faces, cs_real_t);

    cs_face_viscosity(m,
                      mq,
                      cs_glob_space_disc->imvisf,
                      CS_F_(vism)->val,
                      i_visc,
                      b_visc);

  }
  else if (idftnp & CS_ANISOTROPIC_LEFT_DIFFUSION) {
    BFT_MALLOC(i_visc, 9*n_i_faces, cs_real_t);

    cs_face_anisotropic_viscosity_vector(m,
                                         mq,
                                         cs_glob_space_disc->imvisf,
                                         (cs_real_6_t *)CS_F_(vism)->val,
                                         (cs_real_33_t *)i_visc,
                                         b_visc);
  }

  var_cal_opt.relaxv = 1.;
  var_cal_opt.thetav = 1.;
  var_cal_opt.istat  = -1;
  var_cal_opt.idifft = -1;

  cs_equation_iterative_solve_vector(cs_glob_time_step_options->idtvar,
                                     iterns,
                                     CS_F_(mesh_u)->id,
                                     CS_F_(mesh_u)->name,
                                     0, /* ivisep */
                                     0, /* iescap */
                                     &var_cal_opt,
                                     (const cs_real_3_t *)mshvela,
                                     (const cs_real_3_t *)mshvela,
                                     (const cs_real_3_t *)bc_coeffs->a,
                                     (const cs_real_33_t *)bc_coeffs->b,
                                     (const cs_real_3_t *)bc_coeffs->af,
                                     (const cs_real_33_t *)bc_coeffs->bf,
                                     i_massflux,
                                     b_massflux,
                                     i_visc,
                                     b_visc,
                                     i_visc,
                                     b_visc,
                                     NULL, /* i_secvis */
                                     NULL, /* b_secvis */
                                     NULL, /* viscel */
                                     NULL, /* weighf */
                                     NULL, /* weighb */
                                     0,    /* icvflv */
                                     NULL, /* icvfli */
                                     (const cs_real_33_t *)fimp,
                                     smbr,
                                     mshvel,
                                     NULL); /* eswork */

  /* Free memory */
  BFT_FREE(smbr);
  BFT_FREE(fimp);
  BFT_FREE(i_visc);
  BFT_FREE(b_visc);

  /* 3. Update nodes displacement */

  cs_real_3_t *dproj;
  cs_real_33_t *gradm;

  /* Allocate a temporary array */
  BFT_MALLOC(dproj, n_vertices, cs_real_3_t);
  BFT_MALLOC(gradm, n_cells_ext, cs_real_33_t);

  bool use_previous_t = false;
  int inc = 1;

  cs_field_gradient_vector(CS_F_(mesh_u),
                           use_previous_t,
                           inc,
                           gradm);

  cs_ale_project_displacement(ale_bc_type,
                              (const cs_real_3_t *)mshvel,
                              (const cs_real_33_t *)gradm,
                              (const cs_real_3_t *)bc_coeffs->a,
                              (const cs_real_33_t *)bc_coeffs->b,
                              (const cs_real_t *)CS_F_(dt)->val,
                              dproj);

  /* FIXME : warning if nterup > 1, use itrale ? */
  /* Update mesh displacement only where it is not
     imposed by the user (ie when impale <> 1) */
  for (cs_lnum_t inod = 0; inod < n_vertices; inod++) {
    if (impale[inod] == 0) {
      for (int isou = 0; isou < 3; isou++)
        disale[inod][isou] = disala[inod][isou] + dproj[inod][isou];
    }
  }

  /* Free memory */
  BFT_FREE(dproj);
  BFT_FREE(gradm);
}
/*! (DOXYGEN_SHOULD_SKIP_THIS) \endcond */

/*============================================================================
 * Global variables
 *============================================================================*/

int cs_glob_ale = 0;

/*============================================================================
 * Fortran wrapper function definitions
 *============================================================================*/

/*----------------------------------------------------------------------------
 * Get pointer to cs_glob_ale
 *----------------------------------------------------------------------------*/

void
cs_f_ale_get_pointers(int **iale)
{
  *iale = &cs_glob_ale;
}

/*============================================================================
 * Public function definitions
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute cell and face centers of gravity, cell volumes
 *         and update bad cells.
 *
 * \param[out]       min_vol        Minimum cell volume
 * \param[out]       max_vol        Maximum cell volume
 * \param[out]       tot_vol        Total cell volume
 */
/*----------------------------------------------------------------------------*/

void
cs_ale_update_mesh_quantities(cs_real_t  *min_vol,
                              cs_real_t  *max_vol,
                              cs_real_t  *tot_vol)
{
  cs_mesh_t *m = cs_glob_mesh;
  cs_mesh_quantities_t *mq = cs_glob_mesh_quantities;

  cs_mesh_quantities_compute(m, mq);
  cs_mesh_bad_cells_detect(m, mq);

  *min_vol = mq->min_vol;
  *max_vol = mq->max_vol;
  *tot_vol = mq->tot_vol;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Project the displacement on mesh vertices (solved on cell center).
 *
 * \param[in]       ale_bc_type   Type of boundary for ALE
 * \param[in]       meshv         Mesh velocity
 * \param[in]       gradm         Mesh velocity gradient
 *                                (du_i/dx_j : gradv[][i][j])
 * \param[in]       claale        Boundary conditions A
 * \param[in]       clbale        Boundary conditions B
 * \param[in]       dt            Time step
 * \param[out]      disp_proj     Displacement projected on vertices
 */
/*----------------------------------------------------------------------------*/

void
cs_ale_project_displacement(const int           ale_bc_type[],
                            const cs_real_3_t  *meshv,
                            const cs_real_33_t  gradm[],
                            const cs_real_3_t  *claale,
                            const cs_real_33_t *clbale,
                            const cs_real_t    *dt,
                            cs_real_3_t        *disp_proj)
{
  int  j, face_id, vtx_id, cell_id, cell_id1, cell_id2;
  bool *vtx_interior_indicator = NULL;
  cs_real_t *vtx_counter = NULL;
  const cs_mesh_t  *m = cs_glob_mesh;
  cs_mesh_quantities_t *mq = cs_glob_mesh_quantities;
  const int n_vertices = m->n_vertices;
  const int n_cells = m->n_cells;
  const int n_b_faces = m->n_b_faces;
  const int n_i_faces = m->n_i_faces;
  const int dim = m->dim;
  const cs_real_3_t *restrict vtx_coord
    = (const cs_real_3_t *restrict)m->vtx_coord;
  const cs_real_3_t *restrict cell_cen
    = (const cs_real_3_t *restrict)mq->cell_cen;
  const cs_real_3_t *restrict face_cen
    = (const cs_real_3_t *restrict)mq->b_face_cog;

  BFT_MALLOC(vtx_counter, n_vertices, cs_real_t);
  BFT_MALLOC(vtx_interior_indicator, n_vertices, bool);

  for (vtx_id = 0; vtx_id < n_vertices; vtx_id++) {

    vtx_counter[vtx_id] = 0.;
    vtx_interior_indicator[vtx_id] = true;

    for (int i = 0; i < dim; i++)
      disp_proj[vtx_id][i] = 0.;

  }

  /* All nodes wich belongs to a boundary face where the
     displacement is imposed (that is all faces except sliding BCs)
     are boundary nodes, the others are interior nodes. */

  for (face_id = 0; face_id < n_b_faces; face_id++) {

    if (ale_bc_type[face_id] != CS_ALE_SLIDING) {

      for (j = m->b_face_vtx_idx[face_id];
           j < m->b_face_vtx_idx[face_id+1];
           j++) {


        vtx_id = m->b_face_vtx_lst[j];
        vtx_interior_indicator[vtx_id] = false;

      } /* End of loop on vertices of the face */

    }

  } /* End of loop on border faces */


  /* Interior face and nodes treatment */

  for (face_id = 0; face_id < n_i_faces; face_id++) {

    cell_id1 = m->i_face_cells[face_id][0];
    cell_id2 = m->i_face_cells[face_id][1];

    cs_real_t dvol1 = 1./mq->cell_vol[cell_id1];
    cs_real_t dvol2 = 1./mq->cell_vol[cell_id2];

    if (cell_id1 < n_cells) { /* Test to take into account face only once */

      for (j = m->i_face_vtx_idx[face_id];
           j < m->i_face_vtx_idx[face_id+1];
           j++) {

        /* Get the vertex number */

        vtx_id = m->i_face_vtx_lst[j];

        if (vtx_interior_indicator[vtx_id]) {

          /* Get the vector from the cell center to the node */

          cs_real_3_t cen1_node;
          cs_real_3_t cen2_node;
          for (int i = 0; i < 3; i++) {
            cen1_node[i] = vtx_coord[vtx_id][i]-cell_cen[cell_id1][i];
            cen2_node[i] = vtx_coord[vtx_id][i]-cell_cen[cell_id2][i];
          }

          for (int i = 0; i < 3; i++) {
            disp_proj[vtx_id][i] +=
              dvol1*(meshv[cell_id1][i] + gradm[cell_id1][i][0]*cen1_node[0]
                                        + gradm[cell_id1][i][1]*cen1_node[1]
                                        + gradm[cell_id1][i][2]*cen1_node[2])
              * dt[cell_id1]
            + dvol2*(meshv[cell_id2][i] + gradm[cell_id2][i][0]*cen2_node[0]
                                        + gradm[cell_id2][i][1]*cen2_node[1]
                                        + gradm[cell_id2][i][2]*cen2_node[2])
              * dt[cell_id2];
          }

          vtx_counter[vtx_id] += dvol1+dvol2;

        } /* End of Interior nodes */

      }

    }

  } /* End of loop on internal faces */

  /* Border face treatment.
     only border face contribution */

  for (face_id = 0; face_id < n_b_faces; face_id++) {

    cell_id = m->b_face_cells[face_id];

    for (j = m->b_face_vtx_idx[face_id];
         j < m->b_face_vtx_idx[face_id+1];
         j++) {

      vtx_id = m->b_face_vtx_lst[j];

      if (!vtx_interior_indicator[vtx_id]) {

        /* Get the vector from the face center to the node*/

        cs_real_3_t face_node;
        for (int i = 0; i < 3; i++)
          face_node[i] = vtx_coord[vtx_id][i] - face_cen[face_id][i];

        /* 1st order extrapolation of the mesh velocity at the face center
         * to the node */

        cs_real_3_t vel_node;
        for (int i = 0; i<3; i++)
          vel_node[i] = claale[face_id][i]
                      + gradm[cell_id][i][0]*face_node[0]
                      + gradm[cell_id][i][1]*face_node[1]
                      + gradm[cell_id][i][2]*face_node[2];

        cs_real_t dsurf = 1./mq->b_face_surf[face_id];

        for (int i = 0; i<3; i++)
          disp_proj[vtx_id][i] += dsurf * dt[cell_id] *
            (vel_node[i] + clbale[face_id][i][0]*meshv[cell_id][0]
                         + clbale[face_id][i][1]*meshv[cell_id][1]
                         + clbale[face_id][i][2]*meshv[cell_id][2]);

        vtx_counter[vtx_id] += dsurf;

      } /* End of boundary nodes */

    } /* End of loop on vertices of the face */

  } /* End of loop on border faces */


  /* If the boundary face IS a sliding face.
     We project the displacment paralelly to the face. */

  for (face_id = 0; face_id < n_b_faces; face_id++) {

    if (ale_bc_type[face_id] == CS_ALE_SLIDING) {

      for (j = m->b_face_vtx_idx[face_id];
           j < m->b_face_vtx_idx[face_id+1];
           j++) {


        vtx_id = m->b_face_vtx_lst[j];
        disp_proj[vtx_id][0] = clbale[face_id][0][0]*disp_proj[vtx_id][0]
                             + clbale[face_id][0][1]*disp_proj[vtx_id][1]
                             + clbale[face_id][0][2]*disp_proj[vtx_id][2];
        disp_proj[vtx_id][1] = clbale[face_id][1][0]*disp_proj[vtx_id][0]
                             + clbale[face_id][1][1]*disp_proj[vtx_id][1]
                             + clbale[face_id][1][2]*disp_proj[vtx_id][2];
        disp_proj[vtx_id][2] = clbale[face_id][2][0]*disp_proj[vtx_id][0]
                             + clbale[face_id][2][1]*disp_proj[vtx_id][1]
                             + clbale[face_id][2][2]*disp_proj[vtx_id][2];

      } /* End of loop on vertices of the face */

    }

  } /* End of loop on border faces */

  if (m->vtx_interfaces != NULL) {
    cs_interface_set_sum(m->vtx_interfaces,
                         n_vertices,
                         3,
                         true,
                         CS_REAL_TYPE,
                         disp_proj);
    cs_interface_set_sum(m->vtx_interfaces,
                         n_vertices,
                         1,
                         true,
                         CS_REAL_TYPE,
                         vtx_counter);
  }

  for (vtx_id = 0; vtx_id < n_vertices; vtx_id++)
    for (int i = 0; i < dim; i++)
      disp_proj[vtx_id][i] /= vtx_counter[vtx_id];

  BFT_FREE(vtx_counter);
  BFT_FREE(vtx_interior_indicator);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Update mesh in the ALE framework.
 *
 * \param[in]       itrale        number of the current ALE iteration
 * \param[in]       xyzno0        nodes coordinates of the initial mesh
 */
/*----------------------------------------------------------------------------*/

void
cs_ale_update_mesh(const int           itrale,
                   const cs_real_3_t  *xyzno0)
{
  const cs_mesh_t *m = cs_glob_mesh;
  cs_mesh_quantities_t *mq = cs_glob_mesh_quantities;
  cs_real_3_t *vtx_coord = (cs_real_3_t *)m->vtx_coord;
  cs_var_cal_opt_t var_cal_opt;
  const cs_lnum_t n_cells_ext = m->n_cells_with_ghosts;
  const int key_cal_opt_id = cs_field_key_id("var_cal_opt");
  const cs_lnum_t n_vertices = cs_glob_mesh->n_vertices;
  const int ndim = cs_glob_mesh->dim;
  cs_time_step_t *ts = cs_get_glob_time_step();

  /* Initialization */
  cs_field_get_key_struct(CS_F_(mesh_u), key_cal_opt_id, &var_cal_opt);

  if (var_cal_opt.iwarni >= 1) {
    bft_printf("\n ------------------------"
               "---------------------------"
               "---------\n\n\n"
               "  Update mesh (ALE)\n"
               "  =================\n\n");
  }

  /* Retrieving fields */
  cs_real_3_t *disale = (cs_real_3_t *)cs_field_by_name("disale")->val;
  cs_real_3_t *disala = (cs_real_3_t *)cs_field_by_name("disale")->val_pre;

  /* Update geometry */
  for (int inod = 0; inod < n_vertices; inod++) {
    for (int idim = 0; idim < ndim; idim++) {
      vtx_coord[inod][idim] = xyzno0[inod][idim] + disale[inod][idim];
      disala[inod][idim] = vtx_coord[inod][idim] - xyzno0[inod][idim];
    }
  }

  cs_ale_update_mesh_quantities(&(mq->min_vol),
                                &(mq->max_vol),
                                &(mq->tot_vol));

  /* Abort at the end of the current time-step if there is a negative volume */
  if (mq->min_vol <= 0.) {
    ts->nt_max = ts->nt_cur;
  }

  /* The mesh velocity is reverted to its initial value if the current time step is
     the initialization time step */
  if (itrale == 0) {
    cs_field_t *f = cs_field_by_name("mesh_velocity");
    if (f->location_id == CS_MESH_LOCATION_VERTICES) {
      for (cs_lnum_t inod = 0; inod < n_vertices; inod++) {
        for (int idim = 0; idim < ndim; idim++) {
          f->val[3*inod+idim] = f->val_pre[3*inod+idim];
        }
      }
    }
    else if (f->location_id == CS_MESH_LOCATION_CELLS) {
      for (cs_lnum_t cell_id = 0; cell_id < n_cells_ext; cell_id++) {
        for (int idim = 0; idim < ndim; idim++) {
          f->val[3*cell_id+idim] = f->val_pre[3*cell_id+idim];
        }
      }
    }
  }

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Solve a Poisson equation on the mesh velocity in ALE framework.
 *
 * It also updates the mesh displacement
 * so that it can be used to update mass fluxes (due to mesh displacement).
 *
 * \param[in]       iterns        Navier-Stokes iteration number
 * \param[in]       impale        Indicator for fixed node displacement
 * \param[in]       ale_bc_type   Type of boundary for ALE
 */
/*----------------------------------------------------------------------------*/

void
cs_ale_solve_mesh_velocity(const int   iterns,
                           const int  *impale,
                           const int  *ale_bc_type)
{
  if (cs_glob_ale == CS_ALE_LEGACY)
    _ale_solve_poisson_legacy(cs_glob_domain, iterns, impale, ale_bc_type);

  else if (cs_glob_ale == CS_ALE_CDO)
    _ale_solve_poisson_cdo(cs_glob_domain, impale, ale_bc_type);

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Setup the equations solving the mesh velocity
 *
 * \param[in, out]   domain     pointer to a cs_domain_t structure
 */
/*----------------------------------------------------------------------------*/
void
cs_ale_setup(cs_domain_t *domain)
{
  const int key_cal_opt_id = cs_field_key_id("var_cal_opt");
  cs_var_cal_opt_t var_cal_opt;

  /* Mesh viscosity (iso or ortho)
   * TODO declare it before: add in activate, def here...  */
  int dim = cs_field_by_name("mesh_viscosity")->dim;
  cs_property_type_t type = (dim == 1) ? CS_PROPERTY_ISO : CS_PROPERTY_ORTHO;
  cs_property_t  *viscosity = cs_property_add("mesh_viscosity", type);

  cs_property_def_by_field(viscosity, cs_field_by_name("mesh_viscosity"));

  cs_field_get_key_struct(CS_F_(mesh_u), key_cal_opt_id, &var_cal_opt);

  //FIXME should be done elsewhere
  cs_domain_set_output_param(domain,
                             -1,      // restart frequency
                             cs_glob_log_frequency,
                             var_cal_opt.iwarni);

  cs_equation_param_t  *eqp = cs_equation_param_by_name("mesh_velocity");

  cs_equation_add_diffusion(eqp, viscosity);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Setup the equations solving the mesh velocity
 *
 * \param[in]     impale        Indicator for fixed node displacement
 * \param[in]     ale_bc_type   Type of boundary for ALE
 */
/*----------------------------------------------------------------------------*/
void
cs_ale_setup_boundaries(const int           impale[],
                        const int           ale_bc_type[])
{
  cs_mesh_t *m = cs_glob_mesh;
  cs_lnum_t f_id, inod0;
  int face_type, imp_dis;
  int n_zones = cs_boundary_zone_n_zones();
  cs_real_t bc_value = 0.;
  cs_lnum_t _n_input_bc = 0;
  bool is_defined = false;

  /* Maximum size: n_zones */
  BFT_MALLOC(_input_bc, n_zones, cs_ale_bc_input_t);

  cs_equation_param_t *eqp = cs_equation_param_by_name("mesh_velocity");

  for (int j = 0; j < n_zones; j++) {
    is_defined = false;
    const cs_zone_t *z = cs_boundary_zone_by_id(j);
    if (z->id > 0 && strcmp(z->name, "domain_walls") != 0) {

      /* User boundary conditions, already defined */
      if (eqp->bc_defs != NULL) {
        for (int jj = 0; jj < eqp->n_bc_defs; jj++) {
          if (eqp->bc_defs[jj]->z_id == z->id) {
            is_defined = true;
            break;
          }
        }
      }

      if (is_defined)
        continue;

      /* Get face type */
      for (cs_lnum_t ii = 0; ii < z->n_elts; ii++) {
        f_id = z->elt_ids[ii];
        inod0 = m->b_face_vtx_lst[m->b_face_vtx_idx[f_id]];
        face_type = ale_bc_type[f_id];
        imp_dis = impale[inod0];
        break;
      }

      /* Fixed boundary */
      if (face_type == CS_ALE_FIXED) {
        cs_equation_add_bc_by_value(eqp,
                                    CS_PARAM_BC_HMG_DIRICHLET,
                                    z->name,
                                    &bc_value);
      /* Fixed velocity */
      } else if (face_type == CS_ALE_IMPOSED_VEL && !imp_dis) {

        _input_bc[_n_input_bc].z_name = z->name;

        cs_equation_add_bc_by_analytic(eqp,
                                       CS_PARAM_BC_DIRICHLET,
                                       z->name,
                                       _fixed_velocity,
                                       _input_bc + _n_input_bc);

        _n_input_bc++;

      /* Fixed displacement */
      } else if (face_type == CS_ALE_IMPOSED_VEL && imp_dis) {

        _input_bc[_n_input_bc].z_name = z->name;

        cs_equation_add_bc_by_analytic(eqp,
                                       CS_PARAM_BC_DIRICHLET,
                                       z->name,
                                       _fixed_displacement,
                                       _input_bc + _n_input_bc);

        _n_input_bc++;
      /* Free surface */
      } else if (face_type == CS_FREE_SURFACE) {
        _input_bc[_n_input_bc].z_name = z->name;

        cs_equation_add_bc_by_analytic(eqp,
                                       CS_PARAM_BC_DIRICHLET,
                                       z->name,
                                       _free_surface,
                                       _input_bc + _n_input_bc);

        _n_input_bc++;
      }
    }
  }
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Activate the mesh velocity solving with CDO
 */
/*----------------------------------------------------------------------------*/
void
cs_ale_activate(void)
{
  if (_active)
    return;

  _active = true;

  cs_domain_set_cdo_mode(cs_glob_domain, CS_DOMAIN_CDO_MODE_WITH_FV);

  cs_equation_t  *eq =
      cs_equation_add("mesh_velocity", /* equation name */
                      "mesh_velocity", /* associated variable field name */
                      CS_EQUATION_TYPE_PREDEFINED,
                      3, /* dimension of the unknown */
                      CS_PARAM_BC_HMG_NEUMANN); /* default boundary */

  cs_equation_param_t  *eqp = cs_equation_get_param(eq);

  /* System to solve is SPD by construction */
  cs_equation_set_param(eqp, CS_EQKEY_ITSOL, "cg");

#if defined(HAVE_PETSC)  /* Modify the default settings */
  cs_equation_set_param(eqp, CS_EQKEY_SOLVER_FAMILY, "petsc");
  cs_equation_set_param(eqp, CS_EQKEY_PRECOND, "amg");
#else
  cs_equation_set_param(eqp, CS_EQKEY_PRECOND, "jacobi");
#endif

  cs_equation_set_param(eqp, CS_EQKEY_SPACE_SCHEME, "cdo_vb");

  /* BC settings */
  cs_equation_set_param(eqp, CS_EQKEY_BC_ENFORCEMENT, "algebraic");

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Test if mesh velocity solving with CDO is activated
 *
 * \return true ifmesh velocity solving with CDO is requested, false otherwise
 */
/*----------------------------------------------------------------------------*/
bool
cs_ale_is_activated(void)
{
  if (_active)
    return true;
  else
    return false;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Finalize the setup stage for the equation of the mesh velocity
 *
 * \param[in]      connect    pointer to a cs_cdo_connect_t structure
 * \param[in]      cdoq       pointer to a cs_cdo_quantities_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_ale_finalize_setup(const cs_cdo_connect_t       *connect,
                      const cs_cdo_quantities_t    *cdoq)
{
  CS_UNUSED(connect);
  CS_UNUSED(cdoq);

  return;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Free the main structure related to the ALE mesh velocity solving
 */
/*----------------------------------------------------------------------------*/

void
cs_ale_destroy_all(void)
{
  BFT_FREE(_vtx_coord0);
  BFT_FREE(_input_bc);

  return;
}

/*----------------------------------------------------------------------------*/

END_C_DECLS
