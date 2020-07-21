// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#include "admmpd_solver.h"
#include "admmpd_energy.h"
#include "admmpd_collision.h"
#include "admmpd_linsolve.h"

#include <Eigen/Geometry>
#include <Eigen/Sparse>

#include <stdio.h>
#include <iostream>
#include <unordered_map>

#include "BLI_task.h" // threading
#include "BLI_assert.h"

namespace admmpd {
using namespace Eigen;

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
	data->tets = mesh->prims();
	mesh->compute_masses(data->x, options->density_kgm3, data->m);
	if (!compute_matrices(options,data))
		return false;

	printf("Solver::init:\n\tNum tets: %d\n\tNum verts: %d\n",(int)data->tets.rows(),(int)data->x.rows());

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
	BLI_assert(data->A.nonZeros() > 0);
	BLI_assert(options->max_admm_iters > 0);

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

		// Solve Ax=b s.t. Cx=d
		ConjugateGradients().solve(options,data,collision);
		//GaussSeidel().solve(options,data,collision);

	} // end solver iters

	// Update velocity (if not static solve)
	double dt = options->timestep_s;
	if (dt > 0.0)
		data->v.noalias() = (data->x-data->x_start)*(1.0/dt);

	return iters;
} // end solve

void Solver::init_solve(
	const Mesh *mesh,
	const Options *options,
	SolverData *data,
	Collision *collision)
{
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
		data->M_xbar.row(i) = data->m[i]*xbar_i;
		data->x.row(i) = xbar_i; // initial guess
	}

	// Create pin constraint matrix
	std::vector<Triplet<double> > trips;
	std::vector<double> q_coeffs;
	mesh->linearize_pins(trips, q_coeffs);
	if (q_coeffs.size()==0)
	{ // no springs
		data->PtP.resize(nx*3,nx*3);
		data->PtP.setZero();
		data->Ptq.resize(nx*3);
		data->Ptq.setZero();
	}
	else
	{ // Scale stiffness by A diagonal max
		double pin_k_scale = options->mult_pk * data->A_diag_max;
		int np = q_coeffs.size();
		RowSparseMatrix<double> P(np, nx*3);
		P.setFromTriplets(trips.begin(), trips.end());
		data->PtP = pin_k_scale * P.transpose()*P;
		VectorXd q = Map<VectorXd>(q_coeffs.data(), q_coeffs.size());
		data->Ptq = pin_k_scale * P.transpose()*q;
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

	collision->detect(&data->x_start, &data->x);

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

bool Solver::compute_matrices(
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
	append_energies(options,data,trips);
	if (trips.size()==0)
	{
		printf("**admmpd::Solver Error: No reduction coeffs\n");
		return false;
	}
	int n_row_D = trips.back().row()+1;
	double dt2 = options->timestep_s * options->timestep_s;
	if (options->timestep_s <= 0)
		dt2 = 1.0; // static solve, use dt=1 to not scale matrices

	// Diagonal weight matrix
	RowSparseMatrix<double> W2(n_row_D,n_row_D);
	VectorXi W_nnz = VectorXi::Ones(n_row_D);
	W2.reserve(W_nnz);
	int ne = data->indices.size();
	for (int i=0; i<ne; ++i)
	{
		const Vector2i &idx = data->indices[i];
		for (int j=0; j<idx[1]; ++j)
			W2.coeffRef(idx[0]+j,idx[0]+j) = data->weights[i]*data->weights[i];
	}

	// Mass weighted Laplacian
	data->D.resize(n_row_D,nx);
	data->D.setFromTriplets(trips.begin(), trips.end());
	data->DtW2 = dt2 * data->D.transpose() * W2;
	data->A = data->DtW2 * data->D;
	for (int i=0; i<nx; ++i)
		data->A.coeffRef(i,i) += data->m[i];
	data->ldltA.compute(data->A);
	data->b.resize(nx,3);
	data->b.setZero();
	data->A_diag_max = data->A.diagonal().lpNorm<Infinity>();

	// Constraint data
	data->C.resize(1,nx*3);
	data->d = VectorXd::Zero(1);

	data->PtP.resize(nx*3,nx*3);
	data->Ptq.resize(nx*3);
	data->Ptq.setZero();

	// ADMM dual/lagrange
	data->z.resize(n_row_D,3);
	data->z.setZero();
	data->u.resize(n_row_D,3);
	data->u.setZero();

	return true;

} // end compute matrices

void Solver::append_energies(
	const Options *options,
	SolverData *data,
	std::vector<Triplet<double> > &D_triplets)
{
	BLI_assert(data != NULL);
	BLI_assert(options != NULL);
	int nt = data->tets.rows();
	BLI_assert(nt > 0);

	int nx = data->x.rows();
	if ((int)data->energies_graph.size() != nx)
		data->energies_graph.resize(nx,std::set<int>());

	data->indices.reserve(nt);
	data->rest_volumes.reserve(nt);
	data->weights.reserve(nt);
	Lame lame;
	lame.set_from_youngs_poisson(options->youngs, options->poisson);

	// The possibility of having an error in energy initialization
	// while still wanting to continue the simulation is very low.
	// We can parallelize this step if need be.

	int energy_index = 0;
	for (int i=0; i<nt; ++i)
	{
		RowVector4i ele = data->tets.row(i);

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
		data->indices.emplace_back(energy_index, energy_dim);
		energy_index += energy_dim;
	}

} // end append energies

} // namespace admmpd
