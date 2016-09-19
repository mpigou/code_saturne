!-------------------------------------------------------------------------------

! This file is part of Code_Saturne, a general-purpose CFD tool.
!
! Copyright (C) 1998-2016 EDF S.A.
!
! This program is free software; you can redistribute it and/or modify it under
! the terms of the GNU General Public License as published by the Free Software
! Foundation; either version 2 of the License, or (at your option) any later
! version.
!
! This program is distributed in the hope that it will be useful, but WITHOUT
! ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
! FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
! details.
!
! You should have received a copy of the GNU General Public License along with
! this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
! Street, Fifth Floor, Boston, MA 02110-1301, USA.

!-------------------------------------------------------------------------------

!> \file dvvpst.f90
!> \brief Standard output of variables on post-processing meshes
!> (called after \ref cs_user_extra_operations).
!>
!------------------------------------------------------------------------------

!------------------------------------------------------------------------------
! Arguments
!------------------------------------------------------------------------------
!   mode          name          role
!------------------------------------------------------------------------------
!> \param[in]     nummai        post-processing mesh number
!> \param[in]     numtyp        post-processing type number
!>                               - -1: volume
!>                               - -2: edge
!>                               - default: nummai
!> \param[in]     nvar          total number of variables
!> \param[in]     ncelps        post-processing mesh cells number
!> \param[in]     nfbrps        number of boundary faces
!> \param[in]     lstcel        post-processing mesh cell numbers
!> \param[in]     lstfbr        post-processing mesh boundary faces numbers
!> \param[in,out] tracel        post processing cell real values
!> \param[in,out] trafbr        post processing boundary faces real values
!______________________________________________________________________________

subroutine dvvpst &
 ( nummai , numtyp ,                                              &
   nvar   ,                                                       &
   ncelps , nfbrps ,                                              &
   lstcel , lstfbr ,                                              &
   tracel , trafbr )

!===============================================================================
! Module files
!===============================================================================

use paramx
use pointe
use entsor
use cstnum
use cstphy
use optcal
use numvar
use parall
use period
use lagran
use ppppar
use ppthch
use ppincl
use radiat, only: iirayo
use cplsat
use mesh
use field
use field_operator
use post
use cs_f_interfaces
use rotation
use turbomachinery

!===============================================================================

implicit none

! Arguments

integer          nummai , numtyp
integer          nvar
integer          ncelps , nfbrps

integer          lstcel(ncelps), lstfbr(nfbrps)

double precision tracel(ncelps*3)
double precision trafbr(nfbrps*3)

! Local variables

character(len=80) :: name80

logical          ientla, ivarpr
integer          inc   , iccocg
integer          ifac  , iloc  , ivar
integer          ipp   , idimt , kk   , ll, iel
integer          fldid, fldprv, keycpl, iflcpl
integer          ifcsii, iflpst, itplus, iprev, f_id

double precision rbid(1)
double precision vr(3)

double precision, allocatable, dimension(:,:) :: grad
double precision, allocatable, dimension(:,:,:) :: gradv
double precision, dimension(:), pointer :: tplusp
double precision, dimension(:), pointer :: valsp, coefap, coefbp
double precision, dimension(:,:), pointer :: valvp, cofavp, cofbvp
double precision, dimension(:,:,:), pointer :: cofbtp
double precision, dimension(:), pointer :: crom
double precision, dimension(:,:), pointer :: vel
double precision, dimension(:), pointer :: cpotr, cpoti, cvisii
double precision, dimension(:), pointer :: cvar_pr

!===============================================================================

! Initialize variables to avoid compiler warnings

ipp = 0

!===============================================================================
! 1.1. Fluid domain
!===============================================================================

