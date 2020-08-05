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
	typedef Discregrid::CubicLagrangeDiscreteGrid SDFType;

    // Returns meshtype (see admmpd_types)
    virtual int type() const = 0;

    // Copy buffers to internal data, 
    // calculates BVH/SDF, etc...
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

    virtual const Eigen::MatrixXi *prims() const = 0;
    virtual const Eigen::MatrixXd *rest_prim_verts() const = 0;
    virtual const Eigen::MatrixXi *facets() const = 0;
    virtual const Eigen::MatrixXd *rest_facet_verts() const = 0;
    virtual const std::shared_ptr<SDFType> rest_facet_sdf() const = 0;

    // Maps primitive vertex to facet vertex. For standard tet meshes
    // it's just one-to-one, but embedded meshes use bary weighting.
    virtual Eigen::Vector3d get_mapped_facet_vertex(
        const Eigen::MatrixXd *prim_verts,
        int facet_vertex_idx) const = 0;

    // ====================
    //  Utility
    // ====================

    virtual void compute_masses(
        const Eigen::MatrixXd *x,
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
        double k) = 0;

    virtual void clear_pins() = 0;

    // Px=q with stiffnesses baked in
    // Returns true if P (but not q) has changed from last
    // call to linearize_pins.
    virtual bool linearize_pins(
        std::vector<Eigen::Triplet<double> > &trips,
        std::vector<double> &q,
        std::set<int> &pin_inds,
        bool replicate) const = 0;

}; // class Mesh


class EmbeddedMesh : public Mesh {
protected:
    Eigen::MatrixXd lat_V0, emb_V0;
    Eigen::MatrixXi lat_T, emb_F;
    Eigen::VectorXi emb_v_to_tet; // maps embedded vert to tet
    Eigen::MatrixXd emb_barys; // barycoords of the embedding
    std::unordered_map<int,double> emb_pin_k;
    std::unordered_map<int,Eigen::Vector3d> emb_pin_pos;
    admmpd::AABBTree<double,3> emb_rest_facet_tree;
    std::shared_ptr<SDFType> emb_sdf;
    mutable bool P_updated; // set to false on linearize_pins

    bool compute_embedding();

public:

    int type() const { return MESHTYPE_EMBEDDED; }

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

    const Eigen::MatrixXi *prims() const { return &lat_T; }
    const Eigen::MatrixXd *rest_prim_verts() const { return &lat_V0; }
    const Eigen::MatrixXi *facets() const { return &emb_F; }
    const Eigen::MatrixXd *rest_facet_verts() const { return &emb_V0; }
    const Eigen::VectorXi *emb_vtx_to_tet() const { return &emb_v_to_tet; }
    const Eigen::MatrixXd *emb_barycoords() const { return &emb_barys; }
    const std::shared_ptr<SDFType> rest_facet_sdf() const { return emb_sdf; }
    const admmpd::AABBTree<double,3> &emb_rest_tree() const { return emb_rest_facet_tree; }

    Eigen::Vector3d get_mapped_facet_vertex(
        const Eigen::MatrixXd *prim_verts,
        int facet_vertex_idx) const;

    void compute_masses(
        const Eigen::MatrixXd *x,
        double density_kgm3,
        Eigen::VectorXd &m) const;

    int num_pins() const { return emb_pin_k.size(); }

    // Set the position of an embedded pin
    void set_pin(
        int idx,
        const Eigen::Vector3d &p,
        double k);

    void clear_pins()
    {
        if (emb_pin_pos.size()) { P_updated=true; }
        emb_pin_k.clear();
        emb_pin_pos.clear();
    }

    // Px=q with stiffnesses baked in
    bool linearize_pins(
        std::vector<Eigen::Triplet<double> > &trips,
        std::vector<double> &q,
        std::set<int> &pin_inds,
        bool replicate) const;

}; // class EmbeddedMesh


