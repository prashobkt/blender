// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#include "admmpd_mesh.h"
#include "admmpd_geom.h"
#include <unordered_map>
#include <set>
#include <iostream>
#include "BLI_assert.h"

namespace admmpd {
using namespace Eigen;

bool TetMesh::create(
	const float *verts, // size nv*3
	int nv,
	const unsigned int *faces, // size nf*3 (surface faces)
	int nf,
	const unsigned int *tets, // size nt*4
	int nt) // must be > 0
{
	if (nv<=0 || verts == nullptr)
		return false;
	if (nf<=0 || faces == nullptr)
		return false;
	if (nt<=0 || tets == nullptr)
		return false;

	V0.resize(nv,3);
	for (int i=0; i<nv; ++i)
	{
		V0(i,0) = verts[i*3+0];
		V0(i,1) = verts[i*3+1];
		V0(i,2) = verts[i*3+2];
	}

	F.resize(nf,3);
	for (int i=0; i<nf; ++i)
	{
		F(i,0) = faces[i*3+0];
		F(i,1) = faces[i*3+1];
		F(i,2) = faces[i*3+2];
	}

	T.resize(nt,4);
	for (int i=0; i<nt; ++i)
	{
		T(i,0) = tets[i*4+0];
		T(i,1) = tets[i*4+1];
		T(i,2) = tets[i*4+2];
		T(i,3) = tets[i*4+3];
	}

	return true;

} // end TetMesh create

void TetMesh::compute_masses(
	Eigen::Ref<const Eigen::MatrixXd> x,
	double density_kgm3,
	Eigen::VectorXd &m) const
{
	density_kgm3 = std::abs(density_kgm3);

	// Source: https://github.com/mattoverby/mclscene/blob/master/include/MCL/TetMesh.hpp
	// Computes volume-weighted masses for each vertex
	// density_kgm3 is the unit-volume density
	int nx = x.rows();
	m.resize(nx);
	m.setZero();
	int n_tets = T.rows();
	for (int t=0; t<n_tets; ++t)
	{
		RowVector4i tet = T.row(t);
		for (int i=0; i<4; ++i)
		{
			if (tet[i] < 0 || tet[i] >= nx)
				throw std::runtime_error("TetMesh::compute_masses Error: Bad vertex index\n");
		}
		RowVector3d tet_v0 = x.row(tet[0]);
		Matrix3d edges;
		edges.col(0) = x.row(tet[1]) - tet_v0;
		edges.col(1) = x.row(tet[2]) - tet_v0;
		edges.col(2) = x.row(tet[3]) - tet_v0;
		double vol = edges.determinant()/6.0;
		if (vol <= 0)
			throw std::runtime_error("TetMesh::compute_masses Error: Inverted or flattened tet\n");

		double tet_mass = density_kgm3 * vol;
		m[tet[0]] += tet_mass / 4.0;
		m[tet[1]] += tet_mass / 4.0;
		m[tet[2]] += tet_mass / 4.0;
		m[tet[3]] += tet_mass / 4.0;
	}

	// Verify masses
	for (int i=0; i<nx; ++i)
	{
		if (m[i] <= 0.0)
		{
			printf("**TetMesh::compute_masses Error: unreferenced vertex\n");
			m[i]=1;
		}
	}

} // end compute masses

void TetMesh::set_pin(
	int idx,
	const Eigen::Vector3d &p,
	const Eigen::Vector3d &k)
{
	if (k.maxCoeff() <= 0)
		return;

	pin_k[idx] = k;
	pin_pos[idx] = p;
}

void TetMesh::linearize_pins(
	std::vector<Eigen::Triplet<double> > &trips,
	std::vector<double> &q) const
{
	int np = pin_k.size();
	trips.reserve((int)trips.size() + np*3);
	q.reserve((int)q.size() + np*3);

	std::unordered_map<int,Eigen::Vector3d>::const_iterator it_k = pin_k.begin();
	for (; it_k != pin_k.end(); ++it_k)
	{
		int idx = it_k->first;
		const Vector3d &qi = pin_pos.at(idx);
		const Vector3d &ki = it_k->second;
		for (int i=0; i<3; ++i)
		{
			int p_idx = q.size();
			q.emplace_back(qi[i]*ki[i]);
			trips.emplace_back(p_idx, idx*3+i, ki[i]);
		}
	}
}

} // namespace admmpd
