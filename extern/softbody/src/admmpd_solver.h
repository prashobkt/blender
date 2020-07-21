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

    void update_collisions(
        const Options *options,
        SolverData *data,
        Collision *collision);

    void init_solve(
        const Mesh *mesh,
        const Options *options,
        SolverData *data,
        Collision *collision);

	void solve_local_step(
        const Options *options,
        SolverData *data);

    bool compute_matrices(
        const Options *options,
        SolverData *data);

	void append_energies(
		const Options *options,
		SolverData *data,
		std::vector<Eigen::Triplet<double> > &D_triplets);

}; // class ADMMPD_solver

} // namespace admmpd

#endif // ADMMPD_SOLVER_H_
