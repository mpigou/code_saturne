#ifndef __CS_CTWR_PHYSPROP_H__
#define __CS_CTWR_PHYSPROP_H__

/*============================================================================
 * Cooling towers related functions
 *============================================================================*/

/*
  This file is part of code_saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2024 EDF S.A.

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

/*----------------------------------------------------------------------------
 *  Local headers
 *----------------------------------------------------------------------------*/

#include "fvm_nodal.h"

#include "cs_base.h"
#include "cs_halo.h"
#include "cs_mesh.h"
#include "cs_mesh_quantities.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*----------------------------------------------------------------------------*/
/*!
 * \brief Update the thermo physical properties fields for the humid air and
 *        the liquid
 *
 * \param[in]     rho0        Reference density of humid air
 * \param[in]     t0          Reference temperature of humid air
 * \param[in]     p0          Reference pressure
 */
/*----------------------------------------------------------------------------*/

void
cs_ctwr_phyvar_update(cs_real_t  rho0,
                      cs_real_t  t0,
                      cs_real_t  p0);

/*----------------------------------------------------------------------------*/
/*!
 * \brief Reset the field variables based on the restart values
 *
 * \param[in]     rho0        Reference density of humid air
 * \param[in]     t0          Reference temperature of humid air
 * \param[in]     p0          Reference pressure
 * \param[in]     humidity0   Reference humidity
 * \param[in]     molmassrat  Dry air to water vapor molecular mass ratio
 */
/*----------------------------------------------------------------------------*/

void
cs_ctwr_restart_field_vars(cs_real_t  rho0,
                           cs_real_t  t0,
                           cs_real_t  p0,
                           cs_real_t  humidity0,
                           cs_real_t  molmassrat);


/*----------------------------------------------------------------------------*/

END_C_DECLS

#endif /* __CS_CTWR_PHYSPROP_H__ */
