#include "LocalWlNear.h"
#include "wigner3j.h"
#include <stdexcept>
#include <complex>
#include <algorithm>
#include <boost/math/special_functions/spherical_harmonic.hpp>

using namespace std;

/*! \file LocalWlNear.cc
    \brief Compute a Wl per particle using the number of nearest neighbors.  Returns NaN if no neighbors.
*/

namespace freud { namespace order {

LocalWlNear::LocalWlNear(const box::Box& box, float rmax, unsigned int l, unsigned int kn)
    :m_box(box), m_rmax(rmax), m_l(l), m_k(kn)
    {
    if (m_rmax < 0.0f)
        throw invalid_argument("rmax must be positive!");
    if (m_l < 2)
        throw invalid_argument("l must be two or greater (and even)!");
    if (m_l%2 == 1)
        {
        fprintf(stderr,"Current value of m_l is %d\n",m_l);
        throw invalid_argument("This method requires even values of l!");
        }
    m_normalizeWl = false;
    m_nn = new locality::NearestNeighbors(m_rmax, m_k);
    }

LocalWlNear::~LocalWlNear()
    {
    delete m_nn;
    }

void LocalWlNear::Ylm(const float theta, const float phi, std::vector<std::complex<float> > &Y)
    {
    if(Y.size() != 2*m_l+1)
        Y.resize(2*m_l+1);

    for(int m = -m_l; m <=0; m++)
        //Doc for boost spherical harmonic
        //http://www.boost.org/doc/libs/1_53_0/libs/math/doc/sf_and_dist/html/math_toolkit/special/sf_poly/sph_harm.html
        // theta = colatitude = 0..Pi
        // phi = azimuthal (longitudinal) 0..2pi).
        Y[m+m_l]= boost::math::spherical_harmonic(m_l, m, theta, phi);

    for(unsigned int i = 1; i <= m_l; i++)
        Y[i+m_l] = Y[-i+m_l];
    }

void LocalWlNear::compute(const vec3<float> *points, unsigned int Np)
    {
    //Get wigner3j coefficients from wigner3j.cc
    int m_wignersize[10]={19,61,127,217,331,469,631,817,1027,1261};
    std::vector<float> m_wigner3jvalues (m_wignersize[m_l/2-1]);
    m_wigner3jvalues = getWigner3j(m_l);

    //Set local data size
    m_Np = Np;

    //Initialize neighbor list
    m_nn->compute(m_box,points,m_Np,points,m_Np);

    float rmaxsq = m_rmax * m_rmax;

    //newmanrs:  For efficiency, if Np != m_Np, we could not reallocate these! Maybe.
    // for safety and debugging laziness, reallocate each time
    m_Qlmi = boost::shared_array<complex<float> >(new complex<float> [(2*m_l+1)*m_Np]);
    m_Qli = boost::shared_array<float>(new float[m_Np]);
    m_Wli = boost::shared_array<complex<float> >(new complex<float>[m_Np]);
    m_Qlm = boost::shared_array<complex<float> >(new complex<float>[2*m_l+1]);
    memset((void*)m_Qlmi.get(), 0, sizeof(complex<float>)*(2*m_l+1)*m_Np);
    memset((void*)m_Wli.get(), 0, sizeof(complex<float>)*m_Np);
    memset((void*)m_Qlm.get(), 0, sizeof(complex<float>)*(2*m_l+1));
    memset((void*)m_Qli.get(), 0, sizeof(float)*m_Np);

    for (unsigned int i = 0; i<m_Np; i++)
        {
        //get cell point is in
        vec3<float> ref = points[i];
        boost::shared_array<unsigned int> neighbors = m_nn->getNeighbors(i);

        //loop over neighboring cells
        for (unsigned int neigh_idx = 0; neigh_idx < m_k; neigh_idx++)
            {
            unsigned int j = neighbors[neigh_idx];

            // compute r between the two particles
            vec3<float> delta = m_box.wrap(points[j] - ref);
            float rsq = dot(delta, delta);

            if (rsq > 1e-6)
                {
                float phi = atan2(delta.y,delta.x);      //0..2Pi
                float theta = acos(delta.z / sqrt(rsq)); //0..Pi

                std::vector<std::complex<float> > Y;
                LocalWlNear::Ylm(theta, phi,Y);  //Fill up Ylm vector
                for(unsigned int k = 0; k < (2*m_l+1); ++k)
                    {
                    // change to Index later
                    m_Qlmi[(2*m_l+1)*i+k]+=Y[k];
                    }
                }

            } //End loop going over neighbor cells (and thus all neighboring particles);
        //Normalize!
        for(unsigned int k = 0; k < (2*m_l+1); ++k)
            {
            m_Qlmi[(2*m_l+1)*i+k]/= m_k;
            m_Qli[i]+=abs( m_Qlmi[(2*m_l+1)*i+k]*conj(m_Qlmi[(2*m_l+1)*i+k]) );
            m_Qlm[k]+= m_Qlmi[(2*m_l+1)*i+k];
            } //Ends loop over particles i for Qlmi calcs
        m_Qli[i]=sqrt(m_Qli[i]);//*sqrt(m_Qli[i])*sqrt(m_Qli[i]);//Normalize factor for Wli

        //Wli calculation
        unsigned int counter = 0;
        for(unsigned int u1 = 0; u1 < (2*m_l+1); ++u1)
            {
            for(unsigned int u2 = max( 0,int(m_l)-int(u1)); u2 < (min(3*m_l+1-u1,2*m_l+1)); ++u2)
                {
                unsigned int u3 = 3*m_l-u1-u2;
                m_Wli[i] += m_wigner3jvalues[counter]*m_Qlmi[(2*m_l+1)*i+u1]*m_Qlmi[(2*m_l+1)*i+u2]*m_Qlmi[(2*m_l+1)*i+u3];
                counter+=1;
                }
            }//Ends loop for Wli calcs
        if(m_normalizeWl)
            {
            m_Wli[i]/=(m_Qli[i]*m_Qli[i]*m_Qli[i]);//Normalize
            }
        m_counter = counter;
        }
    }

void LocalWlNear::computeAve(const vec3<float> *points, unsigned int Np)
    {

    //Get wigner3j coefficients from wigner3j.cc
    int m_wignersize[10]={19,61,127,217,331,469,631,817,1027,1261};
    std::vector<float> m_wigner3jvalues (m_wignersize[m_l/2-1]);
    m_wigner3jvalues = getWigner3j(m_l);

    //Set local data size
    m_Np = Np;

    //Initialize neighbor list
    m_nn->compute(m_box,points,m_Np,points,m_Np);

    float rmaxsq = m_rmax * m_rmax;
    float normalizationfactor = 4*M_PI/(2*m_l+1);


    //newmanrs:  For efficiency, if Np != m_Np, we could not reallocate these! Maybe.
    // for safety and debugging laziness, reallocate each time
    m_AveQlmi = boost::shared_array<complex<float> >(new complex<float> [(2*m_l+1)*m_Np]);
    m_AveQlm = boost::shared_array<complex<float> >(new complex<float> [(2*m_l+1)]);
    m_AveWli = boost::shared_array<complex<float> >(new complex<float> [m_Np]);
    memset((void*)m_AveQlmi.get(), 0, sizeof(complex<float>)*(2*m_l+1)*m_Np);
    memset((void*)m_AveQlm.get(), 0, sizeof(complex<float>)*(2*m_l+1));
    memset((void*)m_AveWli.get(), 0, sizeof(float)*m_Np);

    for (unsigned int i = 0; i<m_Np; i++)
        {
        //get cell point is in
        // float3 ref = points[i];
        vec3<float> ref = points[i];
        unsigned int neighborcount=1;
        boost::shared_array<unsigned int> neighbors = m_nn->getNeighbors(i);

        //loop over neighboring cells
        for (unsigned int neigh_idx = 0; neigh_idx < m_k; neigh_idx++)
            {
            //get cell points of 1st neighbor
            unsigned int j = neighbors[neigh_idx];

            //iterate over particles in neighboring cells
            if (j == i)
                {
                continue;
                }

            vec3<float> ref1 = points[j];
            vec3<float> delta = m_box.wrap(points[j] - ref);

            float rsq = dot(delta, delta);
            if (rsq > 1e-6)
                {
                boost::shared_array<unsigned int> neighbors_2 = m_nn->getNeighbors(j);

                //loop over 2nd neighboring cells
                for (unsigned int neigh1_idx = 0; neigh1_idx < m_k; neigh1_idx++)
                    {
                    //get cell points of 2nd neighbor
                    unsigned int n1 = neighbors_2[neigh1_idx];

                    if (n1 == j)
                        {
                        continue;
                        }

                    vec3<float> delta1 = m_box.wrap(points[n1] - ref1);
                    float rsq1 = dot(delta1, delta1);

                    if (rsq1 > 1e-6)
                        {
                        for(unsigned int k = 0; k < (2*m_l+1); ++k)
                            {
                            m_AveQlmi[(2*m_l+1)*i+k] += m_Qlmi[(2*m_l+1)*n1+k];
                            }
                         neighborcount++;
                         }
                     }
                }
            }
         //Normalize!
        for (unsigned int k = 0; k < (2*m_l+1); ++k)
            {
                m_AveQlmi[(2*m_l+1)*i+k] += m_Qlmi[(2*m_l+1)*i+k];
                m_AveQlmi[(2*m_l+1)*i+k]/= neighborcount;
                m_AveQlm[k] += m_AveQlmi[(2*m_l+1)*i+k];
            }
        //Ave Wli calculation
        unsigned int counter = 0;
        for(unsigned int u1 = 0; u1 < (2*m_l+1); ++u1)
            {
            for(unsigned int u2 = max( 0,int(m_l)-int(u1)); u2 < (min(3*m_l+1-u1,2*m_l+1)); ++u2)
                {
                unsigned int u3 = 3*m_l-u1-u2;
                m_AveWli[i]+= m_wigner3jvalues[counter]*m_AveQlmi[(2*m_l+1)*i+u1]*m_AveQlmi[(2*m_l+1)*i+u2]*m_AveQlmi[(2*m_l+1)*i+u3];
                counter+=1;
                }
            }//Ends loop for Norm Wli calcs
        m_counter = counter;

        } //Ends loop over particles i for Qlmi calcs
    }

// void LocalWl::computeNorm(const float3 *points, unsigned int Np)
void LocalWlNear::computeNorm(const vec3<float> *points, unsigned int Np)
    {

    //Get wigner3j coefficients from wigner3j.cc
    int m_wignersize[10]={19,61,127,217,331,469,631,817,1027,1261};
    std::vector<float> m_wigner3jvalues (m_wignersize[m_l/2-1]);
    m_wigner3jvalues = getWigner3j(m_l);

    //Set local data size
    m_Np = Np;

    m_WliNorm = boost::shared_array<complex<float> >(new complex<float>[m_Np]);
    memset((void*)m_WliNorm.get(), 0, sizeof(complex<float>)*m_Np);

    //Average Q_lm over all particles, which was calculated in compute
    for(unsigned int k = 0; k < (2*m_l+1); ++k)
        {
        m_Qlm[k]/= m_Np;
        }

    for(unsigned int i = 0; i < m_Np; ++i)
        {
        //Norm Wli calculation
        unsigned int counter = 0;
        for(unsigned int u1 = 0; u1 < (2*m_l+1); ++u1)
            {
            for(unsigned int u2 = max( 0,int(m_l)-int(u1)); u2 < (min(3*m_l+1-u1,2*m_l+1)); ++u2)
                {
                unsigned int u3 = 3*m_l-u1-u2;
                m_WliNorm[i]+= m_wigner3jvalues[counter]*m_Qlm[u1]*m_Qlm[u2]*m_Qlm[u3];
                counter+=1;
                }
            }//Ends loop for Norm Wli calcs
        m_counter = counter;
        }
    }

void LocalWlNear::computeAveNorm(const vec3<float> *points, unsigned int Np)
    {

    //Get wigner3j coefficients from wigner3j.cc
    int m_wignersize[10]={19,61,127,217,331,469,631,817,1027,1261};
    std::vector<float> m_wigner3jvalues (m_wignersize[m_l/2-1]);
    m_wigner3jvalues = getWigner3j(m_l);

    //Set local data size
    m_Np = Np;

    m_WliAveNorm = boost::shared_array<complex<float> >(new complex<float>[m_Np]);
    memset((void*)m_WliAveNorm.get(), 0, sizeof(complex<float>)*m_Np);

    //Average Q_lm over all particles, which was calculated in compute
    for(unsigned int k = 0; k < (2*m_l+1); ++k)
        {
        m_AveQlm[k]/= m_Np;
        }

    for(unsigned int i = 0; i < m_Np; ++i)
        {
        //Norm Wli calculation
        unsigned int counter = 0;
        for(unsigned int u1 = 0; u1 < (2*m_l+1); ++u1)
            {
            for(unsigned int u2 = max( 0,int(m_l)-int(u1)); u2 < (min(3*m_l+1-u1,2*m_l+1)); ++u2)
                {
                unsigned int u3 = 3*m_l-u1-u2;
                m_WliAveNorm[i]+= m_wigner3jvalues[counter]*m_AveQlm[u1]*m_AveQlm[u2]*m_AveQlm[u3];
                counter+=1;
                }
            }//Ends loop for Norm Wli calcs
        m_counter = counter;
        }
    }


// //python wrapper for compute
// void LocalWlNear::computePy(boost::python::numeric::array points)
//     {
//     //validate input type and rank
//     num_util::check_type(points, NPY_FLOAT);
//     num_util::check_rank(points, 2);

//     // validate that the 2nd dimension is only 3
//     num_util::check_dim(points, 1, 3);
//     unsigned int Np = num_util::shape(points)[0];

//     // get the raw data pointers and compute the cell list
//     // float3* points_raw = (float3*) num_util::data(points);
//     vec3<float>* points_raw = (vec3<float>*) num_util::data(points);
//     compute(points_raw, Np);
//     }

// void LocalWlNear::computeNormPy(boost::python::numeric::array points)
//     {
//     //validate input type and rank
//     num_util::check_type(points, NPY_FLOAT);
//     num_util::check_rank(points, 2);

//     // validate that the 2nd dimension is only 3
//     num_util::check_dim(points, 1, 3);
//     unsigned int Np = num_util::shape(points)[0];

//     // get the raw data pointers and compute the cell list
//     // float3* points_raw = (float3*) num_util::data(points);
//     vec3<float>* points_raw = (vec3<float>*) num_util::data(points);
//     compute(points_raw, Np);
//     computeNorm(points_raw, Np);
//     }

// void LocalWlNear::computeAvePy(boost::python::numeric::array points)
//     {
//     //validate input type and rank
//     num_util::check_type(points, NPY_FLOAT);
//     num_util::check_rank(points, 2);

//     // validate that the 2nd dimension is only 3
//     num_util::check_dim(points, 1, 3);
//     unsigned int Np = num_util::shape(points)[0];

//     // get the raw data pointers and compute the cell list
//     // float3* points_raw = (float3*) num_util::data(points);
//     vec3<float>* points_raw = (vec3<float>*) num_util::data(points);
//     compute(points_raw, Np);
//     computeAve(points_raw, Np);
//     }

// void LocalWlNear::computeAveNormPy(boost::python::numeric::array points)
//     {
//     //validate input type and rank
//     num_util::check_type(points, NPY_FLOAT);
//     num_util::check_rank(points, 2);

//     // validate that the 2nd dimension is only 3
//     num_util::check_dim(points, 1, 3);
//     unsigned int Np = num_util::shape(points)[0];

//     // get the raw data pointers and compute the cell list
//     // float3* points_raw = (float3*) num_util::data(points);
//     vec3<float>* points_raw = (vec3<float>*) num_util::data(points);
//     compute(points_raw, Np);
//     computeAve(points_raw, Np);
//     computeAveNorm(points_raw, Np);
//     }

/*! get wigner3j coefficients from python wrapper
 old version of getting wigner3j from python wrapper
void LocalWl::setWigner3jPy(boost::python::numeric::array wigner3jvalues)
	{
	//validate input type and rank
    num_util::check_type(wigner3jvalues, PyArray_DOUBLE);
    num_util::check_rank(wigner3jvalues, 1);

    // get dimension
    unsigned int num_wigner3jcoefs = num_util::shape(wigner3jvalues)[0];
    m_wigner3jvalues = boost::shared_array<float>(new float[num_wigner3jcoefs]);

    // get the raw data pointers and compute the cell list
    float* wig3j = (float*) num_util::data(wigner3jvalues);
    for(unsigned int i = 0; i < num_wigner3jcoefs; i++)
    	{
    	m_wigner3jvalues[i] = wig3j[i];
    	}
    }
 */

// void export_LocalWlNear()
//     {
//     class_<LocalWlNear>("LocalWlNear", init<box::Box&, float, unsigned int, optional<unsigned int> >())
//         .def("getBox", &LocalWlNear::getBox, return_internal_reference<>())
//         .def("compute", &LocalWlNear::computePy)
//         .def("computeNorm", &LocalWlNear::computeNormPy)
//         .def("computeAve", &LocalWlNear::computeAvePy)
//         .def("computeAveNorm", &LocalWlNear::computeAveNormPy)
//         .def("getWl", &LocalWlNear::getWlPy)
//         .def("getWlNorm", &LocalWlNear::getWlNormPy)
//         .def("getAveWl", &LocalWlNear::getAveWlPy)
//         .def("getWlAveNorm", &LocalWlNear::getAveNormWlPy)
//         .def("getQl", &LocalWlNear::getQlPy)
//         .def("setBox",&LocalWlNear::setBox)
//         //.def("setWigner3j", &LocalWl::setWigner3jPy)
//         .def("enableNormalization", &LocalWlNear::enableNormalization)
//         .def("disableNormalization", &LocalWlNear::disableNormalization)
//         ;
//     }

}; }; // end namespace freud::localwlnear
