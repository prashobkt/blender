// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_COLLISION_H_
#define ADMMPD_COLLISION_H_

#include "admmpd_bvh.h"
#include "admmpd_types.h"
#include "admmpd_mesh.h"
#include <set>
#include <Discregrid/All>

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
	typedef Discregrid::CubicLagrangeDiscreteGrid SDFType;

    struct ObstacleData {
        bool has_obs() const { return F.rows()>0; }
        Eigen::MatrixXd V;
        Eigen::MatrixXi F;
        std::unique_ptr<SDFType> sdf;
    } obsdata;

    virtual ~Collision() {}

    // Updates the BVH with or without sorting
    virtual void update_bvh(
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1,
        bool sort = false) = 0;

    // Performs collision detection.
    // Returns the number of active constraints.
    virtual int detect(
        const admmpd::Options *options,
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1) = 0;

    // Appends the per-vertex graph of dependencies
    // for constraints (ignores obstacles).
    virtual void graph(
        std::vector<std::set<int> > &g) = 0;

    // Set the soup of obstacles for this time step.
    // I don't really like having to switch up interface style, but we'll
    // do so here to avoid copies that would happen in admmpd_api.
    // We should actually just pash in a mesh class?
    virtual void set_obstacles(
        const float *v0,
        const float *v1,
        int nv,
        const unsigned int *faces,
        int nf);

    // Linearize the constraints about x and return Jacobian.
    virtual void linearize(
        const Eigen::MatrixXd *x,
    	std::vector<Eigen::Triplet<double> > *trips,
		std::vector<double> *d) = 0;

    // Given a point and a mesh, perform
    // discrete collision detection.
    virtual std::pair<bool,VFCollisionPair>
    detect_against_obs(
        const Eigen::Vector3d &pt_t0,
        const Eigen::Vector3d &pt_t1,
        const ObstacleData *obs) const;

    virtual std::pair<bool,VFCollisionPair>
    detect_against_self(
        int pt_idx,
        const Eigen::Vector3d &pt_t0,
        const Eigen::Vector3d &pt_t1,
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1) const = 0;
};

// Collision detection against multiple meshes
class EmbeddedMeshCollision : public Collision {
public:
    EmbeddedMeshCollision(std::shared_ptr<EmbeddedMesh> mesh_);

    // Performs collision detection and stores pairs
    int detect(
        const admmpd::Options *options,
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1);

    void graph(
        std::vector<std::set<int> > &g);
    
    // Linearizes the collision pairs about x
    // for the constraint Kx=l
    void linearize(
        const Eigen::MatrixXd *x,
    	std::vector<Eigen::Triplet<double> > *trips,
		std::vector<double> *d);

    // Updates the tetmesh BVH for self collisions.
    void update_bvh(
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1,
        bool sort = false);

protected:

    struct MeshData {
        std::shared_ptr<EmbeddedMesh> mesh;
        std::vector<Eigen::AlignedBox<double,3> > tet_boxes;
        AABBTree<double,3> tet_tree;
    } meshdata;

    // Pairs are compute on detect
    std::vector<Eigen::Vector2i> vf_pairs; // index into per_vertex_pairs
    std::vector<std::vector<VFCollisionPair> > per_vertex_pairs;

    std::pair<bool,VFCollisionPair>
    detect_against_self(
        int pt_idx,
        const Eigen::Vector3d &pt_t0,
        const Eigen::Vector3d &pt_t1,
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1) const;
};

} // namespace admmpd

#endif // ADMMPD_COLLISION_H_
