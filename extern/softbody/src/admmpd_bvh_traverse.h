// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_BVH_TRAVERSE_H_
#define ADMMPD_BVH_TRAVERSE_H_ 1

#include <Eigen/Geometry>
#include <vector>
#include "BLI_math_geom.h"

namespace admmpd {


// Traverse class for traversing the structures.
template <typename T, int DIM>
class Traverser
{
protected:
	typedef Eigen::AlignedBox<T,DIM> AABB;
public:
	// Set the boolean flags if we should go left, right, or both.
	// Default for all booleans is true if left unchanged.
	// Note that if stop_traversing ever returns true, it may not
	// go left/right, even if you set go_left/go_right.
	virtual void traverse(
		const AABB &left_aabb, bool &go_left,
		const AABB &right_aabb, bool &go_right,
		bool &go_left_first) = 0;

	// Return true to stop traversing.
	// I.e., returning true is equiv to "hit anything stop checking",
	// finding a closest object should return false (continue traversing).
	virtual bool stop_traversing(const AABB &aabb, int prim) = 0;
};

// Closest hit BVH traversal.
template <typename T>
class RayClosestHit : public Traverser<T,3>
{
protected:
	using typename Traverser<T,3>::AABB;
	typedef Eigen::Matrix<T,3,1> VecType;
	typedef Eigen::Matrix<T,Eigen::Dynamic,Eigen::Dynamic> MatrixXType;

	VecType o, d;
	const MatrixXType *prim_verts;
	const Eigen::MatrixXi *tri_inds;
	T eps;
	T t_min;

public:
	struct Output {
		int prim; // -1 if no intersections
		T t_max;
		VecType barys;
		Output() :
			prim(-1),
			t_max(std::numeric_limits<T>::max()),
			barys(VecType::Zero())
			{}
	} output;

	RayClosestHit(
			const VecType &orig_, // ray origin
			const VecType &dir_,  // normalized ray direction
			const MatrixXType *prim_verts_,
			const Eigen::MatrixXi *tri_inds_,
			T eps_, // if dist(0,tri) < eps, is a hit. eps<=0 skips this test
			T t_min_=0,
			T t_max_=std::numeric_limits<T>::max());

	void traverse(
		const AABB &left_aabb, bool &go_left,
		const AABB &right_aabb, bool &go_right,
		bool &go_left_first);

	// Searches for closest hit, so always returns false
	bool stop_traversing(const AABB &aabb, int prim);
};

// Point in tet mesh traversal
template <typename T>
class PointInTetMeshTraverse : public Traverser<T,3>
{
protected:
	using typename Traverser<T,3>::AABB;
	typedef Eigen::Matrix<T,3,1> VecType;
	typedef Eigen::Matrix<T,Eigen::Dynamic,Eigen::Dynamic> MatrixXType;

	VecType point;
	const MatrixXType *prim_verts;
	const Eigen::MatrixXi *prim_inds;
	std::vector<int> skip_inds;

public:
	struct Output {
		int prim; // -1 if no intersections
		Output() : prim(-1) {}
	} output;

	PointInTetMeshTraverse(
		const VecType &point_,
		const MatrixXType *prim_verts_,
		const Eigen::MatrixXi *prim_inds_,
		const std::vector<int> &skip_inds_=std::vector<int>()) :
		point(point_),
		prim_verts(prim_verts_),
		prim_inds(prim_inds_),
		skip_inds(skip_inds_)
		{}

	void traverse(
		const AABB &left_aabb, bool &go_left,
		const AABB &right_aabb, bool &go_right,
		bool &go_left_first);

	bool stop_traversing(const AABB &aabb, int prim);
};

// Point in triangle mesh traversal.
// Determined by launching a ray in a random direction from
// the point and counting the number of (watertight) intersections. If
// the number of intersections is odd, the point is inside th mesh.
template <typename T>
class PointInTriangleMeshTraverse : public Traverser<T,3>
{
protected:
	using typename Traverser<T,3>::AABB;
	typedef Eigen::Matrix<T,3,1> VecType;
	typedef Eigen::Matrix<T,Eigen::Dynamic,Eigen::Dynamic> MatrixXType;

