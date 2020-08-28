// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_COLLISION_H_
#define ADMMPD_COLLISION_H_

#include "admmpd_types.h"
#include "admmpd_mesh.h"

namespace admmpd {

struct VFCollisionPair {
    int p_idx; // point
    int p_is_obs; // 0 or 1
    int q_idx; // idx of hit face, or -1 if obstacle
    int q_is_obs; // 0 or 1
    Eigen::Vector3d q_pt; // pt of collision (if q obs)
    Eigen::Vector3d q_n; // normal of collision (if q obs)
    Eigen::Vector3d q_bary; // barys of collision (if q !obs)
    VFCollisionPair();
};

class Collision {
public:
    struct ObstacleData {
        int num_obs() const { return sdf.size(); }
        bool compute_sdf(int idx);
        std::vector<SDFType> sdf;
        // Obstacle data stored in custom matrix type to interop with DiscreGrid
        std::vector<Eigen::Matrix<double,Eigen::Dynamic,3,Eigen::RowMajor> > x0;
        std::vector<Eigen::Matrix<double,Eigen::Dynamic,3,Eigen::RowMajor> > x1;
        std::vector<Eigen::Matrix<unsigned int,Eigen::Dynamic,3,Eigen::RowMajor> > F;
        std::vector<Eigen::AlignedBox<double,3> > box;
    } obsdata;

    virtual ~Collision() {}

    // Updates the BVH with or without sorting
    virtual void update_bvh(
        const admmpd::Mesh *mesh,
        const admmpd::Options *options,
        admmpd::SolverData *data,
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1,
        bool sort) = 0;

    // Performs collision detection.
    // Returns the number of active constraints.
    virtual int detect(
        const admmpd::Mesh *mesh,
        const admmpd::Options *options,
        const admmpd::SolverData *data,
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1) = 0;

    // Appends the per-vertex graph of dependencies
    // for constraints (ignores obstacles).
    virtual void graph(
        const admmpd::Mesh *mesh,
        std::vector<std::set<int> > &g) = 0;

    // Updates the collision obstacles. If the
    // obstacles are new or have moved, the SDF
    // is recomputed on the next call to detect(...)
    virtual bool set_obstacles(
        std::vector<Eigen::MatrixXd> &v0,
        std::vector<Eigen::MatrixXd> &v1,
        std::vector<Eigen::MatrixXi> &F,
        std::string *err=nullptr);

    // Linearizes active collision pairs about x
    // for the constraint Cx=d
    virtual void linearize(
        const admmpd::Mesh *mesh,
        const Options *options,
        const admmpd::SolverData *data,
        const Eigen::MatrixXd *x,
    	std::vector<Eigen::Triplet<double> > *trips,
		std::vector<double> *d) const = 0;

    // Given a point and a mesh, perform
    // discrete collision detection.
    virtual std::pair<bool,VFCollisionPair>
    detect_against_obs(
        const admmpd::Mesh *mesh,
        const admmpd::Options *options,
        const admmpd::SolverData *data,
        const Eigen::Vector3d &pt_t0,
        const Eigen::Vector3d &pt_t1,
        const ObstacleData *obs) const;

    // Perform self collision detection
    virtual std::pair<bool,VFCollisionPair>
    detect_against_self(
        const admmpd::Mesh *mesh,
        const admmpd::Options *options,
        const admmpd::SolverData *data,
        int pt_idx,
        const Eigen::Vector3d &pt_t0,
        const Eigen::Vector3d &pt_t1,
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1) const = 0;
};

// Collision detection against multiple meshes
class EmbeddedMeshCollision : public Collision {
public:
    // Performs collision detection and stores pairs
    int detect(
        const admmpd::Mesh *mesh,
        const admmpd::Options *options,
        const admmpd::SolverData *data,
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1);

    void graph(
        const admmpd::Mesh *mesh,
        std::vector<std::set<int> > &g);

    // Linearizes the collision pairs about x
    // for the constraint Cx=d
    void linearize(
        const admmpd::Mesh *mesh,
        const admmpd::Options *options,
        const admmpd::SolverData *data,
        const Eigen::MatrixXd *x,
    	std::vector<Eigen::Triplet<double> > *trips,
		std::vector<double> *d) const;

    // Updates the tetmesh BVH for self collisions.
    void update_bvh(
        const admmpd::Mesh *mesh,
        const admmpd::Options *options,
        admmpd::SolverData *data,
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1,
        bool sort = false);

protected:
    // Pairs are compute on detect and considered temporary.
    std::vector<Eigen::Vector2i> vf_pairs; // index into per_vertex_pairs
    std::vector<std::vector<VFCollisionPair> > per_vertex_pairs;

    std::pair<bool,VFCollisionPair>
    detect_against_self(
        const admmpd::Mesh *mesh,
        const admmpd::Options *options,
        const admmpd::SolverData *data,
        int pt_idx,
        const Eigen::Vector3d &pt_t0,
        const Eigen::Vector3d &pt_t1,
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1) const;
};

} // namespace admmpd

#endif // ADMMPD_COLLISION_H_
