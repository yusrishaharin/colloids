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
**/

#include "particles.hpp"
//#include <boost/progress.hpp>

using namespace std;
using namespace Colloids;
//using namespace tvmet;



/**    \brief empty list constructor */
Particles::Particles(const size_t &n, const double &d, const double &r) : vector<Coord>(n,Coord(d,3)){radius=r;}

/**    \brief constructor from DAT file */
Particles::Particles(const string &filename, const double &r) : vector<Coord>(0,Coord(0.0,3))
{
    radius = r;
    size_t listSize=0, trash;
    string line;

	ifstream file(filename.c_str(), ios::in);
    if(!file)
        throw invalid_argument("No such file as "+filename);

    //Header
	file >> trash >> listSize >> trash;
	this->assign(listSize, Coord(0.0,3));

    for(size_t i=0;i<3;++i)
	{
        bb.edges[i].first  = 0.0;
        file >> bb.edges[i].second;
	}

    //Data
    for(size_t i=0;i<listSize;++i)
        file >> at(i)[0] >> at(i)[1] >> at(i)[2];

    file.close();
    //cout <<"done"<< endl;
    return;
}

/**    \brief constructor from GRV file */
Particles::Particles(const size_t &Nb, const BoundingBox &b, const string &filename, const double &r) : vector<Coord>(0,Coord(0.0,3))
{
	radius=r;
	ifstream file(filename.c_str(), ios::in);
    if(!file)
        throw invalid_argument("No such file as "+filename);

	this->assign(Nb, Coord(0.0,3));

    bb=b;

    //Data
    for(iterator p=begin();p!=end();++p)
        file >> (*p)[0] >> (*p)[1] >> (*p)[2];

    file.close();
    return;
}

void Particles::push_back(const Coord &p)
{
    if(hasIndex())
        index->insert(size(),bounds(p));
    vector<Coord>::push_back(p);
}

/** @brief return a copy with no particle closer than sep.
    First in first served
    The copy is indexed by a R*Tree
  */
Particles Particles::cut(const double &sep) const
{
    Particles out;
    out.bb = this->bb;
    out.reserve(this->size());
    out.setIndex(new RStarIndex_S(vector<BoundingBox>()));
    for(const_iterator p = this->begin(); p!=this->end();++p)
        if(out.getEuclidianNeighbours(*p,sep).empty())
            out.push_back(*p);
    return out;
}

/** @brief return a copy with no particle closer than sep.
    If two particles are closer than sep, BOTH are discarded.
    The copy is not indexed.
  */
Particles Particles::removeShortRange(const double &sep) const
{
    if(!this->hasIndex())
        throw std::logic_error("Set a spatial index before doing spatial queries !");
    Particles out;
    out.bb = this->bb;
    out.reserve(this->size());
    for(size_t p = 0; p!=this->size();++p)
        if(getEuclidianNeighbours(p, sep).empty())
            out.push_back((*this)[p]);
    return out;
}


/** \brief resizing the box and rescaling the coordinates */
Particles& Particles::operator*=(const Coord &v)
{
    for(size_t i=0; i<3;++i)
        bb.edges[i].second*=v[i];

    for(iterator p=begin();p!=end();++p)
        (*p)*=v;

    return *this;
}
/** \brief resizing the box, rescaling the coordinates and the radius */
Particles& Particles::operator*=(const double &mul)
{
    const Coord v(mul,3);
    operator*=(v);
    radius*=mul;

    return *this;
}

/** \brief translation of the box and of the coordinates */
Particles& Particles::operator+=(const Coord &v)
{
    bb+=v;
    #pragma omp parallel for
    for(size_t p=0; p<size(); ++p)
        (*this)[p] += v;

    if(hasIndex())
		(*index)+=v;
    return *this;
}


/**
    \brief get the angle between two vectors joining origin on one hand and respectively a and b on the other hand
    \return An angle in radian between 0 and Pi
*/
double Particles::getAngle(const size_t &origin,const size_t &a,const size_t &b) const
{
    Coord va(3),vb(3);
    va = getDiff(origin,a);
    vb = getDiff(origin,b);
    return acos(dot(va,vb)/sqrt(dot(va,va)*dot(vb,vb)));
}

