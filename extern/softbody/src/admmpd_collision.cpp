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

bool Collision::ObstacleData::compute_sdf(int idx)
{
	if (idx < 0 || idx >x1.size()) {
		return false;
	}

	// There was an error in init
	if (box[idx].isEmpty()) {
		return false;
	}

	// Test that the mesh is closed
	Discregrid::TriangleMesh tm(
		(double const*)x1[idx].data(),
		(unsigned int const*)F[idx].data(),
		x1[idx].rows(), F[idx].rows());
	if (!tm.is_closed()) {
		return false;
	}

	// Generate signed distance field
	Discregrid::MeshDistance md(tm);
	std::array<unsigned int, 3> resolution;
	resolution[0] = 30; resolution[1] = 30; resolution[2] = 30;
	sdf[idx] = Discregrid::CubicLagrangeDiscreteGrid(box[idx], resolution);
	auto func = Discregrid::DiscreteGrid::ContinuousFunction{};
	std::vector<std::thread::id> thread_map;
	md.set_thread_map(&thread_map);
	func = [&md](Eigen::Vector3d const& xi) {
		return md.signedDistanceCached(xi);
	};
	sdf[idx].addFunction(func, &thread_map, false);

	if (sdf[idx].nCells()==0) {
		return false;
	}
	return true;
}

bool Collision::set_obstacles(
	std::vector<Eigen::MatrixXd> &v0,
	std::vector<Eigen::MatrixXd> &v1,
	std::vector<Eigen::MatrixXi> &F,
	std::string *err)
{
	if (v0.size() != v1.size() || v0.size() != F.size()) {
		if (err) { *err = "Bad dimensions on obstacle input"; }
		return false;
	}

	// Copy the obstacle data from the input to the stored
	// data container. If the vertex locations have changed,
	// we need to recompute the SDF. Otherwise, leave it as is.
	int n_obs_new = v0.size();
	int n_obs_old = obsdata.x0.size();
    obsdata.sdf.resize(n_obs_new);
	obsdata.x0.resize(n_obs_new);
	obsdata.x1.resize(n_obs_new);
	obsdata.F.resize(n_obs_new);
	obsdata.box.resize(n_obs_new);

	// We can use isApprox for testing if the obstacle has
	// moved from the last call to set_obstacles. The SDF
	// has limited accuracy anyway...
	double approx_eps = 1e-6;
	for (int i=0; i<n_obs_new; ++i) {

		bool reset_obs = false;
		if (i >= n_obs_old) {
			reset_obs=true; // is new obs
		}
		else if (!obsdata.x1[i].isApprox(v1[i],approx_eps) ||
				!obsdata.x0[i].isApprox(v0[i],approx_eps)) {
			reset_obs = true; // is different than before
		}

		if (reset_obs) {

			obsdata.box[i].setEmpty();
			int nv = v1[i].rows();
			for (int j=0; j<nv; ++j) {
				obsdata.box[i].extend(v1[i].row(j).transpose());
			}
			obsdata.box[i].max() += 1e-3 * obsdata.box[i].diagonal().norm() * Eigen::Vector3d::Ones();
			obsdata.box[i].min() -= 1e-3 * obsdata.box[i].diagonal().norm() * Eigen::Vector3d::Ones();

			obsdata.sdf[i] = SDFType(); // clear old sdf
			obsdata.x0[i] = v0[i];
			obsdata.x1[i] = v1[i];
			obsdata.F[i] = F[i].cast<unsigned int>();

			// Determine if the triangle mesh is closed or not.
			// We want to provide a warning if it is.
			Discregrid::TriangleMesh tm(
				(double const*)obsdata.x1[i].data(),
				(unsigned int const*)obsdata.F[i].data(),
				obsdata.x1[i].rows(), obsdata.F[i].rows());
			if (!tm.is_closed()) {
				obsdata.box[i].setEmpty();
				if (err) { *err = "Collision obstacle not a closed mesh - ignoring"; }
			}
		}
	}

	return true;

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
	(void)(pt_t0);

	int n_obs = obs->num_obs();
	if (n_obs==0) {
		return std::make_pair(false, VFCollisionPair());
	}

	for (int i=0; i<n_obs; ++i)
	{
		if (obs->sdf[i].nCells()==0) {
			continue; // not initialized
		}
		Vector3d n;
		double dist = obs->sdf[i].interpolate(0, pt_t1, &n);
		if (dist > 0) { continue; } // not colliding

		std::pair<bool,VFCollisionPair> ret = 
			std::make_pair(true, VFCollisionPair());
		ret.first = true;
		ret.second.q_idx = -1;
		ret.second.q_is_obs = true;
		ret.second.q_bary.setZero();
		ret.second.q_pt = pt_t1 - dist*n;
		ret.second.q_n = n.normalized();
		return ret;
	}
	return std::make_pair(false, VFCollisionPair());
}

