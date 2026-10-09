#include "leo/kernel/radiation.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
int main(){double re,rs,d;while(std::cin>>re>>rs>>d){leo::InertialVector r{-7e6,0,0};const double S=149597870700.;leo::InertialVector sun=r+leo::InertialVector{S*std::cos(d),S*std::sin(d),0};std::cout<<std::setprecision(17)<<re<<' '<<rs<<' '<<sun.x()<<' '<<sun.y()<<' '<<leo::illumination_factor(leo::EclipseModel::conical,r,sun,re,rs)<<'\n';}}