/** @brief Gives the indices of the particles inside a reduction of the total bonding box. Not using spatial index, thus slower.  */
vector<size_t> Particles::selectInside_noindex(const double &margin, const bool noZ) const
{
	Coord upper(0.0,3), lower = this->front();
	for(const_iterator p=begin(); p!=end(); ++p)
		for(size_t d=0; d<3; ++d)
		{
			upper[d] = max(upper[d], (*p)[d]);
			lower[d] = min(lower[d], (*p)[d]);
		}
    for(size_t d=0; d<3-noZ; ++d)
    {
        upper[d] -= margin;
        lower[d] += margin;
    }
	vector<size_t> ret;
	for(size_t p=0; p<size(); ++p)
		if( ((*this)[p]<=upper).min() && (lower<=(*this)[p]).min() )
			ret.push_back(p);
	return ret;
}



/**
    \brief Makes the bounding box centered on a particle
    \param r radius of the box
*/
BoundingBox Particles::bounds(const Coord &center,const double &r)
{
	BoundingBox bb;

	for(size_t i=0;i<3;++i)
	{
        bb.edges[i].first  = center[i]-r;
        bb.edges[i].second = center[i]+r;
	}

	return bb;
}

/** @brief make a RTree spatial index for the present particles set  */
void Particles::makeRTreeIndex()
{
    vector<BoundingBox> boxes;
    boxes.reserve(this->size());
    for(const_iterator p = this->begin(); p!=this->end();++p)
        boxes.push_back(bounds(*p));

    setIndex(new RStarIndex_S(boxes));
}

/** @brief getOverallBox  */
BoundingBox Particles::getOverallBox() const
{
    if(this->hasIndex())
        return index->getOverallBox();
    else
        return bb;
}



/**
    \brief get the indices of the particles closer than range to center (Euclidian norm)
*/
vector<size_t> Particles::getEuclidianNeighbours(const Coord &center, const double &range) const
{
    vector<size_t> NormOneNeighbours = selectEnclosed(bounds(center,range));
    vector<size_t> NormTwoNeighbours;
    NormTwoNeighbours.reserve(NormOneNeighbours.size());
    Coord diff(3);
    double rSq = range*range;
    for(ssize_t p=0; p<(ssize_t)NormOneNeighbours.size();++p)
    {
        diff = getDiff(center,NormOneNeighbours[p]);
        if(dot(diff,diff)<rSq) NormTwoNeighbours.push_back(NormOneNeighbours[p]);
    }
    return NormTwoNeighbours;
}

/**
    \brief get the indices of the particles closer than range to center (Euclidian norm), discarding center itself
*/
vector<size_t> Particles::getEuclidianNeighbours(const size_t &center, const double &range) const
{
    vector<size_t> NormOneNeighbours = selectEnclosed(bounds((*this)[center],range));
    vector<size_t> NormTwoNeighbours;
    NormTwoNeighbours.reserve(NormOneNeighbours.size());
    Coord diff(3);
    double rSq = range*range;
    for(ssize_t p=0; p<(ssize_t)NormOneNeighbours.size();++p)
    {
    	if(NormOneNeighbours[p] == center) continue;
        diff = getDiff((*this)[center],NormOneNeighbours[p]);
        if(dot(diff,diff)<rSq) NormTwoNeighbours.push_back(NormOneNeighbours[p]);
    }
    return NormTwoNeighbours;
}

/**
    \brief get the index of the particles closer than range to center sorted by Sqare distance to the center (Euclidian norm)
*/
multimap<double,size_t> Particles::getEuclidianNeighboursBySqDist(const Coord &center, const double &range) const
{
    vector<size_t> NormOneNeighbours = selectEnclosed(bounds(center,range));
    multimap<double,size_t> NormTwoNeighbours;
    Coord diff(3);
    double rSq = range*range, distSq;
    for(ssize_t p=0;p<(ssize_t)NormOneNeighbours.size();++p)
    {
        diff = getDiff(center,NormOneNeighbours[p]);
        distSq = dot(diff, diff);
        if(distSq<rSq) NormTwoNeighbours.insert(make_pair(distSq,NormOneNeighbours[p]));
    }
    return NormTwoNeighbours;
}

