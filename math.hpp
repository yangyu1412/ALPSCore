/*****************************************************************************
*
* ALPS Project: Algorithms and Libraries for Physics Simulations
*
* ALPS Libraries
*
* Copyright (C) 1999-2003 by Matthias Troyer <troyer@itp.phys.ethz.ch>,
*                            Synge Todo <wistaria@comp-phys.org>
*
* This software is part of the ALPS libraries, published under the ALPS
* Library License; you can use, redistribute it and/or modify it under
* the terms of the license, either version 1 or (at your option) any later
* version.
* 
* You should have received a copy of the ALPS Library License along with
* the ALPS Libraries; see the file LICENSE.txt. If not, the license is also
* available from http://alps.comp-phys.org/.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
* FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT 
* SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE 
* FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE, 
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
* DEALINGS IN THE SOFTWARE.
*
*****************************************************************************/

/* $Id$ */

#ifndef ALPS_MATH_HPP
#define ALPS_MATH_HPP

#include <alps/config.h>
#include <alps/typetraits.h>

#include <algorithm>
#include <complex>
#include <cmath>
#include <cstddef>

namespace alps {

inline std::size_t binomial(std::size_t l, std::size_t n)
{
  double nominator=1;
  double denominator=1;
  std::size_t n2=std::max(n,l-n);
  std::size_t n1=std::min(n,l-n);
  for (std::size_t i=n2+1;i<=l;i++)
    nominator*=i;
  for (std::size_t i=2;i<=n1;i++)
    denominator*=i;
  return std::size_t(nominator/denominator+0.1);
}

template <class T>
inline typename TypeTraits<T>::norm_t abs2(T x) {
  return std::abs(x)*std::abs(x);        
}

template <class T>
inline T abs2(const std::complex<T>& x) {
  return x.real()*x.real()+x.imag()*x.imag();
}

template<class T>
bool is_zero(T x) { return x == T(0); }

template<class T>
bool is_nonzero(T x) { return !is_zero(x); }

} // end namespace

#endif // ALPS_MATH_HPP
