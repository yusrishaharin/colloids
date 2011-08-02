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

#include "traj.hpp"
#include <boost/tokenizer.hpp>
#include <list>
#include <stdexcept>

using namespace std;

namespace Colloids {


typedef multimap<double, size_t>   FolMap;

struct Link : std::pair<size_t, size_t>
{
	double distance;
	Link(const size_t &a, const size_t &b, const double &distance) :
		std::pair<size_t, size_t>(a,b), distance(distance){};

	bool operator<(const Link &other) const {return this->distance<other.distance;}
};


/** @brief Constructor  */
TrajIndex::TrajIndex(const size_t& nb_initial_positions) : tr2pos(nb_initial_positions), pos2tr(1)
{
    for(size_t p=0; p<nb_initial_positions; ++p)
    	this->tr2pos.push_back(new Traj(0, p));
    this->pos2tr.push_back(new std::vector<size_t>(nb_initial_positions));
    for(size_t p=0; p<nb_initial_positions; ++p)
        	this->pos2tr[0][p] = p;
}

void TrajIndex::add_Frame(const size_t &frame_size, const std::vector<double> &distances, const std::vector<size_t> &p_from, const std::vector<size_t> &p_to)
{
	if(distances.size() != p_from.size() || p_from.size() != p_to.size())
		throw std::invalid_argument("TrajIndex::add_Frame: All arguments must have the same size");
	if(*std::max_element(p_to.begin(), p_to.end()) >= frame_size)
		throw std::invalid_argument("TrajIndex::add_Frame: The largest particle index in the new frame is larger than the new frame size");
	std::list<Link> bonds;
	for(size_t i=0; i< distances.size(); ++i)
		bonds.push_back(Link(p_from[i], p_to[i], distances[i]));
	//sort the bonds by increasing distances
	bonds.sort();
	//any position can be linked only once
	std::vector<bool> from_used(this->pos2tr.back().size(), false), to_used(frame_size, false);
	//create the new frame
	this->pos2tr.push_back(new std::vector<size_t>(frame_size));
	//link the bounded positions into trajectories
	for(std::list<Link>::const_iterator b= bonds.begin(); b!=bonds.end(); ++b)
		if(!from_used[b->first] && !to_used[b->second])
		{
			from_used[b->first] = true;
			to_used[b->second] = true;
			const size_t tr = this->pos2tr[this->pos2tr.size()-2][b->first];
			this->pos2tr.back()[b->second] = tr;
			this->tr2pos[tr].push_back(b->second);
		}
	//the trajectories of the previous frame that are not linked in the new frame are terminated by construction
	//but the trajectories starting in the new frame have to be created
	for(size_t p=0; p<to_used.size(); ++p)
		if(!to_used[p])
		{
			this->pos2tr.back()[p] = this->tr2pos.size();
			this->tr2pos.push_back(new Traj(this->pos2tr.size()-1, p));
		}
}



/** @brief get the latest time spanned by a trajectory  */
/*size_t TrajIndex::getMaxTime() const
{
    int max=1;
    for(const_iterator tr=begin();tr!=end();++tr)
        if(max < tr->get_finish())
            max = tr->get_finish();
    return (size_t)(max-1);
}*/

/** @brief longest_span return the number of time steps that spans the longest trajectory  */
/*size_t TrajIndex::longest_span() const
{
    vector<size_t> lengths(size());
    transform(
        this->begin(), this->end(),
        lengths.begin(),
        mem_fun_ref(&Traj::size)
        );
    return *max_element(lengths.begin(), lengths.end());
}*/



/** @brief count the number of trajectories in each time step  */
/*vector<size_t> TrajIndex::getFrameSizes(const size_t &length) const
{
	vector<size_t> frameSizes(length?length:(getMaxTime()+1), 0);
	for(const_iterator tr=begin(); tr!=end(); ++tr)
		for(int t = tr->get_start(); t < tr->get_finish(); ++t)
			frameSizes[t]++;
	return frameSizes;
}*/



/** @brief make/reset the inverse index  */
/*void TrajIndex::makeInverse(const std::vector<size_t> &frameSizes)
{
    inverse.resize(0);
    inverse.reserve(frameSizes.size());
    for(size_t t=0; t<frameSizes.size(); ++t)
		inverse.push_back(new vector<size_t>(frameSizes[t]));
    for(size_t tr=0;tr<size();++tr)
        for(int t = (*this)[tr].get_start(); t < (*this)[tr].get_finish();++t)
            inverse[t][(*this)[tr][t]]=tr;
}*/

} //Name space