/**
    \brief get the index of the closest particle to center (Euclidian norm)
    \param range Guess of the distance to the nearest neighbour
*/
size_t Particles::getNearestNeighbour(const Coord &center, const double &range) const
{
    double rg = range;
    vector<size_t> ngb = getEuclidianNeighbours(center,rg);
    //seeking for an acceptable range
    while(ngb.empty())
    {
        rg*=1.1;
        ngb = getEuclidianNeighbours(center,rg);
    }
    //if(rg!=range) cout << "you should increase the range by " << rg/range << endl;

    if (ngb.size()==1) return *(ngb.begin());

    size_t nN=size();
    double dist=0.0,mindist=rg*rg;
    Coord diff(3);
    for(ssize_t p=0;p<(ssize_t)ngb.size();++p)
    {
        diff = getDiff(center,ngb[p]);
        dist = dot(diff, diff);
        if(dist<mindist)
        {
            mindist = dist;
            nN=ngb[p];
        }
    }
    return nN;
}

/** @brief get the neighbours of each particle
  * \param bondLength The maximum separation between two neighbour particles. In diameter units
  A particle is not it's own neighbour.
  */
NgbList & Particles::makeNgbList(const double &bondLength)
{
    this->neighboursList.reset(new NgbList(size()));
    const double sep = 2.0*bondLength*radius;
    for(size_t p=0;p<size();++p)
        (*neighboursList)[p] = getEuclidianNeighbours(p, sep);

    return *this->neighboursList;
}

/** @brief make the neighbour list using a list of bonds  */
NgbList & Particles::makeNgbList(const BondSet &bonds)
{
    this->neighboursList.reset(new NgbList(size()));
    for(BondSet::const_iterator b=bonds.begin(); b!=bonds.end();++b)
    {
        (*neighboursList)[b->low()].insert((*neighboursList)[b->low()].end(), b->high());
        (*neighboursList)[b->high()].insert((*neighboursList)[b->high()].end(), b->low());
    }
    return *this->neighboursList;
}


/** \brief return the value of the spherical harmonics for the bound between two particles */
BooData Particles::sphHarm_OneBond(const size_t &center, const size_t &neighbour) const
{
    return BooData(getDiff(center,neighbour));
}

/** \brief get the orientational order around a given particle
    \param numPt Index of the reference particle
    \param ngbList List of the center's neighbours
  */
BooData Particles::getBOO(const size_t &center) const
{
	BooData boo;
	const vector<size_t> & ngbList = getNgbList()[center];
    const size_t nb = ngbList.size();
    if(nb > 0)
    {
        //sum up the contribution of each neighbour to every spherical harmonic.
        for(ssize_t p=0; p<(ssize_t)ngbList.size();++p)
            boo+=sphHarm_OneBond(center,ngbList[p]);

        boo/=(double)nb;
    }
    return boo;
}


/** \brief get the averaged orientational order around a given particle
    \param BOO Array of the non-averaged orientational orders around each particle
    \param numPt Index of the reference particle
    \param ngbList List of the center's neighbours
  */
BooData Particles::getCgBOO(const std::vector<BooData> &BOO, const size_t &center) const
{
    //sum up the contribution of each neighbour including the particle itself.
	BooData avBoo = BOO[center];
    const std::vector<size_t> &ngbList = getNgbList()[center];
    for(ssize_t p=0; p<(ssize_t)ngbList.size();++p)
            avBoo += BOO[ngbList[p]];

    avBoo/=(double)(1+ngbList.size());
    return avBoo;
}

/**
    \brief get the bond orientational order for all particles
*/
void Particles::getBOOs(std::vector<BooData> &BOO) const
{
    BOO.resize(size());
    vector<size_t> nbs(size(),0);
    for(size_t p=0;p<getNgbList().size();++p)
		for(vector<size_t>::const_iterator q=lower_bound(getNgbList()[p].begin(), getNgbList()[p].end(), p+1); q!=getNgbList()[p].end();++q)
		{
		    //calculate the spherical harmonics coefficients of the bond between p and q
            BooData spharm = sphHarm_OneBond(p, *q);
            //add it to the qlm of p and q
            BOO[p] += spharm;
            nbs[p]++;
            BOO[*q] += spharm;
            nbs[*q]++;
		}
    //normalize by the number of bonds
    for(size_t p=0; p<size(); ++p)
        BOO[p] /= complex<double>(nbs[p], 0);
}

