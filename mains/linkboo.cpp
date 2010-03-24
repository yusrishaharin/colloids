/**
    Copyright 2008,2009,2010 Mathieu Leocmach

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

#include "../dynamicParticles.hpp"
#include <boost/progress.hpp>

using namespace std;
using namespace Colloids;

int main(int argc, char ** argv)
{
	try
    {
		if(argc<5)
		{
			cerr<<"syntax: linkboo [path]filename token delta_t span [offset=0]"<<endl;
			return EXIT_FAILURE;
		}

		const string filename(argv[1]), token(argv[2]);
		const double delta_t = atof(argv[3]);
		const size_t span = atoi(argv[4]);
		const size_t offset = (argc>5)?atoi(argv[5]):0;

		cout<<filename.substr(filename.find_last_of("/\\")+1)<<endl;

		//create the needed file series
		FileSerie datSerie(filename, token, span, offset),
				bondSerie = datSerie.changeExt(".bonds"),
				qlmSerie = datSerie.changeExt(".qlm"),
				cloudSerie = datSerie.changeExt(".cloud"),
				cgCloudSerie = datSerie.addPostfix("_space", ".cloud"),
				outsideSerie = datSerie.changeExt(".outside"),
				secondOutsideSerie = datSerie.changeExt(".outside2"),;

		cout<<"load ..."<<endl;
		//load all files in memory with default radius of 1.0
		//by the way, check file existence
		boost::ptr_vector<Particles> positions(span);
		for(size_t t=0; t<span; ++t)
			positions.push_back(new Particles(datSerie%t));

		//spatially index each frame
		cout<<"index ..."<<endl;
		#pragma omp parallel for shared(positions) schedule(runtime)
		for(int t=0; t<(int)span; ++t)
			positions[t].makeRTreeIndex();

		//get averaged g(r)
		vector<double> total_g(200, 0.0);
		{
			ifstream in((datSerie.head()+".rdf").c_str());
			if(in.good())
			{
				cout<<"load "<<datSerie.head()+".rdf"<<endl;
				string skipheader;
				getline(in, skipheader);
				for(size_t r=0; r<total_g.size(); ++r)
					in >> total_g[r] >> total_g[r];
			}
			else
			{
				cout<<"calculate rdf and save it to "<<datSerie.head()+".rdf"<<endl;
				boost::progress_display show_pr(span);
				boost::progress_timer ti;
				//#pragma omp parallel for shared(positions, total_g)
				for(int t=0; t<(int)span; ++t)
				{
					vector<double> g = positions[t].getRdf(200,15.0);
					for(int r=0; r<g.size(); ++r)
						total_g[r] += g[r];
					++show_pr;
				}
				ofstream rdfFile((datSerie.head()+".rdf").c_str(), ios::out | ios::trunc);
				rdfFile << "#r\tg(r)"<<endl;
				for(size_t r=0; r<total_g.size(); ++r)
					rdfFile<< r/200.0*15.0 <<"\t"<< total_g[r]/span <<"\n";
				rdfFile.close();
			}
		}
		//get bondlength and radius from the first minimum of g(r)
		//the loop is here only to get rid of possible multiple centers at small r
		vector<double>::iterator first_peak = total_g.begin();
		size_t first_min;
		do
		{
			first_peak = max_element(total_g.begin(),total_g.end());
			first_min = distance(total_g.begin(), min_element(first_peak,total_g.end()));
			//cout<<"first_peak="<<distance(total_g.begin(), first_peak)<<" first_min="<<first_min<<" ... ";
		}
		while(total_g[first_min]==0.0);
		const double bondLength = first_min/200.0*15.0, radius = bondLength / 1.3;
		cout<<"radius="<<radius<<endl;

		//check the existence of outside and bonds files
		const bool voro = ifstream((outsideSerie%0).c_str()).good()
			&& ifstream((outsideSerie%(span-1)).c_str()).good()
			&& ifstream((secondOutsideSerie%0).c_str()).good()
			&& ifstream((secondOutsideSerie%(span-1)).c_str()).good()
			&& ifstream((bondSerie%0).c_str()).good()
			&& ifstream((bondSerie%(span-1)).c_str()).good();
		if(voro)
			cout<<"using voro++ output"<<endl;


		//treat each file
		cout<<"neighbourlist and BOO at each time step"<<endl;
		boost::progress_display *show_progress;
		#pragma omp parallel shared(positions, bondLength, show_progress)
		{
		#pragma omp single
		show_progress = new boost::progress_display(span);
		#pragma omp for schedule(runtime)
		for(int t=0; t<(int)span; ++t)
		{
			BondSet bonds;
			vector<size_t> inside, secondInside;
			inside.reserve(positions[t].size());
			secondInside.reserve(positions[t].size());
			//if .outside files are present, load bonds and insides
			if(voro)
			{
				bonds = loadBonds(bondSerie%t);
				positions[t].makeNgbList(bonds);

				vector<size_t> all(positions[t].size());
				for(size_t p=0; p<all.size();++p)
					all[p]=p;

				ifstream outsideFile((outsideSerie%t).c_str());
				set_difference(
					all.begin(), all.end(),
					istream_iterator<size_t>(outsideFile), istream_iterator<size_t>(),
					back_inserter(inside)
					);

				ifstream secondOutsideFile((secondOutsideSerie%t).c_str());
				set_difference(
					all.begin(), all.end(),
					istream_iterator<size_t>(secondOutsideFile), istream_iterator<size_t>(),
					back_inserter(secondInside)
					);
			}
			else
			{
				//create neighbour list and export bonds
				positions[t].makeNgbList(bondLength);
				bonds = positions[t].getBonds();
				ofstream bondFile((bondSerie%t).c_str(), ios::out | ios::trunc);
				copy(bonds.begin(), bonds.end(), ostream_iterator<Bond>(bondFile, "\n"));
				bondFile.close();

				//select the particles further than the bond length from the boundaries
				inside = positions[t].selectInside(bondLength);
				secondInside = positions[t].selectInside(2.0*bondLength);
			}
			//calculate and export qlm
			vector<BooData> qlm, qlm_cg;
			positions[t].getBOOs(inside, qlm);
			positions[t].getCgBOOs(secondInside, qlm, qlm_cg);
			ofstream qlmFile((qlmSerie%t).c_str(), ios::out | ios::trunc);
			copy(
				qlm_cg.begin(), qlm_cg.end(),
				ostream_iterator<BooData>(qlmFile,"\n")
				);


			//calculate and export invarients
			ofstream cloudFile((cloudSerie%t).c_str(), ios::out | ios::trunc);
			cloudFile<<"#Q4\tQ6\tW4\tW6"<<endl;
			transform(
				qlm.begin(), qlm.end(),
				ostream_iterator<string>(cloudFile,"\n"),
				cloud_exporter()
				);
			cloudFile.close();

			ofstream cloud_cgFile((cgCloudSerie%t).c_str(), ios::out | ios::trunc);
			cloud_cgFile<<"#Q4\tQ6\tW4\tW6"<<endl;
			transform(
				qlm_cg.begin(), qlm_cg.end(),
				ostream_iterator<string>(cloud_cgFile,"\n"),
				cloud_exporter()
				);
			cloud_cgFile.close();

			//update radius
			positions[t].radius = radius;
			//remove neigbour list from memory (can be heavy)
			positions[t].delNgbList();
			++(*show_progress);
		}
		}

		//link and save trajectories
		DynamicParticles parts(positions, radius, delta_t, datSerie.head()+".displ", offset);
		parts.save(
			datSerie.head()+".traj",
			filename.substr(filename.find_last_of("/\\")+1),
			token, offset, span
			);
	}
    catch(const exception &e)
    {
        cerr<< e.what()<<endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

