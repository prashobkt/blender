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
		const Options *options,
		SolverData *data) = 0;

	virtual void solve(
		const Options *options,
		SolverData *data,
		Collision *collision) = 0;
};

// Preconditioned Conjugate Gradients
class ConjugateGradients : public LinearSolver
{
public:
	void init_solve(
		const Options *options,
		SolverData *data);

	void solve(
		const Options *options,
		SolverData *data,
		Collision *collision);

protected:
	RowSparseMatrix<double> A3_PtP_CtC;
	RowSparseMatrix<double> CtC;
	Eigen::MatrixXd b; // M xbar + DtW2(z-u)
	Eigen::VectorXd Ctd; // ck * Ct d
	Eigen::VectorXd Ptq; // pk * Pt q
	Eigen::VectorXd b3_Ptq_Ctd; // M xbar + DtW2(z-u) + ck Ct d + pk Pt q
	Eigen::VectorXd r; // residual
	Eigen::VectorXd z; // auxilary
	Eigen::VectorXd p; // direction
	Eigen::VectorXd Ap; // A3_PtP_CtC * p
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