/**
    \brief get the bond orientational order for a selection of particles
    \callgraph
*/
void Particles::getBOOs(const vector<size_t> &selection, std::vector<BooData> &BOO) const
{
    BOO.resize(size());
    for(ssize_t p=0;p<(ssize_t)selection.size();++p)
        BOO[selection[p]] = getBOO(selection[p]);
}

/**
    \brief get the coarse grained bond orientational order for all particles
    \callgraph
*/
void Particles::getCgBOOs(const vector<size_t> &selection, const std::vector<BooData> &BOO, std::vector<BooData> &cgBOO) const
{
    cgBOO.resize(size());
    for(ssize_t p=0;p<(ssize_t)selection.size();++p)
        cgBOO[selection[p]] = getCgBOO(BOO, selection[p]);
}

/**
    \brief get the bond orientational order including surface bonds for all particles
*/
void Particles::getSurfBOOs(std::vector<BooData> &BOO) const
{
    BOO.resize(size());
    vector<size_t> nbs(size(),0);
    for(size_t p=0;p<getNgbList().size();++p)
		for(vector<size_t>::const_iterator q=lower_bound(getNgbList()[p].begin(), getNgbList()[p].end(), p+1); q!=getNgbList()[p].end();++q)
		{
		    //calculate the spherical harmonics coefficients of the bond between p and q
            BooData spharm = sphHarm_OneBond(p, *q);
            //add it to the qlm of p and q
            BOO[p] += spharm;
            nbs[p]++;
            BOO[*q] += spharm;
            nbs[*q]++;
            //find the common neighbours of p and q
            vector<size_t> common;
            common.reserve(max(getNgbList()[p].size(), getNgbList()[*q].size())-1);
            set_intersection(
                getNgbList()[p].begin(), getNgbList()[p].end(),
                getNgbList()[*q].begin(), getNgbList()[*q].end(),
                back_inserter(common)
                );
            //add the spherical harmonics coeff to the qlm of the common neighbours of p and q
            for(vector<size_t>::const_iterator c= common.begin(); c!=common.end(); ++c)
            {
                BOO[*c] += spharm;
                nbs[*c]++;
            }
		}
    //normalize by the number of bonds
    for(size_t p=0; p<size(); ++p)
		if(nbs[p]!=0)
			BOO[p] /= complex<double>(nbs[p], 0);
}

void Particles::getBOOs_SurfBOOs(std::vector<BooData> &BOO, std::vector<BooData> &surfBOO) const
{
    BOO.resize(size());
    surfBOO.resize(size());
    vector<size_t> nbs(size(),0);
    vector<size_t> nbsurf(size(),0);
    for(size_t p=0;p<getNgbList().size();++p)
		for(vector<size_t>::const_iterator q=lower_bound(getNgbList()[p].begin(), getNgbList()[p].end(), p+1); q!=getNgbList()[p].end();++q)
		{
		    //calculate the spherical harmonics coefficients of the bond between p and q
            BooData spharm = sphHarm_OneBond(p, *q);
            //add it to the qlm of p and q
            BOO[p] += spharm;
            nbs[p]++;
            BOO[*q] += spharm;
            nbs[*q]++;
            //same for the sum including the surface bonds
            surfBOO[p] += spharm;
            nbsurf[p]++;
            surfBOO[*q] += spharm;
            nbsurf[*q]++;
            //find the common neighbours of p and q
            vector<size_t> common;
            common.reserve(max(getNgbList()[p].size(), getNgbList()[*q].size())-1);
            set_intersection(
                getNgbList()[p].begin(), getNgbList()[p].end(),
                getNgbList()[*q].begin(), getNgbList()[*q].end(),
                back_inserter(common)
                );
            //add the spherical harmonics coeff to the qlm of the common neighbours of p and q
            for(vector<size_t>::const_iterator c= common.begin(); c!=common.end(); ++c)
            {
                surfBOO[*c] += spharm;
                nbsurf[*c]++;
            }
		}
    //normalize by the number of bonds
    for(size_t p=0; p<size(); ++p)
		if(nbs[p]!=0)
			BOO[p] /= complex<double>(nbs[p], 0);
    for(size_t p=0; p<size(); ++p)
		if(nbsurf[p]!=0)
			surfBOO[p] /= complex<double>(nbsurf[p], 0);
}

