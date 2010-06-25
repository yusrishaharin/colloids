/**
    Copyright 2008,2009 Mathieu Leocmach

    This file is part of Colloids.

    Colloids is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Colloids is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Colloids.  If not, see <http://www.gnu.org/licenses/>.

 * \file boo.hpp
 * \brief Defines class for bond orientational order data
 * \author Mathieu Leocmach
 * \date 13 February 2009
 *
 *
 */
#ifndef boo_H
#define boo_H

#include "index.hpp"

#include <valarray>
#include <complex>
#include <string>
#include <boost/array.hpp>
//#include <tvmet/Vector.h>

namespace Colloids
{
    //typedef tvmet::Vector<double, 3>            Coord;

    /** \brief Bond-Orientational-Order data
     *  Coordinates qlm of the local symmetry on the pair spherical harmonics base Ylm(theta,phi)
     *   0 <= l <=10 (pair)
     *  -l <= m <=l
     *  conjugate of Ylm is (-1)^m * Yl(-m) so only positive m coefficients are kept in memory
    */
    class BooData : public std::valarray< std::complex<double> >
    {
        static size_t w3j_l_offset[6],w3j_m1_offset[11];
        public:
            /** the non redundant wigner 3j coefficients for l=0,2,4,6,8,10 */
            static double w3j[91];
            static double &getW3j(const size_t &l, const int &m1, const int &m2);
            static size_t i2l[36], i2m[36];

            /** \brief default constructor */
            BooData() : std::valarray< std::complex <double> >(std::complex <double>(0.0,0.0),36){return;};
            explicit BooData(const Coord &rij);
            explicit BooData(const std::string &str);
            explicit BooData(const double* buff);

            /** \brief access to members */
            //std::complex<double> &operator()(const size_t &l, const size_t &m){return (*this)[m + l*l/4];};
            const std::complex<double> operator()(const size_t &l, const int &m) const;
            double getSumNorm(const size_t &l) const;
            std::valarray<std::complex<double> > getL(const size_t &l) const
            {return std::valarray<std::complex<double> >::operator[](std::slice(l*l/4,l+1,1));}

            double getQl(const size_t &l) const;
            std::complex<double> getWl(const size_t &l) const;
            void getInvarients(const size_t &l, double &Q, std::complex<double> &W) const;
            void getInvarients(const size_t &l, double &Q, double &w) const
            {
                std::complex<double> W(0.0,0.0);
                getInvarients(l,Q,W);
                w=W.real();
            }

			BooData rotate_by_Pi(const Coord &axis) const;

            std::string toString() const;
            char* toBinary(double *output) const;
    } ;

    std::ostream& operator<< (std::ostream& out, const BooData &boo );
    std::istream& operator>> (std::istream& in, BooData &boo );

    struct cloud_exporter : public std::unary_function<const BooData&, std::string>
	{
		std::string operator()(const BooData &boo)
		{
			boost::array<double, 8> qw;
			boo.getInvarients(4, qw[0], qw[4]);
			boo.getInvarients(6, qw[1], qw[5]);
			boo.getInvarients(8, qw[2], qw[6]);
			boo.getInvarients(10, qw[3], qw[7]);
			std::ostringstream os;
			std::copy(qw.begin(), qw.end(), std::ostream_iterator<double>(os, "\t"));
			return os.str();
		}
	};
};
#endif