	VecType point, dir;
	const MatrixXType *prim_verts; // triangle mesh verts
	const Eigen::MatrixXi *prim_inds; // triangle mesh inds
	float o[3], d[3]; // pt and dir casted to float for Blender kernels
	struct IsectRayPrecalc isect_precalc;
	std::vector<int> skip_inds;

public:
	struct Output {
		std::vector< std::pair<int,T> > hits; // [prim,t]
		int num_hits() const { return hits.size(); }
		bool is_inside() const { return hits.size()%2==1; }
	} output;

	PointInTriangleMeshTraverse(
		const VecType &point_,
		const MatrixXType *prim_verts_,
		const Eigen::MatrixXi *prim_inds_,
		const std::vector<int> &skip_inds_=std::vector<int>());

	void traverse(
		const AABB &left_aabb, bool &go_left,
		const AABB &right_aabb, bool &go_right,
		bool &go_left_first);

	// Always returns false (multi-hit, so it doesn't stop)
	bool stop_traversing(const AABB &aabb, int prim);
};

// Search for the nearest triangle to a given point
template <typename T>
class NearestTriangleTraverse : public Traverser<T,3>
{
protected:
	using typename Traverser<T,3>::AABB;
	typedef Eigen::Matrix<T,3,1> VecType;
	typedef Eigen::Matrix<T,Eigen::Dynamic,Eigen::Dynamic> MatrixXType;

	VecType point;
	const MatrixXType *prim_verts; // triangle mesh verts
	const Eigen::MatrixXi *prim_inds; // triangle mesh inds
	std::vector<int> skip_inds;

public:
	struct Output {
		int prim;
		T dist;
		VecType pt_on_tri;
		Output() :
			prim(-1),
			dist(std::numeric_limits<T>::max()),
			pt_on_tri(0,0,0)
			{}
	} output;

	NearestTriangleTraverse(
		const VecType &point_,
		const MatrixXType *prim_verts_,
		const Eigen::MatrixXi *prim_inds_,
		const std::vector<int> &skip_inds_=std::vector<int>()) :
		point(point_),
		prim_verts(prim_verts_),
		prim_inds(prim_inds_),
		skip_inds(skip_inds_)
		{}

	void traverse(
		const AABB &left_aabb, bool &go_left,
		const AABB &right_aabb, bool &go_right,
		bool &go_left_first);

	// Always returns false
	bool stop_traversing(const AABB &aabb, int prim);
};

// Check if a tet intersects a triangle mesh
// with tri-tri collision tests
template <typename T>
class TetIntersectsMeshTraverse : public Traverser<T,3>
{
protected:
	using typename Traverser<T,3>::AABB;
	typedef Eigen::Matrix<T,3,1> VecType;
	typedef Eigen::Matrix<T,Eigen::Dynamic,Eigen::Dynamic> MatrixXType;

	const MatrixXType *prim_verts;
	const Eigen::MatrixXi *prim_inds;
	VecType points[4]; // verts of the tet with proper winding (0,1,2,3)
	float p0[3], p1[3], p2[3], p3[3]; // points casted to floats
	std::vector<std::vector<float*> > tet_faces;
	AABB tet_aabb;

public:
	struct Output {
		int hit_face; // first found
		int ray_hit_count;
		Output() : hit_face(-1) {}
	} output;

	TetIntersectsMeshTraverse(
		const VecType points_[4],
		const MatrixXType *prim_verts_,
		const Eigen::MatrixXi *prim_inds_);

	void traverse(
		const AABB &left_aabb, bool &go_left,
		const AABB &right_aabb, bool &go_right,
		bool &go_left_first);

	// Returns true if intersection
	bool stop_traversing(const AABB &aabb, int prim);
};

} // namespace admmpd

#endif // ADMMPD_BVH_TRAVERSE_H_

