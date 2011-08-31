#include "octavefinder.hpp"
#include <boost/array.hpp>
#include <algorithm>
#include <stdexcept>


using namespace std;

namespace Colloids {

std::map<size_t, cv::Mat_<double> > OctaveFinder::kernels;

const cv::Mat_<double>& OctaveFinder::get_kernel(const double &sigma)
{
	const int m = ((int)(sigma*4+0.5)*2 + 1)|1;
	//grab the kernel if it already exists within 1% precision
	std::map<size_t, cv::Mat_<double> >::iterator ker = kernels.find(100*sigma);
	if(ker==kernels.end())
		ker = kernels.insert(std::make_pair(
				100*sigma,
				cv::getGaussianKernel(m, sigma, CV_32F)
		)).first;
	return ker->second;
}

OctaveFinder::OctaveFinder(const int nrows, const int ncols, const int nbLayers, const double &preblur_radius):
		iterative_radii(nbLayers+2), iterative_gaussian_filters(nbLayers+2), sizes(nbLayers+3)
{
    this->layersG.reserve(nbLayers+3);
    for (int i = 0; i<nbLayers+3; ++i)
    	this->layersG.push_back(Image(nrows, ncols));
    this->layers.reserve(nbLayers+2);
	for (int i = 0; i<nbLayers+2; ++i)
		this->layers.push_back(Image(nrows, ncols));
	this->binary.reserve(nbLayers+2);
	for (int i = 0; i<nbLayers; ++i)
		this->binary.push_back(cv::Mat_<bool>::zeros(nrows, ncols));
	this->set_radius_preblur(preblur_radius);
}

OctaveFinder::~OctaveFinder()
{
    //dtor
}

void Colloids::OctaveFinder::set_radius_preblur(const double &k)
{
	this->preblur_radius = k;
	this->fill_iterative_radii(k);
}

void Colloids::OctaveFinder::fill(const cv::Mat &input)
{
	if(input.rows != this->get_width())
	{
		std::ostringstream os;
		os << "OctaveFinder::fill : the input's rows ("<<input.rows<<") must match the width of the finder ("<<this->get_width()<<")";
		throw std::invalid_argument(os.str().c_str());
	}
	if(input.cols != this->get_height())
	{
		std::ostringstream os;
		os << "OctaveFinder::fill : the input's cols ("<<input.cols<<") must match the height of the finder ("<<this->get_height()<<")";
		throw std::invalid_argument(os.str().c_str());
	}

	input.convertTo(this->layersG[0], this->layersG[0].type());
	//iterative Gaussian blur
	for(size_t i=0; i<this->layersG.size()-1; ++i)
		this->iterative_gaussian_filters[i].apply(
				this->layersG[i],
				this->layersG[i+1]
				);
	//difference of Gaussians
	for(size_t i=0; i<this->layers.size(); ++i)
		cv::subtract(this->layersG[i+1], this->layersG[i], this->layers[i]);
}

void Colloids::OctaveFinder::preblur_and_fill(const cv::Mat &input)
{
	if(input.rows != this->get_width())
	{
		std::ostringstream os;
		os << "OctaveFinder::preblur_and_fill : the input's rows ("<<input.rows<<") must match the width of the finder ("<<this->get_width()<<")";
		throw std::invalid_argument(os.str().c_str());
	}
	if(input.cols != this->get_height())
	{
		std::ostringstream os;
		os << "OctaveFinder::preblur_and_fill : the input's cols ("<<input.cols<<") must match the height of the finder ("<<this->get_height()<<")";
		throw std::invalid_argument(os.str().c_str());
	}
	Image blurred(input.size());
	cv::GaussianBlur(input, blurred, cv::Size(0,0), this->preblur_radius);
	this->fill(blurred);
}

/**
 * \brief Detect local minima of the scale space
 *
 * Uses the dynamic block algorythm by Neubeck and Van Gool
 * a. Neubeck and L. Van Gool, 18th International Conference On Pattern Recognition (ICPRʼ06) 850-855 (2006).
 */
void Colloids::OctaveFinder::initialize_binary(const double & max_ratio)
{
	const size_t nblayers = this->binary.size();
    //initialize
	this->centers_no_subpix.clear();
    for(size_t i = 0;i < nblayers;++i)
        this->binary[i].setTo(0);

	for(size_t k = 1;k < nblayers+1;k += 2)
	{
		const Image & layer0 = this->layers[k], layer1 = this->layers[k+1];
		const size_t si = this->sizes[k];
		for(size_t j = this->sizes[k]+1;j < (size_t)(((this->get_width() - si- 1)));j += 2)
		{
			boost::array<const float*, 4> ngb_ptr = {{
					&layer0(j, si+1),
					&layer0(j+1, si+1),
					&layer1(j, si+1),
					&layer1(j+1, si+1)
			}};
			for(size_t i = si+1;i < (size_t)(((this->get_height() -si - 1)));i += 2){
				//copy the whole neighbourhood together for locality
				boost::array<float, 8> ngb = {{
						*ngb_ptr[0]++, *ngb_ptr[0]++,
						*ngb_ptr[1]++, *ngb_ptr[1]++,
						*ngb_ptr[2]++, *ngb_ptr[2]++,
						*ngb_ptr[3]++, *ngb_ptr[3]++
				}};
				const boost::array<float, 8>::const_iterator mpos = std::min_element(ngb.begin(), ngb.end());
				if(*mpos>=0.0)
					continue;
				const int ml = mpos-ngb.begin();
				size_t mi = i + !!(ml&1);
				size_t mj = j + !!(ml&2);
				size_t mk = k + !!(ml&4);


				//maxima cannot be on the last layer or on image edges
				if(mk > nblayers || !((this->sizes[mk] <= mj) && (mj < (size_t)(((this->get_width() - this->sizes[mk])))) && (this->sizes[mk] <= mi) && (mi < (size_t)(((this->get_height() - this->sizes[mk]))))))
					continue;

				bool *b = &this->binary[mk - 1](mj, mi);
				//consider only negative minima
				//with a value that is actually different from zero
				*b = (*mpos < 0) && (1 + pow(*mpos, 2) > 1);
				//remove the minima if one of its neighbours outside the block has lower value
				for(size_t k2 = mk - 1;k2 < mk + 2 && *b;++k2)
					for(size_t j2 = mj - 1;j2 < mj + 2 && *b;++j2)
						for(size_t i2 = mi - 1;i2 < mi + 2 && *b;++i2)
							if(k2 < k || j2 < j || i2 < i || k2 > k + 1 || j2 > j + 1 || i2 > i + 1)
								*b = *mpos <= this->layers[k2](j2, i2);




				//remove the local minima that are edges (elongated objects)
				if(*b){
					//hessian matrix
					const double hess[3] = {this->layers[mk](mj - 1, mi) - 2 * this->layers[mk](mj, mi) + this->layers[mk](mj + 1, mi), this->layers[mk](mj, mi - 1) - 2 * this->layers[mk](mj, mi) + this->layers[mk](mj, mi + 1), this->layers[mk](mj - 1, mi - 1) + this->layers[mk](mj + 1, mi + 1) - this->layers[mk](mj + 1, mi - 1) - this->layers[mk](mj - 1, mi + 1)};
					//determinant of the Hessian, for the coefficient see
					//H Bay, a Ess, T Tuytelaars, and L Vangool,
					//Computer Vision and Image Understanding 110, 346-359 (2008)
					const double detH = hess[0] * hess[1] - pow(hess[2], 2), ratio = pow(hess[0] + hess[1], 2) / (4.0 * hess[0] * hess[1]);
					*b = !((detH < 0 && 1+detH*detH > 1) || ratio > max_ratio);
					if(*b){
						cv::Vec3i c;
						c[0] = mi;
						c[1] = mj;
						c[2] = mk;
						this->centers_no_subpix.push_back(c);
					}
				}
			}
		}
	}
}
/**
 * \brief Detect local minima of the scale space
 *
 * Uses the dynamic block algorythm by Neubeck and Van Gool
 * a. Neubeck and L. Van Gool, 18th International Conference On Pattern Recognition (ICPRʼ06) 850-855 (2006).
 */
void Colloids::OctaveFinder1D::initialize_binary(const double & max_ratio)
{
    //initialize
	this->centers_no_subpix.clear();
    for(size_t i = 0;i < this->binary.size();++i)
        this->binary[i].setTo(0);

	for(size_t k = 1;k < (size_t)(((this->layers.size() - 1)));k += 2)
	{
		boost::array<const float*, 2> ngb_ptr = {{
							&this->layers[k](0, this->sizes[k]+1),
							&this->layers[k+1](0, this->sizes[k]+1)
		}};
		for(size_t i = this->sizes[k]+1;i < (size_t)(((this->get_height() -this->sizes[k]- 1)));i += 2)
		{
			//copy the whole neighbourhood together for locality
			boost::array<float, 4> ngb = {{
					*ngb_ptr[0]++, *ngb_ptr[0]++,
					*ngb_ptr[1]++, *ngb_ptr[1]++
			}};
			const boost::array<float, 4>::const_iterator mpos = std::min_element(ngb.begin(), ngb.end());
			if(*mpos>=0.0)
					continue;
			const int ml = mpos-ngb.begin();
			size_t mi = i + !!(ml&1);
			size_t mk = k + !!(ml&2);
			//maxima cannot be on the last layer or on image edges
			if(mk > this->binary.size() || !((this->sizes[mk] <= mi) && (mi < (size_t)(((this->get_height() - this->sizes[mk]))))))
				continue;
			bool *b = &this->binary[mk - 1](0, mi);
			*b = (*mpos < 0) && (1 + pow(*mpos, 2) > 1);

			//remove the minima if one of its neighbours outside the block has lower value
			for(size_t k2 = mk - 1;k2 < mk + 2 && *b;++k2)
				for(size_t i2 = mi - 1;i2 < mi + 2 && *b;++i2)
					if(k2 < k || i2 < i || k2 > k + 1 || i2 > i + 1)
						*b = this->layers[mk](0, mi) <= this->layers[k2](0, i2);
			//Eliminating edge response in 1D is more tricky
			//We look for pixels where the ratio between the Laplacian and gradient values is large
			if(*b)
			{
				const float * v = &this->layersG[mk](0, mi);
				*b = std::abs((*(v+1) + *(v-1) - 2 * *v) / (*(v+1) - *(v-1))) > 0.5;
			}
			if(*b){
				cv::Vec3i c;
				c[0] = mi;
				c[1] = 0;
				c[2] = mk;
				this->centers_no_subpix.push_back(c);
			}
		}
	}
}

Center2D Colloids::OctaveFinder::spatial_subpix(const cv::Vec3i & ci) const
{
        const size_t i = ci[0], j = ci[1], k = ci[2];
        Center2D c(0.0);
        cv::Mat_<double> hess(2,2), hess_inv(2,2), grad(2,1), d(2,1), u(1,1);
        //When particles environment is strongly asymmetric (very close particles),
        //it is better to find the maximum of Gausian rather than the minimum of DoG.
        //If possible, we use the Gaussian layer below the detected scale
        //to have better spatial resolution
        const Image & l = (k>0 ? this->layersG[k-1] : this->layersG[k]);
        const double a[4] = {
        		(l(j, i+1) - l(j, i-1))/2.0,
        		(l(j+1, i) - l(j-1, i))/2.0,
        		l(j, i+1) -2*l(j, i) + l(j, i-1),
        		l(j+1, i) -2*l(j, i) + l(j-1, i)
        };
        c[0] = i + 0.5 - (a[2]==0 ? 0 : a[0]/a[2]);
		c[1] = j + 0.5 - (a[3]==0 ? 0 : a[1]/a[3]);
		c.intensity = this->layers[k](j, i);
		return c;
}
Center2D Colloids::OctaveFinder1D::spatial_subpix(const cv::Vec3i & ci) const
{
        const size_t i = ci[0], k = ci[2];
        Center2D c(0.0);
        //When particles environment is strongly asymmetric (very close particles),
		//it is better to find the maximum of Gausian rather than the minimum of DoG.
		//If possible, we use the Gaussian layer below the detected scale
		//to have better spatial resolution
        const Image & l = (k>0 ? this->layersG[k-1] : this->layersG[k]);
        c[0] = ci[0] + 0.5 - (l(0, i+1) - l(0, i-1)) / 2.0 / (l(0, i+1) -2*l(0, i) + l(0, i-1));
        c[1] = 0;
        c.intensity = this->layers[k](0, i) - 0.25 * (c[0]-i) * (l(0, i+1) - l(0, i-1));
        return c;
}

double Colloids::OctaveFinder::gaussianResponse(const size_t & j, const size_t & i, const double & scale) const
{
        if(scale < 0)
            throw std::invalid_argument("Colloids::OctaveFinder::gaussianResponse: the scale must be positive.");

        size_t k = (size_t)(scale);
        if (k>=this->layersG.size())
        	k = this->layersG.size()-1;
		if((scale - k) * (scale - k) + 1 == 1)
			return this->layersG[k](j, i);
		const double sigma = this->get_iterative_radius(scale, (double)k);
		//opencv is NOT dealing right with ROI (even if boasting about it), so we do it by hand
		const cv::Mat_<double>& kernel = get_kernel(sigma);
		const int m = kernel.rows;
		vector<double> gx(m, 0.0);
		const int xmin = max(0, (int)i+m/2+1-this->get_width()),
				xmax = min(m, (int)i+m/2+1),
				ymin = max(0, (int)j+m/2+1-this->get_height()),
				ymax = min(m, (int)j+m/2+1);
		const double *ker = &kernel(ymin,0);
		for(int y=ymin; y<ymax; ++y)
		{
			const OctaveFinder::PixelType * v = &layersG[k](j-y+m/2, i-xmin+m/2);
			for(int x=xmin; x<xmax; ++x)
				gx[x] += *v-- * *ker;
			ker++;
		}

		double resp = 0.0;
		ker = &kernel(0,0);
		for(int x=0; x<m; ++x)
			resp += gx[x] * *ker++;

        return resp;
}

double Colloids::OctaveFinder1D::gaussianResponse(const size_t & j, const size_t & i, const double & scale) const
{
	if(scale < 0)
		throw std::invalid_argument("Colloids::OctaveFinder::gaussianResponse: the scale must be positive.");

	size_t k = (size_t)(scale);
	if (k>=this->layersG.size())
		k = this->layersG.size()-1;
	if((scale - k) * (scale - k) + 1 == 1)
		return this->layersG[k](j, i);
	const double sigma = this->get_iterative_radius(scale, (double)k);
	//opencv is NOT dealing right with ROI (even if boasting about it), so we do it by hand
	const cv::Mat_<double>& kernel = get_kernel(sigma);
	const int m = kernel.rows;
	double resp = 0.0;
	for(int x=0; x<m; ++x)
		resp += layersG[k](0, cv::borderInterpolate(i-x+m/2, this->get_height(), cv::BORDER_DEFAULT)) * kernel(x,0);
	return resp;
}

double Colloids::OctaveFinder::scale_subpix(const cv::Vec3i & ci) const
{
    	const size_t i = ci[0], j = ci[1], k = ci[2];
		//scale is better defined if we consider only the central pixel at different scales
		//Compute intermediate variables to do a quadratic estimate of the derivative
		boost::array<double,8> sublayerG;
		for(size_t u = 0;u < sublayerG.size(); ++u)
			sublayerG[u] = this->gaussianResponse(j, i, k - 1 + 0.5*u);

		boost::array<double,5> a;
		for(size_t u =0; u<a.size();++u)
			a[u] = sublayerG[u+2] - sublayerG[u];
		//Apply newton's method using quadratic estimate of the derivative
		double s = k - (-a[4] + 8*a[3] - 8*a[1] + a[0])/6.0 /(a[4]-2*a[2]+a[0]);
		//saturate the results
		if(s>k+0.5)
			s= k + 0.5;
		if(s<k-0.5)
			s= k- 0.5;
		//second round of newton's method
		if(s>=1)
		{
			if(s+0.1<k)
			{
				s= k- 0.5;
			for(size_t u = 0;u < sublayerG.size(); ++u)
				sublayerG[u] = this->gaussianResponse(j, i, s - 1 + 0.5*u);
			for(size_t u =0; u<a.size();++u)
				a[u] = sublayerG[u+2] - sublayerG[u];
			s -= (-a[4] + 8*a[3] - 8*a[1] + a[0])/6.0 /(a[4]-2*a[2]+a[0]);
			}
		}
		else
		{
			//for sizes significantly below the sampled scale
			//the linear estimate of the derivative is (marginally) better
			if(s+0.25<k)
				s = k - (a[3] - a[1])/(a[4]-2*a[2]+a[0]);
		}
		if(s<k-0.5)
			s= k- 0.5;
		if(s>k+0.5)
			s= k + 0.5;
		return s;
}
double Colloids::OctaveFinder1D::scale_subpix(const cv::Vec3i & ci) const
{
	//Empirical correction
	//return ci[2] + 1.1*(OctaveFinder::scale_subpix(ci)-ci[2]) - 0.1/ci[2]- 0.1;//-0.025*this->layers.size();
	const size_t i = ci[0], j = ci[1], k = ci[2];
	double h = 1.0/3.0;
	boost::array<double,7> a;
	for(int u=0; u<(int)a.size();++u)
		a[u] = this->gaussianResponse(j, i, k - 3*h + u*h);
	double s = 2*h * (a[5] -2*a[3] + a[1])/(a[6] -3*a[4] +3*a[2] -a[0]);
	return k - 1.05*s + 0.08*pow(s,2) - pow(2,-2/(double)this->get_n_layers())+0.025*k -0.025;
}