/** @brief coarse grain the bond orientational order along the bonds after half turn rotation.  */
void Particles::getFlipBOOs(const std::vector<BooData> &BOO, std::vector<BooData> &flipBOO, const BondSet &bonds) const
{
	flipBOO = BOO;
	vector<size_t> nb(BOO.size(), 1);
	for(BondSet::const_iterator b=bonds.begin(); b!=bonds.end(); ++b)
	{
		if(BOO[b->low()][0]==0.0 || BOO[b->high()][0]==0.0)
			continue;
		Coord diff = getDiff(b->low(), b->high());
		flipBOO[b->low()] += BOO[b->high()].rotate_by_Pi(diff);
		flipBOO[b->high()] += BOO[b->low()].rotate_by_Pi(diff);
		nb[b->low()]++;
		nb[b->high()]++;
	}
	for(size_t p=0; p<BOO.size(); ++p)
		flipBOO[p] /= (double)nb[p];
}



/** @brief export qlm in binary  */
void Particles::exportQlm(const std::vector<BooData> &BOO, const std::string &outputPath) const
{
    ofstream qlm;
    qlm.open(outputPath.c_str(), ios::binary | ios::trunc);
    if(!qlm.is_open())
        throw invalid_argument("No such file as "+outputPath);

    double buffer[72];
    for(vector<BooData>::const_iterator p=BOO.begin();p!=BOO.end();++p)
    {
        qlm.write(p->toBinary(&buffer[0]),72*sizeof(double));
    }
    qlm.close();
}
/** @brief export qlm for l==6 in ascii  */
void Particles::exportQ6m(const std::vector<BooData> &BOO, const std::string &outputPath) const
{
    ofstream q6m;
    q6m.open(outputPath.c_str(), std::ios::out | ios::trunc);
    if(!q6m.is_open())
        throw invalid_argument("No such file as "+outputPath);

    for(vector<BooData>::const_iterator p=BOO.begin();p!=BOO.end();++p)
    {
    	for(size_t m=0;m<=6;++m)
			q6m <<"\t"<<(*p)(6,m);
		q6m<<"\n";
    }
    q6m.close();
}

/** @brief load q6m from file as BooData  */
void Particles::load_q6m(const string &filename, vector<BooData> &BOO) const
{
    BOO.resize(size());
	ifstream f(filename.c_str(), ios::in);
	if(!f)
		throw invalid_argument("no such file as "+filename);

	size_t p=0;
	while(!f.eof())
	{
		for(size_t m=0;m<=6;++m)
			f>> BOO[p][m + 9];
        p++;
	}
	f.close();
}

/** @brief BooData from ASCII file  */
void Particles::load_qlm(const std::string &filename, std::vector<BooData> &BOO) const
{
	BOO.resize(size());
	ifstream f(filename.c_str(), ios::in);
	if(!f.good())
		throw invalid_argument("no such file as "+filename);

	copy(
		istream_iterator<BooData>(f),
		istream_iterator<BooData>(),
		BOO.begin()
		);
}



/** \brief Get the bond angle distribution around one particle given the list of the particles it is bounded with    */
boost::array<double,180> Particles::getAngularDistribution(const size_t &numPt) const
{
    boost::array<double,180> angD;
    const std::vector<size_t> &ngbs = getNgbList()[numPt];
    fill(angD.begin(), angD.end(), 0.0);
    const size_t nb = ngbs.size();
    if(nb > 1)
    {
        //histogram is scaled by the number of bond angles
        const double scale = nb>2 ? 1.0 / ((nb-1)*(nb-2)/2) : 1.0;
        //sum up the contribution of each bond angle.
        for(ssize_t a=0;a<(ssize_t)ngbs.size();++a)
            if( numPt != ngbs[a])
            	for(ssize_t b=a+1;b<(ssize_t)ngbs.size();++b)
					if(numPt != ngbs[b])
						angD[(size_t)(getAngle(numPt,ngbs[a],ngbs[b])* 180.0 / M_PI)] = scale;
    }
    return angD;
}

/** \brief get the mean angular distribution of a given set of particles */
/*boost::array<double,180> Particles::getMeanAngularDistribution(const NgbList &selection) const
{
    boost::array<double,180> angD;
    fill(angD.begin(), angD.end(), 0.0);
    for(NgbList::const_iterator p=selection.begin();p!=selection.end();++p)
        transform(
            angD.begin(), angD.end(),
            getAngularDistribution(p->first,p->second).begin(), angD.begin(),
            plus<double>()
            );

    transform(
        angD.begin(), angD.end(),
        angD.begin(),
        bind2nd(divides<double>(), (double)selection.size())
        );
    return angD;
}*/

