// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_TETMESH_H_
#define ADMMPD_TETMESH_H_

#include "admmpd_types.h"
#include "admmpd_bvh.h"
#include <unordered_map>

namespace admmpd {

class Mesh {
public:

    virtual bool create(
        const float *verts, // size nv*3
        int nv,
        const unsigned int *faces, // size nf*3
        int nf,
        const unsigned int *tets, // null or size nt*4
        int nt) = 0; // If embedded mesh, set to zero

    // ====================
    //  Accessors
    // ====================
    virtual Eigen::Ref<const Eigen::MatrixXi> prims() const = 0;
    virtual Eigen::Ref<const Eigen::MatrixXd> rest_prim_verts() const = 0;
    virtual Eigen::Ref<const Eigen::MatrixXi> facets() const = 0;
    virtual Eigen::Ref<const Eigen::MatrixXd> rest_facet_verts() const = 0;

    // Maps primitive vertex to facet vertex. For standard tet meshes
    // it's just one-to-one, but embedded meshes use bary weighting.
    virtual Eigen::Vector3d get_mapped_facet_vertex(
        Eigen::Ref<const Eigen::MatrixXd> prim_verts,
        int facet_vertex_idx) const = 0;

    // ====================
    //  Utility
    // ====================

    virtual void compute_masses(
        Eigen::Ref<const Eigen::MatrixXd> x,
        double density_kgm3,
        Eigen::VectorXd &m) const = 0;

    // ====================
    //  Pins
    // ====================

    virtual int num_pins() const = 0;

    // Pins a vertex at a location with stiffness k
    virtual void set_pin(
        int idx,
        const Eigen::Vector3d &p,
        const Eigen::Vector3d &k) = 0;

    virtual void clear_pins() = 0;

    // Px=q with stiffnesses baked in
    virtual void linearize_pins(
        std::vector<Eigen::Triplet<double> > &trips,
        std::vector<double> &q) const = 0;

}; // class Mesh


class EmbeddedMesh : public Mesh {
protected:
    Eigen::MatrixXd lat_V0, emb_V0;
    Eigen::MatrixXi lat_T, emb_F;
    Eigen::VectorXi emb_v_to_tet; // maps embedded vert to tet
    Eigen::MatrixXd emb_barys; // barycoords of the embedding
    std::unordered_map<int,Eigen::Vector3d> emb_pin_k;
    std::unordered_map<int,Eigen::Vector3d> emb_pin_pos;
    admmpd::AABBTree<double,3> emb_rest_facet_tree;

    bool compute_embedding();

public:

    struct Options
    {
        int max_subdiv_levels;
        Options() :
            max_subdiv_levels(3)
            {}
    } options;

    bool create(
        const float *verts, // size nv*3
        int nv,
        const unsigned int *faces, // size nf*3
        int nf,
        const unsigned int *tets, // ignored
        int nt); // ignored

    Eigen::Ref<const Eigen::MatrixXi> prims() const { return lat_T; }
    Eigen::Ref<const Eigen::MatrixXd> rest_prim_verts() const { return lat_V0; }
    Eigen::Ref<const Eigen::MatrixXi> facets() const { return emb_F; }
    Eigen::Ref<const Eigen::MatrixXd> rest_facet_verts() const { return emb_V0; }
    Eigen::Ref<const Eigen::VectorXi> emb_vtx_to_tet() const { return emb_v_to_tet; }
    Eigen::Ref<const Eigen::MatrixXd> emb_barycoords() const { return emb_barys; }
    const admmpd::AABBTree<double,3> &emb_rest_tree() const { return emb_rest_facet_tree; }

    Eigen::Vector3d get_mapped_facet_vertex(
        Eigen::Ref<const Eigen::MatrixXd> prim_verts,
        int facet_vertex_idx) const;

    void compute_masses(
        Eigen::Ref<const Eigen::MatrixXd> x,
        double density_kgm3,
        Eigen::VectorXd &m) const;

    int num_pins() const { return emb_pin_k.size(); }

    // Set the position of an embedded pin
    void set_pin(
        int idx,
        const Eigen::Vector3d &p,
        const Eigen::Vector3d &k);

    void clear_pins()
    {
        emb_pin_k.clear();
        emb_pin_pos.clear();
    }

    // Px=q with stiffnesses baked in
    void linearize_pins(
        std::vector<Eigen::Triplet<double> > &trips,
        std::vector<double> &q) const;

}; // class EmbeddedMesh


class TetMesh : public Mesh {
protected:
    Eigen::MatrixXd V0; // rest verts
    Eigen::MatrixXi F; // surface faces
    Eigen::MatrixXi T; // tets
    std::unordered_map<int,Eigen::Vector3d> pin_k;
    std::unordered_map<int,Eigen::Vector3d> pin_pos;
    admmpd::AABBTree<double,3> rest_facet_tree;

public:

    bool create(
        const float *verts, // size nv*3
        int nv,
        const unsigned int *faces, // size nf*3 (surface faces)
        int nf,
        const unsigned int *tets, // size nt*4
        int nt); // must be > 0

    Eigen::Ref<const Eigen::MatrixXi> facets() const { return F; }
    Eigen::Ref<const Eigen::MatrixXd> rest_facet_verts() const { return V0; }
    Eigen::Ref<const Eigen::MatrixXi> prims() const { return T; }
    Eigen::Ref<const Eigen::MatrixXd> rest_prim_verts() const { return V0; }

    Eigen::Vector3d get_mapped_facet_vertex(
        Eigen::Ref<const Eigen::MatrixXd> prim_verts,
        int facet_vertex_idx) const
        { return prim_verts.row(facet_vertex_idx); }

    void compute_masses(
        Eigen::Ref<const Eigen::MatrixXd> x,
        double density_kgm3,
        Eigen::VectorXd &m) const;

    int num_pins() const { return pin_k.size(); }

    void set_pin(
        int idx,
        const Eigen::Vector3d &p,
        const Eigen::Vector3d &k);

    void clear_pins()
    {
        pin_k.clear();
        pin_pos.clear();
    }

    void linearize_pins(
        std::vector<Eigen::Triplet<double> > &trips,
        std::vector<double> &q) const;

}; // class TetMesh

} // namespace admmpd

#endif // ADMMPD_LATTICE_H_
