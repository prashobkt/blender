// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_BVH_H_
#define ADMMPD_BVH_H_ 1

#include "admmpd_bvh_traverse.h"
#include <vector>
#include <memory>
#include <functional>

namespace admmpd {

template <typename T, int DIM>
class AABBTree
{
protected:
	typedef Eigen::AlignedBox<T,DIM> AABB;
	typedef Eigen::Matrix<T,DIM,1> VecType;
public:
	// Removes all BVH data
	void clear();

	// Initializes the BVH with a list of leaf bounding boxes.
	// Sorts each split by largest axis.
	void init(const std::vector<AABB> &leaves);

	// Recomputes the bounding boxes of leaf and parent
	// nodes but does not sort the tree.
	void update(const std::vector<AABB> &leaves);

	// Traverse the tree. Returns result of traverser
	bool traverse(Traverser<T,DIM> &traverser) const;

	// Traverse the tree with function pointers instead of class:
	// void traverse(
	//	const AABB &left_aabb, bool &go_left,
	//	const AABB &right_aabb, bool &go_right,
	//	bool &go_left_first);
	// bool stop_traversing( const AABB &aabb, int prim );
	bool traverse(
		std::function<void(const AABB&, bool&, const AABB&, bool&, bool&)> t,
		std::function<bool(const AABB&, int)> s) const;

	struct Node
	{
		AABB aabb;
		Node *left, *right;
		std::vector<int> prims;
		VecType normal;
		T angle;
		bool is_leaf() const { return prims.size()>0; }
		Node() : left(nullptr), right(nullptr) {}
		~Node()
		{
			if(left){ delete left; }
			if(right){ delete right; }
		}
	};

	// Return ptr to the root node
	// Becomes invalidated after init()
	std::shared_ptr<Node> root() { return m_root; }

protected:

	std::shared_ptr<Node> m_root;

	void create_children(
		Node *node,
		std::vector<int> &queue,
		const std::vector<AABB> &leaves);

	void update_children(
		Node *node,
		const std::vector<AABB> &leaves);

	bool traverse_children(
		const Node *node,
		Traverser<T,DIM> &traverser ) const;

}; // class AABBtree


// Octree is actually a quadtree if DIM=2
template<typename T, int DIM>
class Octree
{
protected:
	typedef Eigen::AlignedBox<T,DIM> AABB;
	typedef Eigen::Matrix<T,DIM,1> VecType;
	typedef Eigen::Matrix<T,Eigen::Dynamic,Eigen::Dynamic> MatrixXT;
	static const int nchild = std::pow(2,DIM);
public:

	// Removes all BVH data
	void clear();

	// Creates the Octree up to depth with smart subdivision
	// (only create children if it contains prims) and does not
	// create a cell if it is outside the mesh.
	// ** Assumes a closed mesh and only defined for 3D
	// eps is added to the min and max of the boxes.
	void init(const MatrixXT *V, const Eigen::MatrixXi *F, int stopdepth);

	// Returns bounding box of the root node
	AABB bounds() const;

	struct Node
	{
		VecType center;
		T halfwidth;
		Node *children[4*DIM];
		std::vector<int> prims; // includes childen
		bool is_leaf() const;
		AABB bounds() const;
		Node();
		~Node();
	};

	// Return ptr to the root node
	// Becomes invalidated after init()
	std::shared_ptr<Node> root() { return m_root; }

protected:

	std::shared_ptr<Node> m_root;

	Node* create_children(
		const VecType &center, T halfwidth, int stopdepth,
		const MatrixXT *V, const Eigen::MatrixXi *F,
		const std::vector<int> &queue,
		const std::vector<AABB> &boxes);

}; // class Octree

} // namespace admmpd

#endif // ADMMPD_BVH_H_

