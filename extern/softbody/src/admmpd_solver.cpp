// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#include "admmpd_solver.h"
#include "admmpd_energy.h"
#include "admmpd_collision.h"
#include "admmpd_linsolve.h"
#include "admmpd_geom.h"

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

static inline void throw_err(const std::string &f, const std::string &m)
{
	throw std::runtime_error("Solver::"+f+": "+m);
}

bool Solver::init(
	const Mesh *mesh,
    const Options *options,
    SolverData *data)
{
	BLI_assert(data != NULL);
	BLI_assert(options != NULL);
	BLI_assert(mesh);
	BLI_assert(mesh->prims().cols()==4);

	data->x = mesh->rest_prim_verts();
	data->v.resize(data->x.rows(), 3);
	data->v.setZero();
	mesh->compute_masses(data->x, options->density_kgm3, data->m);
	init_matrices(mesh,options,data);

	int nt = mesh->prims().rows();
	int nx = data->x.rows();
	printf("Solver::init:\n\tNum tets: %d\n\tNum verts: %d\n",nt,nx);

	return true;
} // end init

int Solver::solve(
	const Mesh *mesh,
	const Options *options,
	SolverData *data,
	Collision *collision)
{
	BLI_assert(data != NULL);
	BLI_assert(options != NULL);
	BLI_assert(data->x.cols() == 3);
	BLI_assert(data->x.rows() > 0);
	BLI_assert(options->max_admm_iters > 0);

	update_pin_matrix(mesh,options,data); // P,q
	update_global_matrix(options,data); // A+PtP

	ConjugateGradients cg;
	cg.init_solve(options,data);
	double dt = options->timestep_s;

	// Init the solve which computes
	// quantaties like M_xbar and makes sure
	// the variables are sized correctly.
	init_solve(mesh,options,data,collision);

	// Begin solver loop
	int iters = 0;
	for (; iters < options->max_admm_iters; ++iters)
	{
		// Update ADMM z/u
		solve_local_step(options,data);

		// Collision detection and linearization
		update_collisions(options,data,collision);

		// Solve Ax=b s.t. Px=q and Cx=d
		data->x_prev = data->x;
		cg.solve(options,data,collision);

		// Check convergence
		if (options->min_res>0)
		{
			if (residual_norm(options,data) < options->min_res)
				break;
		}

	} // end solver iters

	// Update velocity (if not static solve)
	if (dt > 0.0)
		data->v.noalias() = (data->x-data->x_start)*(1.0/dt);

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
	(void)(collision);

	if (data->M_xbar.rows() != nx)
		data->M_xbar.resize(nx,3);

	// Initialize:
	// - update velocity with explicit forces
	// - update pin constraint matrix (goal positions)
	// - set x init guess
	double dt = std::max(0.0, options->timestep_s);
	data->x_start = data->x;
	for (int i=0; i<nx; ++i)
	{
		data->v.row(i) += dt*options->grav;
		RowVector3d xbar_i = data->x.row(i) + dt*data->v.row(i);
		data->M_xbar.row(i) = data->m[i]*xbar_i / (dt*dt);
		data->x.row(i) = xbar_i; // initial guess
	}

	if (collision)
	{
		collision->init_bvh(&data->x_start, &data->x);
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
		Lame lame;
		lame.set_from_youngs_poisson(td->options->youngs,td->options->poisson);
		EnergyTerm().update(
			td->data->indices[i][0],
			td->data->indices[i][2],
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

	collision->detect(options, &data->x_start, &data->x);

	std::vector<double> d_coeffs;
	std::vector<Eigen::Triplet<double> > trips;

	// TODO collision detection
	collision->linearize(
		&data->x,
		&trips,
		&d_coeffs);

	// Check number of constraints.
	// If no constraints, clear Jacobian.
	int nx = data->x.rows();
	int nc = d_coeffs.size();
	if (nc==0)
	{
		data->d.setZero();
		data->C.setZero();
		return;
	}

	// Otherwise update the data.
	data->d = Map<VectorXd>(d_coeffs.data(), d_coeffs.size());
	data->C.resize(nc,nx*3);
	data->C.setFromTriplets(trips.begin(),trips.end());

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

	// Constraint data
	data->C.resize(1,nx*3);
	data->d = VectorXd::Zero(1);
	data->P.resize(1,nx*3);
	data->q = VectorXd::Zero(1);

	// Mass weighted Laplacian
	data->D.resize(n_row_D,nx);
	data->D.setFromTriplets(trips.begin(), trips.end());
	data->DtW2 = data->D.transpose() * W2;
	update_global_matrix(options,data);

	// Perform factorization
	data->ldlt_A3.compute(data->A3_plus_PtP);

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

void Solver::update_pin_matrix(
	const Mesh *mesh,
	const Options *options,
	SolverData *data)
{
	(void)(options);
	int nx = data->x.rows();
	if (nx==0)
		return;

	// Create pin constraint matrix
	std::vector<Triplet<double> > trips;
	std::vector<double> q_coeffs;
	mesh->linearize_pins(trips, q_coeffs);

	if (q_coeffs.size()==0)
	{ // no springs
		data->P.resize(1,nx*3);
		data->P.setZero();
		data->q = VectorXd::Zero(1);
	}
	else
	{ // Scale stiffness by A diagonal max
		int np = q_coeffs.size();
		data->P.resize(np,nx*3);
		data->P.setFromTriplets(trips.begin(), trips.end());
		data->q = Map<VectorXd>(q_coeffs.data(), q_coeffs.size());
	}
}

void Solver::update_global_matrix(
	const Options *options,
	SolverData *data)
{
	int nx = data->x.rows();

	if (data->DtW2.rows() != data->x.rows())
		throw_err("update_global_matrix","bad matrix dim");
	
	if (data->m.rows() != data->x.rows())
		throw_err("update_global_matrix","no masses");

	if (data->P.cols() != nx*3)
		throw_err("update_global_matrix","no pin matrix");

	double dt = options->timestep_s;
	double dt2 = dt*dt;
	if (dt2 < 0) // static solve
		dt2 = 1.0;

	data->A = data->DtW2 * data->D;
	data->A_diag_max = 0;
	for (int i=0; i<nx; ++i)
	{
		data->A.coeffRef(i,i) += data->m[i]/dt2;
		double Aii = data->A.coeff(i,i);
		if (Aii>data->A_diag_max)
			data->A_diag_max = Aii;
	}

	SparseMatrix<double> A3;
	geom::make_n3<double>(data->A,A3);
	double pk = options->mult_pk * data->A_diag_max;
	data->A3_plus_PtP = A3 + pk * data->P.transpose()*data->P;
}

void Solver::append_energies(
	const Mesh *mesh,
	const Options *options,
	SolverData *data,
	std::vector<Triplet<double> > &D_triplets)
{
	BLI_assert(data != NULL);
	BLI_assert(options != NULL);
	Ref<const MatrixXi> tets = mesh->prims();
	int nt = tets.rows();
	BLI_assert(nt > 0);

	int nx = data->x.rows();
	if ((int)data->energies_graph.size() != nx)
		data->energies_graph.resize(nx,std::set<int>());

	data->indices.reserve(nt);
	data->rest_volumes.reserve(nt);
	data->weights.reserve(nt);
	Lame lame;
	lame.set_from_youngs_poisson(options->youngs, options->poisson);
	lame.m_material = options->elastic_material;

	// The possibility of having an error in energy initialization
	// while still wanting to continue the simulation is very low.
	// We can parallelize this step.

	int energy_index = 0;
	for (int i=0; i<nt; ++i)
	{
		RowVector4i ele = tets.row(i);

		// Initialize the energy
		data->rest_volumes.emplace_back();
		data->weights.emplace_back();
		int energy_dim = EnergyTerm().init_tet(
			energy_index,
			lame,
			ele,
			&data->x,
			data->rest_volumes.back(),
			data->weights.back(),
			D_triplets );

		// Error in initialization
		if( energy_dim <= 0 ){
			data->rest_volumes.pop_back();
			data->weights.pop_back();
			continue;
		}

		// Record energy indices
		int ele_dim = ele.cols();
		for (int j=0; j<ele_dim; ++j)
		{
			int ej = ele[j];
			for (int k=0; k<ele_dim; ++k)
			{
				int ek = ele[k];
				if (ej==ek)
					continue;
				data->energies_graph[ej].emplace(ek);
			}
		}
		data->indices.emplace_back(energy_index, energy_dim, ENERGYTERM_TET);
		energy_index += energy_dim;
	}

} // end append energies

} // namespace admmpd
