// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_TYPES_H_
#define ADMMPD_TYPES_H_

#include <Eigen/Geometry>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <thread>
#include <vector>
#include <set>

// TODO template type for float/double

namespace admmpd {
template <typename T> using RowSparseMatrix = Eigen::SparseMatrix<T,Eigen::RowMajor>;

#define ENERGYTERM_TET 0

#define ELASTIC_ARAP 0 // As-rigid-as-possible
#define ELASTIC_NH 1 // NeoHookean

struct Options {
    double timestep_s;
    int max_admm_iters;
    int max_cg_iters;
    int max_gs_iters;
    int max_threads; // -1 = auto (num cpu threads - 1)
    int elastic_material; // ENUM, see admmpd_energy.h
    double gs_omega; // Gauss-Seidel relaxation
    double mult_ck; // stiffness multiplier for constraints
    double mult_pk; // (global) stiffness multiplier for pins
    double min_res; // exit tolerance for global step
    double youngs; // Young's modulus // TODO variable per-tet
    double poisson; // Poisson ratio // TODO variable per-tet
    double density_kgm3; // density of mesh
    double floor; // floor location
    double collision_thickness;
    bool self_collision; // process self collisions
    Eigen::Vector3d grav;
    Options() :
        timestep_s(1.0/24.0),
        max_admm_iters(30),
        max_cg_iters(10),
        max_gs_iters(100),
        max_threads(-1),
        gs_omega(1),
        mult_ck(3),
        mult_pk(3),
        min_res(1e-8),
        youngs(1000000),
        poisson(0.399),
        density_kgm3(1522),
        floor(-std::numeric_limits<double>::max()),
        collision_thickness(1e-6),
        self_collision(false),
        grav(0,0,-9.8)
        {}
};

struct SolverData {
    Eigen::MatrixXd x; // vertices, n x 3
    Eigen::MatrixXd v; // velocity, n x 3
    Eigen::MatrixXd x_start; // x at t=0 (and goal if k>0), n x 3
    Eigen::MatrixXd x_prev; // x at k-1
    Eigen::VectorXd m; // masses, n x 1
    Eigen::MatrixXd z; // ADMM z variable
    Eigen::MatrixXd u; // ADMM u aug lag with W inv
    Eigen::MatrixXd M_xbar; // M*(x + dt v)
    Eigen::MatrixXd Dx; // D * x
    RowSparseMatrix<double> D; // reduction matrix
    RowSparseMatrix<double> DtW2; // D'W'W
    RowSparseMatrix<double> A; // M + DtW'WD
    RowSparseMatrix<double> W; // weight matrix

    double A_diag_max; // Max coeff of diag of A
    RowSparseMatrix<double> A3_plus_PtP; // A + pk PtP replicated
	Eigen::SimplicialLDLT<Eigen::SparseMatrix<double> > ldlt_A3;

    RowSparseMatrix<double> P; // pin constraint Px=q (P.cols=3n)
    Eigen::VectorXd q; // pin constraint rhs
    RowSparseMatrix<double> C; // collision constraints Cx=d (C.cols=3n)
    Eigen::VectorXd d; // collision constraints rhs

    // Set in append_energies:
    std::vector<std::set<int> > energies_graph; // per-vertex adjacency list (graph)
	std::vector<Eigen::Vector3i> indices; // per-energy index into D (row, num rows, type)
	std::vector<double> rest_volumes; // per-energy rest volume
	std::vector<double> weights; // per-energy weights
};

static inline int get_max_threads(const Options *options)
{
    if (options->max_threads > 0)
        return options->max_threads;
    return std::max(1,(int)std::thread::hardware_concurrency()-1);
}

} // namespace admmpd

#endif // ADMMPD_TYPES_H_
