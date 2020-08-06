// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#include "admmpd_collision.h"
#include "admmpd_bvh_traverse.h"
#include "admmpd_geom.h"

#include "BLI_assert.h"
#include "BLI_task.h"
#include "BLI_threads.h"
#include <thread>

#include <iostream>
#include <sstream>

namespace admmpd {
using namespace Eigen;

VFCollisionPair::VFCollisionPair() :
    p_idx(-1),
    p_is_obs(0),
    q_idx(-1),
    q_is_obs(0),
	q_pt(0,0,0),
	q_n(0,0,0),
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
		// Why do this? Are you just being mean?
		return;
	}

	std::vector<double> v1_dbl(nv*3);
    Eigen::AlignedBox<double,3> domain;

	if (obsdata.V.rows() != nv)
		obsdata.V.resize(nv,3);
	for (int i=0; i<nv; ++i)
	{
		for (int j=0; j<3; ++j)
		{
			obsdata.V(i,j) = v1[i*3+j];
			v1_dbl[i*3+j] = v1[i*3+j];
		}
		domain.extend(obsdata.V.row(i).transpose());
	}

	if (obsdata.F.rows() != nf)
		obsdata.F.resize(nf,3);
	for (int i=0; i<nf; ++i)
	{
		for (int j=0; j<3; ++j)
			obsdata.F(i,j) = faces[i*3+j];
	}

	// Generate signed distance field
	{
		Discregrid::TriangleMesh tm(v1_dbl.data(), faces, nv, nf);
		Discregrid::MeshDistance md(tm);
		domain.max() += 1e-3 * domain.diagonal().norm() * Eigen::Vector3d::Ones();
		domain.min() -= 1e-3 * domain.diagonal().norm() * Eigen::Vector3d::Ones();
		std::array<unsigned int, 3> resolution;
		resolution[0] = 30; resolution[1] = 30; resolution[2] = 30;
		obsdata.sdf = Discregrid::CubicLagrangeDiscreteGrid(domain, resolution);
		auto func = Discregrid::DiscreteGrid::ContinuousFunction{};
		func = [&md](Eigen::Vector3d const& xi) {
			return md.signedDistanceCached(xi);
		};
		obsdata.sdf.addFunction(func, false);
	}

} // end add obstacle

std::pair<bool,VFCollisionPair>
Collision::detect_against_obs(
	    const admmpd::Mesh *mesh,
	    const admmpd::Options *options,
        const admmpd::SolverData *data,
        const Eigen::Vector3d &pt_t0,
		const Eigen::Vector3d &pt_t1,
        const ObstacleData *obs) const
{
	(void)(mesh);
	(void)(options);
	(void)(data);

	std::pair<bool,VFCollisionPair> ret = 
		std::make_pair(false, VFCollisionPair());

	if (!obs->has_obs())
		return ret;

	// So I feel bad because we're using the SDF only
	// for inside/outside query. Unfortunately this implementation
	// doesn't store the face indices within the grid cells, so
	// the interpolate function won't return the nearest
	// face at the gradient + distance.
	Vector3d n;
	double dist = obs->sdf.interpolate(0, pt_t1, &n);
	if (dist > 0)
		return ret;

	ret.first = true;
	ret.second.q_idx = -1;
	ret.second.q_is_obs = true;
	ret.second.q_bary.setZero();
	ret.second.q_pt = pt_t1 - dist*n;
	ret.second.q_n = n.normalized();
	return ret;
}

