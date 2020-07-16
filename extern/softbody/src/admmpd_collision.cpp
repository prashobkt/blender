// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#include "admmpd_collision.h"
#include "admmpd_bvh_traverse.h"
#include "admmpd_geom.h"

#include "BLI_assert.h"
#include "BLI_task.h"
#include "BLI_threads.h"

#include <iostream>
#include <sstream>

namespace admmpd {
using namespace Eigen;

VFCollisionPair::VFCollisionPair() :
    p_idx(-1), // point
    p_is_obs(0), // 0 or 1
    q_idx(-1), // face
    q_is_obs(0), // 0 or 1
	q_bary(0,0,0)
	{}

void Collision::set_obstacles(
	const float *v0,
	const float *v1,
	int nv,
	const unsigned int *faces,
	int nf)
{
	(void)(v0);
	if (nv==0 || nf==0)
	{
//		obsdata.mesh = admmpd::EmbeddedMesh();
		return;
	}

	if (obsdata.V.rows() != nv)
		obsdata.V.resize(nv,3);
	for (int i=0; i<nv; ++i)
	{
		for (int j=0; j<3; ++j)
			obsdata.V(i,j) = v1[i*3+j];
	}

	if ((int)obsdata.leaves.size() != nf)
		obsdata.leaves.resize(nf);
	if (obsdata.F.rows() != nf)
		obsdata.F.resize(nf,3);
	for (int i=0; i<nf; ++i)
	{
		obsdata.leaves[i].setEmpty();
		for (int j=0; j<3; ++j)
		{
			obsdata.F(i,j) = faces[i*3+j];
			Vector3d vi = obsdata.V.row(obsdata.F(i,j)).transpose();
			obsdata.leaves[i].extend(vi);
		}
	}

	obsdata.tree.init(obsdata.leaves);

} // end add obstacle

std::pair<bool,VFCollisionPair>
Collision::detect_against_obs(
        const Eigen::Vector3d &pt,
        const ObstacleData *obs) const
{
	std::pair<bool,VFCollisionPair> ret = 
		std::make_pair(false, VFCollisionPair());

	if (!obs->has_obs())
		return ret;

	PointInTriangleMeshTraverse<double> pt_in_mesh(pt,&obs->V,&obs->F);
	obs->tree.traverse(pt_in_mesh);
	if (pt_in_mesh.output.num_hits()%2==0)
		return ret;

	NearestTriangleTraverse<double> nearest_tri(pt,&obs->V,&obs->F);
	obs->tree.traverse(nearest_tri);

	ret.first = true;
	ret.second.q_idx = nearest_tri.output.prim;
	ret.second.q_is_obs = true;
	ret.second.q_pt = nearest_tri.output.pt_on_tri;
	return ret;
}

int EmbeddedMeshCollision::detect(
	const Eigen::MatrixXd *x0,
	const Eigen::MatrixXd *x1)
{
	if (mesh==NULL)
		return 0;

	// Do we even need to process collisions?
	if (!this->settings.floor_collision &&
		(!this->settings.obs_collision || !obsdata.has_obs()) &&
		!this->settings.self_collision)
		return 0;

	if (!tet_tree.root())
		throw std::runtime_error("EmbeddedMeshCollision::Detect: No tree");

	// We store the results of the collisions in a per-vertex buffer.
	// This is a workaround so we can create them in threads.
	int nev = mesh->emb_rest_x.rows();
	if ((int)per_vertex_pairs.size() != nev)
		per_vertex_pairs.resize(nev, std::vector<VFCollisionPair>());

	//
	// Thread data for detection
	//
	typedef struct {
		const Collision::Settings *settings;
		const Collision *collision;
		const TetMeshData *tetmesh;
		const EmbeddedMesh *embmesh;
		const Collision::ObstacleData *obsdata;
		const Eigen::MatrixXd *x0;
		const Eigen::MatrixXd *x1;
		std::vector<std::vector<VFCollisionPair> > *per_vertex_pairs;
	} DetectThreadData;

	//
	// Detection function for a single embedded vertex
	//
	auto per_embedded_vertex_detect = [](
		void *__restrict userdata,
		const int vi,
		const TaskParallelTLS *__restrict tls)->void
	{
		(void)(tls);
		DetectThreadData *td = (DetectThreadData*)userdata;
		if (td->embmesh == nullptr)
			return;

		std::vector<VFCollisionPair> &vi_pairs = td->per_vertex_pairs->at(vi);
		vi_pairs.clear();
		Vector3d pt_t1 = td->embmesh->get_mapped_vertex(td->x1,vi);

		// Special case, check if we are below the floor
		if (td->settings->floor_collision)
		{
			if (pt_t1[2] < td->settings->floor_z)
			{
				vi_pairs.emplace_back();
				VFCollisionPair &pair = vi_pairs.back();
				pair.p_idx = vi;
				pair.p_is_obs = false;
				pair.q_idx = -1;
				pair.q_is_obs = 1;
				pair.q_bary.setZero();
				pair.q_pt = Vector3d(pt_t1[0],pt_t1[1],td->settings->floor_z);
			}
		}

		// Detect against obstacles
		if (td->settings->obs_collision)
		{
			std::pair<bool,VFCollisionPair> pt_hit_obs =
				td->collision->detect_against_obs(pt_t1,td->obsdata);
			if (pt_hit_obs.first)
			{
				pt_hit_obs.second.p_idx = vi;
				pt_hit_obs.second.p_is_obs = false;
				vi_pairs.emplace_back(pt_hit_obs.second);
			}
		}

		// Detect against self
		if (td->settings->self_collision)
		{
			std::pair<bool,VFCollisionPair> pt_hit_self =
				td->collision->detect_against_self(vi, pt_t1, td->x1);
			if (pt_hit_self.first)
			{
				vi_pairs.emplace_back(pt_hit_self.second);
			}
		}

	}; // end detect for a single embedded vertex

	DetectThreadData thread_data = {
		.settings = &settings,
		.collision = this,
		.tetmesh = nullptr,
		.embmesh = mesh,
		.obsdata = &obsdata,
		.x0 = x0,
		.x1 = x1,
		.per_vertex_pairs = &per_vertex_pairs
	};

	TaskParallelSettings thrd_settings;
	BLI_parallel_range_settings_defaults(&thrd_settings);
	BLI_task_parallel_range(0, nev, &thread_data, per_embedded_vertex_detect, &thrd_settings);

	vf_pairs.clear();
	for (int i=0; i<nev; ++i)
	{
		int pvp = per_vertex_pairs[i].size();
		for (int j=0; j<pvp; ++j)
			vf_pairs.emplace_back(Vector2i(i,j));
	}

	return vf_pairs.size();
} // end detect

void EmbeddedMeshCollision::init_bvh(
	const Eigen::MatrixXd *x0,
	const Eigen::MatrixXd *x1)
{

	(void)(x0);
	int nt = mesh->lat_tets.rows();
	if (nt==0)
		throw std::runtime_error("EmbeddedMeshCollision::init_bvh: No tets");

	if ((int)tet_boxes.size() != nt)
		tet_boxes.resize(nt);

	// Update leaf boxes
	for (int i=0; i<nt; ++i)
	{
		RowVector4i tet = mesh->lat_tets.row(i);
		tet_boxes[i].setEmpty();
		tet_boxes[i].extend(x1->row(tet[0]).transpose());
		tet_boxes[i].extend(x1->row(tet[1]).transpose());
		tet_boxes[i].extend(x1->row(tet[2]).transpose());
		tet_boxes[i].extend(x1->row(tet[3]).transpose());
	}

	tet_tree.init(tet_boxes);
}

void EmbeddedMeshCollision::update_bvh(
	const Eigen::MatrixXd *x0,
	const Eigen::MatrixXd *x1)
{
	if (!tet_tree.root())
	{
		init_bvh(x0,x1);
		return;
	}

	int nt = mesh->lat_tets.rows();
	if ((int)tet_boxes.size() != nt)
		tet_boxes.resize(nt);

	// Update leaf boxes
	for (int i=0; i<nt; ++i)
	{
		RowVector4i tet = mesh->lat_tets.row(i);
		tet_boxes[i].setEmpty();
		tet_boxes[i].extend(x1->row(tet[0]).transpose());
		tet_boxes[i].extend(x1->row(tet[1]).transpose());
		tet_boxes[i].extend(x1->row(tet[2]).transpose());
		tet_boxes[i].extend(x1->row(tet[3]).transpose());
	}

	tet_tree.update(tet_boxes);

} // end update bvh

void EmbeddedMeshCollision::update_constraints(
    const Eigen::MatrixXd *x0,
    const Eigen::MatrixXd *x1)
{

	(void)(x0); (void)(x1);
//	BLI_assert(x != NULL);
//	BLI_assert(x->cols() == 3);

//	int np = vf_pairs.size();
//	if (np==0)
//		return;

	//int nx = x->rows();
//	d->reserve((int)d->size() + np);
//	trips->reserve((int)trips->size() + np*3*4);

//	for (int i=0; i<np; ++i)
//	{
//		const Vector2i &pair_idx = vf_pairs[i];
//	}
}

// Self collisions
std::pair<bool,VFCollisionPair>
EmbeddedMeshCollision::detect_against_self(
	int pt_idx,
	const Eigen::Vector3d &pt,
	const Eigen::MatrixXd *x) const
{
	std::pair<bool,VFCollisionPair> ret = 
		std::make_pair(false, VFCollisionPair());

	int self_tet_idx = mesh->emb_vtx_to_tet[pt_idx];
	RowVector4i self_tet = mesh->lat_tets.row(self_tet_idx);
	std::vector<int> skip_tet_inds = {self_tet[0],self_tet[1],self_tet[2],self_tet[3]};
	PointInTetMeshTraverse<double> pt_in_tet(pt,x,&mesh->lat_tets,skip_tet_inds);
	bool in_mesh = tet_tree.traverse(pt_in_tet);
	if (!in_mesh)
		return ret;

	// Transform point to rest shape
	int tet_idx = pt_in_tet.output.prim;
	RowVector4i tet = mesh->lat_tets.row(tet_idx);
	Vector4d barys = geom::point_tet_barys(pt,
		x->row(tet[0]), x->row(tet[1]),
		x->row(tet[2]), x->row(tet[3]));
	if (barys.minCoeff()<-1e-8 || barys.sum() > 1+1e-8)
	{
		std::cout << barys.transpose() << std::endl;
		throw std::runtime_error("EmbeddedMeshCollision: Bad tet barys");
	}

	Vector3d rest_pt =
		barys[0]*mesh->lat_rest_x.row(tet[0])+
		barys[1]*mesh->lat_rest_x.row(tet[1])+
		barys[2]*mesh->lat_rest_x.row(tet[2])+
		barys[3]*mesh->lat_rest_x.row(tet[3]);

	// Find triangle surface projection that doesn't
	// include the penetration vertex
	std::vector<int> skip_tri_inds = {pt_idx};
	NearestTriangleTraverse<double> nearest_tri(rest_pt,
		&mesh->emb_rest_x,&mesh->emb_faces,skip_tri_inds);
	mesh->emb_rest_tree.traverse(nearest_tri);

	if (nearest_tri.output.prim<0)
		throw std::runtime_error("EmbeddedMeshCollision: Failed to find triangle");

	// If we're on the "wrong" side of the nearest
	// triangle, we're probably outside the mesh.
	RowVector3i hit_face = mesh->emb_faces.row(nearest_tri.output.prim);
	Vector3d tri_v[3] = {
		mesh->emb_rest_x.row(hit_face[0]),
		mesh->emb_rest_x.row(hit_face[1]),
		mesh->emb_rest_x.row(hit_face[2])
	};

	Vector3d tri_n = (tri_v[1]-tri_v[0]).cross(tri_v[2]-tri_v[0]);
	tri_n.normalize();
	bool wrong_side = tri_n.dot(rest_pt-nearest_tri.output.pt_on_tri) > 0;
	if (wrong_side)
		return ret;


	ret.first = true;
	ret.second.p_idx = pt_idx;
	ret.second.p_is_obs = false;
	ret.second.q_idx = nearest_tri.output.prim;
	ret.second.q_is_obs = false;
	ret.second.q_pt = nearest_tri.output.pt_on_tri;

	// Compute barycoords of projection
	RowVector3i f = mesh->emb_faces.row(nearest_tri.output.prim);
	Vector3d v3[3] = {
		mesh->emb_rest_x.row(f[0]),
		mesh->emb_rest_x.row(f[1]),
		mesh->emb_rest_x.row(f[2])};
	ret.second.q_bary = geom::point_triangle_barys<double>(
		nearest_tri.output.pt_on_tri, v3[0], v3[1], v3[2]);
	if (ret.second.q_bary.minCoeff()<-1e-8 || ret.second.q_bary.sum() > 1+1e-8)
	{
		std::cout << barys.transpose() << std::endl;
		throw std::runtime_error("EmbeddedMeshCollision: Bad triangle barys");
	}

	return ret;
}


void EmbeddedMeshCollision::graph(
	std::vector<std::set<int> > &g)
{
	int np = vf_pairs.size();
	if (np==0)
		return;

	int nv = mesh->lat_rest_x.rows();
	if ((int)g.size() < nv)
		g.resize(nv, std::set<int>());

	for (int i=0; i<np; ++i)
	{
		Vector2i pair_idx = vf_pairs[i];
		VFCollisionPair &pair = per_vertex_pairs[pair_idx[0]][pair_idx[1]];
		std::set<int> stencil;

		if (!pair.p_is_obs)
		{
			int tet_idx = mesh->emb_vtx_to_tet[pair.p_idx];
			RowVector4i tet = mesh->lat_tets.row(tet_idx);
			stencil.emplace(tet[0]);
			stencil.emplace(tet[1]);
			stencil.emplace(tet[2]);
			stencil.emplace(tet[3]);
		}
		if (!pair.q_is_obs)
		{
			RowVector3i emb_face = mesh->emb_faces.row(pair.q_idx);
			for (int j=0; j<3; ++j)
			{
				int tet_idx = mesh->emb_vtx_to_tet[emb_face[j]];
				RowVector4i tet = mesh->lat_tets.row(tet_idx);
				stencil.emplace(tet[0]);
				stencil.emplace(tet[1]);
				stencil.emplace(tet[2]);
				stencil.emplace(tet[3]);	
			}
		}

		for (std::set<int>::iterator it = stencil.begin();
			it != stencil.end(); ++it)
		{
			for (std::set<int>::iterator it2 = stencil.begin();
				it2 != stencil.end(); ++it2)
			{
				if (*it == *it2)
					continue;
				g[*it].emplace(*it2);
			}
		}
	}
} // end graph

void EmbeddedMeshCollision::linearize(
	const Eigen::MatrixXd *x,
	std::vector<Eigen::Triplet<double> > *trips,
	std::vector<double> *d)
{
	BLI_assert(x != NULL);
	BLI_assert(x->cols() == 3);

	int np = vf_pairs.size();
	if (np==0)
		return;

	//int nx = x->rows();
	d->reserve((int)d->size() + np);
	trips->reserve((int)trips->size() + np*3*4);

	for (int i=0; i<np; ++i)
	{
		const Vector2i &pair_idx = vf_pairs[i];
		VFCollisionPair &pair = per_vertex_pairs[pair_idx[0]][pair_idx[1]];
		int emb_p_idx = pair.p_idx;
		Vector3d p_pt = mesh->get_mapped_vertex(x,emb_p_idx);

		//
		// If we collided with an obstacle
		//
		if (pair.q_is_obs)
		{
			Vector3d q_n(0,0,0);

			// Special case, floor
			if (pair.q_idx == -1)
			{
				q_n = Vector3d(0,0,1);
			}
			else
			{
				RowVector3i q_inds = obsdata.F.row(pair.q_idx);
				Vector3d q_tris[3] = {
					obsdata.V.row(q_inds[0]),
					obsdata.V.row(q_inds[1]),
					obsdata.V.row(q_inds[2])
				};
				q_n = ((q_tris[1]-q_tris[0]).cross(q_tris[2]-q_tris[0]));
				q_n.normalize();

				// Update constraint linearization
				pair.q_pt = geom::point_on_triangle<double>(p_pt,
					q_tris[0], q_tris[1], q_tris[2]);
			}

			// Get the four deforming verts that embed
			// the surface vertices, and add constraints on those.
			RowVector4d bary = mesh->emb_barys.row(emb_p_idx);
			int tet_idx = mesh->emb_vtx_to_tet[emb_p_idx];
			RowVector4i tet = mesh->lat_tets.row(tet_idx);
			int c_idx = d->size();
			d->emplace_back(q_n.dot(pair.q_pt));
			for (int j=0; j<4; ++j)
			{
				trips->emplace_back(c_idx, tet[j]*3+0, bary[j]*q_n[0]);
				trips->emplace_back(c_idx, tet[j]*3+1, bary[j]*q_n[1]);
				trips->emplace_back(c_idx, tet[j]*3+2, bary[j]*q_n[2]);
			}

		} // end q is obs
		else
		{
			int c_idx = d->size();
			d->emplace_back(0);

			// Compute the normal in the deformed space
			RowVector3i q_face = mesh->emb_faces.row(pair.q_idx);
			Vector3d q_v0 = mesh->get_mapped_vertex(x,q_face[0]);
			Vector3d q_v1 = mesh->get_mapped_vertex(x,q_face[1]);
			Vector3d q_v2 = mesh->get_mapped_vertex(x,q_face[2]);
			Vector3d q_n = (q_v1-q_v0).cross(q_v2-q_v0);
			q_n.normalize();

			// The penetrating vertex:
			{
				int tet_idx = mesh->emb_vtx_to_tet[emb_p_idx];
				RowVector4d bary = mesh->emb_barys.row(emb_p_idx);
				RowVector4i tet = mesh->lat_tets.row(tet_idx);
				for (int j=0; j<4; ++j)
				{
					trips->emplace_back(c_idx, tet[j]*3+0, bary[j]*q_n[0]);
					trips->emplace_back(c_idx, tet[j]*3+1, bary[j]*q_n[1]);
					trips->emplace_back(c_idx, tet[j]*3+2, bary[j]*q_n[2]);
				}
			}

			// The intersected face:
			for (int j=0; j<3; ++j)
			{
				int emb_q_idx = q_face[j];
				RowVector4d bary = mesh->emb_barys.row(emb_q_idx);
				int tet_idx = mesh->emb_vtx_to_tet[emb_q_idx];
				RowVector4i tet = mesh->lat_tets.row(tet_idx);
				for (int k=0; k<4; ++k)
				{
					trips->emplace_back(c_idx, tet[k]*3+0, -pair.q_bary[j]*bary[k]*q_n[0]);
					trips->emplace_back(c_idx, tet[k]*3+1, -pair.q_bary[j]*bary[k]*q_n[1]);
					trips->emplace_back(c_idx, tet[k]*3+2, -pair.q_bary[j]*bary[k]*q_n[2]);
				}
			}

		} // end q is obs
	
	} // end loop pairs

} // end jacobian

} // namespace admmpd

