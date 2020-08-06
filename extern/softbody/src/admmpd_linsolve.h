// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_LINSOLVE_H_
#define ADMMPD_LINSOLVE_H_

#include "admmpd_types.h"
#include "admmpd_collision.h"

namespace admmpd {

class LinearSolver
{
public:
	// Called once at simulation initialization
	virtual void init_solve(
        const Mesh *mesh,
        const Options *options,
		const Collision *collision,
        SolverData *data) = 0;

	virtual void solve(
        const Mesh *mesh,
        const Options *options,
		const Collision *collision,
        SolverData *data) = 0;
};

class LDLT : public LinearSolver
{
public:
	// Factors the matrix on any change to P
	void init_solve(
        const Mesh *mesh,
        const Options *options,
		const Collision *collision,
        SolverData *data);

	// Factors the matrix on any change to C
	void solve(
        const Mesh *mesh,
        const Options *options,
		const Collision *collision, // may be null
        SolverData *data);
};

// Preconditioned Conjugate Gradients
class ConjugateGradients : public LinearSolver
{
public:
	void init_solve(
        const Mesh *mesh,
        const Options *options,
		const Collision *collision,
        SolverData *data);

	void solve(
        const Mesh *mesh,
        const Options *options,
		const Collision *collision,
        SolverData *data);

	void apply_preconditioner(
		SolverData *data,
		Eigen::MatrixXd &x,
		const Eigen::MatrixXd &b);
};

#if 0
// Multi-Colored Gauss-Seidel
class GaussSeidel : public LinearSolver
{
public:
	void init(
		const Options *options,
		SolverData *data,
		Collision *collision);

	void solve(
		const Options *options,
		SolverData *data,
		Collision *collision);

protected:
    std::vector<std::vector<int> > A_colors; // colors of (original) A matrix
    std::vector<std::vector<int> > A3_plus_CtC_colors; // colors of A3+CtC

	void compute_colors(
		const std::vector<std::set<int> > &vertex_energies_graph,
		const std::vector<std::set<int> > &vertex_constraints_graph,
		std::vector<std::vector<int> > &colors);

	// For debugging:
	void verify_colors(SolverData *data);

};
#endif

} // namespace admmpd

#endif // ADMMPD_LINSOLVE_H_
