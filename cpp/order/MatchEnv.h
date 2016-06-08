#ifndef _MATCH_ENV_H__
#define _MATCH_ENV_H__

#include <boost/shared_array.hpp>
#include <boost/bimap.hpp>

#include "HOOMDMath.h"
#include "VectorMath.h"

#include <vector>
#include <set>

#include "Cluster.h"
#include "NearestNeighbors.h"

#include "box.h"
#include <stdexcept>
#include <complex>
#include <map>
#include <algorithm>
#include <iostream>


namespace freud { namespace order {
//! Clusters particles according to whether their local environments match or not, according to various shape matching metrics

//! My environment data structure
struct Environment
    {
    //! Constructor.
    Environment(unsigned int n) : vecs(0), vec_ind(0)
        {
        num_neigh = n;
        env_ind = 0;
        num_vecs = 0;
        ghost = false;
        }
    //! Add a vector to define the local environment
    void addVec(vec3<float> vec)
        {
        if (num_vecs > num_neigh)
            {
            fprintf(stderr, "Current number of vecs is %d\n", num_vecs);
            throw std::invalid_argument("You've added too many vectors to the environment!");
            }
        vecs.push_back(vec);
        vec_ind.push_back(num_vecs);
        num_vecs++;
        }

    unsigned int env_ind;                   //!< The index of the environment
    std::vector<vec3<float> > vecs;         //!< The vectors that define the environment
    bool ghost;                             //!< Is this environment a ghost? Do we ignore it when we compute actual physical quantities associated with all environments?
    unsigned int num_vecs;                  //!< The number of vectors defining the environment currently
    unsigned int num_neigh;                 //!< The maximum allowed number of vectors to define the environment
    std::vector<unsigned int> vec_ind;      //!< The order that the vectors must be in to define the environment
    };

//! General disjoint set class, taken mostly from Cluster.h
class EnvDisjointSet
    {
    public:
        //! Constructor
        EnvDisjointSet(unsigned int num_neigh, unsigned int Np);
        //! Merge two sets
        void merge(const unsigned int a, const unsigned int b, boost::bimap<unsigned int, unsigned int> vec_map);
        //! Find the set with a given element
        unsigned int find(const unsigned int c);
        //! Return ALL nodes in the tree that correspond to the head index m
        std::vector<unsigned int> findSet(const unsigned int m);
        //! Get the vectors corresponding to environment head index m. Vectors are averaged over all members of the environment cluster.
        boost::shared_array<vec3<float> > getAvgEnv(const unsigned int m);
        //! Get the vectors corresponding to index m in the dj set
        std::vector<vec3<float> > getIndividualEnv(const unsigned int m);

        std::vector<Environment> s;         //!< The disjoint set data
        std::vector<unsigned int> rank;     //!< The rank of each tree in the set
        unsigned int m_num_neigh;           //!< The number of neighbors allowed per environment
    };

class MatchEnv
    {
    public:
        //! Constructor
        /**Constructor for Match-Environment analysis class.  After creation, call cluster to agnostically calculate clusters grouped by matching environment,
        or matchMotif to match all particle environments against an input motif.  Use accessor functions to retrieve data.
        @param rmax Cutoff radius for cell list and clustering algorithm.  Values near first minimum of the rdf are recommended.
        @param k Number of nearest neighbors taken to construct the environment of any given particle.
        **/
        MatchEnv(const box::Box& box, float rmax, unsigned int k=12);

        //! Destructor
        ~MatchEnv();

        //! Construct and return a local environment surrounding the particle indexed by i. Set the environment index to env_ind.
        //! if hard_r is true, only add the neighbor particles to the environment if they fall within the threshold of m_rmaxsq
        Environment buildEnv(const vec3<float> *points, unsigned int i, unsigned int env_ind, bool hard_r);