class TetMesh : public Mesh {
protected:
    Eigen::MatrixXd V0; // rest verts
    Eigen::MatrixXi F; // surface faces
    Eigen::MatrixXi T; // tets
    std::unordered_map<int,double> pin_k;
    std::unordered_map<int,Eigen::Vector3d> pin_pos;
    admmpd::AABBTree<double,3> rest_facet_tree;
    std::shared_ptr<SDFType> rest_sdf;
    mutable bool P_updated; // set to false on linearize_pins

public:

    int type() const { return MESHTYPE_TET; }

    bool create(
        const float *verts, // size nv*3
        int nv,
        const unsigned int *faces, // size nf*3 (surface faces)
        int nf,
        const unsigned int *tets, // size nt*4
        int nt); // must be > 0

    const Eigen::MatrixXi *facets() const { return &F; }
    const Eigen::MatrixXd *rest_facet_verts() const { return &V0; }
    const Eigen::MatrixXi *prims() const { return &T; }
    const Eigen::MatrixXd *rest_prim_verts() const { return &V0; }
    const std::shared_ptr<SDFType> rest_facet_sdf() const { return rest_sdf; }

    Eigen::Vector3d get_mapped_facet_vertex(
        const Eigen::MatrixXd *prim_verts,
        int facet_vertex_idx) const
        { return prim_verts->row(facet_vertex_idx); }

    void compute_masses(
        const Eigen::MatrixXd *x,
        double density_kgm3,
        Eigen::VectorXd &m) const;

    int num_pins() const { return pin_k.size(); }

    void set_pin(
        int idx,
        const Eigen::Vector3d &p,
        double k);

    void clear_pins()
    {
        if (pin_pos.size()) { P_updated=true; }
        pin_k.clear();
        pin_pos.clear();
    }

    // pin_inds refers to the index of the embedded vertex
    bool linearize_pins(
        std::vector<Eigen::Triplet<double> > &trips,
        std::vector<double> &q,
        std::set<int> &pin_inds,
        bool replicate) const;

}; // class TetMesh

class TriangleMesh : public Mesh {
protected:
    Eigen::MatrixXi F;
    Eigen::MatrixXd V0;
    std::unordered_map<int,Eigen::Vector3d> pin_pos;
    std::unordered_map<int,double> pin_k;
    admmpd::AABBTree<double,3> rest_facet_tree;
    mutable bool P_updated; // set to false on linearize_pins

public:

    int type() const { return MESHTYPE_TRIANGLE; }

    bool create(
        const float *verts, // size nv*3
        int nv,
        const unsigned int *faces, // size nf*3
        int nf,
        const unsigned int *tets, // ignored
        int nt); // ignored

    const Eigen::MatrixXi *prims() const { return nullptr; }
    const Eigen::MatrixXd *rest_prim_verts() const { return nullptr; }
    const Eigen::MatrixXi *facets() const { return &F; }
    const Eigen::MatrixXd *rest_facet_verts() const { return &V0; }
    const std::shared_ptr<SDFType> rest_facet_sdf() const { return nullptr; }

    Eigen::Vector3d get_mapped_facet_vertex(
        const Eigen::MatrixXd *prim_verts,
        int facet_vertex_idx) const {
            return prim_verts->row(facet_vertex_idx);
    }

    void compute_masses(
        const Eigen::MatrixXd *x,
        double density_kgm2,
        Eigen::VectorXd &m) const;

    int num_pins() const { return pin_pos.size(); }

    // Pins a vertex at a location with stiffness k
    void set_pin(
        int idx,
        const Eigen::Vector3d &p,
        double k);

    void clear_pins() {
        if (pin_pos.size()) { P_updated=true; }
        pin_pos.clear();
        pin_k.clear();
    }

    // Px=q with stiffnesses baked in
    bool linearize_pins(
        std::vector<Eigen::Triplet<double> > &trips,
        std::vector<double> &q,
        std::set<int> &pin_inds,
        bool replicate) const;

}; // class TriangleMesh

} // namespace admmpd

#endif // ADMMPD_LATTICE_H_