	void Colloids::OctaveFinder::single_subpix(const cv::Vec3i & ci, Center2D &c) const
    {
        c = this->spatial_subpix(ci);
        c.r = this->scale_subpix(ci);
    }

    void Colloids::OctaveFinder::subpix(std::vector<Center2D> &centers) const
    {
        centers.clear();
        centers.resize(this->centers_no_subpix.size());
        //subpixel resolution in pixel units
        std::list<cv::Vec3i>::const_iterator ci = this->centers_no_subpix.begin();
        for(size_t c=0; c<centers.size(); ++c)
        	this->single_subpix(*ci++, centers[c]);
    }

    std::vector<Center2D> Colloids::OctaveFinder::operator ()(const cv::Mat & input, const bool preblur)
    {
        if(preblur)
            this->preblur_and_fill(input);

        else
            this->fill(input);

        this->initialize_binary();
        std::vector<Center2D> centers;
        this->subpix(centers);
        for(size_t c=0; c<centers.size(); ++c)
        	this->scale(centers[c]);
        return centers;
    }
    const double OctaveFinder::get_iterative_radius(const double & larger, const double & smaller) const
    {
    	return this->preblur_radius * sqrt(pow(2.0, 2.0*larger/this->get_n_layers()) - pow(2.0, 2.0*smaller/this->get_n_layers()));
    }
/**
 * \brief Eliminate pixel centers duplicated at the seam between two Octaves
 */
void OctaveFinder::seam_binary(OctaveFinder & other)
{
	const double sizefactor = this->get_height()/other.get_height();
	OctaveFinder & a = sizefactor>1?(*this):other, &b = sizefactor>1?other:(*this);
	const double sf = sizefactor>1?sizefactor:(1.0/sizefactor);
	//centers in a that corresponds to a lower intensity in b
	std::list<cv::Vec3i>::iterator c = a.centers_no_subpix.begin();
	while(c!=a.centers_no_subpix.end())
	{
		if(
			((*c)[2] == a.get_n_layers()) &&
			(b.binary.front()((*c)[1]/sf+0.5, (*c)[0]/sf+0.5)) &&
			(a.layers[(*c)[2]]((*c)[1], (*c)[0]) > b.layers[1]((*c)[1]/sf+0.5, (*c)[0]/sf+0.5))
			)
		{
			a.binary[(*c)[2]-1]((*c)[1], (*c)[0]) = false;
			c = a.centers_no_subpix.erase(c);
		}
		else c++;
	}
	//centers in b that corresponds to a lower intensity in a
	c = b.centers_no_subpix.begin();
	while(c!=b.centers_no_subpix.end())
	{
		if(
			((*c)[2] == 1) &&
			(a.binary.back()((*c)[1]*sf+0.5, (*c)[0]*sf+0.5)) &&
			(b.layers[1]((*c)[1], (*c)[0]) > a.layers[a.get_n_layers()]((*c)[1]*sf+0.5, (*c)[0]*sf+0.5))
			)
		{
			b.binary.front()((*c)[1], (*c)[0]) = false;
			c = b.centers_no_subpix.erase(c);
		}
		else c++;
	}

}

void Colloids::OctaveFinder::fill_iterative_radii(const double & k)
{
		//target blurring radii
        vector<double> sigmas(this->get_n_layers()+3);
        for(size_t i=0; i<sigmas.size(); ++i)
        	sigmas[i] = k * pow(2, i/double(this->get_n_layers()));
        //corresponding blob sizes
        const double n = this->get_n_layers();
        if(this->get_height()==1 || this->get_width()==1)
        	this->prefactor = sqrt(2.0 * log(2.0) / n / (pow(2.0, 2.0 / n) - 1));
        else
        	this->prefactor = 2.0 * sqrt(log(2.0) / n / (pow(2.0, 2.0 / n) - 1));
        vector<size_t>::iterator si = this->sizes.begin();
        for(vector<double>::const_iterator sig=sigmas.begin(); sig!=sigmas.end(); sig++)
        	*si++ = static_cast<size_t>((*sig * prefactor) + 0.5);
        //iterative blurring radii
        transform(sigmas.begin(), sigmas.end(), sigmas.begin(), sigmas.begin(), multiplies<double>());
        for(size_t i=0; i<this->iterative_radii.size(); ++i)
        	this->iterative_radii[i] = sqrt(sigmas[i+1] - sigmas[i]);
        //iterative gaussian filters
        for(size_t i=0; i<this->iterative_radii.size(); ++i)
        	this->iterative_gaussian_filters[i] = *cv::createGaussianFilter
        	(
        			this->layersG[0].type(),
        			cv::Size(0,0),
        			this->iterative_radii[i]
        	);
}



}; //namespace