/*
std::pair<bool,VFCollisionPair>
Collision::detect_point_against_mesh(
        int pt_idx,
        bool pt_is_obs,
        const Eigen::Vector3d &pt,
        bool mesh_is_obs,
        const EmbeddedMesh *emb_mesh,
        const Eigen::MatrixXd *mesh_tets_x,
        const AABBTree<double,3> *mesh_tets_tree) const
{
	std::pair<bool,VFCollisionPair> ret = 
		std::make_pair(false, VFCollisionPair());

	if (mesh_tets_x->rows()==0)
		return ret;

	// Point in tet?
	PointInTetMeshTraverse<double> pt_in_tet(pt,mesh_tets_x,&emb_mesh->lat_tets);
	bool in_mesh = mesh_tets_tree->traverse(pt_in_tet);
	if (!in_mesh)
		return ret;

	// Transform to rest shape
	int tet_idx = pt_in_tet.output.prim;
	RowVector4i tet = emb_mesh->lat_tets.row(tet_idx);
	Vector4d barys = geom::point_tet_barys(pt,
		mesh_tets_x->row(tet[0]), mesh_tets_x->row(tet[1]),
		mesh_tets_x->row(tet[2]), mesh_tets_x->row(tet[3]));
	Vector3d rest_pt =
		barys[0]*emb_mesh->lat_rest_x.row(tet[0])+
		barys[1]*emb_mesh->lat_rest_x.row(tet[1])+
		barys[2]*emb_mesh->lat_rest_x.row(tet[2])+
		barys[3]*emb_mesh->lat_rest_x.row(tet[3]);

	// We are inside the lattice. Find nearest
	// face and see if we are on the inside of the mesh.
	NearestTriangleTraverse<double> nearest_tri(rest_pt,
		&emb_mesh->emb_rest_x,&emb_mesh->emb_faces);
	emb_mesh->emb_rest_tree.traverse(nearest_tri);

	if (nearest_tri.output.prim < 0)
		throw std::runtime_error("detect_point_against_mesh failed to project out");

	RowVector3i f = emb_mesh->emb_faces.row(nearest_tri.output.prim);
	Vector3d fv[3] = {
		emb_mesh->emb_rest_x.row(f[0]),
		emb_mesh->emb_rest_x.row(f[1]),
		emb_mesh->emb_rest_x.row(f[2])
	};
	Vector3d n = (fv[1]-fv[0]).cross(fv[2]-fv[0]);
	n.normalize();
	if ((rest_pt-fv[0]).dot(n)>0)
		return ret; // outside of surface

	ret.first = true;
	ret.second.p_idx = pt_idx;
	ret.second.p_is_obs = pt_is_obs;
	ret.second.q_idx = 
	ret.second.q_is_obs = mesh_is_obs;
	ret.second.q_bary = geom::point_triangle_barys<double>(
		nearest_tri.output.pt_on_tri, fv[0], fv[1], fv[2]);

	return ret;
} // end detect_point_against_mesh
*/