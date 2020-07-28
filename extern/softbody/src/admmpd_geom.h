// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_GEOM_H_
#define ADMMPD_GEOM_H_

#include <Eigen/Geometry>
#include <Eigen/Sparse>
#include <vector>

// Common geometry kernels
namespace admmpd {
class geom {
public:

static Eigen::Vector4d point_tet_barys(
    const Eigen::Vector3d &p,
    const Eigen::Vector3d &a,
    const Eigen::Vector3d &b,
    const Eigen::Vector3d &c,
    const Eigen::Vector3d &d);

static bool point_in_tet(
    const Eigen::Vector3d &p,
    const Eigen::Vector3d &a,
    const Eigen::Vector3d &b,
    const Eigen::Vector3d &c,
    const Eigen::Vector3d &d);

static void create_tets_from_box(
    const Eigen::Vector3d &bmin,
    const Eigen::Vector3d &bmax,
    std::vector<Eigen::Vector3d> &verts,
    std::vector<Eigen::RowVector4i> &tets);

template<typename T>
static Eigen::Matrix<T,3,1> point_triangle_barys(
    const Eigen::Matrix<T,3,1> &p,
    const Eigen::Matrix<T,3,1> &a,
    const Eigen::Matrix<T,3,1> &b,
    const Eigen::Matrix<T,3,1> &c);

template<typename T>
static Eigen::Matrix<T,3,1> point_on_triangle(
    const Eigen::Matrix<T,3,1> &p,
    const Eigen::Matrix<T,3,1> &a,
    const Eigen::Matrix<T,3,1> &b,
    const Eigen::Matrix<T,3,1> &c);

template<typename T>
static bool aabb_plane_intersect(
    const Eigen::Matrix<T,3,1> &bmin,
    const Eigen::Matrix<T,3,1> &bmax,
    const Eigen::Matrix<T,3,1> &p, // pt on plane
    const Eigen::Matrix<T,3,1> &n); // normal

template<typename T>
static bool aabb_triangle_intersect(
    const Eigen::Matrix<T,3,1> &bmin,
    const Eigen::Matrix<T,3,1> &bmax,
    const Eigen::Matrix<T,3,1> &a,
    const Eigen::Matrix<T,3,1> &b,
    const Eigen::Matrix<T,3,1> &c);

template<typename T>
static bool ray_aabb(
    const Eigen::Matrix<T,3,1> &o,
    const Eigen::Matrix<T,3,1> &d,
    const Eigen::AlignedBox<T,3> &aabb,
    T t_min, T t_max);

template<typename T>
static bool ray_triangle(
	const Eigen::Matrix<T,3,1> &o,
	const Eigen::Matrix<T,3,1> &d,
	const Eigen::Matrix<T,3,1> &p0,
	const Eigen::Matrix<T,3,1> &p1,
	const Eigen::Matrix<T,3,1> &p2,
	T t_min,
	T &t_max,
	Eigen::Matrix<T,3,1> *bary=nullptr);

static void merge_close_vertices(
	std::vector<Eigen::Vector3d> &verts,
	std::vector<Eigen::RowVector4i> &tets,
	double eps = 1e-12);

// Replicates a matrix along the diagonal
template<typename T>
static void make_n3(
		const Eigen::SparseMatrix<T> &A,
		Eigen::SparseMatrix<T> &A3);

}; // class geom
} // namespace admmpd

#endif // ADMMPD_GEOM_H_
