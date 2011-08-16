/*
 * reconstructor.cpp
 *
 *  Created on: 3 août 2011
 *      Author: mathieu
 */

#include "reconstructor.h"
#include "RStarTree/RStarTree.h"
#include "multiscalefinder.hpp"
#include <math.h>

using namespace std;

namespace Colloids {

Reconstructor::Reconstructor() {
	// TODO Auto-generated constructor stub

}

Reconstructor::~Reconstructor() {
	// TODO Auto-generated destructor stub
}

void Reconstructor::clear()
{
	this->clusters.clear();
	this->trajectories.reset();
	this->last_frame.clear();
}
/**
 * \param tolerance Fraction of the contact distance (sum of radii) accepted. For tolerance<=1 accept overlap only.
 */
void Reconstructor::push_back(const Frame &fr, const double &tolerance)
{
	if(this->empty())
	{
		this->trajectories.reset(new TrajIndex(fr.size()));
		for(Frame::const_iterator c=fr.begin(); c!=fr.end(); ++c)
			this->clusters.push_back(Cluster(1, Center3D(*c, 0)));
	}
	else
	{
		std::vector<double> distances;
		std::vector<size_t> from, to;
		//use RStarTree spatial indexing
		this->links_by_RStarTree(fr, distances, from, to, tolerance);
		//brute force
		//this->links_by_brute_force(fr, distances, from, to);
		//remember the time step and the number of previously existing trajectories
		const size_t
			t = this->size(),
			old_traj = this->trajectories->size();
		//link trajectories
		this->trajectories->add_Frame(fr.size(), distances, from, to);
		for(size_t p=0; p<fr.size(); ++p)
		{
			const size_t tr = this->trajectories->getTraj(t, p);
			if(tr<old_traj)
				//update formerly existing clusters
				this->clusters[tr].push_back(Center3D(fr[p], t));
			else
				//create new cluster
				this->clusters.push_back(Cluster(1, Center3D(fr[p], t)));
		}
	}
	//keep a copy of the inserted frame
	this->last_frame = fr;
}

void Reconstructor::split_clusters()
{
	const size_t cl_end =  this->clusters.size();
	for(size_t cl=0; cl<cl_end;++cl)
	{
		if(this->clusters[cl].size()<6)
			continue;
		//compute the position gradient
		cv::Mat_<double> grad(1, this->clusters[cl].size()-1);
		Cluster::const_iterator c0 = this->clusters[cl].begin(), c1 = this->clusters[cl].begin();
		c1++;
		for(int i=0; i<grad.cols; ++i)
		{
			grad(0, i) = pow((*c0)[0]-(*c1)[0], 2) + pow((*c0)[1]-(*c1)[1], 2);
			c0++;
			c1++;
		}
		//feed into a Multiscale finder to find altitude where the position is moving a lot function of altitude
		MultiscaleFinder1D finder(grad.cols);
		std::vector<Center2D> blobs;
		finder.get_centers(grad, blobs);
		if(blobs.empty())
			continue;

		//split, from end to begin
		for(size_t i=0; i<blobs.size(); ++i)
		{
			this->clusters.push_back(Cluster());
			Cluster::iterator u = this->clusters[cl].begin();
			std::advance(u, (size_t)blobs[blobs.size()-i-1][0]);
			this->clusters.back().splice(this->clusters.back().begin(), this->clusters[cl], u, this->clusters[cl].end());
		}
	}
}

void Reconstructor::get_blobs(std::deque<Center3D>& centers)
{
	const size_t margin = 6;
	centers.clear();
	for(std::deque<Cluster>::const_iterator cl=this->clusters.begin(); cl!=this->clusters.end(); ++cl)
	{
		if(cl->size()<margin)
			continue;
		//copy the radii adding margins on each size to allow blob tracking on short signals
		cv::Mat_<double> signal(1, cl->size()+2*margin);
		signal.setTo(0);
		Cluster::const_iterator c=cl->begin();
		for(size_t i=0; i<cl->size(); ++i)
		{
			signal(0, i+margin) = c->r;
			c++;
		}
		MultiscaleFinder1D finder(signal.cols);
		std::vector<Center2D> blobs, blo;
		finder.get_centers(signal, blobs);

		//do the same with minus the intensity
		signal.setTo(0);
		c=cl->begin();
		for(size_t i=0; i<cl->size(); ++i)
		{
			signal(0, i+margin) = -c->intensity;
			c++;
		}
		finder.get_centers(signal, blo);
		//insert the intensity blobs only if they do not overlap radius blobs
		blobs.reserve(blo.size());
		for(size_t b=0; b<blo.size(); ++b)
		{
			bool ok = true;
			for(size_t a=0; a<blobs.size() && ok; ++a)
				ok = !(std::abs(blo[b][0] - blobs[a][0]) < blo[b].r + blobs[a].r);
			if(ok)
				blobs.push_back(blo[b]);
		}

		for(std::vector<Center2D>::const_iterator b=blobs.begin(); b!=blobs.end(); ++b)
		{
			//get in the cluster just before the blob
			const size_t pos = (*b)[0];
			if(pos<margin || pos>cl->size()+margin)
				continue;
			const double frac = (*b)[0] - pos;
			Cluster::const_iterator it = cl->begin();
			std::advance(it, pos-margin);
			//add the center
			centers.push_back(*it);
			//centers.back()[2] = (*b)[0] - margin;
			//modulate by the next position (that may be closer to the blob position)
			it++;
			centers.back()[0] += frac * ((*it)[0] - centers.back()[0]);
			centers.back()[1] += frac * ((*it)[1] - centers.back()[1]);
			centers.back()[2] += frac * ((*it)[2] - centers.back()[2]) - 0.5;
			centers.back().r += frac * (it->r - centers.back().r);
			centers.back().intensity += frac * (it->intensity - centers.back().intensity);
		}
		/*if(blobs.empty())
		{
			//the signal is probably too short to localize a blob. We just take the maximum of the signal.
			centers.push_back(*std::max_element(cl->begin(), cl->end(), compare_radii<3>()));
		}*/
	}
}

void Reconstructor::links_by_brute_force(const Frame& fr, std::vector<double> &distances, std::vector<size_t> &from, std::vector<size_t> &to) const
{
	const size_t n = fr.size() * this->last_frame.size();
	distances.resize(n);
	from.resize(n);
	to.resize(n);
	std::vector<double>::iterator d = distances.begin();
	std::vector<size_t>::iterator i = from.begin(), j = to.begin();
	for(size_t f=0; f<this->last_frame.size(); ++f)
		for(size_t t=0; t<fr.size(); ++t)
		{
			*i++ = f;
			*j++ = t;
			*d++ = pow(this->last_frame[f][0] - fr[t][0], 2) + pow(this->last_frame[f][1] - fr[t][1], 2);
		}
}

inline RStarBoundingBox<2,double> get_bb(const Center2D &c, const double &tolerance=1.0)
{
	RStarBoundingBox<2,double> bb;
	bb.edges[0].first = c[0] - c.r * tolerance;
	bb.edges[1].first = c[1] - c.r * tolerance;
	bb.edges[0].second = c[0] + c.r * tolerance;
	bb.edges[1].second = c[1] + c.r * tolerance;
	return bb;
}
typedef RStarTree<size_t, 2, 4, 32, double> 	RTree;

struct Gatherer {
	std::list<size_t> *gathered;
	bool ContinueVisiting;

