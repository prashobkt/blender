// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_SOLVER_H_
#define ADMMPD_SOLVER_H_

#include "admmpd_types.h"
#include "admmpd_collision.h"
#include "admmpd_mesh.h"
#include "admmpd_linsolve.h"

namespace admmpd {

class Solver {
public:
    // Initialies solver data.
    // Returns true on success
    bool init(
        const Mesh *mesh,
        const Options *options,
        SolverData *data);

    // Solve a single time step.
    // Collision ptr can be null.
    void solve(
        const Mesh *mesh,
        const Options *options,
        SolverData *data,
        Collision *collision);

protected:

    // Computes start-of-solve quantites.
    // Collision ptr can be null.
    void init_solve(
        const Mesh *mesh,
        const Options *options,
        SolverData *data,
        Collision *collision);

    // Update z and u in parallel, g(Dx)
	void solve_local_step(
        const Options *options,
        SolverData *data);

    // Solves the linear system, f(x)
    void solve_global_step(
        const Mesh *mesh,
        const Options *options,
        SolverData *data,
        Collision *collision);

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

    // Generates energies from a mesh
	void append_energies(
        const Mesh *mesh,
		const Options *options,
		SolverData *data,
		std::vector<Eigen::Triplet<double> > &D_triplets);

}; // class ADMMPD_solver

} // namespace admmpd

#endif // ADMMPD_SOLVER_H_
