// Copyright Matt Overby 2020.
// Distributed under the MIT License.
#if 0

#include "admmpd_embeddedmesh.h"
#include "admmpd_geom.h"
#include "admmpd_bvh.h"
#include "admmpd_bvh_traverse.h"

#include <iostream>
#include <fstream>
#include <unordered_map>
#include <set>
#include <numeric>

#include "BLI_task.h" // threading
#include "BLI_assert.h"

namespace admmpd {
using namespace Eigen;

// Gen lattice with subdivision
struct LatticeData {
	//SDF<double> *emb_sdf;
	const Eigen::MatrixXd *V;
	const Eigen::MatrixXi *F;
	std::vector<Vector3d> verts;
	std::vector<Vector4i> tets;
};

static inline void merge_close_vertices(LatticeData *data, double eps=1e-12)
{
	int nv = data->verts.size();
	std::vector<Vector3d> new_v(nv); // new verts
	std::vector<int> idx(nv,0); // index mapping
	std::vector<int> visited(nv,0);
	int count = 0;
	for (int i=0; i<nv; ++i)
	{
		if(!visited[i])
		{
			visited[i] = 1;
			new_v[count] = data->verts[i];
			idx[i] = count;
			Vector3d vi = data->verts[i];
			for (int j = i+1; j<nv; ++j)
			{
				if((data->verts[j]-vi).norm() < eps)
				{
					visited[j] = 1;
					idx[j] = count;
				}
			}
			count++;
		}
	}
	new_v.resize(count);
	data->verts = new_v;
	int nt = data->tets.size();
	for (int i=0; i<nt; ++i)
	{
		for (int j=0; j<4; ++j)
		{
			data->tets[i][j] = idx[data->tets[i][j]];
		}
	}
}

static inline void add_tets_from_box(
	const Vector3d &min,
	const Vector3d &max,
	std::vector<Vector3d> &verts,
	std::vector<Vector4i> &tets)
{
	std::vector<Vector3d> v = {
		// Top plane, clockwise looking down
		max,
		Vector3d(min[0], max[1], max[2]),
		Vector3d(min[0], max[1], min[2]),
		Vector3d(max[0], max[1], min[2]),
		// Bottom plane, clockwise looking down
		Vector3d(max[0], min[1], max[2]),
		Vector3d(min[0], min[1], max[2]),
		min,
		Vector3d(max[0], min[1], min[2])
	};
	// Add vertices and get indices of the box
	std::vector<int> b;
	for(int i=0; i<8; ++i)
	{
		b.emplace_back(verts.size());
		verts.emplace_back(v[i]);
	}
	// From the box, create five new tets
	std::vector<Vector4i> new_tets = {
		Vector4i( b[0], b[5], b[7], b[4] ),
		Vector4i( b[5], b[7], b[2], b[0] ),
		Vector4i( b[5], b[0], b[2], b[1] ),
		Vector4i( b[7], b[2], b[0], b[3] ),
		Vector4i( b[5], b[2], b[7], b[6] )
	};
	for(int i=0; i<5; ++i)
		tets.emplace_back(new_tets[i]);
};

static void gather_octree_tets(
	Octree<double,3>::Node *node,
	const MatrixXd *V, const MatrixXi *F,
	AABBTree<double,3> *face_tree,
	std::vector<Vector3d> &verts,
	std::vector<Vector4i> &tets
	)
{
	if (node == nullptr)
	{
		return;
	}

	bool is_leaf = node->is_leaf();
	bool has_prims = (int)node->prims.size()>0;
	if (is_leaf)
	{
		Vector3d bmin = node->center-Vector3d::Ones()*node->halfwidth;
		Vector3d bmax = node->center+Vector3d::Ones()*node->halfwidth;

		// If we have primitives in the cell,
		// create tets. Otherwise, launch a ray
		// to determine if we are inside or outside
		// the mesh. If we're outside, don't create tets.
		if (has_prims)
		{
			add_tets_from_box(bmin,bmax,verts,tets);
		}
		else
		{
			PointInTriangleMeshTraverse<double> pt_in_mesh(node->center,V,F);
			face_tree->traverse(pt_in_mesh);
			if (pt_in_mesh.output.is_inside())
				add_tets_from_box(bmin,bmax,verts,tets);
		}
		return;
	}
	for (int i=0; i<8; ++i)
	{
		gather_octree_tets(node->children[i],V,F,face_tree,verts,tets);
	}

} // end gather octree tets


bool EmbeddedMesh::generate(
	const Eigen::MatrixXd &V, // embedded verts
	const Eigen::MatrixXi &F, // embedded faces
	bool trim_lattice,
	int subdiv_levels)
{
	emb_faces = F;
	emb_rest_x = V;

	if (F.rows()==0 || V.rows()==0)
		throw std::runtime_error("EmbeddedMesh::generate Error: Missing data");

	LatticeData data;
	data.V = &V;
	data.F = &F;

	Octree<double,3> octree;
	octree.init(&V,&F,subdiv_levels);

	int nf = F.rows();
	std::vector<AlignedBox<double,3> > face_boxes(nf);
	for (int i=0; i<nf; ++i)
	{
		face_boxes[i].extend(V.row(F(i,0)).transpose());
		face_boxes[i].extend(V.row(F(i,1)).transpose());
		face_boxes[i].extend(V.row(F(i,2)).transpose());
	}

	emb_rest_tree.init(face_boxes);

	Octree<double,3>::Node *root = octree.root().get();
	gather_octree_tets(root,&V,&F,&emb_rest_tree,data.verts,data.tets);
	merge_close_vertices(&data);

	int nv = data.verts.size();
	lat_rest_x.resize(nv,3);
	for (int i=0; i<nv; ++i)
	{
		for(int j=0; j<3; ++j){
			lat_rest_x(i,j) = data.verts[i][j];
		}
	}
	int nt = data.tets.size();
	lat_tets.resize(nt,4);
	for(int i=0; i<nt; ++i){
		for(int j=0; j<4; ++j){
			lat_tets(i,j) = data.tets[i][j];
		}
	}

	// Now compute the baryweighting for embedded vertices
	bool embed_success = compute_embedding();

	if (!emb_rest_tree.root())
		throw std::runtime_error("EmbeddedMesh::generate Error: Failed to create tree");
	if (lat_rest_x.rows()==0)
		throw std::runtime_error("EmbeddedMesh::generate Error: Failed to create verts");
	if (lat_tets.rows()==0)
		throw std::runtime_error("EmbeddedMesh::generate Error: Failed to create tets");
	if (emb_faces.rows()==0)
		throw std::runtime_error("EmbeddedMesh::generate Error: Did not set faces");
	if (emb_rest_x.rows()==0)
		throw std::runtime_error("EmbeddedMesh::generate Error: Did not set verts");
	if (!embed_success)
		throw std::runtime_error("EmbeddedMesh::generate Error: Failed embedding");

	// Export the mesh for funsies
//	std::ofstream of("v_lattice.txt"); of << lat_rest_x; of.close();
//	std::ofstream of2("t_lattice.txt"); of2 << lat_tets; of2.close();

	return embed_success;

} // end gen lattice

void EmbeddedMesh::compute_masses(
	Eigen::VectorXd *masses_tets, // masses of the lattice verts
	double density_kgm3)
{
	BLI_assert(masses_tets != NULL);
	BLI_assert(density_kgm3 > 0);

	// TODO
	// map the area of the surface to the tet vertices

	// Source: https://github.com/mattoverby/mclscene/blob/master/include/MCL/TetMesh.hpp
	// Computes volume-weighted masses for each vertex
	// density_kgm3 is the unit-volume density
	int nx = lat_rest_x.rows();
	masses_tets->resize(nx);
	masses_tets->setZero();
	int n_tets = lat_tets.rows();
	for (int t=0; t<n_tets; ++t)
	{
		RowVector4i tet = lat_tets.row(t);
		RowVector3d tet_v0 = lat_rest_x.row(tet[0]);
		Matrix3d edges;
		edges.col(0) = lat_rest_x.row(tet[1]) - tet_v0;
		edges.col(1) = lat_rest_x.row(tet[2]) - tet_v0;
		edges.col(2) = lat_rest_x.row(tet[3]) - tet_v0;
		double vol = std::abs((edges).determinant()/6.f);
		double tet_mass = density_kgm3 * vol;
		masses_tets->operator[](tet[0]) += tet_mass / 4.f;
		masses_tets->operator[](tet[1]) += tet_mass / 4.f;
		masses_tets->operator[](tet[2]) += tet_mass / 4.f;
		masses_tets->operator[](tet[3]) += tet_mass / 4.f;
	}

	// Verify masses
	for (int i=0; i<nx; ++i)
	{
		if (masses_tets->operator[](i) <= 0.0)
		{
			printf("**EmbeddedMesh::compute_masses Error: unreferenced vertex\n");
			masses_tets->operator[](i)=1;
		}
	}
} // end compute masses

typedef struct FindTetThreadData {
	AABBTree<double,3> *tree;
	EmbeddedMesh *emb_mesh; // thread sets vtx_to_tet and barys
} FindTetThreadData;

static void parallel_point_in_tet(
	void *__restrict userdata,
	const int i,
	const TaskParallelTLS *__restrict tls)
{
	(void)(tls);
	FindTetThreadData *td = (FindTetThreadData*)userdata;
	Vector3d pt = td->emb_mesh->emb_rest_x.row(i);
	PointInTetMeshTraverse<double> traverser(
			pt,
			&td->emb_mesh->lat_rest_x,
			&td->emb_mesh->lat_tets);
	bool success = td->tree->traverse(traverser);
	int tet_idx = traverser.output.prim;
	if (success && tet_idx >= 0)
	{
		RowVector4i tet = td->emb_mesh->lat_tets.row(tet_idx);
		Vector3d t[4] = {
			td->emb_mesh->lat_rest_x.row(tet[0]),
			td->emb_mesh->lat_rest_x.row(tet[1]),
			td->emb_mesh->lat_rest_x.row(tet[2]),
			td->emb_mesh->lat_rest_x.row(tet[3])
		};
		td->emb_mesh->emb_vtx_to_tet[i] = tet_idx;
		Vector4d b = geom::point_tet_barys(pt,t[0],t[1],t[2],t[3]);
		td->emb_mesh->emb_barys.row(i) = b;
	}
} // end parallel lin solve

bool EmbeddedMesh::compute_embedding()
{
	int nv = emb_rest_x.rows();
	if (nv==0)
	{
		printf("**EmbeddedMesh::compute_embedding: No embedded vertices");
		return false;
	}

	emb_barys.resize(nv,4);
	emb_barys.setOnes();
	emb_vtx_to_tet.resize(nv);
	int nt = lat_tets.rows();

	// BVH tree for finding point-in-tet and computing
	// barycoords for each embedded vertex
	std::vector<AlignedBox<double,3> > tet_aabbs;
	tet_aabbs.resize(nt);
	Vector3d veta = Vector3d::Ones()*1e-12;
	for (int i=0; i<nt; ++i)
	{
		tet_aabbs[i].setEmpty();
		RowVector4i tet = lat_tets.row(i);
		for (int j=0; j<4; ++j)
			tet_aabbs[i].extend(lat_rest_x.row(tet[j]).transpose());

		tet_aabbs[i].extend(tet_aabbs[i].min()-veta);
		tet_aabbs[i].extend(tet_aabbs[i].max()+veta);
	}

	AABBTree<double,3> tree;
	tree.init(tet_aabbs);

	FindTetThreadData thread_data = {
		.tree = &tree,
		.emb_mesh = this
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

Eigen::Vector3d EmbeddedMesh::get_mapped_vertex(
	const Eigen::MatrixXd *x_data, int idx) const
{
    int t_idx = emb_vtx_to_tet[idx];
    RowVector4i tet = lat_tets.row(t_idx);
    RowVector4d b = emb_barys.row(idx);
    return Vector3d(
		x_data->row(tet[0]) * b[0] +
		x_data->row(tet[1]) * b[1] +
		x_data->row(tet[2]) * b[2] +
		x_data->row(tet[3]) * b[3]);
}

} // namespace admmpd

#endif