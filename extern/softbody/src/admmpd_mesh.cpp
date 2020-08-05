// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#include "admmpd_mesh.h"
#include "admmpd_geom.h"
#include "admmpd_timer.h"
#include <unordered_map>
#include <set>
#include <iostream>
#include "BLI_assert.h"
#include "BLI_task.h"

namespace admmpd {
using namespace Eigen;

static inline void throw_err(const std::string f, const std::string &msg)
{
	throw std::runtime_error("Mesh::"+f+": "+msg);
}

static void gather_octree_tets(
	Octree<double,3>::Node *node,
	const MatrixXd *emb_V,
	const MatrixXi *emb_F,
	Eigen::VectorXi *emb_v_to_tet,
    Eigen::MatrixXd *emb_barys,
	Discregrid::CubicLagrangeDiscreteGrid *sdf,
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
		}
		else
		{
			double dist = sdf->interpolate(0, node->center);
			if (dist <= 0)
				geom::create_tets_from_box(bmin,bmax,verts,tets);
		}
		return;
	}
	for (int i=0; i<8; ++i)
	{
		gather_octree_tets(node->children[i],emb_V,emb_F,emb_v_to_tet,emb_barys,sdf,verts,tets);
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
	P_updated = true;
	if (nv<=0 || verts == nullptr)
		return false;
	if (nf<=0 || faces == nullptr)
		return false;
	(void)(tets);
	(void)(nt);

	MicroTimer t;

	AlignedBox<double,3> domain;
	std::vector<double> verts_dbl(nv*3);
	emb_V0.resize(nv,3);
	for (int i=0; i<nv; ++i)
	{
		emb_V0(i,0) = verts[i*3+0];
		emb_V0(i,1) = verts[i*3+1];
		emb_V0(i,2) = verts[i*3+2];
		verts_dbl[i*3+0] = verts[i*3+0];
		verts_dbl[i*3+1] = verts[i*3+1];
		verts_dbl[i*3+2] = verts[i*3+2];
		domain.extend(emb_V0.row(i).transpose());
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
		box.extend(box.min()-Vector3d::Ones()*1e-4);
		box.extend(box.max()+Vector3d::Ones()*1e-4);
	}

//	std::cout << "T CREATE BOXES: " << t.elapsed_ms() << std::endl;
	t.reset();

 	// Create the signed distance field for inside/outside tests
	{
		Discregrid::TriangleMesh tm(verts_dbl.data(), faces, nv, nf);
		Discregrid::MeshDistance md(tm);
		domain.max() += 1e-3 * domain.diagonal().norm() * Eigen::Vector3d::Ones();
		domain.min() -= 1e-3 * domain.diagonal().norm() * Eigen::Vector3d::Ones();
		std::array<unsigned int, 3> resolution;
		resolution[0] = 30; resolution[1] = 30; resolution[2] = 30;
		emb_sdf = std::make_shared<SDFType>(Discregrid::CubicLagrangeDiscreteGrid(domain, resolution));
		auto func = Discregrid::DiscreteGrid::ContinuousFunction{};
		func = [&md](Eigen::Vector3d const& xi) {
			return md.signedDistanceCached(xi);
		};
		emb_sdf->addFunction(func, false);
	}

//	std::cout << "T SDF: " << t.elapsed_ms() << std::endl;
	t.reset();

	// Create a tree of the facets
	emb_rest_facet_tree.init(emb_leaves);

	// Create an octree to generate the tets from
	Octree<double,3> octree;
	octree.init(&emb_V0,&emb_F,options.max_subdiv_levels);

//	std::cout << "T OCTREE AND BVH TREE INIT: " << t.elapsed_ms() << std::endl;
	t.reset();

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
		emb_sdf.get(),
		lat_verts,
		lat_tets);
	geom::merge_close_vertices(lat_verts,lat_tets);

//	std::cout << "T GATHER TETS: " << t.elapsed_ms() << std::endl;
	t.reset();

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

