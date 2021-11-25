#ifndef __CS_CDOFB_MONOLITHIC_PRIV_H__
#define __CS_CDOFB_MONOLITHIC_PRIV_H__

/*============================================================================
 * Build an algebraic CDO face-based system for the Navier-Stokes equations
 * and solved it as one block (monolithic approach of the velocity-pressure
 * coupling)
 *============================================================================*/

/*
  This file is part of Code_Saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2021 EDF S.A.

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
 *  Local headers
 *----------------------------------------------------------------------------*/

#include "cs_cdo_bc.h"
#include "cs_cdofb_priv.h"
#include "cs_cdofb_scaleq.h"
#include "cs_cdofb_vecteq.h"
#include "cs_cdofb_monolithic_sles.h"
#include "cs_cdofb_navsto.h"
#include "cs_equation_bc.h"
#include "cs_equation_common.h"
#include "cs_equation_priv.h"
#include "cs_iter_algo.h"
#include "cs_navsto_coupling.h"
#include "cs_static_condensation.h"
#include "cs_timer.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*=============================================================================
 * Additional doxygen documentation
 *============================================================================*/

/*!
 * \file cs_cdofb_monolithic_priv.h
 *
 * \brief Structures and function pointers useful to build and solve
 *        the Navier-Stokes equations with face-based schemes and a
 *        monolithic approach
 */

typedef struct _cdofb_monolithic_t  cs_cdofb_monolithic_t;

/*! \cond DOXYGEN_SHOULD_SKIP_THIS */

/*=============================================================================
 * Definitions of function pointers
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Initialize a matrix and its related structures needed during the
 *         assembly step.
 *
 * \param[in, out]  sc           pointer to scheme context structure
 */
/*----------------------------------------------------------------------------*/

typedef void
(cs_cdofb_monolithic_init_matrix_t)(cs_cdofb_monolithic_t     *sc);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Perform the assembly stage for a vector-valued system obtained
 *         with CDO-Fb schemes
 *
 * \param[in]       csys              pointer to a cs_cell_sys_t structure
 * \param[in]       cm                pointer to a cs_cell_mesh_t structure
 * \param[in]       div_op            array with the divergence op. values
 * \param[in]       has_sourceterm    has the equation a source term?
 * \param[in, out]  sc                pointer to scheme context structure
 * \param[in, out]  eqc               context structure for a vector-valued Fb
 * \param[in, out]  eqa               pointer to cs_equation_assemble_t
 */
/*----------------------------------------------------------------------------*/

typedef void
(cs_cdofb_monolithic_assemble_t)(const cs_cell_sys_t            *csys,
                                 const cs_cell_mesh_t           *cm,
                                 const cs_real_t                *div_op,
                                 const bool                      has_sourceterm,
                                 cs_cdofb_monolithic_t          *sc,
                                 cs_cdofb_vecteq_t              *eqc,
                                 cs_equation_assemble_t         *eqa);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Build a linear system for Stokes, Oseen or Navier-Stokes in the
 *         steady-state case. Specific case: GKB algorithm is used to solve
 *         the saddle-point system. In case of unsteady computation, indice n
 *         means the previous time step (one computes the new state at n+1) and
 *         state at n-1 is the previous state of the previous state.
 *
 * \param[in]      nsp          pointer to a \ref cs_navsto_param_t structure
 * \param[in]      vel_f_n      velocity face DoFs at time step n
 * \param[in]      vel_c_n      velocity cell DoFs at time step n
 * \param[in]      vel_f_nm1    velocity face DoFs at time step n-1 or NULL
 * \param[in]      vel_c_nm1    velocity cell DoFs at time step n-1 or NULL
 * \param[in]      dir_values   array storing the Dirichlet values
 * \param[in]      forced_ids   indirection in case of internal enforcement
 * \param[in, out] sc           pointer to the scheme context
 */
/*----------------------------------------------------------------------------*/

typedef void
(cs_cdofb_monolithic_build_t)(const cs_navsto_param_t      *nsp,
                              const cs_real_t               vel_f_n[],
                              const cs_real_t               vel_c_n[],
                              const cs_real_t               vel_f_nm1[],
                              const cs_real_t               vel_c_nm1[],
                              const cs_real_t              *dir_values,
                              const cs_lnum_t               forced_ids[],
                              cs_cdofb_monolithic_t        *sc);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Use the GKB algorithm to solve the saddle-point problem arising
 *         from CDO-Fb schemes for Stokes and Navier-Stokes with a monolithic
 *         coupling
 *
 * \param[in]      nsp      pointer to a cs_navsto_param_t structure
 * \param[in]      eqp      pointer to a cs_equation_param_t structure
 * \param[in, out] msles    pointer to a cs_cdofb_monolithic_sles_t structure
 *
 * \return the cumulated number of iterations of the solver
 */
/*----------------------------------------------------------------------------*/

typedef int
(cs_cdofb_monolithic_solve_t)(const cs_navsto_param_t       *nsp,
                              const cs_equation_param_t     *eqp,
                              cs_cdofb_monolithic_sles_t    *msles);

/*=============================================================================
 * Structure definitions
 *============================================================================*/

/*! \struct cs_cdofb_monolithic_t
 *  \brief Context related to CDO face-based discretization when dealing with
 *         Navier-Stokes equations and vector-valued face unknowns.
 *         Case of a monolithic approach (i.e fully coupled)
 */

