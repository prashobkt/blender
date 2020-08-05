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
protected:
	typedef Eigen::SimplicialLDLT<Eigen::SparseMatrix<double> > Cholesky;
	std::unique_ptr<Cholesky> m_ldlt_A_PtP;
	double last_pk;

	Eigen::MatrixXd m_rhs;
	Eigen::MatrixXd m_Ptq;
	Eigen::SparseMatrix<double> m_A_PtP;
	Eigen::SparseMatrix<double> m_A_PtP_3; // replicated

public:
	LDLT();

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

	Cholesky *cholesky() { return m_ldlt_A_PtP.get(); }
	const Eigen::SparseMatrix<double> *A_PtP() { return &m_A_PtP; }
	const Eigen::SparseMatrix<double> *A_PtP_3() { return &m_A_PtP_3; }
	const Eigen::MatrixXd *Ptq() { return &m_Ptq; }
};

// Preconditioned Conjugate Gradients
class ConjugateGradients : public LinearSolver
{
public:
	ConjugateGradients();

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
		Eigen::MatrixXd &x,
		const Eigen::MatrixXd &b);

protected:
	typedef Eigen::SimplicialLDLT<Eigen::SparseMatrix<double> > Cholesky;

	// If true, factor A + PtP on a call
	// to init_solve if P has changed.
	// If false, just A is factored which will
	// result in more PCG iterations.
	bool factor_A_PtP;
	double last_pk; // last pin stiffness on init_solve

	std::unique_ptr<LDLT> m_ldlt;
	Eigen::MatrixXd rhs; // Mxbar + DtW2(z-u) + Ptq + Ctd
	Eigen::MatrixXd Ctd;
	Eigen::MatrixXd r;
	Eigen::MatrixXd z;
	Eigen::MatrixXd p;
	Eigen::VectorXd p3;
	Eigen::MatrixXd Ap;

//	Eigen::MatrixXd x3; // x flattened
//	Eigen::MatrixXd r; // residual
//	Eigen::MatrixXd z; // auxilary
//	Eigen::MatrixXd p; // direction
//	Eigen::MatrixXd Ap; // A3_PtP_CtC * p
	Eigen::MatrixXd thread_x, thread_b;
	RowSparseMatrix<double> A3_PtP_CtC;
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