        //! Determine clusters of particles with matching environments
        //! The threshold is a unitless number, which we multiply by the length scale of the MatchEnv instance, rmax.
        //! This quantity is the maximum squared magnitude of the vector difference between two vectors, below which you call them matching.
        //! Note that ONLY values of (threshold < 2) make any sense, since 2*rmax is the absolute maximum difference between any two environment vectors.
        //! If hard_r is true, only add the neighbor particles to the environment if they fall within the threshold of m_rmaxsq
        void cluster(const vec3<float> *points, unsigned int Np, float threshold, bool hard_r=false);

        //! Determine whether particles match a given input motif, characterized by refPoints (of which there are numRef)
        //! The threshold is a unitless number, which we multiply by the length scale of the MatchEnv instance, rmax.
        //! This quantity is the maximum squared magnitude of the vector difference between two vectors, below which you call them matching.
        //! Note that ONLY values of (threshold < 2) make any sense, since 2*rmax is the absolute maximum difference between any two environment vectors.
        //! If hard_r is true, only add the neighbor particles to the environment if they fall within the threshold of m_rmaxsq
        void matchMotif(const vec3<float> *points, unsigned int Np, const vec3<float> *refPoints, unsigned int numRef, float threshold, bool hard_r=false);

        //! Renumber the clusters in the disjoint set dj from zero to num_clusters-1
        void populateEnv(EnvDisjointSet dj, bool reLabel=true);

        //! Is the environment e1 similar to the environment e2?
        //! If so, return the mapping between the vectors of the environments that will make them correspond to each other.
        //! If not, return an empty map
        boost::bimap<unsigned int, unsigned int> isSimilar(Environment e1, Environment e2, float threshold_sq);

        //! Overload: is the set of vectors refPoints1 similar to the set of vectors refPoints2?
        //! Construct the environments accordingly, and utilize isSimilar() as above.
        //! Return a std map for ease of use.
        std::map<unsigned int, unsigned int> isSimilar(const vec3<float> *refPoints1, const vec3<float> *refPoints2, unsigned int numRef, float threshold_sq);

        //! Get a reference to the particles, indexed into clusters according to their matching local environments
        boost::shared_array<unsigned int> getClusters()
            {
            return m_env_index;
            }

        //! Reset the simulation box
        void setBox(const box::Box newbox)
            {
            m_box = newbox;
            delete m_nn;
            m_nn = new locality::NearestNeighbors(m_rmax, m_k);
            }

        //! Returns the set of vectors defining the environment indexed by i (indices culled from m_env_index)
        boost::shared_array< vec3<float> > getEnvironment(unsigned int i)
            {
            std::map<unsigned int, boost::shared_array<vec3<float> > >::iterator it = m_env.find(i);
            boost::shared_array<vec3<float> > vecs = it->second;
            return vecs;
            }

        //! Returns the entire m_Np by m_k by 3 matrix of all environments for all particles
        boost::shared_array<vec3<float> > getTotEnvironment()
            {
            return m_tot_env;
            }

        unsigned int getNP()
            {
            return m_Np;
            }

        unsigned int getNumClusters()
            {
            return m_num_clusters;
            }
        unsigned int getNumNeighbors()
            {
            return m_k;
            }

    private:
        box::Box m_box;              //!< Simulation box
        float m_rmax;                       //!< Maximum cutoff radius at which to determine local environment
        float m_rmaxsq;                     //!< square of m_rmax
        float m_k;                          //!< Number of nearest neighbors used to determine local environment
        locality::NearestNeighbors *m_nn;   //!< NearestNeighbors to bin particles for the computation of local environments
        unsigned int m_Np;                  //!< Last number of points computed
        unsigned int m_num_clusters;        //!< Last number of local environments computed

        boost::shared_array<unsigned int> m_env_index;                          //!< Cluster index determined for each particle
        std::map<unsigned int, boost::shared_array<vec3<float> > > m_env;       //!< Dictionary of (cluster id, vectors) pairs
        boost::shared_array<vec3<float> > m_tot_env;              //!< m_NP by m_k by 3 matrix of all environments for all particles
    };

}; }; // end namespace freud::match_env

#endif // _MATCH_ENV_H__