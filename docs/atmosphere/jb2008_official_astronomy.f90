! Project-owned helper around the *unmodified* official SUNPOS and THETA
! subroutines of the reference driver (JB08DRVAUTO.f).
!
! The published validation grid does not record the spacecraft right ascension
! or the local hour angle it was evaluated at; it records only the solar right
! ascension and declination, to 1e-4 rad, and a longitude.  The official driver
! formed the position right ascension itself as
!
!     SAT(1) = DMOD(THETA(D1950) + XLON*DG2RD + 2*PI, 2*PI)
!
! so reproducing the published numbers requires THETA and DG2RD exactly as the
! driver has them.  THETA is the driver's own sidereal expression, which is not
! the IAU 1982 formula, and DG2RD is built from a pi truncated to eight
! significant digits.  Both are load-bearing at the precision of the fixture.
!
! This program therefore evaluates the official subroutines directly instead of
! substituting a published standard.
!
! Input : one record per case, whitespace separated: D1950 XLON
! Output: one record per case, whitespace separated, 17 significant digits:
!           AMJD GWRAS SAT_RA SOLRAS SOLDEC
!
! AMJD   is the MJD the model receives, D1950 + 33281 exactly as the driver
!         builds it;
! GWRAS  is the driver's THETA(D1950), the Greenwich right ascension;
! SAT_RA is the position right ascension, the driver's own
!         DMOD(GWRAS + XLON*DG2RD + 2*PI, 2*PI) over the truncated 2*PI;
! SOLRAS and SOLDEC are the driver's SUNPOS(AMJD) results.
!
! The caller sets the latitude separately from the printed grid; it is not
! computed here because THETA and SUNPOS do not depend on it.
!
! It copies nothing from the official sources; SUNPOS and THETA are linked from
! the hash-pinned external driver object.

program jb2008_official_astronomy
  implicit none
  integer, parameter :: dp = kind(1.0d0)
  ! The driver's own parameter, verbatim: PARAMETER (PI = 3.1415927D0).
  real(dp), parameter :: pi = 3.1415927d0
  real(dp), parameter :: dg2rd = pi / 180.d0
  interface
    ! REAL*8 FUNCTION THETA(T1950) in the driver; declared here because a plain
    ! EXTERNAL declaration cannot give a function a return type.  Spelled with
    ! double precision rather than kind(...) so no host-association of the kind
    ! parameter is involved in the explicit interface.
    double precision function THETA(t1950)
      double precision, intent(in) :: t1950
    end function THETA
    subroutine SUNPOS(amjd, solras, soldec)
      double precision, intent(in) :: amjd
      double precision, intent(out) :: solras, soldec
    end subroutine SUNPOS
  end interface
  real(dp) :: amjd, d1950, xlon, solras, soldec, gwr, sat1
  real(dp) :: twopi
  integer :: ios

  twopi = 2.d0 * pi
  do
    read (*, *, iostat=ios) d1950, xlon
    if (ios /= 0) exit
    amjd = d1950 + 33281.d0
    call SUNPOS(amjd, solras, soldec)
    gwr = THETA(d1950)
    ! Exactly the driver's expression, including its DMOD over the truncated 2pi.
    sat1 = modulo(gwr + xlon * dg2rd + twopi, twopi)
    write (*, '(5es25.17e3)') amjd, gwr, sat1, solras, soldec
  end do

end program jb2008_official_astronomy