//	std::cout << "T COMPUTE EMBEDDING: " << t.elapsed_ms() << std::endl;
	t.reset();

	// Verify embedding is correct
	for (int i=0; i<nv; ++i)
	{
		if (emb_v_to_tet[i]<0)
			throw_err("create","Failed embedding");
		if (std::abs(emb_barys.row(i).sum()-1.0)>1e-6)
		{
			std::stringstream ss; ss << emb_barys.row(i);
			throw_err("create","Bad embedding barys: "+ss.str());
		}
	}

	if (!emb_rest_facet_tree.root())
		throw_err("create","Failed to create tree");
	if (lat_V0.rows()==0)
		throw_err("create","Failed to create verts");
	if (lat_T.rows()==0)
		throw_err("create","Failed to create tets");
	if (emb_F.rows()==0)
		throw_err("create","Did not set faces");
	if (emb_V0.rows()==0)
		throw_err("create","Did not set verts");

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
		const MatrixXd *emb_V0 = td->emb_mesh->rest_facet_verts();
		Vector3d pt = emb_V0->row(i);
		PointInTetMeshTraverse<double> traverser(
				pt,
				td->lat_V0,
				td->lat_T);
		bool success = td->tree->traverse(traverser);
		int tet_idx = traverser.output.prim;
		if (success && tet_idx >= 0)
		{
			const MatrixXd *tet_V0 = td->emb_mesh->rest_prim_verts();
			RowVector4i tet = td->emb_mesh->prims()->row(tet_idx);
			Vector3d t[4] = {
				tet_V0->row(tet[0]),
				tet_V0->row(tet[1]),
				tet_V0->row(tet[2]),
				tet_V0->row(tet[3])
			};
			td->emb_v_to_tet->operator[](i) = tet_idx;
			Vector4d b = geom::point_tet_barys<double>(pt,t[0],t[1],t[2],t[3]);
			td->emb_barys->row(i) = b;
		}
	}; // end parallel find tet

	int nv = emb_V0.rows();
	if (nv==0)
	{
		throw_err("compute_embedding", "No embedded verts");
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
			throw_err("compute_embedding", "negative barycoords");
		}
		if (b.maxCoeff() > 1 + eps)
		{
			throw_err("compute_embedding", "max barycoord > 1");
		}
		if (b.sum() > 1 + eps)
		{
			throw_err("compute_embedding", "barycoord sum > 1");
		}
	}

	return true;

} // end compute vtx to tet mapping

Eigen::Vector3d EmbeddedMesh::get_mapped_facet_vertex(
	const Eigen::MatrixXd *prim_verts,
	int facet_vertex_idx) const
{
    int t_idx = emb_v_to_tet[facet_vertex_idx];
    RowVector4i tet = lat_T.row(t_idx);
    RowVector4d b = emb_barys.row(facet_vertex_idx);
    return Vector3d(
		prim_verts->row(tet[0]) * b[0] +
		prim_verts->row(tet[1]) * b[1] +
		prim_verts->row(tet[2]) * b[2] +
		prim_verts->row(tet[3]) * b[3]);
}

void EmbeddedMesh::compute_masses(
	const Eigen::MatrixXd *x,
	double density_kgm3,
	Eigen::VectorXd &m) const
{
	density_kgm3 = std::abs(density_kgm3);

	int nx = x->rows();
	m.resize(nx);
	m.setZero();
	int n_tets = lat_T.rows();
	for (int t=0; t<n_tets; ++t)
	{
		RowVector4i tet = lat_T.row(t);
		RowVector3d tet_v0 = x->row(tet[0]);
		Matrix3d edges;
		edges.col(0) = x->row(tet[1]) - tet_v0;
		edges.col(1) = x->row(tet[2]) - tet_v0;
		edges.col(2) = x->row(tet[3]) - tet_v0;
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
			throw_err("compute_masses","unref'd vertex");
			m[i]=1;
		}
	}
}

void EmbeddedMesh::set_pin(
	int idx,
	const Eigen::Vector3d &p,
    double k)
{
	std::unordered_map<int,double>::const_iterator it = emb_pin_k.find(idx);
	if (it == emb_pin_k.end()) { P_updated = true; }
	else if (k != it->second) { P_updated = true; }

	// Remove pin
	if (k <= 1e-5)
	{
		emb_pin_k.erase(idx);
		emb_pin_pos.erase(idx);
		return;
	}

	emb_pin_k[idx] = k;
	emb_pin_pos[idx] = p;
}

