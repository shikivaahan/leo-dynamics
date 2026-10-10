! Project-owned probe driver for the JB2008 altitude-datum investigation.
!
! It contains no JB2008 physics. The unmodified upstream `JB2008.f` is compiled
! and linked against this file; the only thing here is the calling convention
! and a line-oriented stdin/stdout loop so that the external reference can be
! driven from Python without a compiled project library in the loop.
!
! One request line per evaluation, whitespace separated:
!   amjd sun_ra sun_dec sat_ra sat_lat sat_alt
!   f10 f10b s10 s10b m10 m10b y10 y10b dstdtc
! One response line per request, in `ES25.16` fields:
!   rho  temp_exobase  temp_local
!
! `sat_lat` and `sat_alt` are handed to the upstream subroutine untouched: the
! whole point of the experiment is that the caller decides the datum, and the
! reference must not silently re-interpret either value.

program jb2008_altitude_probe
  implicit none
  integer :: ios
  real(8) :: amjd
  real(8) :: sun(2), sat(3)
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

  do
    read (*, *, iostat=ios) amjd, sun, sat, f10, f10b, s10, s10b, xm10, &
         xm10b, y10, y10b, dtc
    if (ios /= 0) exit
    temp = 0.0d0
    rho = 0.0d0
    call jb2008(amjd, sun, sat, f10, f10b, s10, s10b, xm10, xm10b, y10, &
                y10b, dtc, temp, rho)
    write (*, '(3(1x,es25.16))') rho, temp(1), temp(2)
  end do
end program jb2008_altitude_probe