/** @brief Do the particles listed in common form a ring ?  */
bool Particles::is_ring(std::list<size_t> common) const
{
	for(list<size_t>::const_iterator c=common.begin(); c!=common.end(); ++c)
	{
		list<size_t> ringngb;
		set_intersection(
			getNgbList()[*c].begin(), getNgbList()[*c].end(),
			common.begin(), common.end(),
			back_inserter(ringngb)
			);
		if(ringngb.size()!=2)
			return false;
	}
	return true;
}



/**
    \brief get the SP5c clusters (TCC, Williams 2008) = Honeycut & Andersen's 1551 pairs
*/
void Particles::getSP5c(std::vector< std::vector<size_t> > &SP5c) const
{
    for(size_t p=0;p<getNgbList().size();++p)
		for(vector<size_t>::const_iterator q=lower_bound(getNgbList()[p].begin(), getNgbList()[p].end(), p+1); q!=getNgbList()[p].end();++q)
		{
            //find the common neighbours of p and q
            vector<size_t> common;
            common.reserve(max(getNgbList()[p].size(), getNgbList()[*q].size())+1);
            common.push_back(p);
            common.push_back(*q);
            set_intersection(
                getNgbList()[p].begin(), getNgbList()[p].end(),
                getNgbList()[*q].begin(), getNgbList()[*q].end(),
                back_inserter(common)
                );
            if(common.size()==7)
                SP5c.push_back(common);
            //should look here if it's a ring or not, but not crucial if non voronoi bonds
		}
}

/** @brief get 1551 pairs of particles (linked particles having exactly 5 common neighbours forming a ring) */
BondSet Particles::get1551pairs() const
{
	BondSet ret;
	for(size_t p=0; p<getNgbList().size(); ++p)
		for(vector<size_t>::const_iterator q=lower_bound(getNgbList()[p].begin(), getNgbList()[p].end(), p+1); q!=getNgbList()[p].end();++q)
		{
			//find the common neighbours of the two extremities of the bond
			list<size_t> common;
			set_intersection(
                getNgbList()[p].begin(), getNgbList()[p].end(),
                getNgbList()[*q].begin(), getNgbList()[*q].end(),
                back_inserter(common)
                );
			if(common.size()!=5 || !is_ring(common)) continue;

			ret.insert(ret.end(), Bond(p,*q));
		}
	return ret;
}

/** @brief get 2331 pairs of particles (unlinked particles having exactly 3 common neighbours forming a ring) */
BondSet Particles::get2331pairs() const
{
	BondSet ret;
	for(size_t p=0; p<getNgbList().size(); ++p)
	{
		list<size_t> second_ngb, not_first_ngb;
		//list the first and second shell
		for(vector<size_t>::const_iterator c=getNgbList()[p].begin(); c!=getNgbList()[p].end();++c)
			copy(
				getNgbList()[*c].begin(), getNgbList()[*c].end(),
				back_inserter(second_ngb)
				);
		second_ngb.sort();
		second_ngb.unique();

		//reduce to the second shell only
		set_difference(
			second_ngb.begin(), second_ngb.end(),
			getNgbList()[p].begin(), getNgbList()[p].end(),
			back_inserter(not_first_ngb)
			);

		for(list<size_t>::const_iterator q=lower_bound(not_first_ngb.begin(), not_first_ngb.end(), p+1); q!=not_first_ngb.end();++q)
		{
			//find the common neighbours of the two extremities of the bond
			list<size_t> common;
			set_intersection(
				getNgbList()[p].begin(), getNgbList()[p].end(),
				getNgbList()[*q].begin(), getNgbList()[*q].end(),
				back_inserter(common)
				);
			if(common.size()!=3 || !is_ring(common)) continue;

			ret.insert(ret.end(), Bond(p,*q));
		}
	}
	return ret;
}