bool EmbeddedMesh::linearize_pins(
	std::vector<Eigen::Triplet<double> > &trips,
	std::vector<double> &q,
	std::set<int> &pin_inds,
    bool replicate) const
{
	int np = emb_pin_k.size();
	trips.reserve((int)trips.size() + np*3*4);
	q.reserve((int)q.size() + np*3);

	std::unordered_map<int,double>::const_iterator it_k = emb_pin_k.begin();
	for (; it_k != emb_pin_k.end(); ++it_k)
	{
		int emb_idx = it_k->first;
		pin_inds.emplace(emb_idx);
		const Vector3d &qi = emb_pin_pos.at(emb_idx);
		const double &ki = it_k->second;

		int tet_idx = emb_v_to_tet[emb_idx];
		RowVector4d bary = emb_barys.row(emb_idx);
		RowVector4i tet = lat_T.row(tet_idx);

		for (int i=0; i<3; ++i)
		{
			int p_idx = q.size();
			q.emplace_back(qi[i]*ki);
			if (replicate)
			{
				for (int j=0; j<4; ++j)
					trips.emplace_back(p_idx, tet[j]*3+i, bary[j]*ki);
			}
			else if (i==0)
			{
				for (int j=0; j<4; ++j)
					trips.emplace_back(p_idx/3, tet[j], bary[j]*ki);	
			}
		}
	}

	bool has_P_updated = P_updated;
	P_updated = false;
	return has_P_updated;
}

bool TetMesh::create(
	const float *verts, // size nv*3
	int nv,
	const unsigned int *faces, // size nf*3 (surface faces)
	int nf,
	const unsigned int *tets, // size nt*4
	int nt) // must be > 0
{
	P_updated = true;
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
	const Eigen::MatrixXd *x,
	double density_kgm3,
	Eigen::VectorXd &m) const
{
	density_kgm3 = std::abs(density_kgm3);

	// Source: https://github.com/mattoverby/mclscene/blob/master/include/MCL/TetMesh.hpp
	// Computes volume-weighted masses for each vertex
	// density_kgm3 is the unit-volume density
	int nx = x->rows();
	m.resize(nx);
	m.setZero();
	int n_tets = T.rows();
	for (int t=0; t<n_tets; ++t)
	{
		RowVector4i tet = T.row(t);
		for (int i=0; i<4; ++i)
		{
			if (tet[i] < 0 || tet[i] >= nx)
				throw_err("compute_masses","Bad vertex index");
		}
		RowVector3d tet_v0 = x->row(tet[0]);
		Matrix3d edges;
		edges.col(0) = x->row(tet[1]) - tet_v0;
		edges.col(1) = x->row(tet[2]) - tet_v0;
		edges.col(2) = x->row(tet[3]) - tet_v0;
		double vol = edges.determinant()/6.0;
		if (vol <= 0)
			throw_err("compute_masses","Inverted or flattened tet");

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
			throw_err("compute_masses","unref'd vertex");
			m[i]=1;
		}
	}

} // end compute masses

void TetMesh::set_pin(
	int idx,
	const Eigen::Vector3d &p,
    double k)
{
	std::unordered_map<int,double>::const_iterator it = pin_k.find(idx);
	if (it == pin_k.end()) { P_updated = true; }
	else if (k != it->second) { P_updated = true; }

	// Remove pin
	if (k <= 1e-5)
	{
		pin_k.erase(idx);
		pin_pos.erase(idx);
		return;
	}

	pin_k[idx] = k;
	pin_pos[idx] = p;
}

bool TetMesh::linearize_pins(
	std::vector<Eigen::Triplet<double> > &trips,
	std::vector<double> &q,
	std::set<int> &pin_inds,
    bool replicate) const
{
	int np = pin_k.size();
	trips.reserve((int)trips.size() + np*3);
	q.reserve((int)q.size() + np*3);

	std::unordered_map<int,double>::const_iterator it_k = pin_k.begin();
	for (; it_k != pin_k.end(); ++it_k)
	{
		int idx = it_k->first;
		pin_inds.emplace(idx);
		const Vector3d &qi = pin_pos.at(idx);
		const double &ki = it_k->second;
		for (int i=0; i<3; ++i)
		{
			int p_idx = q.size();
			q.emplace_back(qi[i]*ki);
			if (replicate)
			{
				trips.emplace_back(p_idx, idx*3+i, ki);
			}
			else if (i==0)
			{
				trips.emplace_back(p_idx/3, idx, ki);	
			}
		}
	}

	bool has_P_updated = P_updated;
	P_updated = false;
	return has_P_updated;
}

