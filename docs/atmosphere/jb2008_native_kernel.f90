! Project-owned stdin/stdout kernel around the *unmodified* official JB2008
! Fortran model.  It exists so the official reference can be invoked directly,
! with exactly the native input variables, without the C++ bridge, the
! coordinate layer or the weather loader in the path.
!
! Nothing of the official model is copied here: the subroutine is linked from
! the hash-pinned external JB2008.f that the build already requires.  This file
! only moves doubles across stdin/stdout.
!
! Input  : one record per case, whitespace separated, in native JB2008 order
!            AMJD  SUN(1) SUN(2)  SAT(1) SAT(2) SAT(3)
!            F10 F10B S10 S10B XM10 XM10B Y10 Y10B DSTDTC
!          terminated by EOF.
! Output : one record per case, whitespace separated,
!            RHO TEMP(1) TEMP(2)
!          TEMP(1) is the exospheric temperature, TEMP(2) the local one, as in
!          the official driver's own TEMP argument.  Values are emitted with 17
!          significant digits so the probe itself adds no rounding.
!
! The first record is a single mandatory integer selecting the output style:
!   0 = full double precision, 17 significant digits (the measurement style)
!   1 = the official driver's own printed widths (FORMAT 104), which is what the
!       published grid actually holds
! Callers must send it; it is read with list-directed input, so it must be the
! whole first record. Every later record is one case in native JB2008 order.

program jb2008_native_kernel
  implicit none
  ! The linked official model is fixed-form with IMPLICIT REAL*8(A-H,O-Z) and
  ! IMPLICIT INTEGER*4(I-N).  Every dummy it uses for these inputs is a double
  ! precision scalar or array element, and no COMMON block is shared with this
  ! program, so a plain external reference with real(dp) arguments matches its
  ! ABI.  Declared here rather than in an interface block so the reference stays
  ! external.
  external JB2008
  integer, parameter :: dp = kind(1.0d0)
  real(dp) :: amjd, sun(2), sat(3)
  real(dp) :: f10, f10b, s10, s10b, xm10, xm10b, y10, y10b, dstdtc
  real(dp) :: temp(2), rho
  integer :: style, ios

  read (*, *, iostat=ios) style
  if (ios /= 0) then
    write (*, '(a)') 'jb2008_native_kernel: first record must be the style '// &
                    'integer (0 = 17 digits, 1 = official printed widths)'
    stop 2
  end if

  do
    read (*, *, iostat=ios) amjd, sun(1), sun(2), sat(1), sat(2), sat(3), &
                             f10, f10b, s10, s10b, xm10, xm10b, y10, y10b, dstdtc
    if (ios /= 0) exit
    call JB2008(amjd, sun, sat, f10, f10b, s10, s10b, xm10, xm10b, &
                y10, y10b, dstdtc, temp, rho)
    if (style == 1) then
      ! The precision the published grid actually carries: D15.5 density and
      ! F8.1 temperatures, i.e. FORMAT 104 of JB08DRVAUTO.f for the three
      ! result columns.  The Z/LON/LAT columns are geometry the caller already
      ! knows and are not echoed.
      write (*, '(d15.5,2f8.1)') rho, temp(1), temp(2)
    else
      write (*, '(3es25.17e3)') rho, temp(1), temp(2)
    end if
  end do

end program jb2008_native_kernel