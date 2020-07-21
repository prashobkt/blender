// Copyright Matt Overby 2020.
// Distributed under the MIT License.
#if 0

#ifndef ADMMPD_EMBEDDEDMESH_H_
#define ADMMPD_EMBEDDEDMESH_H_

#include "admmpd_types.h"
#include "admmpd_bvh.h"
//#include "admmpd_sdf.h"

namespace admmpd {

class EmbeddedMesh {
public:
    Eigen::MatrixXd emb_rest_x; // embedded verts at rest
    Eigen::MatrixXi emb_faces; // embedded faces
    Eigen::VectorXi emb_vtx_to_tet; // what tet vtx is embedded in, p x 1
    Eigen::MatrixXd emb_barys; // barycoords of the embedding, p x 4
    admmpd::AABBTree<double,3> emb_rest_tree; // tree of embedded verts at rest
    Eigen::MatrixXi lat_tets; // lattice elements, m x 4
    Eigen::MatrixXd lat_rest_x; // lattice verts at rest

    // Returns true on success
    bool generate(
        const Eigen::MatrixXd &V, // embedded verts
        const Eigen::MatrixXi &F, // embedded faces
        bool trim_lattice = true, // remove elements outside embedded volume
        int subdiv_levels=3); // number of subdivs (resolution)

    // Returns the vtx mapped from x/v and tets
    // Idx is the embedded vertex, and x_data is the
    // data it should be mapped to.
    Eigen::Vector3d get_mapped_vertex(
        const Eigen::MatrixXd *x_data,
        int idx) const;

    // Given an embedding, compute masses
    // for the lattice vertices
    void compute_masses(
        Eigen::VectorXd *masses_tets, // masses of the lattice verts
        double density_kgm3 = 1100);

protected:

    // Returns true on success
    // Computes the embedding data, like barycoords
    bool compute_embedding();

}; // class EmbeddedMesh

} // namespace admmpd

#endif // ADMMPD_EMBEDDEDMESH_H_
#endif