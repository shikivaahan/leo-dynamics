! Project-owned check that drives the unmodified upstream JB2008 with the exact
! quantity the vendor driver feeds it, so that the probe harness used by the
! altitude-datum experiment can be shown to reproduce the vendor's own
! published validation output before any datum question is opened.
!
! `THETA` is the upstream Greenwich right-ascension function from the vendor
! driver `JB08DRVAUTO.f`. It is not stored here: `altitude_datum_experiment.py`
! copies the upstream block verbatim into the build directory, so no upstream
! code is redistributed by this repository.
!
! Input line:
!   amjd d1950 sun_ra sun_dec lon_deg lat_deg alt_km
!   f10 f10b s10 s10b m10 m10b y10 y10b dstdtc
! Output line: rho t_exobase t_local (3x ES25.16)

program jb2008_official_grid_check
  implicit none
  real(8), parameter :: PI = 3.1415927D0
  real(8), parameter :: DG2RD = PI / 180.D0
  integer :: ios
  real(8) :: amjd, d1950
  real(8) :: sun(2), sat(3)
  real(8) :: lon_deg, lat_deg, alt_km
  real(8) :: f10, f10b, s10, s10b, xm10, xm10b, y10, y10b, dtc
  real(8) :: temp(2), rho

  interface
    subroutine jb2008(amjd, sun, sat, f10, f10b, s10, s10b, xm10, xm10b, &
                      y10, y10b, dtc, temp, rho)
      implicit none
      real(8) :: amjd, sun(2), sat(3)
      real(8) :: f10, f10b, s10, s10b, xm10, xm10b, y10, y10b, dtc
      real(8) :: temp(2), rho
    end subroutine jb2008
  end interface

  ! Upstream, provided verbatim at build time by the experiment driver.
  interface
    real(8) function theta(t1950)
      implicit none
      real(8) :: t1950
    end function theta
  end interface

  do
    read (*, *, iostat=ios) amjd, d1950, sun, lon_deg, lat_deg, alt_km, &
         f10, f10b, s10, s10b, xm10, xm10b, y10, y10b, dtc
    if (ios /= 0) exit
    temp = 0.0d0
    rho = 0.0d0
    sat(1) = dmod(theta(d1950) + lon_deg * DG2RD + 2.D0 * PI, 2.D0 * PI)
    sat(2) = lat_deg * DG2RD
    sat(3) = alt_km
    call jb2008(amjd, sun, sat, f10, f10b, s10, s10b, xm10, xm10b, y10, &
                y10b, dtc, temp, rho)
    write (*, '(3(1x,es25.16))') rho, temp(1), temp(2)
  end do
end program jb2008_official_grid_check