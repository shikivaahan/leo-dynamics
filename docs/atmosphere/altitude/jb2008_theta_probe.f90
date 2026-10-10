! Project-owned scalar wrapper; THETA is extracted byte-for-byte out of tree.
program jb2008_theta_probe
  implicit none
  double precision, external :: THETA
  double precision :: d1950
  read (*, *) d1950
  write (*, '(es25.17e3)') THETA(d1950)
end program jb2008_theta_probe