/** @brief get the bonds to the neighbours and to their neighbours   */
BondSet Particles::getSecondShell() const
{
	BondSet ret;
	for(size_t p=0; p<getNgbList().size(); ++p)
	{
		list<size_t> second_ngb;
		//list the first and second shell
		for(vector<size_t>::const_iterator c=getNgbList()[p].begin(); c!=getNgbList()[p].end();++c)
			copy(
				getNgbList()[*c].begin(), getNgbList()[*c].end(),
				back_inserter(second_ngb)
				);
		second_ngb.sort();
		second_ngb.unique();
		for(list<size_t>::const_iterator q = lower_bound(second_ngb.begin(), second_ngb.end(), p+1); q!=second_ngb.end(); ++q)
			ret.insert(ret.end(), Bond(p, *q));
	}
	return ret;
}



Particles::Binner::~Binner(void){};

/**	\brief Bin the particles given by selection (coupled to their neighbours). */
void Particles::Binner::operator<<(const std::vector<size_t> &selection)
{
    #pragma omp parallel for schedule(dynamic)
    for(ssize_t p=0; p<(ssize_t)selection.size(); ++p)
    {
        std::vector<size_t> around = parts.getEuclidianNeighbours(selection[p],cutoff);
        for(ssize_t q=0;q<(ssize_t)around.size();++q)
			(*this)(selection[p],around[q]);
    }
}

/**	\brief Normalize the histogram. Do not bin afterward */
void Particles::RdfBinner::normalize(const size_t &n)
{
    g[0]=0.0;
    const double norm = 4.0 * M_PI * parts.getNumberDensity() / pow(scale,3.0) *n;
    for(size_t r=0;r<g.size();++r)
        g[r]/=norm;
    for(size_t r=1;r<g.size();++r)
        g[r]/=r*r;
}

/**	\brief Make and export the rdf of the selection */
std::vector<double> Particles::getRdf(const std::vector<size_t> &selection, const size_t &n, const double &nbDiameterCutOff) const
{
	RdfBinner b(*this,n,nbDiameterCutOff);
	b<<selection;
	b.normalize(selection.size());
	return b.g;
}

/**	\brief Make and export the rdf */
std::vector<double> Particles::getRdf(const size_t &n, const double &nbDiameterCutOff) const
{
	return getRdf(index->getInside(2.0*radius*nbDiameterCutOff), n, nbDiameterCutOff);
}

/**	\brief Normalize the histogram. Do not bin afterward */
void Particles::GlBinner::normalize(const size_t &n)
{
    gl[0]=0.0;
    const double norm = 13.0/(4.0*M_PI);
    for(size_t r=1;r<g.size();++r)
		if(1.0+g[r]*g[r] == 1.0)
			gl[r]=0;
		else
			gl[r] /= norm * g[r];
	RdfBinner::normalize(n);
}



/** \brief export the data to a dat file */
void Particles::exportToFile(const string &filename) const
{
    //cout << "export to " << filename << endl;

    ofstream output(filename.c_str(), ios::out | ios::trunc);
    if(output)
    {
      //DAT header
      output << "1\t" << size() << "\t1" << endl;
      output << bb.edges[0].second << "\t" << bb.edges[1].second << "\t" << bb.edges[2].second << endl;

      for(const_iterator p=begin();p!=end();++p)
      {
        for(size_t i=0;i<3;++i)
          output << (*p)[i] << "\t";
        output << "\n";
      }
      output.close();
    }
    else
		throw invalid_argument("Cannot write on "+filename);
}

/** @brief export the coordinates to a stream in vtk format (including header)  */
std::ostream & Particles::toVTKstream(std::ostream &out, const std::string &dataName) const
{
	out<<"# vtk DataFile Version 3.0\n"
			<<dataName<<"\n"
			"ASCII\n"
			"DATASET POLYDATA\n"
			"POINTS "<<size()<<" double\n";
	for(const_iterator p=begin();p!=end();++p)
	{
		for(size_t d=0;d<3;++d)
			out<<(*p)[d]<<" ";
		out<<"\n";
	}
	return out;
}



/** @brief Most general export to VTK Polydata format
	\param filename Name of the file to export to
	\param bonds The explicit unoriented bonds between particles
	\param scalars N Scalar fields with a name and mapping particle numbers to scalar (double) values
	\param vectors N Vector fields with a name and mapping particle numbers to vector (Coord) values
	\param dataName The name of the full dataset
*/
void Particles::exportToVTK(
	const std::string &filename,
	const BondSet &bonds,
	const std::vector<ScalarField> &scalars,
	const std::vector<VectorField> &vectors,
	const std::string &dataName
) const
{
	ofstream output(filename.c_str(), ios::out | ios::trunc);
    if(!output)
		throw invalid_argument("Cannot write on "+filename);

	toVTKstream(output, dataName);

	Colloids::toVTKstream(output, bonds);

	output<<"POINT_DATA "<<size()<<endl;
	copy(
		scalars.begin(), scalars.end(),
		ostream_iterator<ScalarField>(output)
		);
	copy(
		vectors.begin(), vectors.end(),
		ostream_iterator<VectorField>(output)
		);
	output.close();
}

