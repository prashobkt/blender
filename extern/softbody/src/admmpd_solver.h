// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_SOLVER_H_
#define ADMMPD_SOLVER_H_

#include "admmpd_types.h"
#include "admmpd_collision.h"
#include "admmpd_mesh.h"

namespace admmpd {

class Solver {
public:
    // Initialies solver data. If a per-vertex
    // variable is resized it is initialized to zero.
    // Returns true on success
    bool init(
        const Mesh *mesh,
        const Options *options,
        SolverData *data);

    // Solve a single time step.
    // Returns number of iterations.
    // Collision ptr can be null.
    // Pin ptr can be null
    int solve(
        const Mesh *mesh,
        const Options *options,
        SolverData *data,
        Collision *collision);

protected:

    // Returns the combined residual norm
    double residual_norm(
        const Options *options,
        SolverData *data);

    // Computes start-of-solve quantites
    void init_solve(
        const Mesh *mesh,
        const Options *options,
        SolverData *data,
        Collision *collision);

    // Performs collision detection
    // and updates C, d
    void update_collisions(
        const Options *options,
        SolverData *data,
        Collision *collision);

    // Update z and u in parallel
	void solve_local_step(
        const Options *options,
        SolverData *data);

    // Called once at start of simulation.
    // Computes constant quantities
    void init_matrices(
        const Mesh *mesh,
        const Options *options,
        SolverData *data);

    // Computes the W matrix
    // from the current weights
    void update_weight_matrix(
        const Options *options,
        SolverData *data,
        int rows);

    // Linearizes pin constraints P, q
    void update_pin_matrix(
        const Mesh *mesh,
        const Options *options,
        SolverData *data);
    
    // Input: DtW'W, D, and P
    void update_global_matrix(
        const Options *options,
        SolverData *data);

    // Generates energies from the mesh
	void append_energies(
        const Mesh *mesh,
		const Options *options,
		SolverData *data,
		std::vector<Eigen::Triplet<double> > &D_triplets);

}; // class ADMMPD_solver

} // namespace admmpd

#endif // ADMMPD_SOLVER_H_