	Gatherer(std::list<size_t> &result) : gathered(&result), ContinueVisiting(true) {};

	void operator()(const RTree::Leaf * const leaf)
	{
		gathered->push_back(leaf->leaf);
	}
};
/**
 * \param tolerance Fraction of the contact distance (sum of radii) accepted. For tolerance<=1 accept overlap only.
 */
void Reconstructor::links_by_RStarTree(const Frame& fr, std::vector<double> &distances, std::vector<size_t> &from, std::vector<size_t> &to, const double &tolerance) const
{
	//(over)reserve memory
	const size_t n = 12 * max(fr.size(), this->last_frame.size());
	distances.clear();
	distances.reserve(n);
	from.clear();
	from.reserve(n);
	to.clear();
	to.reserve(n);

	//spatial index the new frame
	RTree tree;
	for(size_t p=0; p<fr.size(); ++p)
		tree.Insert(p, get_bb(fr[p]));

	//for each particle in previous frame, get all the particles in new frame that have an overlap with it
	for(size_t p=0; p<this->last_frame.size(); ++p)
	{
		std::list<size_t> ngb1;
		tree.Query(
				RTree::AcceptOverlapping(get_bb(this->last_frame[p], tolerance)),
				Gatherer(ngb1)
		);
		for(std::list<size_t>::const_iterator it= ngb1.begin(); it!=ngb1.end(); ++it)
		{
			const double dist = pow(this->last_frame[p][0] - fr[*it][0], 2) + pow(this->last_frame[p][1] - fr[*it][1], 2);
			if(dist < pow((this->last_frame[p].r + fr[*it].r) * tolerance, 2))
			{
				distances.push_back(dist);
				from.push_back(p);
				to.push_back(*it);
			}
		}
	}

}

}//namespace Colloids