/** @brief exportToVTK without bonds  */
void Particles::exportToVTK(const std::string &filename, const std::vector<ScalarField> &scalars, const std::vector<VectorField> &vectors, const std::string &dataName) const
{
	exportToVTK(filename,getBonds(),scalars,vectors,dataName);
}

/** @brief export only positions and scalar fields to VTK	*/
void Particles::exportToVTK(const std::string &filename, const std::vector<ScalarField> &scalars, const std::string &dataName) const
{
	exportToVTK(filename,scalars,std::vector<VectorField>(),dataName);
}


/** \brief return the minimum dimension of the bounding box */
double Particles::getMinDim() const
{
    return min(bb.edges[0].second,min(bb.edges[1].second,bb.edges[2].second));
}

/** \brief return the number density */
double Particles::getNumberDensity() const
{
    //get the volume accessible to the particles
    BoundingBox b;
    if(hasIndex())
        b = index->getOverallBox();
    else
    {
        valarray<double> maxi = front(), mini = front();
        for(Particles::const_iterator p=begin(); p!=end(); ++p)
            for(int d=0; d<3;++d)
            {
                maxi[d] = max(maxi[d], (*p)[d]);
                mini[d] = min(mini[d], (*p)[d]);
            }
        for(int d=0; d<3;++d)
        {
            b.edges[d].first = mini[d];
            b.edges[d].second = maxi[d];
        }
    }
    //calculate the number density (number of particles per unit size^3)
    return size()/b.area();
}

/** \brief return the volume fraction, considering a margin equal to the radius */
double Particles::getVF() const
{
    return 4*M_PI*pow(radius,3.0)/3.0 * getNumberDensity();
}

/** \brief return true if the two particles are closer together than Sep */
/*bool Particles::areTooClose(const Coord &c, const Coord &d,const double &Sep)
{
     const valarray<double> diff = c-d;
     return dot(diff*diff).sum()<Sep*Sep ;
}*/

/** @brief get rotational invariants ql, wl (l=4 to l=10) from a cloud file  */
void Particles::loadBoo(const string &filename, boost::multi_array<double,2>&qw) const
{
	ifstream cloud(filename.c_str(), ios::in);
	if(!cloud)
		throw invalid_argument("no such file as "+filename);

	string trash;
	//trashing the header
	getline(cloud, trash);

	boost::array<size_t, 2> shape = {{size(), 8}};
	qw.resize(shape);
	copy(
		istream_iterator<double>(cloud), istream_iterator<double>(),
		qw.origin()
		);

	cloud.close();
}

/** @brief from a neighbour list to a bond list  */
BondSet Colloids::ngb2bonds(const NgbList& ngbList)
{
    BondSet bonds;
	for(size_t p=0;p<ngbList.size();++p)
		for(vector<size_t>::const_iterator q=lower_bound(ngbList[p].begin(), ngbList[p].end(), p+1); q!=ngbList[p].end();++q)
			bonds.insert(bonds.end(), Bond(p,*q));
	return bonds;
}

/** @brief load bonds from file  */
BondSet Colloids::loadBonds(const std::string &filename)
{
	BondSet bonds;
	ifstream f(filename.c_str());
	if(!f)
		throw invalid_argument("no such file as "+filename);
	copy(
		istream_iterator<Bond>(f), istream_iterator<Bond>(),
		inserter(bonds, bonds.end())
		);
	return bonds;
}

/** @brief export a bondset to a stream in VTK format (heavier than saveBond)  */
ostream & Colloids::toVTKstream(std::ostream &out, const BondSet &bonds)
{
	out << "LINES "<<bonds.size()<<" "<<bonds.size()*3<<endl;
	for(BondSet::const_iterator b= bonds.begin();b!=bonds.end();++b)
		out<<"2 "<< *b <<"\n";
    return out;
}




