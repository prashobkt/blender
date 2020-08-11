// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_TYPES_H_
#define ADMMPD_TYPES_H_

#include <Eigen/Geometry>
#include <Eigen/SparseCholesky>
#include <thread>
#include <vector>
#include <set>
#include <Discregrid/All> // SDF
#include "admmpd_bvh.h"

namespace admmpd {

template <typename T> using RowSparseMatrix = Eigen::SparseMatrix<T,Eigen::RowMajor>;
typedef Eigen::SimplicialLDLT<Eigen::SparseMatrix<double> > Cholesky;
typedef Discregrid::CubicLagrangeDiscreteGrid SDFType;

#define MESHTYPE_EMBEDDED 0
#define MESHTYPE_TET 1
#define MESHTYPE_TRIANGLE 2
#define MESHTYPE_NUM 3

#define ENERGYTERM_TET 0
#define ENERGYTERM_TRIANGLE 1
#define ENERGYTERM_NUM 2

#define ELASTIC_ARAP 0 // As-rigid-as-possible
#define ELASTIC_NH 1 // NeoHookean
#define ELASTIC_NUM 2

#define SOLVERSTATE_INIT 0
#define SOLVERSTATE_SOLVE 1
#define SOLVERSTATE_INIT_SOLVE 2
#define SOLVERSTATE_LOCAL_STEP 3
#define SOLVERSTATE_GLOBAL_STEP 4
#define SOLVERSTATE_COLLISION_UPDATE 5
#define SOLVERSTATE_TEST_CONVERGED 6
#define SOLVERSTATE_NUM 7

#define LOGLEVEL_NONE 0
#define LOGLEVEL_LOW 1
#define LOGLEVEL_HIGH 2
#define LOGLEVEL_DEBUG 3
#define LOGLEVEL_NUM 4

#define LINSOLVER_LDLT 0 // Eigen's LDL^T
#define LINSOLVER_PCG 1 // Precon. Conj. Grad.
#define LINSOLVER_MCGS 2 // Multi-Color Gauss-Siedel
#define LINSOLVER_NUM 3

struct Options {
    double timestep_s;
    int log_level;
    int linsolver;
    int max_admm_iters;
    int max_cg_iters;
    int max_gs_iters;
    int max_threads; // -1 = auto (num cpu threads - 1)
    int elastic_material; // ENUM, see admmpd_energy.h
    int substeps; // used externally, ignore in solve()
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
        log_level(LOGLEVEL_NONE),
        linsolver(LINSOLVER_PCG),
        max_admm_iters(20),
        max_cg_iters(10),
        max_gs_iters(100),
        max_threads(-1),
        elastic_material(ELASTIC_ARAP),
        substeps(1),
        gs_omega(1),
        mult_ck(3),
        mult_pk(3),
        min_res(1e-6),
        youngs(1000000),
        poisson(0.399),
        density_kgm3(1522),
        floor(-std::numeric_limits<double>::max()),
        collision_thickness(1e-6),
        self_collision(false),
        grav(0,0,-9.8)
        {}
};

class SolverData {
public:
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
    // Set in append_energies:
    std::vector<std::set<int> > energies_graph; // per-vertex adjacency list (graph)
	std::vector<Eigen::Vector3i> indices; // per-energy index into D (row, num rows, type)
	std::vector<double> rest_volumes; // per-energy rest volume
	std::vector<double> weights; // per-energy weights
    struct LinSolveData {
        LinSolveData() : last_pk(0) {}
        LinSolveData(const LinSolveData &src); // see comments below
        mutable std::unique_ptr<Cholesky> ldlt_A_PtP; // see copy constructor
        double last_pk; // buffered to flag P update
        Eigen::MatrixXd rhs; // Mxbar + DtW2(z-u) + Ptq + Ctd
        Eigen::MatrixXd Ptq;
        Eigen::MatrixXd Ctd;
        Eigen::SparseMatrix<double> A_PtP;
        Eigen::SparseMatrix<double> A_PtP_3; // replicated
        RowSparseMatrix<double> A_PtP_CtC_3;
        Eigen::MatrixXd r;
        Eigen::MatrixXd z;
        Eigen::MatrixXd p;
        Eigen::VectorXd p3;
        Eigen::MatrixXd Ap;
    } ls;
    struct CollisionData {
        std::vector<Eigen::AlignedBox<double,3> > prim_boxes;
        AABBTree<double,3> prim_tree;
    } col;
};

static inline int get_max_threads(const Options *options)
{
    if (options->max_threads > 0)
        return options->max_threads;
    return std::max(1,(int)std::thread::hardware_concurrency()-1);
}

// Copying the LinSolveData is a special operation.
// Basically everything can be copied easily except the
// Cholesky decomp (the one thing we want to avoid recomputing).
// Because of this, we "move" the copied SolverData's ptr
// to the new copy, thus setting src's ptr to null. 
inline SolverData::LinSolveData::LinSolveData(const LinSolveData &src)
{
    this->last_pk = src.last_pk;
    this->rhs = src.rhs;
    this->Ptq = src.Ptq;
    this->Ctd = src.Ctd;
    this->A_PtP = src.A_PtP;
    this->A_PtP_3 = src.A_PtP_3;
    this->A_PtP_CtC_3 = src.A_PtP_CtC_3;
    this->r = src.r;
    this->z = src.z;
    this->p = src.p;
    this->p3 = src.p3;
    this->Ap = src.Ap;
    this->ldlt_A_PtP = std::move(src.ldlt_A_PtP);
}

} // namespace admmpd

#endif // ADMMPD_TYPES_H_