bool TriangleMesh::create(
	const float *verts,
	int nv,
	const unsigned int *faces,
	int nf,
	const unsigned int *tets,
	int nt)
{
	P_updated = true;
	(void)(tets); (void)(nt);
	if (nv<=0 || verts == nullptr)
		return false;
	if (nf<=0 || faces == nullptr)
		return false;

	V0.resize(nv,3);
	for (int i=0; i<nv; ++i)
	{
		V0(i,0) = verts[i*3+0];
		V0(i,1) = verts[i*3+1];
		V0(i,2) = verts[i*3+2];
	}

	std::vector<AlignedBox<double,3> > leaves(nf);
	F.resize(nf,3);
	for (int i=0; i<nf; ++i)
	{
		F(i,0) = faces[i*3+0];
		F(i,1) = faces[i*3+1];
		F(i,2) = faces[i*3+2];
		AlignedBox<double,3> &box = leaves[i];
		box.setEmpty();
		box.extend(V0.row(F(i,0)).transpose());
		box.extend(V0.row(F(i,1)).transpose());
		box.extend(V0.row(F(i,2)).transpose());
		box.extend(box.min()-Vector3d::Ones()*1e-4);
		box.extend(box.max()+Vector3d::Ones()*1e-4);
	}

	rest_facet_tree.init(leaves);
	return true;
}

void TriangleMesh::compute_masses(
	const Eigen::MatrixXd *x,
	double density_kgm2,
	Eigen::VectorXd &m) const
{
	int nv = x->rows();
	m.resize(nv);
	m.setZero();
	int nf = F.rows();
	for (int i=0; i<nf; ++i)
	{
		RowVector3i f = F.row(i);
		Vector3d edge1 = x->row(f[1]) - x->row(f[0]);
		Vector3d edge2 = x->row(f[2]) - x->row(f[0]);
		double area = 0.5 * (edge1.cross(edge2)).norm();
		double tri_mass = density_kgm2 * area;
		m[f[0]] += tri_mass / 3.0;
		m[f[1]] += tri_mass / 3.0;
		m[f[2]] += tri_mass / 3.0;
	}
}

void TriangleMesh::set_pin(
	int idx,
	const Eigen::Vector3d &p,
    double k)
{
	std::unordered_map<int,double>::const_iterator it = pin_k.find(idx);
	if (it == pin_k.end()) { P_updated = true; }
	else if (k != it->second) { P_updated = true; }

	// Remove pin if effectively zero
	if (k <= 1e-5)
	{
		pin_k.erase(idx);
		pin_pos.erase(idx);
		return;
	}

	pin_pos[idx] = p;
	pin_k[idx] = k;
}

bool TriangleMesh::linearize_pins(
	std::vector<Eigen::Triplet<double> > &trips,
	std::vector<double> &q,
	std::set<int> &pin_inds,
    bool replicate) const
{
	int np = pin_k.size();
	trips.reserve((int)trips.size() + np*3);
	q.reserve((int)q.size() + np*3);

	std::unordered_map<int,double>::const_iterator it_k = pin_k.begin();
	for (; it_k != pin_k.end(); ++it_k)
	{
		int idx = it_k->first;
		pin_inds.emplace(idx);
		const Vector3d &qi = pin_pos.at(idx);
		const double &ki = it_k->second;
		for (int i=0; i<3; ++i)
		{
			int p_idx = q.size();
			q.emplace_back(qi[i]*ki);
			if (replicate)
			{
				trips.emplace_back(p_idx, idx*3+i, ki);
			}
			else if (i==0)
			{
				trips.emplace_back(p_idx/3, idx, ki);	
			}
		}
	}

	bool has_P_updated = P_updated;
	P_updated = false;
	return has_P_updated;
}

} // namespace admmpd