struct _cdofb_monolithic_t {

  /*! \var coupling_context
   *  Pointer to a \ref cs_navsto_monolithic_t (owned by \ref
   *  cs_navsto_system_t) containing the settings related to the monolithic
   *  approach
   */

  cs_navsto_monolithic_t   *coupling_context;

  /*!
   * @name Main field variables
   * Fields for every main variable of the equation. Got from cs_navsto_system_t
   */

  /*! \var velocity
   *  Pointer to \ref cs_field_t (owned by \ref cs_navsto_system_t) containing
   *  the cell DoFs of the velocity
   */

  cs_field_t               *velocity;

  /*! \var pressure
   *  Pointer to \ref cs_field_t (owned by \ref cs_navsto_system_t) containing
   *  the cell DoFs of the pressure
   */

  cs_field_t               *pressure;

  /*! \var divergence
   *  Pointer to \ref cs_real_t containing the values of the divergence on the
   *  cells
   */

  cs_field_t               *divergence;

  /*!
   * @}
   * @name Advection quantities
   * Members related to the advection
   * @{
   *
   *  \var adv_field
   *  Pointer to the cs_adv_field_t related to the Navier-Stokes eqs (Shared)
   */
  cs_adv_field_t           *adv_field;

  /*! \var mass_flux_array
   *  Current values of the mass flux at primal faces (Shared)
   */
  cs_real_t                *mass_flux_array;

  /*! \var mass_flux_array_pre
   *  Previous values of the mass flux at primal faces (Shared)
   */
  cs_real_t                *mass_flux_array_pre;

  /*!
   * @}
   * @name Boundary conditions (BC) management
   * Functions and elements used for enforcing the BCs
   * @{
   *
   *  \var bf_type
   *  Array of boundary type for each boundary face. (Shared)
   */

  const cs_boundary_type_t       *bf_type;

  /*!
   * \var pressure_bc
   * Structure storing the metadata after processing the user-defined boundary
   * conditions related to the pressure field
   */

  cs_cdo_bc_face_t               *pressure_bc;
  int                             pressure_rescaling;

  /*! \var apply_fixed_wall
   *  \ref cs_cdo_apply_boundary_t function pointer defining how to apply a
   *  wall boundary (no slip boundary)
   *
   *  \var apply_sliding_wall
   *  \ref cs_cdo_apply_boundary_t function pointer defining how to apply a
   *  wall boundary (a tangential velocity is specified at the wall)
   *
   *  \var apply_velocity_inlet
   *  \ref cs_cdo_apply_boundary_t function pointer defining how to apply a
   *  boundary with a fixed velocity at the inlet
   *
   *  \var apply_symmetry
   *  \ref cs_cdo_apply_boundary_t function pointer defining how to apply a
   *  symmetry boundary
   */

  cs_cdo_apply_boundary_t        *apply_fixed_wall;
  cs_cdo_apply_boundary_t        *apply_sliding_wall;
  cs_cdo_apply_boundary_t        *apply_velocity_inlet;
  cs_cdo_apply_boundary_t        *apply_symmetry;

  /*!
   * @}
   * @name Build stage
   * Additional members which corresponds to function pointers
   * @{
   */

  cs_cdofb_monolithic_init_matrix_t   *init_system;
  cs_cdofb_monolithic_build_t         *steady_build;
  cs_cdofb_monolithic_build_t         *build;

  /*!
   * \var add_gravity_term
   * \ref Compute and add the source term related to the gravity vector
   *      This can be the Boussinesq term or the hydrostatic term (rho*g)
   */

  cs_cdofb_navsto_source_t           *add_gravity_term;

  /*!
   * @}
   * @name Assembly stage
   * Additional members which may be used to assemble the system
   * @{
   */

  /* \var assemble
   * Function pointer to manage the assembly process for the Navier-Stokes
   * system of equation
   */

  cs_cdofb_monolithic_assemble_t      *assemble;

  /* \var elemental_assembly
   * Function pointer to manage the assembly process at low-level
   */

  cs_equation_assembly_t              *elemental_assembly;

  /* \var mav_structures
   * Set of pointers to the matrix assembler structures for matrix values
   * Size = 1 or 9
   */

  cs_matrix_assembler_values_t       **mav_structures;

  /*!
   * @}
   * @name Solve stage
   * Additional members which may be used to solve the system
   * @{
   */

  cs_cdofb_monolithic_solve_t      *solve;

  /* \var msles
   * Set of pointers to enable the resolution of saddle-point system
   * with various algorithms. This structure allows us to unify the prototype
   * of "solve" functions
   * Some members of this structure are allocated only if a specific algorithm
   * is requested.
   */

  cs_cdofb_monolithic_sles_t       *msles;

  /*!
   * \var algo_info
   * Structure used to drive the convergence of high-level iterative algorithms
   */

  cs_iter_algo_t                   *algo_info;

  /*!
   * @}
   * @name Performance monitoring
   * Monitoring the efficiency of the algorithm used to solve the Navier-Stokes
   * system
   * @{
   */

  /*! \var timer
   *  Cumulated elapsed time for building and solving the Navier--Stokes system
   */
  cs_timer_counter_t  timer;

  /*! @} */

};

/*! (DOXYGEN_SHOULD_SKIP_THIS) \endcond */

/*============================================================================
 * Public function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/

END_C_DECLS

#endif /* __CS_CDOFB_MONOLITHIC_PRIV_H__ */
