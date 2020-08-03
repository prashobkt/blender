// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#include "admmpd_geom.h"

namespace admmpd {
using namespace Eigen;

Matrix<double,4,1> geom::point_tet_barys(
	const Vector3d &p,
	const Vector3d &a,
	const Vector3d &b,
	const Vector3d &c,
	const Vector3d &d)
{
	auto scalar_triple_product = [](
		const Vector3d &u,
		const Vector3d &v,
		const Vector3d &w )
	{
		return u.dot(v.cross(w));
	};
	Vector3d ap = p - a;
	Vector3d bp = p - b;
	Vector3d ab = b - a;
	Vector3d ac = c - a;
	Vector3d ad = d - a;
	Vector3d bc = c - b;
	Vector3d bd = d - b;
	double va6 = scalar_triple_product(bp, bd, bc);
	double vb6 = scalar_triple_product(ap, ac, ad);
	double vc6 = scalar_triple_product(ap, ad, ab);
	double vd6 = scalar_triple_product(ap, ab, ac);
	double v6 = 1.0 / scalar_triple_product(ab, ac, ad);
	return Matrix<double,4,1>(va6*v6, vb6*v6, vc6*v6, vd6*v6);
} // end point tet barycoords

// Checks that it's on the "correct" side of the normal
// for each face of the tet. Assumes winding points inward.
bool geom::point_in_tet(
	const Vector3d &p,
	const Vector3d &a,
	const Vector3d &b,
	const Vector3d &c,
	const Vector3d &d)
{
	auto check_face = [](
		const Vector3d &point,
		const Vector3d &p0,
		const Vector3d &p1,
		const Vector3d &p2,
		const Vector3d &p3 )
	{
		Vector3d n = (p1-p0).cross(p2-p0);
		double dp3 = n.dot(p3-p0);
		double dp = n.dot(point-p0);
		return (dp3*dp >= 0);
	};
	return
		check_face(p, a, b, c, d) &&
		check_face(p, b, c, d, a) &&
		check_face(p, c, d, a, b) &&
		check_face(p, d, a, b, c);
}

void geom::create_tets_from_box(
    const Eigen::Vector3d &bmin,
    const Eigen::Vector3d &bmax,
    std::vector<Eigen::Vector3d> &verts,
    std::vector<Eigen::RowVector4i> &tets)
{
	std::vector<Vector3d> v = {
		// Top plane, clockwise looking down
		bmax,
		Vector3d(bmin[0], bmax[1], bmax[2]),
		Vector3d(bmin[0], bmax[1], bmin[2]),
		Vector3d(bmax[0], bmax[1], bmin[2]),
		// Bottom plane, clockwise looking down
		Vector3d(bmax[0], bmin[1], bmax[2]),
		Vector3d(bmin[0], bmin[1], bmax[2]),
		bmin,
		Vector3d(bmax[0], bmin[1], bmin[2])
	};
	// Add vertices and get indices of the box
	std::vector<int> b;
	for(int i=0; i<8; ++i)
	{
		b.emplace_back(verts.size());
		verts.emplace_back(v[i]);
	}
	// From the box, create five new tets
	std::vector<RowVector4i> new_tets = {
		RowVector4i( b[0], b[5], b[7], b[4] ),
		RowVector4i( b[5], b[7], b[2], b[0] ),
		RowVector4i( b[5], b[0], b[2], b[1] ),
		RowVector4i( b[7], b[2], b[0], b[3] ),
		RowVector4i( b[5], b[2], b[7], b[6] )
	};
	for(int i=0; i<5; ++i)
		tets.emplace_back(new_tets[i]);
}

// From Real-Time Collision Detection by Christer Ericson
template<typename T>
Eigen::Matrix<T,3,1> geom::point_triangle_barys(
	const Eigen::Matrix<T,3,1> &p,
	const Eigen::Matrix<T,3,1> &a,
	const Eigen::Matrix<T,3,1> &b,
	const Eigen::Matrix<T,3,1> &c)
{
	Eigen::Matrix<T,3,1> v0 = b - a;
	Eigen::Matrix<T,3,1> v1 = c - a;
	Eigen::Matrix<T,3,1> v2 = p - a;
	T d00 = v0.dot(v0);
	T d01 = v0.dot(v1);
	T d11 = v1.dot(v1);
	T d20 = v2.dot(v0);
	T d21 = v2.dot(v1);
	T denom = d00 * d11 - d01 * d01;
	if (std::abs(denom)<=0)
		return Eigen::Matrix<T,3,1>::Zero();
	Eigen::Matrix<T,3,1> r;
	r[1] = (d11 * d20 - d01 * d21) / denom;
	r[2] = (d00 * d21 - d01 * d20) / denom;
	r[0] = 1.0 - r[1] - r[2];
	return r;
} // end point triangle barycoords

// From Real-Time Collision Detection by Christer Ericson
template<typename T>
Eigen::Matrix<T,3,1> geom::point_on_triangle_ce(
		const Eigen::Matrix<T,3,1> &p,
		const Eigen::Matrix<T,3,1> &a,
		const Eigen::Matrix<T,3,1> &b,
		const Eigen::Matrix<T,3,1> &c)
{
	typedef Eigen::Matrix<T,3,1> VecType;
	auto Dot = [](const VecType &v0, const VecType &v1)
	{
		return v0.dot(v1);
	};
	auto Cross = [](const VecType &v0, const VecType &v1)
	{
		return v0.cross(v1);
	};
	VecType ab = b - a;
	VecType ac = c - a;
	VecType bc = c - b;
	// Compute parametric position s for projection P’ of P on AB,
	// P’ = A + s*AB, s = snom/(snom+sdenom)
	T snom = Dot(p - a, ab), sdenom = Dot(p - b, a - b);
	// Compute parametric position t for projection P’ of P on AC,
	// P’ = A + t*AC, s = tnom/(tnom+tdenom)
	T tnom = Dot(p - a, ac), tdenom = Dot(p - c, a - c);
	if (snom <= 0.0f && tnom <= 0.0f) return a;
	// Vertex region early out
	// Compute parametric position u for projection P’ of P on BC,
	// P’ = B + u*BC, u = unom/(unom+udenom)
	T unom = Dot(p - b, bc), udenom = Dot(p - c, b - c);
	if (sdenom <= 0.0f && unom <= 0.0f) return b; // Vertex region early out
	if (tdenom <= 0.0f && udenom <= 0.0f) return c; // Vertex region early out
	// P is outside (or on) AB if the triple scalar product [N PA PB] <= 0
	VecType n = Cross(b - a, c - a);
	T vc = Dot(n, Cross(a - p, b - p));
	// If P outside AB and within feature region of AB,
	// return projection of P onto AB
	if (vc <= 0.0f && snom >= 0.0f && sdenom >= 0.0f)
	return a + snom / (snom + sdenom) * ab;
	// P is outside (or on) BC if the triple scalar product [N PB PC] <= 0
	T va = Dot(n, Cross(b - p, c - p));
	// If P outside BC and within feature region of BC,
	// return projection of P onto BC
	if (va <= 0.0f && unom >= 0.0f && udenom >= 0.0f)
	return b + unom / (unom + udenom) * bc;
	// P is outside (or on) CA if the triple scalar product [N PC PA] <= 0
	T vb = Dot(n, Cross(c - p, a - p));
	// If P outside CA and within feature region of CA,
	// return projection of P onto CA
	if (vb <= 0.0f && tnom >= 0.0f && tdenom >= 0.0f)
	return a + tnom / (tnom + tdenom) * ac;
	// P must project inside face region. Compute Q using barycentric coordinates
	T u = va / (va + vb + vc);
	T v = vb / (va + vb + vc);
	T w = 1.0f - u - v; // = vc / (va + vb + vc)
	return u * a + v * b + w * c;
}

// https://github.com/mattoverby/mclscene/blob/master/include/MCL/Projection.hpp
template<typename T>
Eigen::Matrix<T,3,1> geom::point_on_triangle(
		const Eigen::Matrix<T,3,1> &point,
		const Eigen::Matrix<T,3,1> &p1,
		const Eigen::Matrix<T,3,1> &p2,
		const Eigen::Matrix<T,3,1> &p3)
{
	typedef Matrix<T,3,1> VecType;
	auto myclamp = [](const T &x){ return x<0 ? 0 : (x>1 ? 1 : x); };

	VecType edge0 = p2 - p1;
	VecType edge1 = p3 - p1;
	VecType v0 = p1 - point;
	T a = edge0.dot( edge0 );
	T b = edge0.dot( edge1 );
	T c = edge1.dot( edge1 );
	T d = edge0.dot( v0 );
	T e = edge1.dot( v0 );
	T det = a*c - b*b;
	T s = b*e - c*d;
	T t = b*d - a*e;

	const T zero(0);
	const T one(1);
	if ( s + t < det ) {
		if ( s < zero ) {
		    if ( t < zero ) {
				if ( d < zero ) {
					s = myclamp( -d/a );
					t = zero;
				}
				else {
					s = zero;
					t = myclamp( -e/c );
				}
			}
			else {
				s = zero;
				t = myclamp( -e/c );
		    }
		}
		else if ( t < zero ) {
		    s = myclamp( -d/a );
		    t = zero;
		}
		else {
		    T invDet = one / det;
		    s *= invDet;
		    t *= invDet;
		}
	}
	else {
		if ( s < zero ) {
		    T tmp0 = b+d;
		    T tmp1 = c+e;
		    if ( tmp1 > tmp0 ) {
				T numer = tmp1 - tmp0;
				T denom = a-T(2)*b+c;
				s = myclamp( numer/denom );
				t = one-s;
		    }
		    else {
				t = myclamp( -e/c );
				s = zero;
		    }
		}
		else if ( t < zero ) {
		    if ( a+d > b+e ) {
				T numer = c+e-b-d;
				T denom = a-T(2)*b+c;
				s = myclamp( numer/denom );
				t = one-s;
		    }
		    else {
				s = myclamp( -e/c );
				t = zero;
		    }
		}
		else {
		    T numer = c+e-b-d;
		    T denom = a-T(2)*b+c;
		    s = myclamp( numer/denom );
		    t = one - s;
		}
	}

	return ( p1 + edge0*s + edge1*t );
}

// From Real-Time Collision Detection by Christer Ericson
template<typename T>
bool geom::aabb_plane_intersect(
		const Eigen::Matrix<T,3,1> &bmin,
		const Eigen::Matrix<T,3,1> &bmax,
		const Eigen::Matrix<T,3,1> &p, // pt on plane
		const Eigen::Matrix<T,3,1> &n) // normal
{
	typedef Eigen::Matrix<T,3,1> VecType;
	T d = p.dot(n);
	// These two lines not necessary with a (center, extents) AABB representation
	VecType c = (bmax+bmin)*0.5; // Compute AABB center
	VecType e = bmax-c; // Compute positive extents
	// Compute the projection interval radius of b onto L(t) = b.c + t * p.n
	T r = e[0]*std::abs(n[0])+e[1]*std::abs(n[1])+e[2]*std::abs(n[2]);
	// Compute distance of box center from plane
	T s = n.dot(c)-d;
	// Intersection occurs when distance s falls within [-r,+r] interval
	return std::abs(s) <= r;
}

// From Real-Time Collision Detection by Christer Ericson
template<typename T>
bool geom::aabb_triangle_intersect(
    const Eigen::Matrix<T,3,1> &bmin,
    const Eigen::Matrix<T,3,1> &bmax,
    const Eigen::Matrix<T,3,1> &a,
    const Eigen::Matrix<T,3,1> &b,
    const Eigen::Matrix<T,3,1> &c)
{
	Eigen::AlignedBox<T,3> box;
	box.extend(bmin);
	box.extend(bmax);
	if (box.contains(a))
		return true;
	if (box.contains(b))
		return true;
	if (box.contains(c))
		return true;

	typedef Eigen::Matrix<T,3,1> VecType;
	auto Max = [](T x, T y, T z){ return std::max(std::max(x,y),z); };
	auto Min = [](T x, T y, T z){ return std::min(std::min(x,y),z); };
    T p0, p1, p2, r, minp, maxp;
    // Compute box center and extents (if not already given in that format)
    VecType cent = (bmin+bmax)*0.5;
    T e0 = (bmax[0]-bmin[0])*0.5;
    T e1 = (bmax[1]-bmin[1])*0.5;
    T e2 = (bmax[2]-bmin[2])*0.5;
    // Translate triangle as conceptually moving AABB to origin
    VecType v0 = a-cent;
    VecType v1 = b-cent;
    VecType v2 = c-cent;
	const VecType box_normals[3] = { VecType(1,0,0), VecType(0,1,0), VecType(0,0,1) };
    // Compute edge vectors for triangle
	const VecType edge_vectors[3] = { v1-v0, v2-v1, v0-v2 }; // f0,f1,f2
    // Test axes a00..a22 (category 3)
	VecType aij;
	for (int i=0; i<3; ++i)
	{
		const VecType &u = box_normals[i];
		for (int j=0; j<3; ++j)
		{
			const VecType &f = edge_vectors[j];
			aij = u.cross(f);
			// Axis is separating axis
			p0 = v0.dot(aij);
			p1 = v1.dot(aij);
			p2 = v2.dot(aij);
			r = e0*std::abs(box_normals[0].dot(aij)) +
				e1*std::abs(box_normals[1].dot(aij)) +
				e2*std::abs(box_normals[2].dot(aij));
			minp = Min(p0, p1, p2);
			maxp = Max(p0, p1, p2);
			if (maxp < -r || minp > r)
				return false;
		}
	}
	// Test the three axes corresponding to the face normals of AABB b (category 1).
    // Exit if...
    // ... [-e0, e0] and [min(v0[0],v1[0],v2[0]), max(v0[0],v1[0],v2[0])] do not overlap
    if (Max(v0[0], v1[0], v2[0]) < -e0 || Min(v0[0], v1[0], v2[0]) > e0) return false;
    // ... [-e1, e1] and [min(v0[1],v1[1],v2[1]), max(v0[1],v1[1],v2[1])] do not overlap
    if (Max(v0[1], v1[1], v2[1]) < -e1 || Min(v0[1], v1[1], v2[1]) > e1) return false;
    // ... [-e2, e2] and [min(v0[2],v1[2],v2[2]), max(v0[2],v1[2],v2[2])] do not overlap
    if (Max(v0[2], v1[2], v2[2]) < -e2 || Min(v0[2], v1[2], v2[2]) > e2) return false;
    // Test separating axis corresponding to triangle face normal (category 2)
	VecType n = edge_vectors[0].cross(edge_vectors[1]);
	T dot = v0.dot(n);
	// These two lines not necessary with a (center, extents) AABB representation
	VecType e = bmax-cent; // Compute positive extents
	// Compute the projection interval radius of b onto L(t) = b.c + t * p.n
	r = e[0]*std::abs(n[0])+e[1]*std::abs(n[1])+e[2]*std::abs(n[2]);
	// Compute distance of box center from plane
	T s = n.dot(cent)-dot;
	// Intersection occurs when distance s falls within [-r,+r] interval
	return std::abs(s) <= r;
}

// https://people.csail.mit.edu/amy/papers/box-jgt.pdf
template<typename T>
bool geom::ray_aabb(
		const Eigen::Matrix<T,3,1> &o,
		const Eigen::Matrix<T,3,1> &d,
		const Eigen::AlignedBox<T,3> &aabb,
		T t_min, T t_max)
{
	if (aabb.contains(o))
		return true;
	const Matrix<T,3,1> &bmin = aabb.min();
	const Matrix<T,3,1> &bmax = aabb.max();
	T tmin, tymin, tzmin;
	T tmax, tymax, tzmax;
	if (d[0] >= 0)
	{
		tmin = (bmin[0] - o[0]) / d[0];
		tmax = (bmax[0] - o[0]) / d[0];
	}
	else
	{
		tmin = (bmax[0] - o[0]) / d[0];
		tmax = (bmin[0] - o[0]) / d[0];
	}
	if (d[1] >= 0)
	{
		tymin = (bmin[1] - o[1]) / d[1];
		tymax = (bmax[1] - o[1]) / d[1];
	}
	else
	{
		tymin = (bmax[1] - o[1]) / d[1];
		tymax = (bmin[1] - o[1]) / d[1];
	}
	if ( (tmin > tymax) || (tymin > tmax) )
		return false;
	if (tymin > tmin)
		tmin = tymin;
	if (tymax < tmax)
		tmax = tymax;
	if (d[2] >= 0)
	{
		tzmin = (bmin[2] - o[2]) / d[2];
		tzmax = (bmax[2] - o[2]) / d[2];
	}
	else
	{
		tzmin = (bmax[2] - o[2]) / d[2];
		tzmax = (bmin[2] - o[2]) / d[2];
	}
	if ( (tmin > tzmax) || (tzmin > tmax) )
		return false;
	if (tzmin > tmin)
		tmin = tzmin;
	if (tzmax < tmax)
		tmax = tzmax;
	return ( (tmin < t_max) && (tmax > t_min) );
} // end ray - aabb test

template<typename T>
bool geom::ray_triangle(
		const Eigen::Matrix<T,3,1> &o,
		const Eigen::Matrix<T,3,1> &d,
		const Eigen::Matrix<T,3,1> &p0,
		const Eigen::Matrix<T,3,1> &p1,
		const Eigen::Matrix<T,3,1> &p2,
		T t_min,
		T &t_max,
		Eigen::Matrix<T,3,1> *bary)
{
	typedef Eigen::Matrix<T,3,1> VecType;
	VecType e0 = p1 - p0;
	VecType e1 = p0 - p2;
	VecType n = e1.cross( e0 );
	VecType e2 = (p0-o) / (n.dot(d));
	T t = n.dot(e2);
	if (t > t_max)
		return false;
	if (t < t_min)
		return false;
	if (bary != nullptr)
	{
		VecType i  = d.cross(e2);
		T beta = i.dot(e1);
		T gamma = i.dot(e0);
		*bary = VecType(1.0-beta-gamma, beta, gamma);
		const T eps = 1e-8;
		if (bary->sum()>1+eps)
			return false;
	}
	t_max = t;
	return true;
} // end ray - triangle test

void geom::merge_close_vertices(
	std::vector<Vector3d> &verts,
	std::vector<RowVector4i> &tets,
	double eps)
{
	int nv = verts.size();
	std::vector<Vector3d> new_v(nv); // new verts
	std::vector<int> idx(nv,0); // index mapping
	std::vector<int> visited(nv,0);
	int count = 0;
	for (int i=0; i<nv; ++i)
	{
		if(!visited[i])
		{
			visited[i] = 1;
			new_v[count] = verts[i];
			idx[i] = count;
			Vector3d vi = verts[i];
			for (int j = i+1; j<nv; ++j)
			{
				if((verts[j]-vi).norm() < eps)
				{
					visited[j] = 1;
					idx[j] = count;
				}
			}
			count++;
		}
	}
	new_v.resize(count);
	verts = new_v;
	int nt = tets.size();
	for (int i=0; i<nt; ++i)
	{
		for (int j=0; j<4; ++j)
		{
			tets[i][j] = idx[tets[i][j]];
		}
	}
}

template<typename T>
void geom::make_n3(
		const Eigen::SparseMatrix<T> &A,
		Eigen::SparseMatrix<T> &A3)
{
	int na = A.rows();
	A3.resize(na*3, na*3);
	A3.reserve(A.nonZeros()*3);
	int cols = A.rows();
	for(int c=0; c<cols; ++c)
	{
		for(int dim=0; dim<3; ++dim)
		{
			int col = c*3+dim;
			A3.startVec(col);
			for(typename SparseMatrix<T>::InnerIterator itA(A,c); itA; ++itA)
				A3.insertBack((itA.row()*3+dim), col) = itA.value();
		}
	}
	A3.finalize();
	A3.makeCompressed();
}

//
// Compile template types
//
template Eigen::Matrix<double,3,1>
	admmpd::geom::point_triangle_barys<double>(
	const Eigen::Vector3d&, const Eigen::Vector3d&,
	const Eigen::Vector3d&, const Eigen::Vector3d&);
template Eigen::Matrix<float,3,1>
	admmpd::geom::point_triangle_barys<float>(
	const Eigen::Vector3f&, const Eigen::Vector3f&,
	const Eigen::Vector3f&, const Eigen::Vector3f&);
template Eigen::Matrix<double,3,1>
	admmpd::geom::point_on_triangle<double>(
	const Eigen::Vector3d&, const Eigen::Vector3d&,
	const Eigen::Vector3d&, const Eigen::Vector3d&);
template Eigen::Matrix<float,3,1>
	admmpd::geom::point_on_triangle<float>(
	const Eigen::Vector3f&, const Eigen::Vector3f&,
	const Eigen::Vector3f&, const Eigen::Vector3f&);
template bool geom::aabb_plane_intersect<double>(
	const Eigen::Matrix<double,3,1>&,
	const Eigen::Matrix<double,3,1>&,
	const Eigen::Matrix<double,3,1>&,
	const Eigen::Matrix<double,3,1>&);
template bool geom::aabb_plane_intersect<float>(
	const Eigen::Matrix<float,3,1>&,
	const Eigen::Matrix<float,3,1>&,
	const Eigen::Matrix<float,3,1>&,
	const Eigen::Matrix<float,3,1>&);
template bool geom::aabb_triangle_intersect<double>(
    const Eigen::Matrix<double,3,1>&,
    const Eigen::Matrix<double,3,1>&,
    const Eigen::Matrix<double,3,1>&,
    const Eigen::Matrix<double,3,1>&,
    const Eigen::Matrix<double,3,1>&);
template bool geom::aabb_triangle_intersect<float>(
    const Eigen::Matrix<float,3,1>&,
    const Eigen::Matrix<float,3,1>&,
    const Eigen::Matrix<float,3,1>&,
    const Eigen::Matrix<float,3,1>&,
    const Eigen::Matrix<float,3,1>&);
template bool admmpd::geom::ray_aabb<double>(
	const Eigen::Vector3d&, const Eigen::Vector3d&,
	const Eigen::AlignedBox<double,3>&, double, double);
template bool admmpd::geom::ray_aabb<float>(
	const Eigen::Vector3f&, const Eigen::Vector3f&,
	const Eigen::AlignedBox<float,3>&, float, float);
template bool admmpd::geom::ray_triangle<double>(
	const Eigen::Vector3d&, const Eigen::Vector3d&,
	const Eigen::Vector3d&, const Eigen::Vector3d&,
	const Eigen::Vector3d&, double, double&, Eigen::Vector3d*);
template bool admmpd::geom::ray_triangle<float>(
	const Eigen::Vector3f&, const Eigen::Vector3f&,
	const Eigen::Vector3f&, const Eigen::Vector3f&,
	const Eigen::Vector3f&, float, float&, Eigen::Vector3f*);
template void admmpd::geom::make_n3<float>(
	const Eigen::SparseMatrix<float>&,
	Eigen::SparseMatrix<float>&);
template void admmpd::geom::make_n3<double>(
	const Eigen::SparseMatrix<double>&,
	Eigen::SparseMatrix<double>&);

} // namespace admmpd