int EmbeddedMeshCollision::detect(
	const admmpd::Mesh *mesh,
	const admmpd::Options *options,
	const admmpd::SolverData *data,
	const Eigen::MatrixXd *x0,
	const Eigen::MatrixXd *x1)
{
	if (!mesh)
		return 0;

	if (mesh->type() != MESHTYPE_EMBEDDED)
		return 0;

	// Do we even need to process collisions?
	if (!this->obsdata.has_obs() && !options->self_collision)
	{
		if (x1->col(1).minCoeff() > options->floor)
		{
			return 0;
		}
	}

	// We store the results of the collisions in a per-vertex buffer.
	// This is a workaround so we can create them in threads.
	int nev = mesh->rest_facet_verts()->rows();
	if ((int)per_vertex_pairs.size() != nev)
		per_vertex_pairs.resize(nev, std::vector<VFCollisionPair>());

	//
	// Thread data for detection
	//
	typedef struct {
		const Mesh *mesh;
		const Options *options;
		const SolverData *data;
		const Collision *collision;
		const Collision::ObstacleData *obsdata;
		const Eigen::MatrixXd *x0;
		const Eigen::MatrixXd *x1;
		std::vector<std::vector<VFCollisionPair> > *per_vertex_pairs;
		std::vector<std::vector<Eigen::Vector2i> > *per_thread_results;
	} DetectThreadData;

	//
	// Detection function for a single embedded vertex
	//
//	auto per_embedded_vertex_detect = [](
//		void *__restrict userdata,
//		const int vi,
//		const TaskParallelTLS *__restrict tls)->void
	auto per_embedded_vertex_detect = [](
		DetectThreadData *td,
		int thread_idx,
		int vi)->void
	{
//		(void)(tls);
//		DetectThreadData *td = (DetectThreadData*)userdata;
		if (!td->mesh)
			return;

		std::vector<Eigen::Vector2i> &pt_res = td->per_thread_results->at(thread_idx);
		std::vector<VFCollisionPair> &vi_pairs = td->per_vertex_pairs->at(vi);
		vi_pairs.clear();
		Vector3d pt_t0 = td->mesh->get_mapped_facet_vertex(td->x0,vi);
		Vector3d pt_t1 = td->mesh->get_mapped_facet_vertex(td->x1,vi);

		// Special case, check if we are below the floor
		if (pt_t1[2] < td->options->floor)
		{
			pt_res.emplace_back(vi,vi_pairs.size());
			vi_pairs.emplace_back();
			VFCollisionPair &pair = vi_pairs.back();
			pair.p_idx = vi;
			pair.p_is_obs = false;
			pair.q_idx = -1;
			pair.q_is_obs = 1;
			pair.q_bary.setZero();
			pair.q_pt = Vector3d(pt_t1[0],pt_t1[1],td->options->floor);
			pair.q_n = Vector3d(0,0,1);
		}

		// Detect against obstacles
		if (td->obsdata->has_obs())
		{
			std::pair<bool,VFCollisionPair> pt_hit_obs =
				td->collision->detect_against_obs(
					td->mesh,
					td->options,
					td->data,
					pt_t0,
					pt_t1,
					td->obsdata);
			if (pt_hit_obs.first)
			{
				pt_hit_obs.second.p_idx = vi;
				pt_hit_obs.second.p_is_obs = false;
				pt_res.emplace_back(vi,vi_pairs.size());
				vi_pairs.emplace_back(pt_hit_obs.second);
			}
		}

		// Detect against self
		if (td->options->self_collision)
		{
			std::pair<bool,VFCollisionPair> pt_hit_self =
				td->collision->detect_against_self(
					td->mesh,
					td->options,
					td->data,
					vi,
					pt_t0,
					pt_t1,
					td->x0,
					td->x1);
			if (pt_hit_self.first)
			{
				pt_res.emplace_back(vi,vi_pairs.size());
				vi_pairs.emplace_back(pt_hit_self.second);
			}
		}

	}; // end detect for a single embedded vertex

	std::vector<std::vector<Eigen::Vector2i> > per_thread_results;
	DetectThreadData thread_data = {
		.mesh = mesh,
		.options = options,
		.data = data,
		.collision = this,
		.obsdata = &obsdata,
		.x0 = x0,
		.x1 = x1,
		.per_vertex_pairs = &per_vertex_pairs,
		.per_thread_results = &per_thread_results
	};

	// The pooling is a little unusual here.
	// Collisions are processed per-vertex. If one vertex is colliding, it's
	// likely that adjacent vertices are also colliding.
	// Because of this, it may be better to interlace/offset the pooling so that
	// vertices next to eachother are on different threads to provide
	// better concurrency. Otherwise a standard slice may end up doing
	// all of the BVH traversals and the other threads do none.
	// I haven't actually profiled this, so maybe I'm wrong. Either way it
	// won't hurt. I think.
	int max_threads = std::max(1, std::min(nev, admmpd::get_max_threads(options)));
	const auto & per_thread_function = [&per_embedded_vertex_detect,&max_threads,&nev]
		(DetectThreadData *td, int thread_idx)
	{
    	int slice = std::max((int)std::round((nev+1)/double(max_threads)),1);
		for (int i=0; i<slice; ++i)
		{
			int vi = i*max_threads + thread_idx;
			if (vi >= nev)
				break;

			per_embedded_vertex_detect(td,thread_idx,vi);
		}
	};

	// Launch threads
	std::vector<std::thread> pool;
	per_thread_results.resize(max_threads, std::vector<Vector2i>());
	for (int i=0; i<max_threads; ++i)
		pool.emplace_back(per_thread_function,&thread_data,i);

	// Combine parallel results
	vf_pairs.clear();
	for (int i=0; i<max_threads; ++i)
	{
		if (pool[i].joinable())
			pool[i].join(); // wait for thread to finish

		// Other threads may be finishing while we insert results
		// into the global buffer. That's okay!
		vf_pairs.insert(vf_pairs.end(),
			per_thread_results[i].begin(), per_thread_results[i].end());
	}

//	TaskParallelSettings thrd_settings;
//	BLI_parallel_range_settings_defaults(&thrd_settings);
//	BLI_task_parallel_range(0, nev, &thread_data, per_embedded_vertex_detect, &thrd_settings);
//	vf_pairs.clear();
//	for (int i=0; i<nev; ++i)
//	{
//		int pvp = per_vertex_pairs[i].size();
//		for (int j=0; j<pvp; ++j)
//			vf_pairs.emplace_back(Vector2i(i,j));
//	}

	return vf_pairs.size();
} // end detect