if (numtyp .eq. -1) then

  ! Map field arrays
  call field_get_val_s(ivarfl(ipr), cvar_pr)
  call field_get_val_v(ivarfl(iu), vel)

  !  1.1.2 Automatic additional variables
  !  ------------------------------------

  ! Wall distance (if LES+VanDriest or Rij+Echo or K-w SST)

  if (ineedy.eq.1) then

    idimt = 1
    ientla = .true.
    ivarpr = .true.

    call post_write_var(nummai, 'DistWall', idimt, ientla, ivarpr,  &
                        ntcabs, ttcabs, dispar, rbid, rbid)

  endif

  ! Yplus (if LES+VanDriest)

  if (ineedy.eq.1) then

    if (itytur.eq.4.and.idries.eq.1) then

      idimt = 1
      ientla = .true.
      ivarpr = .true.

      call post_write_var(nummai, 'Yplus', idimt, ientla, ivarpr,  &
                          ntcabs, ttcabs, yplpar, rbid, rbid)

    endif

  endif

  ! Vitesse et pression absolues en cas de calcul en rep�re relatif

  if (icorio.eq.1) then

    call field_get_val_s(icrom, crom)

    idimt = 1
    ientla = .true.
    ivarpr = .false.

    do iloc = 1, ncelps

      iel = lstcel(iloc)
      call rotation_velocity(1, xyzcen(:,iel), vr)

      tracel(iloc) = cvar_pr(iel) + &
             0.5d0*crom(iel)*(vr(1)**2 + vr(2)**2 + vr(3)**2)

    enddo

    call post_write_var(nummai, 'Pressure', idimt, ientla, ivarpr,  &
                        ntcabs, ttcabs, tracel, rbid, rbid)

    idimt = 3
    ientla = .true.
    ivarpr = .false.

    do iloc = 1, ncelps

      iel = lstcel(iloc)
      call rotation_velocity(1, xyzcen(:,iel), vr)

      tracel(1 + (iloc-1)*idimt) = vel(1,iel) + vr(1)
      tracel(2 + (iloc-1)*idimt) = vel(2,iel) + vr(2)
      tracel(3 + (iloc-1)*idimt) = vel(3,iel) + vr(3)

    enddo

    call post_write_var(nummai, 'Velocity', idimt, ientla, ivarpr,  &
                        ntcabs, ttcabs, tracel, rbid, rbid)

  endif

  ! Vitesse et pression relatives en cas de calcul en rep�re fixe

  if (imobil.eq.1 .or. iturbo.eq.1 .or. iturbo.eq.2) then

    call field_get_val_s(icrom, crom)

    idimt = 1
    ientla = .true.
    ivarpr = .false.

    do iloc = 1, ncelps

      iel = lstcel(iloc)
      call rotation_velocity(irotce(iel), xyzcen(:,iel), vr)

      tracel(iloc) =   cvar_pr(iel) &
                     - crom(iel)*0.5d0*(vr(1)**2 + vr(2)**2 + vr(3)**2)

    enddo

    call post_write_var(nummai, 'Rel Pressure', idimt, ientla, ivarpr,  &
                        ntcabs, ttcabs, tracel, rbid, rbid)

    idimt = 3
    ientla = .true.
    ivarpr = .false.

    do iloc = 1, ncelps

      iel = lstcel(iloc)
      call rotation_velocity(irotce(iel), xyzcen(:,iel), vr)

      tracel(1 + (iloc-1)*idimt) = vel(1,iel) - vr(1)
      tracel(2 + (iloc-1)*idimt) = vel(2,iel) - vr(2)
      tracel(3 + (iloc-1)*idimt) = vel(3,iel) - vr(3)

    enddo

    call post_write_var(nummai, 'Rel Velocity', idimt, ientla, ivarpr,  &
                        ntcabs, ttcabs, tracel, rbid, rbid)

  endif


!===============================================================================
! 1.2. Boundary
!===============================================================================

