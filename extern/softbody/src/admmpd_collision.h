// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_COLLISION_H_
#define ADMMPD_COLLISION_H_

#include "admmpd_bvh.h"
#include "admmpd_types.h"
#include "admmpd_embeddedmesh.h"
#include <set>

namespace admmpd {

struct VFCollisionPair {
    int p_idx; // point
    int p_is_obs; // 0 or 1
    int q_idx; // face, or -1 if floor
    int q_is_obs; // 0 or 1
    Eigen::Vector3d q_pt; // pt of collision (if q obs)
    Eigen::Vector3d q_bary; // barys of collision (if q !obs)
    VFCollisionPair();
};

class Collision {
public:

    struct Settings {
        double collision_eps;
        double floor_z;
        bool floor_collision;
        bool obs_collision;
        bool self_collision;
        Settings() :
            collision_eps(1e-10),
            floor_z(-std::numeric_limits<double>::max()),
            floor_collision(true),
            obs_collision(true),
            self_collision(false)
            {}
    } settings;

    struct ObstacleData {
        bool has_obs() const { return F.rows()>0; }
        Eigen::MatrixXd V;
        Eigen::MatrixXi F;
        AABBTree<double,3> tree;
        std::vector<Eigen::AlignedBox<double,3> > leaves;
    } obsdata;

    virtual ~Collision() {}

    // Resets the BVH
    virtual void init_bvh(
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1) = 0;

    // Updates the BVH without sorting
    virtual void update_bvh(
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1) = 0;

    // Updates the active set of constraints,
    // but does not detect new ones.
    virtual void update_constraints(
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1) = 0;

    // Performs collision detection.
    // Returns the number of active constraints.
    virtual int detect(
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1) = 0;

    // Appends the per-vertex graph of dependencies
    // for constraints (ignores obstacles).
    virtual void graph(
        std::vector<std::set<int> > &g) = 0;

    // Set the soup of obstacles for this time step.
    // I don't really like having to switch up interface style, but we'll
    // do so here to avoid copies that would happen in admmpd_api.
    virtual void set_obstacles(
        const float *v0,
        const float *v1,
        int nv,
        const unsigned int *faces,
        int nf);

    // Special case for floor since it's common.
    virtual void set_floor(double z) { settings.floor_z=z; }
    virtual double get_floor() const { return settings.floor_z; }

    // Linearize the constraints and return Jacobian.
    virtual void linearize(
        const Eigen::MatrixXd *x,
    	std::vector<Eigen::Triplet<double> > *trips,
		std::vector<double> *d) = 0;

    // Given a point and a mesh, perform
    // discrete collision detection.
    virtual std::pair<bool,VFCollisionPair>
    detect_against_obs(
        const Eigen::Vector3d &pt,
        const ObstacleData *obs) const;

    virtual std::pair<bool,VFCollisionPair>
    detect_against_self(
        int pt_idx,
        const Eigen::Vector3d &pt,
        const Eigen::MatrixXd *x) const = 0;
};

// Collision detection against multiple meshes
class EmbeddedMeshCollision : public Collision {
public:
    EmbeddedMeshCollision(const EmbeddedMesh *mesh_) : mesh(mesh_){}

    // Performs collision detection and stores pairs
    int detect(
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

    void init_bvh(
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1);

    // Updates the tetmesh BVH for self collisions.
    void update_bvh(
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1);

    // Updates the active set of constraints,
    // but does not detect new ones.
    void update_constraints(
        const Eigen::MatrixXd *x0,
        const Eigen::MatrixXd *x1);

protected:
    // A ptr to the embedded mesh data
    const EmbeddedMesh *mesh;
    std::vector<Eigen::AlignedBox<double,3> > tet_boxes;
    AABBTree<double,3> tet_tree;

    // Pairs are compute on detect
    std::vector<Eigen::Vector2i> vf_pairs; // index into per_vertex_pairs
    std::vector<std::vector<VFCollisionPair> > per_vertex_pairs;

    std::pair<bool,VFCollisionPair>
    detect_against_self(
        int pt_idx,
        const Eigen::Vector3d &pt,
        const Eigen::MatrixXd *x) const;
};

} // namespace admmpd

#endif // ADMMPD_COLLISION_H_