int EmbeddedMeshCollision::detect(
	const admmpd::Mesh *mesh,
	const admmpd::Options *options,
	const admmpd::SolverData *data,
	const Eigen::MatrixXd *x0,
	const Eigen::MatrixXd *x1)
{
	if (!mesh) {
		return 0;
	}

	if (mesh->type() != MESHTYPE_EMBEDDED) {
		return 0;
	}

	// Compute SDFs if the mesh is intersecting
	// the associated obstacle. The sdf generation is internally threaded,
	// but it might be faster to thread the different SDFs.
	bool has_obs_intersection = false;
	int n_obs = obsdata.num_obs();
	AlignedBox<double,3> mesh_box = data->col.prim_tree.bounds();
	for (int i=0; i<n_obs; ++i) {
		AlignedBox<double,3> &box = obsdata.box[i];
		if (box.isEmpty()) { continue; }
		if (!box.intersects(mesh_box)) { continue; }
		has_obs_intersection = true;
		// Do we need to generate a new SDF?
		if (obsdata.sdf[i].nCells()==0) {
			obsdata.compute_sdf(i);
		}
	}

	// Do we even need to process collisions and launch
	// the per-vertex threads?
	if (!has_obs_intersection && !options->self_collision) {
		if (x1->col(2).minCoeff() > options->floor) {
			return 0;
		}
	}

	// We store the results of the collisions in a per-vertex buffer.
	// This is a workaround so we can create them in threads.
	int nev = mesh->rest_facet_verts()->rows();
	if ((int)per_vertex_pairs.size() != nev) {
		per_vertex_pairs.resize(nev, std::vector<VFCollisionPair>());
	}

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
	// This function is very poorly optimized in terms of
	// cache friendlyness.
	// Some refactoring would greatly improve run time.
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

//		if (td->options->log_level >= LOGLEVEL_DEBUG) {
//			printf("\tDetecting collisions for emb vertex %d: %f %f %f\n", vi, pt_t1[0], pt_t1[1], pt_t1[2]);
//		}

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
		bool had_obstacle_collision = false;
		if (td->obsdata->num_obs()>0)
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
				had_obstacle_collision = true;
			}
		}

		// We perform self collision if the self_collision flag is true and:
		// a) there was no obstacle collision
		// b) the vertex is in the set of self collision vertices (or its empty)
		bool do_self_collision = !had_obstacle_collision && td->options->self_collision;
		if (do_self_collision) {
			if (td->data->col.selfcollision_verts.size()>0) {
				do_self_collision = td->data->col.selfcollision_verts.count(vi)>0;
			}
		}

		// Detect against self
		if (do_self_collision)
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
		mesh,
		options,
		data,
		this,
		&obsdata,
		x0,
		x1,
		&per_vertex_pairs,
		&per_thread_results
	};

	// The pooling is a little unusual here.
	// Collisions are processed per-vertex. If one vertex is colliding, it's
	// likely that adjacent vertices are also colliding.
	// Because of this, it may be better to interlace/offset the pooling so that
	// vertices next to eachother are on different threads to provide
	// better concurrency. Otherwise a standard slice may end up doing
	// all of the BVH traversals and the other threads do none.
	// I haven't actually profiled this, so maybe I'm wrong.
	int max_threads = std::max(1, std::min(nev, admmpd::get_max_threads(options)));
	if (options->log_level >= LOGLEVEL_DEBUG) {
		max_threads = 1;
	}
	const auto & per_thread_function = [&per_embedded_vertex_detect,&max_threads,&nev]
		(DetectThreadData *td, int thread_idx)
	{
		float slice_f = float(nev+1) / float(max_threads);
    	int slice = std::max((int)std::ceil(slice_f),1);
		for (int i=0; i<slice; ++i)
		{
			int vi = i*max_threads + thread_idx;

			// Yeah okay I know this is dumb and I can just do a better job
			// of calculating the slice. We can save thread optimization
			// for the future, since this will be written different anyway.
			if (vi >= nev) {
				break;
			}

			per_embedded_vertex_detect(td,thread_idx,vi);
		}
	};

	// Launch threads
	std::vector<std::thread> pool;
	per_thread_results.resize(max_threads, std::vector<Vector2i>());
	for (int i=0; i<max_threads; ++i) {
		per_thread_results[i].reserve(std::max(1,nev/max_threads));
	}
	for (int i=0; i<max_threads; ++i) {
		pool.emplace_back(per_thread_function,&thread_data,i);
	}

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
	(void)(options);
	(void)(x0);

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
	(void)(pt_t0);
	(void)(x0);
	(void)(options);
	(void)(data);

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
	if (barys.minCoeff()<-1e-8 || barys.sum() > 1+1e-8) {
		throw std::runtime_error("EmbeddedMeshCollision: Bad tet barys");
	}

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
		if (dist > 0) {
			return ret; // nope
		}
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

	if (nearest_tri.output.prim<0) {
		throw std::runtime_error("EmbeddedMeshCollision: Failed to find triangle");
	}

	ret.first = true;
	ret.second.p_idx = pt_idx;
	ret.second.p_is_obs = false;
	ret.second.q_idx = nearest_tri.output.prim;
	ret.second.q_is_obs = false;

	// Compute barycoords of projection
	RowVector3i f = mesh->facets()->row(nearest_tri.output.prim);
	Vector3d v3[3] = { emb_V0->row(f[0]), emb_V0->row(f[1]), emb_V0->row(f[2]) };
	ret.second.q_bary = geom::point_triangle_barys<double>(
		nearest_tri.output.pt_on_tri, v3[0], v3[1], v3[2]);
	if (ret.second.q_bary.minCoeff()<-1e-8 || ret.second.q_bary.sum() > 1+1e-8) {
		throw std::runtime_error("EmbeddedMeshCollision: Bad triangle barys");
	}

	// q_pt is not used for self collisions, but we'll use it
	// to define the tet constraint stencil.
	ret.second.q_pt = pt_t1;

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
	(void)(data);
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
	double eta = 0;//std::max(0.0,options->collision_thickness);

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