void EmbeddedMeshCollision::update_bvh(
	const admmpd::Mesh *mesh,
	const admmpd::Options *options,
	admmpd::SolverData *data,
	const Eigen::MatrixXd *x0,
	const Eigen::MatrixXd *x1,
	bool sort)
{
	if (!mesh)
		return;

	if (mesh->type() != MESHTYPE_EMBEDDED)
		return;

	int nt = mesh->prims()->rows();
	if ((int)data->col.prim_boxes.size() != nt)
		data->col.prim_boxes.resize(nt);

	for (int i=0; i<nt; ++i)
	{
		RowVector4i tet = mesh->prims()->row(i);
		AlignedBox<double,3> &box = data->col.prim_boxes[i];
		box.setEmpty();
		box.extend(x1->row(tet[0]).transpose());
		box.extend(x1->row(tet[1]).transpose());
		box.extend(x1->row(tet[2]).transpose());
		box.extend(x1->row(tet[3]).transpose());
	}

	if (!data->col.prim_tree.root() || sort)
		{ data->col.prim_tree.init(data->col.prim_boxes); } // sort
	else
		{ data->col.prim_tree.update(data->col.prim_boxes); } // grow

} // end update bvh

// Self collisions
std::pair<bool,VFCollisionPair>
EmbeddedMeshCollision::detect_against_self(
	const admmpd::Mesh *mesh_,
	const admmpd::Options *options,
	const admmpd::SolverData *data,
	int pt_idx,
	const Eigen::Vector3d &pt_t0,
	const Eigen::Vector3d &pt_t1,
	const Eigen::MatrixXd *x0,
	const Eigen::MatrixXd *x1) const
{
	(void)(pt_t0); (void)(x0);
	std::pair<bool,VFCollisionPair> ret = 
		std::make_pair(false, VFCollisionPair());

	if (!mesh_)
		return ret;

	if (mesh_->type() != MESHTYPE_EMBEDDED)
		return ret;

	const EmbeddedMesh* mesh = dynamic_cast<const EmbeddedMesh*>(mesh_);

	// Are we in the (deforming) tet mesh?
	int self_tet_idx = mesh->emb_vtx_to_tet()->operator[](pt_idx);
	std::vector<int> skip_tet_inds = {self_tet_idx};
	PointInTetMeshTraverse<double> pt_in_tet(
		pt_t1,
		x1,
		mesh->prims(), // tets
		std::vector<int>(), // skip tets that contain these verts
		skip_tet_inds); // skip tet that is this index
	bool in_mesh = data->col.prim_tree.traverse(pt_in_tet);
	if (!in_mesh)
		return ret;

	// Transform point to rest shape
	int tet_idx = pt_in_tet.output.prim;
	RowVector4i tet = mesh->prims()->row(tet_idx);
	Vector4d barys = geom::point_tet_barys<double>(pt_t1,
		x1->row(tet[0]),
		x1->row(tet[1]),
		x1->row(tet[2]),
		x1->row(tet[3]));
	if (barys.minCoeff()<-1e-8 || barys.sum() > 1+1e-8)
		throw std::runtime_error("EmbeddedMeshCollision: Bad tet barys");

	const MatrixXd *rest_V0 = mesh->rest_prim_verts();
	Vector3d rest_pt =
		barys[0]*rest_V0->row(tet[0])+
		barys[1]*rest_V0->row(tet[1])+
		barys[2]*rest_V0->row(tet[2])+
		barys[3]*rest_V0->row(tet[3]);

	// Verify we are in the surface mesh, not just the lattice tet mesh
	const SDFType *rest_emb_sdf = mesh->rest_facet_sdf();
	if (rest_emb_sdf)
	{
		double dist = rest_emb_sdf->interpolate(0, rest_pt);
		if (dist > 0)
			return ret; // nope
	}

	// Find triangle surface projection that doesn't
	// include the penetrating vertex
	const MatrixXd *emb_V0 = mesh->rest_facet_verts();
	std::vector<int> skip_tri_inds = {pt_idx};
	NearestTriangleTraverse<double> nearest_tri(
		rest_pt,
		emb_V0,
		mesh->facets(), // triangles
		skip_tri_inds);
	mesh->emb_rest_tree()->traverse(nearest_tri);

	if (nearest_tri.output.prim<0)
		throw std::runtime_error("EmbeddedMeshCollision: Failed to find triangle");

	ret.first = true;
	ret.second.p_idx = pt_idx;
	ret.second.p_is_obs = false;
	ret.second.q_idx = nearest_tri.output.prim;
	ret.second.q_is_obs = false;
	ret.second.q_pt = nearest_tri.output.pt_on_tri;

	// Compute barycoords of projection
	RowVector3i f = mesh->facets()->row(nearest_tri.output.prim);
	Vector3d v3[3] = { emb_V0->row(f[0]), emb_V0->row(f[1]), emb_V0->row(f[2]) };
	ret.second.q_bary = geom::point_triangle_barys<double>(
		nearest_tri.output.pt_on_tri, v3[0], v3[1], v3[2]);
	if (ret.second.q_bary.minCoeff()<-1e-8 || ret.second.q_bary.sum() > 1+1e-8)
		throw std::runtime_error("EmbeddedMeshCollision: Bad triangle barys");

	return ret;
}


