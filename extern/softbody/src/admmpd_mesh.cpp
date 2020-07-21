// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#include "admmpd_mesh.h"
#include "admmpd_geom.h"
#include <unordered_map>
#include <set>
#include <iostream>
#include "BLI_assert.h"
#include "BLI_task.h"

namespace admmpd {
using namespace Eigen;

static void gather_octree_tets(
	Octree<double,3>::Node *node,
	const MatrixXd *emb_V,
	const MatrixXi *emb_F,
	Eigen::VectorXi *emb_v_to_tet,
    Eigen::MatrixXd *emb_barys,
	AABBTree<double,3> *face_tree,
	std::vector<Vector3d> &verts,
	std::vector<RowVector4i> &tets
	)
{
	if (node == nullptr)
		return;

	bool is_leaf = node->is_leaf();
	bool has_prims = (int)node->prims.size()>0;
	if (is_leaf)
	{
		Vector3d bmin = node->center-Vector3d::Ones()*node->halfwidth;
		Vector3d bmax = node->center+Vector3d::Ones()*node->halfwidth;

		// If we have primitives in the cell,
		// create tets and compute embedding
		if (has_prims)
		{
//			int prev_tets = tets.size();
			geom::create_tets_from_box(bmin,bmax,verts,tets);
//			int num_box_tets = (int)tets.size()-prev_tets;
/*
			// Loop over faces embedded in this box.
			// Loop over tets created for this box.
			// Find which tet the face vertex is embedded in.
			int np = node->prims.size();
			for (int i=0; i<np; ++i)
			{
				for (int j=0; j<3; ++j)
				{
					int v_idx = emb_F->operator()(i,j);
					Vector3d v = emb_V->row(v_idx);
					for (int k=0; k<num_box_tets; ++k)
					{
						const RowVector4i &t = tets[prev_tets+k];
						if (geom::point_in_tet(v,verts[t[0]],verts[t[1]],verts[t[2]],verts[t[3]]))
						{
							emb_v_to_tet->operator[](v_idx) = prev_tets+k;
							emb_barys->row(v_idx) = geom::point_tet_barys(v,
								verts[t[0]],verts[t[1]],verts[t[2]],verts[t[3]]);
							break;
						}

					}
				}
			}
*/
		}
		else
		{
			// Otherwise, launch a ray
			// to determine if we are inside or outside
			// the mesh. If we're outside, don't create tets.
			PointInTriangleMeshTraverse<double> pt_in_mesh(node->center,emb_V,emb_F);
			face_tree->traverse(pt_in_mesh);
			if (pt_in_mesh.output.is_inside())
				geom::create_tets_from_box(bmin,bmax,verts,tets);
		}
		return;
	}
	for (int i=0; i<8; ++i)
	{
		gather_octree_tets(node->children[i],emb_V,emb_F,emb_v_to_tet,emb_barys,face_tree,verts,tets);
	}

} // end gather octree tets

bool EmbeddedMesh::create(
	const float *verts, // size nv*3
	int nv,
	const unsigned int *faces, // size nf*3
	int nf,
	const unsigned int *tets, // ignored
	int nt)
{
	if (nv<=0 || verts == nullptr)
		return false;
	if (nf<=0 || faces == nullptr)
		return false;
	(void)(tets);
	(void)(nt);

	emb_V0.resize(nv,3);
	for (int i=0; i<nv; ++i)
	{
		emb_V0(i,0) = verts[i*3+0];
		emb_V0(i,1) = verts[i*3+1];
		emb_V0(i,2) = verts[i*3+2];
	}

	emb_F.resize(nf,3);
	std::vector<AlignedBox<double,3> > emb_leaves(nf);
	for (int i=0; i<nf; ++i)
	{
		emb_F(i,0) = faces[i*3+0];
		emb_F(i,1) = faces[i*3+1];
		emb_F(i,2) = faces[i*3+2];
		AlignedBox<double,3> &box = emb_leaves[i];
		box.extend(emb_V0.row(emb_F(i,0)).transpose());
		box.extend(emb_V0.row(emb_F(i,1)).transpose());
		box.extend(emb_V0.row(emb_F(i,2)).transpose());
		box.extend(box.min()-Vector3d::Ones()*1e-8);
		box.extend(box.max()+Vector3d::Ones()*1e-8);
	}

	Octree<double,3> octree;
	octree.init(&emb_V0,&emb_F,options.max_subdiv_levels);
	emb_rest_facet_tree.init(emb_leaves);

	emb_v_to_tet.resize(nv);
	emb_v_to_tet.array() = -1;
   	emb_barys.resize(nv,4);
	emb_barys.setZero();

	std::vector<Vector3d> lat_verts;
	std::vector<RowVector4i> lat_tets;
	Octree<double,3>::Node *root = octree.root().get();
	gather_octree_tets(
		root,
		&emb_V0,
		&emb_F,
		&emb_v_to_tet,
		&emb_barys,
		&emb_rest_facet_tree,
		lat_verts,
		lat_tets);
	geom::merge_close_vertices(lat_verts,lat_tets);

	int nlv = lat_verts.size();
	lat_V0.resize(nlv,3);
	for (int i=0; i<nlv; ++i)
	{
		for(int j=0; j<3; ++j){
			lat_V0(i,j) = lat_verts[i][j];
		}
	}
	int nlt = lat_tets.size();
	lat_T.resize(nlt,4);
	for(int i=0; i<nlt; ++i){
		for(int j=0; j<4; ++j){
			lat_T(i,j) = lat_tets[i][j];
		}
	}

	compute_embedding();

	auto return_error = [](const std::string &msg)
	{
		printf("EmbeddedMesh::generate create: %s\n", msg.c_str());
		return false;
	};

	// Verify embedding is correct
	for (int i=0; i<nv; ++i)
	{
		if (emb_v_to_tet[i]<0)
			return return_error("Failed embedding");
		if (std::abs(emb_barys.row(i).sum()-1.0)>1e-6)
		{
			std::stringstream ss; ss << emb_barys.row(i);
			return return_error("Bad embedding barys: "+ss.str());
		}
	}

	if (!emb_rest_facet_tree.root())
		return return_error("Failed to create tree");
	if (lat_V0.rows()==0)
		return return_error("Failed to create verts");
	if (lat_T.rows()==0)
		return return_error("Failed to create tets");
	if (emb_F.rows()==0)
		return return_error("Did not set faces");
	if (emb_V0.rows()==0)
		return return_error("Did not set verts");

	return true;
}

bool EmbeddedMesh::compute_embedding()
{
	struct FindTetThreadData {
		AABBTree<double,3> *tree;
		EmbeddedMesh *emb_mesh; // thread sets vtx_to_tet and barys
		MatrixXd *lat_V0;
		MatrixXi *lat_T;
		MatrixXd *emb_barys;
		VectorXi *emb_v_to_tet;
	};

	auto parallel_point_in_tet = [](
		void *__restrict userdata,
		const int i,
		const TaskParallelTLS *__restrict tls)->void
	{
		(void)(tls);
		FindTetThreadData *td = (FindTetThreadData*)userdata;
		Vector3d pt = td->emb_mesh->rest_facet_verts().row(i);
		PointInTetMeshTraverse<double> traverser(
				pt,
				td->lat_V0,
				td->lat_T);
		bool success = td->tree->traverse(traverser);
		int tet_idx = traverser.output.prim;
		if (success && tet_idx >= 0)
		{
			RowVector4i tet = td->emb_mesh->prims().row(tet_idx);
			Vector3d t[4] = {
				td->emb_mesh->rest_prim_verts().row(tet[0]),
				td->emb_mesh->rest_prim_verts().row(tet[1]),
				td->emb_mesh->rest_prim_verts().row(tet[2]),
				td->emb_mesh->rest_prim_verts().row(tet[3])
			};
			td->emb_v_to_tet->operator[](i) = tet_idx;
			Vector4d b = geom::point_tet_barys(pt,t[0],t[1],t[2],t[3]);
			td->emb_barys->row(i) = b;
		}
	}; // end parallel find tet

	int nv = emb_V0.rows();
	if (nv==0)
	{
		printf("**EmbeddedMesh::compute_embedding: No embedded vertices");
		return false;
	}

	emb_barys.resize(nv,4);
	emb_barys.setOnes();
	emb_v_to_tet.resize(nv);
	int nt = lat_T.rows();

	// BVH tree for finding point-in-tet and computing
	// barycoords for each embedded vertex
	std::vector<AlignedBox<double,3> > tet_aabbs;
	tet_aabbs.resize(nt);
	Vector3d veta = Vector3d::Ones()*1e-12;
	for (int i=0; i<nt; ++i)
	{
		tet_aabbs[i].setEmpty();
		RowVector4i tet = lat_T.row(i);
		for (int j=0; j<4; ++j)
			tet_aabbs[i].extend(lat_V0.row(tet[j]).transpose());

		tet_aabbs[i].extend(tet_aabbs[i].min()-veta);
		tet_aabbs[i].extend(tet_aabbs[i].max()+veta);
	}

	AABBTree<double,3> tree;
	tree.init(tet_aabbs);

	FindTetThreadData thread_data = {
		.tree = &tree,
		.emb_mesh = this,
		.lat_V0 = &lat_V0,
		.lat_T = &lat_T,
		.emb_barys = &emb_barys,
		.emb_v_to_tet = &emb_v_to_tet
	};
	TaskParallelSettings settings;
	BLI_parallel_range_settings_defaults(&settings);
	BLI_task_parallel_range(0, nv, &thread_data, parallel_point_in_tet, &settings);

	// Double check we set (valid) barycoords for every embedded vertex
	const double eps = 1e-8;
	for (int i=0; i<nv; ++i)
	{
		RowVector4d b = emb_barys.row(i);
		if (b.minCoeff() < -eps)
		{
			printf("**Lattice::generate Error: negative barycoords\n");
			return false;
		}
		if (b.maxCoeff() > 1 + eps)
		{
			printf("**Lattice::generate Error: max barycoord > 1\n");
			return false;
		}
		if (b.sum() > 1 + eps)
		{
			printf("**Lattice::generate Error: barycoord sum > 1\n");
			return false;
		}
	}

	return true;

} // end compute vtx to tet mapping

Eigen::Vector3d EmbeddedMesh::get_mapped_facet_vertex(
	Eigen::Ref<const Eigen::MatrixXd> prim_verts,
	int facet_vertex_idx) const
{
    int t_idx = emb_v_to_tet[facet_vertex_idx];
    RowVector4i tet = lat_T.row(t_idx);
    RowVector4d b = emb_barys.row(facet_vertex_idx);
    return Vector3d(
		prim_verts.row(tet[0]) * b[0] +
		prim_verts.row(tet[1]) * b[1] +
		prim_verts.row(tet[2]) * b[2] +
		prim_verts.row(tet[3]) * b[3]);
}

void EmbeddedMesh::compute_masses(
	Eigen::Ref<const Eigen::MatrixXd> x,
	double density_kgm3,
	Eigen::VectorXd &m) const
{
	density_kgm3 = std::abs(density_kgm3);

	int nx = x.rows();
	m.resize(nx);
	m.setZero();
	int n_tets = lat_T.rows();
	for (int t=0; t<n_tets; ++t)
	{
		RowVector4i tet = lat_T.row(t);
		RowVector3d tet_v0 = x.row(tet[0]);
		Matrix3d edges;
		edges.col(0) = x.row(tet[1]) - tet_v0;
		edges.col(1) = x.row(tet[2]) - tet_v0;
		edges.col(2) = x.row(tet[3]) - tet_v0;
		double vol = std::abs((edges).determinant()/6.f);
		double tet_mass = density_kgm3 * vol;
		m[tet[0]] += tet_mass / 4.f;
		m[tet[1]] += tet_mass / 4.f;
		m[tet[2]] += tet_mass / 4.f;
		m[tet[3]] += tet_mass / 4.f;
	}

	// Verify masses
	for (int i=0; i<nx; ++i)
	{
		if (m[i] <= 0.0)
		{
			printf("**EmbeddedMesh::compute_masses Error: unreferenced vertex\n");
			m[i]=1;
		}
	}
}

void EmbeddedMesh::set_pin(
	int idx,
	const Eigen::Vector3d &p,
	const Eigen::Vector3d &k)
{
	if (idx<0 || idx>=emb_V0.rows())
		return;

	if (k.maxCoeff()<=0)
		return;

	emb_pin_k[idx] = k;
	emb_pin_pos[idx] = p;
}

void EmbeddedMesh::linearize_pins(
	std::vector<Eigen::Triplet<double> > &trips,
	std::vector<double> &q) const
{
	int np = emb_pin_k.size();
	trips.reserve((int)trips.size() + np*3*4);
	q.reserve((int)q.size() + np*3);

	std::unordered_map<int,Eigen::Vector3d>::const_iterator it_k = emb_pin_k.begin();
	for (; it_k != emb_pin_k.end(); ++it_k)
	{
		int emb_idx = it_k->first;
		const Vector3d &qi = emb_pin_pos.at(emb_idx);
		const Vector3d &ki = it_k->second;

		int tet_idx = emb_v_to_tet[emb_idx];
		RowVector4d bary = emb_barys.row(emb_idx);
		RowVector4i tet = lat_T.row(tet_idx);

		for (int i=0; i<3; ++i)
		{
			int p_idx = q.size();
			q.emplace_back(qi[i]*ki[i]);
			for (int j=0; j<4; ++j)
				trips.emplace_back(p_idx, tet[j]*3+i, bary[j]*ki[i]);
		}
	}
}

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
	std::vector<AlignedBox<double,3> > leaves(nf);
	for (int i=0; i<nf; ++i)
	{
		F(i,0) = faces[i*3+0];
		F(i,1) = faces[i*3+1];
		F(i,2) = faces[i*3+2];
		leaves.emplace_back();
		AlignedBox<double,3> &box = leaves[i];
		box.extend(V0.row(F(i,0)).transpose());
		box.extend(V0.row(F(i,1)).transpose());
		box.extend(V0.row(F(i,2)).transpose());
		box.extend(box.min()-Vector3d::Ones()*1e-8);
		box.extend(box.max()+Vector3d::Ones()*1e-8);
	}

	T.resize(nt,4);
	for (int i=0; i<nt; ++i)
	{
		T(i,0) = tets[i*4+0];
		T(i,1) = tets[i*4+1];
		T(i,2) = tets[i*4+2];
		T(i,3) = tets[i*4+3];
	}

	rest_facet_tree.init(leaves);
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
