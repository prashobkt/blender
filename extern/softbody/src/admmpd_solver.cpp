// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#include "admmpd_solver.h"
#include "admmpd_energy.h"
#include "admmpd_collision.h"
#include "admmpd_linsolve.h"
#include "admmpd_geom.h"
#include "admmpd_timer.h"

#include <Eigen/Geometry>
#include <Eigen/Sparse>

#include <stdio.h>
#include <iostream>
#include <unordered_map>
#include <numeric>

#include "BLI_task.h" // threading
#include "BLI_assert.h"

namespace admmpd {
using namespace Eigen;

// Basic timer manager class
class SolverLog {
protected:
	std::unordered_map<int,double> elapsed_ms;
	std::unordered_map<int,MicroTimer> curr_timer;
	int m_log_level; // copied from options
public:
	int &log_level() { return m_log_level; }
	void reset();
	void start_state(int state);
	double stop_state(int state); // ret time elapsed
	std::string state_string(int state);
	std::string to_string();
};
static SolverLog solverlog;

// Throws an exception for a given function with message
static inline void throw_err(const std::string &f, const std::string &m)
{
	throw std::runtime_error("Solver::"+f+": "+m);
}

bool Solver::init(
	const Mesh *mesh,
    const Options *options,
    SolverData *data)
{
	solverlog.reset();
	solverlog.log_level() = options->log_level;
	solverlog.start_state(SOLVERSTATE_INIT);

	BLI_assert(data != NULL);
	BLI_assert(options != NULL);
	BLI_assert(mesh != NULL);

    data->energies_graph.clear();
	data->indices.clear();
	data->rest_volumes.clear();
	data->weights.clear();

	switch (mesh->type())
	{
		case MESHTYPE_EMBEDDED:
		case MESHTYPE_TET: {
			data->x = *mesh->rest_prim_verts();
		} break;
		case MESHTYPE_TRIANGLE: {
			data->x = *mesh->rest_facet_verts();
		} break;
	}

	BLI_assert(data->x.rows()>0);
	BLI_assert(data->x.cols()==0);
	data->v.resize(data->x.rows(), 3);
	data->v.setZero();
	mesh->compute_masses(&data->x, options->density_kgm3, data->m);
	init_matrices(mesh,options,data);

	switch (options->linsolver)
	{
		default: {
			throw_err("init","unknown linsolver");
		} break;
		case LINSOLVER_LDLT: {
			linsolver = std::make_unique<LDLT>();
		} break;
		case LINSOLVER_PCG: {
			linsolver = std::make_unique<ConjugateGradients>();
		} break;
		case LINSOLVER_MCGS: {
			linsolver = std::make_unique<LDLT>(); // TODO
		} break;
	}

	int ne = data->indices.size();
	int nx = data->x.rows();
	if (options->log_level >= LOGLEVEL_LOW)
		printf("Solver::init:\n\tNum energy terms: %d\n\tNum verts: %d\n",ne,nx);

	solverlog.stop_state(SOLVERSTATE_INIT);
	return true;
} // end init

int Solver::solve(
	const Mesh *mesh,
	const Options *options,
	SolverData *data,
	Collision *collision)
{
	solverlog.log_level() = options->log_level;
	solverlog.start_state(SOLVERSTATE_SOLVE);

	BLI_assert(mesh != NULL);
	BLI_assert(options != NULL);
	BLI_assert(data != NULL);
	BLI_assert(LINSOLVER_LDLT);
	BLI_assert(data->x.cols() == 3);
	BLI_assert(data->x.rows() > 0);
	BLI_assert(options->max_admm_iters > 0);
	double dt = options->timestep_s;

	// If doing CCD, we can do broad phase collision here
	// and shrink the time step.

	// Init the solve which computes
	// quantaties like M_xbar and makes sure
	// the variables are sized correctly.
	solverlog.start_state(SOLVERSTATE_INIT_SOLVE);
	init_solve(mesh,options,data,collision);
	linsolver->init_solve(mesh,options,collision,data);
	solverlog.stop_state(SOLVERSTATE_INIT_SOLVE);

	// Begin solver loop
	int iters = 0;
	for (; iters < options->max_admm_iters; ++iters)
	{
		// Update ADMM z/u
		solverlog.start_state(SOLVERSTATE_LOCAL_STEP);
		solve_local_step(options,data);
		solverlog.stop_state(SOLVERSTATE_LOCAL_STEP);

		// Collision detection and linearization
		solverlog.start_state(SOLVERSTATE_COLLISION_UPDATE);
		update_collisions(options,data,collision);
		solverlog.stop_state(SOLVERSTATE_COLLISION_UPDATE);

		// Solve Ax=b s.t. Px=q and Cx=d
		solverlog.start_state(SOLVERSTATE_GLOBAL_STEP);
		data->x_prev = data->x;
		linsolver->solve(mesh,options,collision,data);
		solverlog.stop_state(SOLVERSTATE_GLOBAL_STEP);

		// Check convergence
		if (options->min_res>0)
		{
			solverlog.start_state(SOLVERSTATE_TEST_CONVERGED);
			bool converged = residual_norm(options,data) <= options->min_res;
			solverlog.stop_state(SOLVERSTATE_TEST_CONVERGED);
			if (converged)
				break;
		}

	} // end solver iters

	// Update velocity (if not static solve)
	if (dt > 0.0)
		data->v.noalias() = (data->x-data->x_start)*(1.0/dt);

	solverlog.stop_state(SOLVERSTATE_SOLVE);

	if (options->log_level >= LOGLEVEL_HIGH)
		printf("Timings:\n%s", solverlog.to_string().c_str());

	return iters;
} // end solve

double Solver::residual_norm(
	const Options *options,
	SolverData *data)
{
	(void)(options);
	double ra = ((data->D*data->x) - data->z).norm();
	double rx = (data->D*(data->x-data->x_prev)).norm();
	return ra + rx;
}

void Solver::init_solve(
	const Mesh *mesh,
	const Options *options,
	SolverData *data,
	Collision *collision)
{
	(void)(mesh);
	BLI_assert(data != NULL);
	BLI_assert(options != NULL);
	int nx = data->x.rows();
	BLI_assert(nx > 0);

	if (data->M_xbar.rows() != nx)
		data->M_xbar.resize(nx,3);

	// Initialize:
	// - update velocity with explicit forces
	// - update pin constraint matrix (goal positions)
	// - set x init guess
	double dt = std::max(0.0, options->timestep_s);
	data->x_start = data->x;
	data->x_prev = data->x;
	for (int i=0; i<nx; ++i)
	{
		data->v.row(i) += dt*options->grav;
		RowVector3d xbar_i = data->x.row(i) + dt*data->v.row(i);
		data->M_xbar.row(i) = data->m[i]*xbar_i / (dt*dt);
		data->x.row(i) = xbar_i; // initial guess
	}

	if (collision)
	{
		bool sort_tree = true;
		collision->update_bvh(&data->x_start, &data->x, sort_tree);
	}

	// ADMM variables
	data->Dx.noalias() = data->D * data->x;
	data->z = data->Dx;
	data->u.setZero();

} // end init solve

void Solver::solve_local_step(
	const Options *options,
	SolverData *data)
{
	BLI_assert(data != NULL);
	BLI_assert(options != NULL);

	struct LocalStepThreadData {
		const Options *options;
		SolverData *data;
	};

	auto parallel_zu_update = [](
		void *__restrict userdata,
		const int i,
		const TaskParallelTLS *__restrict tls)->void
	{
		(void)(tls);
		LocalStepThreadData *td = (LocalStepThreadData*)userdata;
		// We'll unnecessarily recompute Lame here, but in the future each
		// energy may have a different stiffness.
		Lame lame;
		lame.set_from_youngs_poisson(td->options->youngs,td->options->poisson);
		EnergyTerm().update(
			td->data->indices[i][0], // index
			td->data->indices[i][2], // type
			lame,
			td->data->rest_volumes[i],
			td->data->weights[i],
			&td->data->x,
			&td->data->Dx,
			&td->data->z,
			&td->data->u );
	}; // end parallel zu update

	data->Dx.noalias() = data->D * data->x;
	int ne = data->indices.size();
	BLI_assert(ne > 0);
  	LocalStepThreadData thread_data = {.options=options, .data = data};
	TaskParallelSettings settings;
	BLI_parallel_range_settings_defaults(&settings);
	BLI_task_parallel_range(0, ne, &thread_data, parallel_zu_update, &settings);

} // end local step

void Solver::update_collisions(
	const Options *options,
	SolverData *data,
	Collision *collision)
{
	BLI_assert(data != NULL);
	BLI_assert(options != NULL);

	if (collision==NULL)
		return;

	collision->update_bvh(&data->x_start, &data->x, false);
	collision->detect(options, &data->x_start, &data->x);

} // end update constraints

void Solver::init_matrices(
	const Mesh *mesh,
	const Options *options,
	SolverData *data)
{
	BLI_assert(data != NULL);
	BLI_assert(options != NULL);
	int nx = data->x.rows();
	BLI_assert(nx > 0);
	BLI_assert(data->x.cols() == 3);

	double dt = options->timestep_s;
	double dt2 = dt*dt;
	if (dt2 < 0) // static solve
		dt2 = 1.0;

	// Allocate per-vertex data
	data->x_start = data->x;
	data->M_xbar.resize(nx,3);
	data->M_xbar.setZero();
	data->Dx.resize(nx,3);
	data->Dx.setZero();
	if (data->v.rows() != nx)
	{
		data->v.resize(nx,3);
		data->v.setZero();
	}

	// Add per-element energies to data
	std::vector<Triplet<double> > trips;
	append_energies(mesh,options,data,trips);
	if (trips.size()==0)
	{
		throw_err("compute_matrices","No reduction coeffs");
	}
	int n_row_D = trips.back().row()+1;

	update_weight_matrix(options,data,n_row_D);
	RowSparseMatrix<double> W2 = data->W*data->W;

	// Mass weighted Laplacian
	data->D.resize(n_row_D,nx);
	data->D.setFromTriplets(trips.begin(), trips.end());
	data->DtW2 = data->D.transpose() * W2;
	data->A = data->DtW2 * data->D;
	data->A_diag_max = 0;
	for (int i=0; i<nx; ++i)
	{
		data->A.coeffRef(i,i) += data->m[i]/dt2;
		double Aii = data->A.coeff(i,i);
		if (Aii > data->A_diag_max)
			data->A_diag_max = Aii;
	}

	// ADMM dual/lagrange
	data->z.resize(n_row_D,3);
	data->z.setZero();
	data->u.resize(n_row_D,3);
	data->u.setZero();

} // end compute matrices

void Solver::update_weight_matrix(
	const Options *options,
	SolverData *data,
	int rows)
{
	(void)(options);
	data->W.resize(rows,rows);
	VectorXi W_nnz = VectorXi::Ones(rows);
	data->W.reserve(W_nnz);
	int ne = data->indices.size();
	if (ne != (int)data->weights.size())
		throw_err("update_weight_matrix","bad num indices/weights");

	for (int i=0; i<ne; ++i)
	{
		const Vector3i &idx = data->indices[i];
		if (idx[0]+idx[1] > rows)
			throw_err("update_weight_matrix","bad matrix dim");

		for (int j=0; j<idx[1]; ++j)
			data->W.coeffRef(idx[0]+j,idx[0]+j) = data->weights[i];
	}
	data->W.finalize();
}

void Solver::append_energies(
	const Mesh *mesh,
	const Options *options,
	SolverData *data,
	std::vector<Triplet<double> > &D_triplets)
{
	BLI_assert(mesh != NULL);
	BLI_assert(options != NULL);
	BLI_assert(data != NULL);

	const MatrixXi *elems = nullptr;
	int mesh_type = mesh->type();
	switch (mesh_type)
	{
		default: {
			throw_err("append_energies","unknown mesh type");
		} break;
		case MESHTYPE_EMBEDDED:
		case MESHTYPE_TET: {
			elems = mesh->prims(); // tets
			BLI_assert(elems->cols()==4);
		} break;
		case MESHTYPE_TRIANGLE: {
			elems = mesh->facets(); // triangles
			BLI_assert(elems->cols()==3);
		} break;
	}

	int n_elems = elems->rows();
	BLI_assert(n_elems > 0);

	int nx = data->x.rows();
	if ((int)data->energies_graph.size() != nx)
		data->energies_graph.resize(nx,std::set<int>());

	data->indices.reserve((int)data->indices.size()+n_elems);
	data->rest_volumes.reserve((int)data->rest_volumes.size()+n_elems);
	data->weights.reserve((int)data->weights.size()+n_elems);
	Lame lame;
	lame.set_from_youngs_poisson(options->youngs, options->poisson);
	lame.m_material = options->elastic_material;

	// The possibility of having an error in energy initialization
	// while still wanting to continue the simulation is very low.
	// We can parallelize this step in the future if needed.

	int energy_index = 0;
	for (int i=0; i<n_elems; ++i)
	{

		data->rest_volumes.emplace_back();
		data->weights.emplace_back();
		int energy_dim = -1;
		int energy_type = -1;

		switch (mesh_type)
		{
			case MESHTYPE_EMBEDDED:
			case MESHTYPE_TET: {
				energy_type = ENERGYTERM_TET;
				RowVector4i ele(
					elems->operator()(i,0),
					elems->operator()(i,1),
					elems->operator()(i,2),
					elems->operator()(i,3)
				);
				energy_dim = EnergyTerm().init_tet(
					energy_index,
					lame,
					ele,
					&data->x,
					data->rest_volumes.back(),
					data->weights.back(),
					D_triplets );
			} break;
			case MESHTYPE_TRIANGLE: {
				energy_type = ENERGYTERM_TRIANGLE;
				RowVector3i ele(
					elems->operator()(i,0),
					elems->operator()(i,1),
					elems->operator()(i,2)
				);
				energy_dim = EnergyTerm().init_triangle(
					energy_index,
					lame,
					ele,
					&data->x,
					data->rest_volumes.back(),
					data->weights.back(),
					D_triplets );
			} break;
		}

		// Error in initialization
		if( energy_dim <= 0 ){
			data->rest_volumes.pop_back();
			data->weights.pop_back();
			continue;
		}

		// Add stencil to graph
		int ele_dim = elems->cols();
		for (int j=0; j<ele_dim; ++j)
		{
			int ej = elems->operator()(i,j);
			for (int k=0; k<ele_dim; ++k)
			{
				int ek = elems->operator()(i,k);
				if (ej==ek)
					continue;
				data->energies_graph[ej].emplace(ek);
			}
		}

		data->indices.emplace_back(energy_index, energy_dim, energy_type);
		energy_index += energy_dim;
	}

} // end append energies

void SolverLog::reset()
{
	curr_timer.clear();
	elapsed_ms.clear();
}

void SolverLog::start_state(int state)
{
	if (m_log_level < LOGLEVEL_HIGH)
		return;

	if (m_log_level >= LOGLEVEL_DEBUG)
		printf("Starting state %s\n",state_string(state).c_str());

	if (curr_timer.count(state)==0)
	{
		elapsed_ms[state] = 0;
		curr_timer[state] = MicroTimer();
		return;
	}
	curr_timer[state].reset();
}

// Returns time elapsed
double SolverLog::stop_state(int state)
{
	if (m_log_level < LOGLEVEL_HIGH)
		return 0;

	if (m_log_level >= LOGLEVEL_DEBUG)
		printf("Stopping state %s\n",state_string(state).c_str());

	if (curr_timer.count(state)==0)
	{
		elapsed_ms[state] = 0;
		curr_timer[state] = MicroTimer();
		return 0;
	}
	double dt = curr_timer[state].elapsed_ms();
	elapsed_ms[state] += dt;
	return dt;
}

std::string SolverLog::state_string(int state)
{
	std::string str = "unknown";
	switch (state)
	{
		default: break;
		case SOLVERSTATE_INIT: str="init"; break;
		case SOLVERSTATE_SOLVE: str="solve"; break;
		case SOLVERSTATE_INIT_SOLVE: str="init_solve"; break;
		case SOLVERSTATE_LOCAL_STEP: str="local_step"; break;
		case SOLVERSTATE_GLOBAL_STEP: str="global_step"; break;
		case SOLVERSTATE_COLLISION_UPDATE: str="collision_update"; break;
		case SOLVERSTATE_TEST_CONVERGED: str="test_converged"; break;
	}
	return str;
}

std::string SolverLog::to_string()
{
	// Sort by largest time
	auto sort_ms = [](const std::pair<int, double> &a, const std::pair<int, double> &b)
		{ return (a.second > b.second); };
	std::vector<std::pair<double, int> > ms(elapsed_ms.begin(), elapsed_ms.end());
	std::sort(ms.begin(), ms.end(), sort_ms);

	// Concat string
	std::stringstream ss;
	int n_timers = ms.size();
	for (int i=0; i<n_timers; ++i)
		ss << state_string(ms[i].first) << ": " << ms[i].second << "ms" << std::endl;

	return ss.str();
}

} // namespace admmpd
