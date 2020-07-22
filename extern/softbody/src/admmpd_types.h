// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_TYPES_H_
#define ADMMPD_TYPES_H_

//#include "admmpd_mesh.h"
#include <Eigen/Geometry>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <vector>
#include <set>

// TODO template type for float/double

namespace admmpd {
template <typename T> using RowSparseMatrix = Eigen::SparseMatrix<T,Eigen::RowMajor>;

enum EnergyTermType {
    ENERGYTERM_TET = 0
};

struct Options {
    double timestep_s; // TODO: Figure out delta time from blender api
    int max_admm_iters;
    int max_cg_iters;
    int max_gs_iters;
    double gs_omega; // Gauss-Seidel relaxation
    double mult_ck; // stiffness multiplier for constraints
    double mult_pk; // (global) stiffness multiplier for pins
    double min_res; // exit tolerance for global step
    double youngs; // Young's modulus // TODO variable per-tet
    double poisson; // Poisson ratio // TODO variable per-tet
    double density_kgm3; // density of mesh
    Eigen::Vector3d grav;
    Options() :
        timestep_s(1.0/24.0),
        max_admm_iters(30),
        max_cg_iters(10),
        max_gs_iters(100),
        gs_omega(1),
        mult_ck(3),
        mult_pk(1),
        min_res(1e-8),
        youngs(1000000),
        poisson(0.399),
        density_kgm3(1522),
        grav(0,0,-9.8)
        {}
};

struct SolverData {
    // Set from input
    Eigen::MatrixXi tets; // elements t x 4, copy from mesh
    Eigen::MatrixXd x; // vertices, n x 3
    Eigen::MatrixXd v; // velocity, n x 3
    // Set in compute_matrices: 
    Eigen::MatrixXd x_start; // x at t=0 (and goal if k>0), n x 3
    Eigen::VectorXd m; // masses, n x 1
    Eigen::MatrixXd z; // ADMM z variable
    Eigen::MatrixXd u; // ADMM u aug lag with W inv
    Eigen::MatrixXd M_xbar; // M*(x + dt v)
    Eigen::MatrixXd Dx; // D * x
    Eigen::MatrixXd b; // M xbar + DtW2(z-u)
    RowSparseMatrix<double> D; // reduction matrix
    RowSparseMatrix<double> DtW2; // D'W^2
    RowSparseMatrix<double> A; // M + D'W^2D
    double A_diag_max; // Max coeff of diag of A
    RowSparseMatrix<double> C; // linearized constraints (cols = n x 3)
    Eigen::VectorXd d; // constraints rhs
    RowSparseMatrix<double> PtP; // pin_k Pt P
    Eigen::VectorXd Ptq; // pin_k Pt q
	Eigen::SimplicialLDLT<Eigen::SparseMatrix<double> > ldltA;
    struct GlobalStepData { // Temporaries used in global step
        RowSparseMatrix<double> A3; // (M + D'W^2D) n3 x n3
        RowSparseMatrix<double> CtC; // col_k * Ct C
        RowSparseMatrix<double> A3_CtC_PtP;
        Eigen::VectorXd Ctd; // k * Ct d
        Eigen::VectorXd b3_Ctd_Ptx; // M xbar + DtW2(z-u) + col_k Ct d + pin_k Pt x_start
        // Used by Conjugate-Gradients:
        Eigen::VectorXd r; // residual
        Eigen::VectorXd z; // auxilary
        Eigen::VectorXd p; // direction
        Eigen::VectorXd A3p; // A3 * p
        // Used by Gauss-Seidel:
        std::vector<std::vector<int> > A_colors; // colors of (original) A matrix
        std::vector<std::vector<int> > A3_plus_CtC_colors; // colors of A3+KtK
    } gsdata;
    // Set in append_energies:
    std::vector<std::set<int> > energies_graph; // per-vertex adjacency list (graph)
	std::vector<Eigen::Vector3i> indices; // per-energy index into D (row, num rows, type)
	std::vector<double> rest_volumes; // per-energy rest volume
	std::vector<double> weights; // per-energy weights
};

} // namespace admmpd

#endif // ADMMPD_TYPES_H_
