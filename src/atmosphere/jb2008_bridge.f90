! Project-owned ABI bridge; no JB2008 equations are copied here.
subroutine leo_jb2008(mjd, sun, sat, f10, f10b, s10, s10b, m10, m10b, &
                      y10, y10b, dtc, temp, rho) bind(C, name="leo_jb2008")
  use, intrinsic :: iso_c_binding, only: c_double
  implicit none
  real(c_double), intent(in) :: mjd, sun(2), sat(3)
  real(c_double), intent(in) :: f10, f10b, s10, s10b, m10, m10b, y10, y10b, dtc
  real(c_double), intent(out) :: temp(2), rho
  interface
    subroutine JB2008(amjd, sun, sat, f10, f10b, s10, s10b, m10, m10b, &
                       y10, y10b, dtc, temp, rho)
      import c_double
      real(c_double) :: amjd, sun(2), sat(3), f10, f10b, s10, s10b
      real(c_double) :: m10, m10b, y10, y10b, dtc, temp(2), rho
    end subroutine
  end interface
  call JB2008(mjd, sun, sat, f10, f10b, s10, s10b, m10, m10b, &
              y10, y10b, dtc, temp, rho)
end subroutine
