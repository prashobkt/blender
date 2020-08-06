// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#include "admmpd_bvh.h"
#include <numeric> // iota

// Adapted from:
// https://github.com/mattoverby/mclscene/blob/master/include/MCL/BVH.hpp

namespace admmpd {

template <typename T, int DIM>
AABBTree<T,DIM>::AABBTree(const AABBTree<T,DIM> &other_tree)
{
    m_root = std::make_shared<Node>();
    std::function<void(const Node*, Node*)> copy_children;
    copy_children = [&copy_children](const Node *src, Node *dest)->void
    {
        if (src==nullptr)
        {
            dest = nullptr;
            return;
        }
        dest->aabb = src->aabb;
        dest->prims = src->prims;
        copy_children(dest->left, src->left);
        copy_children(dest->right, src->right);
    };
    copy_children(other_tree.m_root.get(), m_root.get());
}

template <typename T, int DIM>
void AABBTree<T,DIM>::clear()
{
    m_root = std::make_shared<Node>();
}

template <typename T, int DIM>
void AABBTree<T,DIM>::init(const std::vector<AABB> &leaves)
{
    m_root = std::make_shared<Node>();
    int np = leaves.size();
    if (np==0)
        return;
    std::vector<int> queue(np);
    std::iota(queue.begin(), queue.end(), 0);
    create_children(m_root.get(), queue, leaves);
}

template <typename T, int DIM>
void AABBTree<T,DIM>::update(const std::vector<AABB> &leaves)
{
    if (!m_root || (int)leaves.size()==0)
        return;
    update_children(m_root.get(), leaves);
}

template <typename T, int DIM>
bool AABBTree<T,DIM>::traverse(Traverser<T,DIM> &traverser) const
{
    if (!m_root)
        return false;
    return traverse_children(m_root.get(), traverser);
}

// If we are traversing with function pointers, we'll just
// wrap our own traverser that calls these functions to
// avoid duplicate traverse_children code.
template <typename T, int DIM>
class TraverserFromFunctionPtrs : public Traverser<T,DIM>
{
using typename Traverser<T,DIM>::AABB;
public:
    std::function<void(const AABB&, bool&, const AABB&, bool&, bool&)> t;
    std::function<bool(const AABB&, int)> s;
    void traverse(
		const AABB &left_aabb, bool &go_left,
		const AABB &right_aabb, bool &go_right,
		bool &go_left_first)
    {
        t(left_aabb, go_left, right_aabb, go_right, go_left_first);
    }
	bool stop_traversing(const AABB &aabb, int prim)
    {
        return s(aabb,prim);
    }
};

template <typename T, int DIM>
bool AABBTree<T,DIM>::traverse(
    std::function<void(const AABB&, bool&, const AABB&, bool&, bool&)> t,
    std::function<bool(const AABB&, int)> s) const
{
    if (!m_root)
        return false;
    TraverserFromFunctionPtrs<T,DIM> traverser;
    traverser.t = t;
    traverser.s = s;
    return traverse_children(m_root.get(), traverser);
}

template <typename T, int DIM>
void AABBTree<T,DIM>::create_children(
    Node *node,
    std::vector<int> &queue,
    const std::vector<AABB> &leaves)
{
	node->aabb.setEmpty();
    int n_queue = queue.size();
	if (n_queue == 1)
    {
	    node->prims.emplace_back(queue[0]);
		node->aabb = leaves[queue[0]];
		return;
	}

    for (int i=0; i<n_queue; ++i)
    {
        int q = queue[i];
        node->aabb.extend(leaves[q]);
    }

    struct SortByAxis
    {
        int axis;
        const std::vector<AABB> &aabbs;
        SortByAxis(int axis_, const std::vector<AABB> &aabbs_) :
            axis(axis_), aabbs(aabbs_) {}
        bool operator()(size_t l, size_t r) const
        {
            return aabbs[l].center()[axis] < aabbs[r].center()[axis];
        }
    };

    // Sort tree and split queue
    int sort_axis = 0;
    VecType sizes = node->aabb.sizes();
    sizes.maxCoeff(&sort_axis);
    std::sort(queue.begin(), queue.end(), SortByAxis(sort_axis,leaves));
	std::vector<int> left_queue(queue.begin(), queue.begin()+(n_queue/2));
	std::vector<int> right_queue(queue.begin()+(n_queue/2), queue.end());

    // Recursive top-down constructrion
    node->left = new Node();
	create_children(node->left, left_queue, leaves);
    node->right = new Node();
	create_children(node->right, right_queue, leaves);

} // end create children

template <typename T, int DIM>
void AABBTree<T,DIM>::update_children(
    Node *node,
    const std::vector<AABB> &leaves)
{
	node->aabb.setEmpty();

    if (node->is_leaf())
    {
        int np = node->prims.size();
        for (int i=0; i<np; ++i)
        {
            node->aabb.extend(leaves[node->prims[i]]);
        }
        return;
    }

	if (node->left != nullptr)
    {
		update_children(node->left, leaves);
		node->aabb.extend(node->left->aabb);
	}
	if (node->right != nullptr)
    {
		update_children(node->right, leaves);
		node->aabb.extend(node->right->aabb);
	}

} // end update children

template <typename T, int DIM>
bool AABBTree<T,DIM>::traverse_children(
    const Node *node,
    Traverser<T,DIM> &traverser ) const
{
	if( node->is_leaf() ){
		int np = node->prims.size();
		for(int i=0; i<np; ++i)
        {
			if(traverser.stop_traversing(node->aabb, node->prims[i]))
                return true;
		}
		return false;
	}

	bool go_left = true;
	bool go_right = true;
	bool go_left_first = true;
    const AABB &left_aabb = (node->left == nullptr ? AABB() : node->left->aabb);
    const AABB &right_aabb = (node->right == nullptr ? AABB() : node->right->aabb);
	traverser.traverse(
		left_aabb, go_left,
		right_aabb, go_right,
		go_left_first );

	if (go_left && go_right)
    {
        if (go_left_first)
        {
            if (traverse_children(node->left, traverser)) { return true; }
		    else { return traverse_children(node->right, traverser); }
        }
        else
        {
            if (traverse_children(node->right, traverser)) { return true; }
		    else { return traverse_children(node->left, traverser); }
        }
	}
	if (go_left && !go_right)
    {
		return traverse_children(node->left, traverser);
	}
	if (!go_left && go_right)
    {
		return traverse_children(node->right, traverser);
	}

	return false;

} // end traverse children

template<typename T, int DIM>
void Octree<T,DIM>::clear()
{
    m_root = std::make_shared<Node>();
}

template<typename T, int DIM>
typename Octree<T,DIM>::AABB Octree<T,DIM>::bounds() const
{
    if (m_root)
        return m_root->bounds();
    return AABB();
}

template<typename T, int DIM>
void Octree<T,DIM>::init(const MatrixXT *V, const Eigen::MatrixXi *F, int stopdepth)
{
    BLI_assert(V != nullptr);
    BLI_assert(F != nullptr);
    BLI_assert(F->cols()==3);
    m_root = std::make_shared<Node>();

    if (DIM !=3 || F->cols()!=3)
    {
        return;
    }

    int nf = F->rows();
    AABB global_box;
    std::vector<AABB> boxes(nf);
    std::vector<int> queue(nf);
    for (int i=0; i<nf; ++i)
    {
        Eigen::RowVector3i f = F->row(i);
		queue[i]=i;
		boxes[i].extend(V->row(f[0]).transpose());
		boxes[i].extend(V->row(f[1]).transpose());
		boxes[i].extend(V->row(f[2]).transpose());
		global_box.extend(boxes[i]);
    }

	T halfwidth = global_box.sizes().maxCoeff()*0.5;
    m_root.reset(
        create_children(global_box.center(),halfwidth,stopdepth,V,F,queue,boxes)
    );
}

template<typename T, int DIM>
typename Octree<T,DIM>::Node* Octree<T,DIM>::create_children(
    const VecType &center, T halfwidth, int stopdepth,
    const MatrixXT *V, const Eigen::MatrixXi *F,
    const std::vector<int> &queue,
    const std::vector<AABB> &boxes)
{
    BLI_assert((int)queue.size()>0);
    BLI_assert((int)boxes.size()>0);
    BLI_assert(F != nullptr);
    BLI_assert(V != nullptr);
    BLI_assert(F->cols()==3);
    BLI_assert(V->cols()==3);
    if (stopdepth >= 0)
    {
        Node *node = new Node();
        node->center = center;
        node->halfwidth = halfwidth;
        node->prims.clear();
        AABB box = node->bounds();
        // Set list of intersected prims
        int nq = queue.size();
        for (int i=0; i<nq; ++i)
        {
            int p_idx = queue[i];
            if (box.intersects(boxes[p_idx]))
                node->prims.emplace_back(p_idx);
        }
        // Create children only if prims intersect
        bool has_prims = node->prims.size()>0;
        for (int i=0; i<nchild && has_prims; ++i)
        {

            T step = node->halfwidth * 0.5;
            VecType offset = VecType::Zero();
            offset[0] = ((i & 1) ? step : -step);
            offset[1] = ((i & 2) ? step : -step);
            if (DIM==3) offset[2] = ((i & 4) ? step : -step);

            node->children[i] = create_children(
                node->center+offset, step, stopdepth-1,
                V, F, node->prims, boxes);
        }
        return node;
    }
    return nullptr;
}

template<typename T, int DIM>
Octree<T,DIM>::Node::Node() :
    center(VecType::Zero()),
    halfwidth(0)
{
	for (int i=0; i<nchild; ++i)
		children[i] = nullptr;
}

template<typename T, int DIM>
Octree<T,DIM>::Node::~Node()
{
    for (int i=0; i<nchild; ++i)
	    if (children[i] != nullptr)
			delete children[i];
}

template<typename T, int DIM>
bool Octree<T,DIM>::Node::is_leaf() const
{
    return children[0] == nullptr;
}

template<typename T, int DIM>
typename Octree<T,DIM>::AABB Octree<T,DIM>::Node::bounds() const
{
    AABB box;
    box.extend(center + VecType::Ones()*halfwidth);
    box.extend(center - VecType::Ones()*halfwidth);
    return box;
}

// Compile types
template class admmpd::AABBTree<double,2>;
template class admmpd::AABBTree<double,3>;
template class admmpd::AABBTree<float,2>;
template class admmpd::AABBTree<float,3>;
template class admmpd::TraverserFromFunctionPtrs<double,2>;
template class admmpd::TraverserFromFunctionPtrs<double,3>;
template class admmpd::TraverserFromFunctionPtrs<float,2>;
template class admmpd::TraverserFromFunctionPtrs<float,3>;
template class admmpd::Octree<double,2>;
template class admmpd::Octree<double,3>;
template class admmpd::Octree<float,2>;
template class admmpd::Octree<float,3>;

} // namespace admmpd