// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_BVH_H_
#define ADMMPD_BVH_H_ 1

#include "admmpd_bvh_traverse.h"
#include "admmpd_geom.h"
#include "BLI_assert.h"
#include "BLI_math_geom.h"

namespace admmpd {
using namespace Eigen;

template <typename T>
void PointInTetMeshTraverse<T>::traverse(
	const AABB &left_aabb, bool &go_left,
	const AABB &right_aabb, bool &go_right,
	bool &go_left_first )
{
	if (left_aabb.contains(point))
		go_left = true;
	if (right_aabb.contains(point))
		go_right = true;
	(void)(go_left_first); // doesn't matter for point-in-tet
}

template <typename T>
bool PointInTetMeshTraverse<T>::stop_traversing(
		const AABB &aabb, int prim )
{
	BLI_assert(prim_verts->cols()==3);
	BLI_assert(prim_inds->cols()==4);

	if (!aabb.contains(point))
		return false;

	int n_tet_skip = skip_tet_inds.size();
	for (int i=0; i<n_tet_skip; ++i)
	{
		if (skip_tet_inds[i]==prim) return false;
	}

	RowVector4i t = prim_inds->row(prim);
	int n_skip = skip_vert_inds.size();
	for (int i=0; i<n_skip; ++i)
	{
		if (skip_vert_inds[i]==t[0]) return false;
		if (skip_vert_inds[i]==t[1]) return false;
		if (skip_vert_inds[i]==t[2]) return false;
		if (skip_vert_inds[i]==t[3]) return false;
	}

	VecType v[4] = {
		prim_verts->row(t[0]),
		prim_verts->row(t[1]),
		prim_verts->row(t[2]),
		prim_verts->row(t[3])
	};

	bool hit = geom::point_in_tet<T>(point, v[0], v[1], v[2], v[3]);

	if (hit)
		output.prim = prim;

	return hit; // stop traversing if hit
}

template <typename T>
PointInTriangleMeshTraverse<T>::PointInTriangleMeshTraverse(
	const VecType &point_,
	const MatrixXType *prim_verts_,
	const Eigen::MatrixXi *prim_inds_,
	const std::vector<int> &skip_inds_) :
	point(point_),
	dir(0,0,1),
	prim_verts(prim_verts_),
	prim_inds(prim_inds_),
	skip_inds(skip_inds_)
{
	//dir = VecType::Random();
	BLI_assert(prim_verts->rows()>=0);
	BLI_assert(prim_inds->rows()>=0);
	BLI_assert(prim_inds->cols()==3);
	dir.normalize(); // TODO random unit vector
	for (int i=0; i<3; ++i)
	{
		o[i] = (float)point[i];
		d[i] = (float)dir[i];
	}
	isect_ray_tri_watertight_v3_precalc(&isect_precalc, d);
}

template <typename T>
void PointInTriangleMeshTraverse<T>::traverse(
	const AABB &left_aabb, bool &go_left,
	const AABB &right_aabb, bool &go_right,
	bool &go_left_first )
{
	const T t_min = 0;
	const T t_max = std::numeric_limits<T>::max();
	go_left = geom::ray_aabb<T>(point,dir,left_aabb,t_min,t_max);
	go_right = geom::ray_aabb<T>(point,dir,right_aabb,t_min,t_max);
	go_left_first = go_left;
} // end point in mesh traverse

template <typename T>
bool PointInTriangleMeshTraverse<T>::stop_traversing(
		const AABB &aabb, int prim )
{
	const T t_min = 0;
	T t_max = std::numeric_limits<T>::max();

	// Check if the tet box doesn't intersect the triangle box
	if (!geom::ray_aabb<T>(point,dir,aabb,t_min,t_max))
		return false;

	// Get the vertices of the face in float arrays
	// to interface with Blender kernels.
	BLI_assert(prim >= 0 && prim < prim_inds->rows());
	RowVector3i q_f = prim_inds->row(prim);
	int n_skip = skip_inds.size();
	for (int i=0; i<n_skip; ++i)
	{
		if (skip_inds[i]==q_f[0]) return false;
		if (skip_inds[i]==q_f[1]) return false;
		if (skip_inds[i]==q_f[2]) return false;
	}
	BLI_assert(q_f[0] < prim_verts->rows());
	BLI_assert(q_f[1] < prim_verts->rows());
	BLI_assert(q_f[2] < prim_verts->rows());
	float q0[3], q1[3], q2[3];
	for (int i=0; i<3; ++i)
	{
		q0[i] = (float)prim_verts->operator()(q_f[0],i);
		q1[i] = (float)prim_verts->operator()(q_f[1],i);
		q2[i] = (float)prim_verts->operator()(q_f[2],i);
	}

	// If we didn't have a triangle-triangle intersection
	// then record if it was a ray-hit.
	float lambda = 0;
	float uv[2] = {0,0};
	bool hit = isect_ray_tri_watertight_v3(o, &isect_precalc, q0, q1, q2, &lambda, uv);

	if (hit)
		output.hits.emplace_back(std::make_pair(prim,lambda));

	return false; // multi-hit, so keep traversing

} // end point in mesh stop traversing

template <typename T>
void NearestTriangleTraverse<T>::traverse(
	const AABB &left_aabb, bool &go_left,
	const AABB &right_aabb, bool &go_right,
	bool &go_left_first)
{
	T l_d = left_aabb.exteriorDistance(point);
	go_left = l_d < output.dist;
	T r_d = right_aabb.exteriorDistance(point);
	go_right = r_d < output.dist;
	go_left_first = go_left <= go_right;
}

template <typename T>
bool NearestTriangleTraverse<T>::stop_traversing(const AABB &aabb, int prim)
{
	BLI_assert(prim >= 0);
	BLI_assert(prim < prim_inds->rows());
	BLI_assert(prim_inds->cols()==3);

	T b_dist = aabb.exteriorDistance(point);
	if (b_dist > output.dist)
		return false;

	RowVector3i tri = prim_inds->row(prim);

	int n_skip = skip_inds.size();
	for (int i=0; i<n_skip; ++i)
	{
		if (skip_inds[i]==tri[0]) return false;
		if (skip_inds[i]==tri[1]) return false;
		if (skip_inds[i]==tri[2]) return false;
	}

	VecType v[3] = {
		prim_verts->row(tri[0]),
		prim_verts->row(tri[1]),
		prim_verts->row(tri[2])
	};
	VecType pt_on_tri = geom::point_on_triangle<T>(point,v[0],v[1],v[2]);
	double dist = (point-pt_on_tri).norm();
	if (dist < output.dist)
	{
		output.prim = prim;
		output.dist = dist;
		output.pt_on_tri = pt_on_tri;		
	}

	return false;
}

// Compile template types
template class admmpd::PointInTetMeshTraverse<double>;
template class admmpd::PointInTetMeshTraverse<float>;
template class admmpd::PointInTriangleMeshTraverse<double>;
template class admmpd::PointInTriangleMeshTraverse<float>;
template class admmpd::NearestTriangleTraverse<double>;
template class admmpd::NearestTriangleTraverse<float>;

} // namespace admmpd

#endif // ADMMPD_BVH_H_