else if (numtyp .eq. -2) then

  !  Projection of variables at boundary with no reconstruction
  !  ----------------------------------------------------------

  call field_get_key_id('coupled', keycpl)

  fldprv = -1

  do ivar = 1, nvar  ! Loop on main cell-based variables

    fldid = ivarfl(ivar)

    if (fldid .eq. fldprv) cycle ! already output for multiple components

    fldprv = fldid

    call field_get_key_int(fldid, keyvis, iflpst)

    if (iand(iflpst, POST_BOUNDARY_NR) .eq. 0) cycle ! nothing to do

    call field_get_dim (fldid, idimt)
    call field_get_name(fldid, name80(4:80))
    name80(1:3) = 'bc_'

    !  Compute non-reconstructed values at boundary faces

    if (idimt.ne.1) then
      call field_get_key_int(fldid, keycpl, iflcpl)
    else
      iflcpl = 0
    endif

    if (idimt.eq.1) then  ! Scalar

      call field_get_val_s(fldid, valsp)
      call field_get_coefa_s(fldid, coefap)
      call field_get_coefb_s(fldid, coefbp)

      do iloc = 1, nfbrps

        ifac = lstfbr(iloc)
        iel = ifabor(ifac)

        trafbr(iloc) =   coefap(ifac) + coefbp(ifac)*valsp(iel)

      enddo

    else if (iflcpl.eq.0) then  ! Uncoupled vector or tensor

      call field_get_val_v(fldid, valvp)
      call field_get_coefa_v(fldid, cofavp)
      call field_get_coefb_uv(fldid, cofbvp)

      do kk = 0, idimt-1

        do iloc = 1, nfbrps

          ifac = lstfbr(iloc)
          iel = ifabor(ifac)

          trafbr(kk + (iloc-1)*idimt + 1)                      &
               =   cofavp(kk+1,ifac)                           &
                 + cofbvp(kk+1,ifac)*valvp(kk+1,iel)

        enddo

      enddo

    else ! Coupled vector or tensor

      call field_get_val_v(fldid, valvp)
      call field_get_coefa_v(fldid, cofavp)
      call field_get_coefb_v(fldid, cofbtp)

      do kk = 0, idimt-1

        do iloc = 1, nfbrps

          ifac = lstfbr(iloc)
          iel = ifabor(ifac)

          trafbr(kk + (iloc-1)*idimt + 1) = cofavp(kk+1,ifac)

          do ll = 1, idimt
            trafbr(kk + (iloc-1)*idimt + 1)                    &
               =   trafbr(kk + (iloc-1)*idimt + 1)             &
                 + cofbtp(kk+1,ll,ifac)*valvp(ll,iel)
          enddo

        enddo

      enddo

    endif ! test on field dimension and interleaving

    ientla = .true.  ! interleaved result values
    ivarpr = .false. ! defined on work array

    call post_write_var(nummai, trim(name80), idimt, ientla, ivarpr,  &
                        ntcabs, ttcabs, rbid, rbid, trafbr)

  enddo ! End of loop on variables

  ! Handle stresses at boundary
  ! ---------------------------

  if (iand(ipstdv(ipstfo), 1) .ne. 0) then

    ! Compute variable values on boundary faces

    call post_stress(nfbrps, lstfbr, trafbr)

    idimt = 3        ! variable dimension
    ientla = .true.  ! interleaved values
    ivarpr = .false. ! defined on work array

    call post_write_var(nummai, 'Stress', idimt, ientla, ivarpr,  &
                        ntcabs, ttcabs, rbid, rbid, trafbr)

  endif

  if (iand(ipstdv(ipstfo), 2) .ne. 0) then

    ! Compute variable values on boundary faces

    call post_stress_tangential(nfbrps, lstfbr, trafbr)

    idimt = 3        ! variable dimension
    ientla = .true.  ! interleaved values
    ivarpr = .false. ! defined on work array

    call post_write_var(nummai, 'Shear Stress', idimt, ientla, ivarpr,  &
                        ntcabs, ttcabs, rbid, rbid, trafbr)

  endif

  if (iand(ipstdv(ipstfo), 4) .ne. 0) then

    ! Calcul des valeurs de la variable sur les faces de bord

    call post_stress_normal(nfbrps, lstfbr, trafbr)

    idimt = 1        ! variable dimension
    ientla = .true.  ! interleaved values
    ivarpr = .false. ! defined on work array

    call post_write_var(nummai, 'Normal Stress', idimt, ientla, ivarpr,  &
                        ntcabs, ttcabs, rbid, rbid, trafbr)

  endif

  ! T+ near the boundary
  ! --------------------

  if (ipstdv(ipsttp).ne.0) then

    call field_get_id_try('tplus', itplus)

    if (itplus.ge.0) then

      call field_get_val_s(itplus, tplusp)

      idimt = 1        ! variable dimension
      ientla = .true.  ! interleaved values
      ivarpr = .true.  ! defined on parent array

      if (itherm .eq. 1) then
        name80 = 'Tplus'
      else if (itherm .eq. 2) then
        name80 = 'Hplus'
      else if (itherm .eq. 3) then
        name80 = 'Eplus'
      else
        return
      endif

      call post_write_var(nummai, name80, idimt, ientla, ivarpr,  &
                          ntcabs, ttcabs, rbid, rbid, tplusp)

    endif ! end of test on presence ot T+

  endif ! end of test on output of y+

  ! Thermal flux at boundary
  ! ------------------------
  !  If working with enthalpy, compute an enthalpy flux

  if (ipstdv(ipstft).ne.0) then

    if (iscalt.gt.0) then

      call post_boundary_thermal_flux(nfbrps, lstfbr, trafbr)

      idimt = 1        ! variable dimension
      ientla = .true.  ! interleaved values
      ivarpr = .false. ! defined on work array

      call post_write_var(nummai, 'Input thermal flux', idimt, ientla, ivarpr,  &
                          ntcabs, ttcabs, rbid, rbid, trafbr)

    endif

  endif

  ! Nusselt at the boundary
  ! -----------------------

  if (ipstdv(ipstnu).ne.0) then

    idimt = 1        ! variable dimension
    ientla = .true.  ! interleaved values
    ivarpr = .false. ! defined on work array

    ! Compute variable on boundary faces

    call post_boundary_nusselt(nfbrps, lstfbr, trafbr)

    call post_write_var(nummai, 'Dimensionless heat flux', idimt, ientla, ivarpr,  &
                        ntcabs, ttcabs, rbid, rbid, trafbr)

  endif ! end of test on output of Nusselt