void EmbeddedMeshCollision::graph(
	const admmpd::Mesh *mesh_,
	std::vector<std::set<int> > &g)
{
	if (!mesh_)
		return;

	if (mesh_->type() != MESHTYPE_EMBEDDED)
		return;

	const EmbeddedMesh *mesh = dynamic_cast<const EmbeddedMesh*>(mesh_);

	int np = vf_pairs.size();
	if (np==0)
		return;

	int nv = mesh->rest_prim_verts()->rows();
	if ((int)g.size() < nv)
		g.resize(nv, std::set<int>());

	for (int i=0; i<np; ++i)
	{
		Vector2i pair_idx = vf_pairs[i];
		VFCollisionPair &pair = per_vertex_pairs[pair_idx[0]][pair_idx[1]];
		std::set<int> stencil;

		if (!pair.p_is_obs)
		{
			int tet_idx = mesh->emb_vtx_to_tet()->operator[](pair.p_idx);
			RowVector4i tet = mesh->prims()->row(tet_idx);
			stencil.emplace(tet[0]);
			stencil.emplace(tet[1]);
			stencil.emplace(tet[2]);
			stencil.emplace(tet[3]);
		}
		if (!pair.q_is_obs)
		{
			RowVector3i emb_face = mesh->facets()->row(pair.q_idx);
			for (int j=0; j<3; ++j)
			{
				int tet_idx = mesh->emb_vtx_to_tet()->operator[](emb_face[j]);
				RowVector4i tet = mesh->prims()->row(tet_idx);
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
	const admmpd::Mesh *mesh_,
	const admmpd::Options *options,
	const admmpd::SolverData *data,
	const Eigen::MatrixXd *x,
	std::vector<Eigen::Triplet<double> > *trips,
	std::vector<double> *d) const
{
	BLI_assert(x != NULL);
	BLI_assert(x->cols() == 3);

	if (!mesh_)
		return;

	if (mesh_->type() != MESHTYPE_EMBEDDED)
		return;

	const EmbeddedMesh *mesh = dynamic_cast<const EmbeddedMesh*>(mesh_);

	int np = vf_pairs.size();
	if (np==0)
		return;

	//int nx = x->rows();
	d->reserve((int)d->size() + np);
	trips->reserve((int)trips->size() + np*3*4);
	double eta = std::max(0.0,options->collision_thickness);

	for (int i=0; i<np; ++i)
	{
		const Vector2i &pair_idx = vf_pairs[i];
		const VFCollisionPair &pair = per_vertex_pairs[pair_idx[0]][pair_idx[1]];
		int emb_p_idx = pair.p_idx;
//		Vector3d p_pt = meshdata.mesh->get_mapped_facet_vertex(x,emb_p_idx);

		//
		// Obstacle collision
		//
		if (pair.q_is_obs)
		{
			// Get the four deforming verts that embed
			// the surface vertices, and add constraints on those.
			RowVector4d bary = mesh->emb_barycoords()->row(emb_p_idx);
			int tet_idx = mesh->emb_vtx_to_tet()->operator[](emb_p_idx);
			RowVector4i tet = mesh->prims()->row(tet_idx);
			int c_idx = d->size();
			d->emplace_back(pair.q_n.dot(pair.q_pt) + eta);
			for (int j=0; j<4; ++j)
			{
				trips->emplace_back(c_idx, tet[j]*3+0, bary[j]*pair.q_n[0]);
				trips->emplace_back(c_idx, tet[j]*3+1, bary[j]*pair.q_n[1]);
				trips->emplace_back(c_idx, tet[j]*3+2, bary[j]*pair.q_n[2]);
			}

		} // end q is obs
		//
		// Self collision
		//
		else
		{
			int c_idx = d->size();
			d->emplace_back(eta);

			// Compute the normal in the deformed space
			RowVector3i q_face = mesh->facets()->row(pair.q_idx);
			Vector3d q_v0 = mesh->get_mapped_facet_vertex(x,q_face[0]);
			Vector3d q_v1 = mesh->get_mapped_facet_vertex(x,q_face[1]);
			Vector3d q_v2 = mesh->get_mapped_facet_vertex(x,q_face[2]);
			Vector3d q_n = (q_v1-q_v0).cross(q_v2-q_v0);
			q_n.normalize();

			// The penetrating vertex:
			{
				int tet_idx = mesh->emb_vtx_to_tet()->operator[](emb_p_idx);
				RowVector4d bary = mesh->emb_barycoords()->row(emb_p_idx);
				RowVector4i tet = mesh->prims()->row(tet_idx);
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
				RowVector4d bary = mesh->emb_barycoords()->row(emb_q_idx);
				int tet_idx = mesh->emb_vtx_to_tet()->operator[](emb_q_idx);
				RowVector4i tet = mesh->prims()->row(tet_idx);
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