endif ! end of test on postprocessing mesh number

!===============================================================================
!     2.2. VARIABLES RADIATIVES AUX FRONTIERES
!===============================================================================

if (numtyp.eq.-2) then

  if (iirayo.gt.0) then

    do iloc = 1, nfbrps
      ifac = lstfbr(iloc)
      trafbr(iloc) = izfrad(ifac)
    enddo

    idimt  = 1
    ientla = .true.
    ivarpr = .false.

    call post_write_var(nummai, 'radiative_boundary_zones', idimt,           &
                        ientla, ivarpr, ntcabs, ttcabs, rbid, rbid, trafbr)

  endif
endif

!===============================================================================
! 2.3. Electric module variables
!===============================================================================

if (numtyp.eq.-1) then

  if (     ippmod(ieljou).ge.1                                      &
      .or. ippmod(ielarc).ge.1) then

    allocate(grad(3,ncelet))

    if (.true.) then

      ! Gradient of the real potential

      call field_get_id('elec_pot_r', f_id)

      inc = 1
      iprev = 0
      iccocg = 1

      call field_gradient_scalar(f_id, iprev, imrgra, inc,                   &
                                 iccocg,                                     &
                                 grad)
      idimt  = 3
      ientla = .true.
      ivarpr = .true.

      call post_write_var(nummai, 'Pot_Gradient_R', idimt, ientla, ivarpr,   &
                          ntcabs, ttcabs, grad, rbid, rbid)

    endif

    ! For Joule Heating by direct conduction:
    !   gradient of the imaginary component of the potential

    if (.true.                                                               &
        .and. (ippmod(ieljou).eq.2 .or. ippmod(ieljou).eq.4)) then

      call field_get_id('elec_pot_i', f_id)

      inc = 1
      iprev = 0
      iccocg = 1

      call field_gradient_scalar(f_id, iprev, imrgra, inc,                   &
                                 iccocg,                                     &
                                 grad)

      idimt  = 3
      ientla = .true.
      ivarpr = .true.

      call post_write_var(nummai, 'Pot_Gradient_Im', idimt, ientla, ivarpr,  &
                          ntcabs, ttcabs, grad, rbid, rbid)

    endif

    ! For Joule heating by direct conduction:
    !   imaginary component of the current density

    if (.true.                                                               &
        .and. (ippmod(ieljou).eq.2 .or. ippmod(ieljou).eq.4)) then

      call field_get_id('elec_pot_i', f_id)

      ! As in elflux

      call field_get_key_int (f_id, kivisl, ifcsii)
      if (ifcsii .ge. 0) then
        call field_get_val_s(ifcsii, cvisii)
      endif

      inc = 1
      iprev = 0
      iccocg = 1

      call field_gradient_scalar(f_id, iprev, imrgra, inc,                   &
                                 iccocg,                                     &
                                 grad)

      do iloc = 1, ncelps
        iel = lstcel(iloc)
        tracel(1 + (iloc-1)*idimt) = -cvisii(iel)*grad(1,iel)
        tracel(2 + (iloc-1)*idimt) = -cvisii(iel)*grad(2,iel)
        tracel(3 + (iloc-1)*idimt) = -cvisii(iel)*grad(3,iel)
      enddo

      idimt  = 3
      ientla = .true.
      ivarpr = .false.

      call post_write_var(nummai, 'Current_Im', idimt, ientla, ivarpr,       &
                          ntcabs, ttcabs, grad, rbid, rbid)

    endif

    ! For electric arcs: electromagnetic field calculation

    if (.true. .and. ippmod(ielarc).ge.2) then

      call field_get_id('vec_potential', f_id)

      inc = 1
      iprev = 0

      allocate(gradv(3,3,ncelet))

      call field_gradient_vector(f_id, iprev, imrgra, inc, gradv)

      idimt  = 3

      ! B = rot A ( B = curl A)

      do iloc = 1, ncelps
        iel = lstcel(iloc)
        tracel(1 + (iloc-1)*idimt) = -gradv(3,2,iel)+gradv(2,3,iel)
        tracel(2 + (iloc-1)*idimt) =  gradv(3,1,iel)-gradv(1,3,iel)
        tracel(3 + (iloc-1)*idimt) = -gradv(2,1,iel)+gradv(1,2,iel)
      enddo

      deallocate(gradv)

      ientla = .true.
      ivarpr = .false.

      call post_write_var(nummai, 'Magnetic_field', idimt, ientla, ivarpr,  &
                          ntcabs, ttcabs, tracel, rbid, rbid)

    endif

    ! Calculation of Module and Argument of the complex potential if IELJOU = 4

    if (.true. .and. ippmod(ieljou).eq.4) then

      ivar = 0

      call field_get_val_s_by_name('elec_pot_r', cpotr)
      call field_get_val_s_by_name('elec_pot_i', cpoti)

      do iloc = 1, ncelps
        iel = lstcel(iloc)
        tracel(iloc) = sqrt(cpotr(iel)*cpotr(iel) + cpoti(iel)*cpoti(iel))
      enddo

      idimt  = 1
      ientla = .true.
      ivarpr = .false.

      call post_write_var(nummai, 'Pot_Module', idimt, ientla, ivarpr,  &
                          ntcabs, ttcabs, tracel, rbid, rbid)

      do iloc = 1, ncelps

        iel = lstcel(iloc)

        if (cpotr(iel) .ne. 0.d0) then
          if (cpotr(iel) .ge. 0.d0) then
            tracel(iloc) = atan(cpoti(iel)/cpotr(iel))
          else
            if (cpoti(iel) .gt. 0.d0) then
              tracel(iloc) = 4.d0*atan(1.d0)                      &
                             + atan(cpoti(iel) / cpotr(iel))
            else
              tracel(iloc) = -4.d0*atan(1.d0)                     &
                             + atan(cpoti(iel) / cpotr(iel))
            endif
          endif
        else
          tracel(iloc) = 2.d0*atan(1.d0)
        endif

        if (tracel(iloc) .lt. 0.d0) then
          tracel(iloc) = tracel(iloc) + 8.d0**atan(1.d0)
        endif

      enddo

      idimt  = 1
      ientla = .true.
      ivarpr = .false.

      call post_write_var(nummai, 'Pot_Arg', idimt, ientla, ivarpr,  &
                          ntcabs, ttcabs, tracel, rbid, rbid)

    endif

    ! Free memory
    deallocate(grad)

  endif

endif

!--------
! Formats
!--------

!----
! End
!----

return
end subroutine
